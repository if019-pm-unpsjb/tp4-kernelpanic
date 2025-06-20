#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <netinet/in.h>

#define BUFFER_SIZE 1024
#define FILE_CHUNK_SIZE 1024

void enviar_archivo_client(int sock, const char *dest, const char *filepath);

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        fprintf(stderr, "Uso: %s <IP_o_DNS> <Puerto> <NombreUsuario>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *host = argv[1];
    int port = atoi(argv[2]);
    const char *username = argv[3];
    int sock;
    fd_set read_fds;
    char buffer[BUFFER_SIZE];

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Error al crear el socket");
        exit(EXIT_FAILURE);
    }

    // Resolver DNS
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", port);

    int err = getaddrinfo(host, port_str, &hints, &res);
    if (err != 0)
    {
        fprintf(stderr, "Error en getaddrinfo: %s\n", gai_strerror(err));
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Conectar al servidor
    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0)
    {
        perror("Conexión fallida");
        freeaddrinfo(res);
        close(sock);
        exit(EXIT_FAILURE);
    }
    freeaddrinfo(res);

    // Enviar nombre de usuario
    if (send(sock, username, strlen(username), 0) < 0)
    {
        perror("Error al enviar nombre de usuario");
        close(sock);
        exit(EXIT_FAILURE);
    }
    send(sock, "\n", 1, 0);

    printf("Conectado como '%s'.\n"
          "========================================================================================\n"
          " - Para escribir un mensaje use este comando 'PRIV|<usuario destino>|<mensaje>'\n"
          "\tPor ejemplo: PRIV|gabi|hola!\n"
          " - Para enviar archivos use el comando '/file <usuario destino> <direccion del archivo>'\n"
          "Precione Enter para actualizar los mensajes\n"
          "Para salir del chat precione Ctrl + C\n"
          "========================================================================================\n",
          username);

    while (1)
    {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock, &read_fds);

        if (select(sock + 1, &read_fds, NULL, NULL, NULL) < 0)
        {
            perror("select");
            break;
        }

        // Mensajes del servidor
        if (FD_ISSET(sock, &read_fds))
        {
            int bytes = recv(sock, buffer, BUFFER_SIZE, 0);
            if (bytes <= 0)
            {
                if (bytes == 0)
                    printf("Servidor desconectado.\n");
                else
                    perror("recv");

                close(sock);
                exit(EXIT_FAILURE);
            }

            int offset = 0;
            while (offset < bytes)
            {
                char *fp = strstr(buffer + offset, "FILE|");
                if (!fp)
                {
                    fwrite(buffer + offset, 1, bytes - offset, stdout);
                    break;
                }

                int before = fp - (buffer + offset);
                if (before > 0)
                {
                    fwrite(buffer + offset, 1, before, stdout);
                }

                char *nl = memchr(fp, '\n', bytes - offset - before);
                if (!nl)
                {
                    fprintf(stderr, "Cabecera de FILE incompleta\n");
                    break;
                }

                int header_len = nl - fp + 1;
                char hdr[BUFFER_SIZE];
                memcpy(hdr, fp, header_len);
                hdr[header_len - 1] = '\0';

                char *p = hdr + 5;
                char *remit = strtok(p, "|");
                char *fname = strtok(NULL, "|");
                long filesize = atol(strtok(NULL, "\0"));

                char localfile[256];
                snprintf(localfile, sizeof(localfile), "recv_%s", fname);
                FILE *f = fopen(localfile, "wb");
                if (!f)
                {
                    perror("fopen");
                    break;
                }

                int data_here = bytes - offset - before - header_len;
                if (data_here > 0)
                {
                    fwrite(nl + 1, 1, data_here, f);
                }
                long received = data_here;
                offset += before + header_len + data_here;

                while (received < filesize)
                {
                    int want = (filesize - received > BUFFER_SIZE) ? BUFFER_SIZE : filesize - received;
                    int r = recv(sock, buffer, want, 0);
                    if (r <= 0)
                    {
                        perror("recv archivo");
                        break;
                    }
                    fwrite(buffer, 1, r, f);
                    received += r;
                }
                fclose(f);
                printf("Archivo '%s' recibido de %s (%ld bytes)\n", fname, remit, received);
            }
        }

        // Entrada del usuario
        if (FD_ISSET(STDIN_FILENO, &read_fds))
        {
            if (fgets(buffer, BUFFER_SIZE, stdin) != NULL)
            {
                if (strncmp(buffer, "/file ", 6) == 0)
                {
                    char *p = buffer + 6;
                    char *dest = strtok(p, " \n");
                    char *path = strtok(NULL, " \n");
                    if (dest && path)
                    {
                        enviar_archivo_client(sock, dest, path);
                    }
                    else
                    {
                        printf("Uso: /file <destino> <ruta_del_archivo>\n");
                    }
                }
                else
                {
                    if (send(sock, buffer, strlen(buffer), 0) < 0)
                    {
                        perror("Error al enviar");
                        break;
                    }
                }
            }
        }
    }

    close(sock);
    exit(EXIT_SUCCESS);
}

void enviar_archivo_client(int sock, const char *dest, const char *filepath)
{
    FILE *f = fopen(filepath, "rb");
    if (!f)
    {
        perror("Error al abrir el archivo");
        return;
    }
    if (fseek(f, 0, SEEK_END) != 0)
    {
        perror("fseek");
        fclose(f);
        return;
    }
    long filesize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char header[BUFFER_SIZE];
    snprintf(header, sizeof(header), "FILE|%s|%s|%ld", dest, filepath, filesize);
    if (send(sock, header, strlen(header), 0) < 0)
    {
        perror("Error al enviar cabecera de archivo");
        fclose(f);
        return;
    }
    send(sock, "\n", 1, 0);

    char buf[FILE_CHUNK_SIZE];
    long sent = 0;
    while (sent < filesize)
    {
        size_t to_read = (filesize - sent > FILE_CHUNK_SIZE) ? FILE_CHUNK_SIZE : (filesize - sent);
        size_t r = fread(buf, 1, to_read, f);
        if (r <= 0)
        {
            perror("Error de lectura de archivo");
            break;
        }
        if (send(sock, buf, r, 0) < 0)
        {
            perror("Error al enviar datos de archivo");
            break;
        }
        sent += r;
    }
    fclose(f);
    printf("Archivo enviado a %s: %s (%ld bytes)\n", dest, filepath, sent);
}
