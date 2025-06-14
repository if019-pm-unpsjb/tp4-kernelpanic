#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#define CERRAR_SOCKET(s) closesocket(s)
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#define CERRAR_SOCKET(s) close(s)
#endif

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define NAME_SIZE 32
#define FILE_CHUNK_SIZE 1024

typedef struct
{
    int fd;
    char nombre[NAME_SIZE];
} Cliente;

Cliente clientes[MAX_CLIENTS];

void inicializar_clientes()
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        clientes[i].fd = -1;
        clientes[i].nombre[0] = '\0';
    }
}

int nombre_duplicado(const char *nombre)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clientes[i].fd != -1 && strcmp(clientes[i].nombre, nombre) == 0)
            return 1;
    return 0;
}

void enviar_lista_usuarios()
{
    char lista[BUFFER_SIZE] = "USERS|";
    int primero = 1;
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clientes[i].fd != -1)
        {
            if (!primero)
                strncat(lista, "|", sizeof(lista) - strlen(lista) - 1);
            strncat(lista, clientes[i].nombre, sizeof(lista) - strlen(lista) - 1);
            primero = 0;
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clientes[i].fd != -1)
            send(clientes[i].fd, lista, strlen(lista), 0);
    }
}

void enviar_privado(int idx_emisor, const char *dest, const char *texto)
{
    // Buscar socket de destino
    int fd_dest = -1;
    for (int j = 0; j < MAX_CLIENTS; j++)
        if (clientes[j].fd != -1 && strcmp(clientes[j].nombre, dest) == 0)
            fd_dest = clientes[j].fd;
    if (fd_dest < 0)
    {
        char *err = "ERROR|Usuario no encontrado\n";
        send(clientes[idx_emisor].fd, err, strlen(err), 0);
        return;
    }
    // Formatear y enviar
    char out[BUFFER_SIZE];
    snprintf(out, sizeof(out), "(Privado) %s: %s\n",
             clientes[idx_emisor].nombre, texto);
    send(fd_dest, out, strlen(out), 0);
}

void enviar_archivo(int idx_emisor, const char *destino, const char *filename, long filesize)
{
    char header[BUFFER_SIZE];

    // Cabecera: FILE|remitente|filename|filesize
    snprintf(header, sizeof(header), "FILE|%s|%s|%ld\n",
             clientes[idx_emisor].nombre, filename, filesize);

    // Buscar socket del destino
    int fd_dest = -1;
    for (int j = 0; j < MAX_CLIENTS; j++)
    {
        if (clientes[j].fd != -1 && strcmp(clientes[j].nombre, destino) == 0)
        {
            fd_dest = clientes[j].fd;
            break;
        }
    }
    if (fd_dest == -1)
    {
        char *err = "ERROR|Usuario receptor no encontrado\n";
        send(clientes[idx_emisor].fd, err, strlen(err), 0);
        return;
    }

    // Enviar cabecera al receptor
    send(fd_dest, header, strlen(header), 0);

    // Transmitir el contenido en chunks
    long remaining = filesize;
    char chunk[FILE_CHUNK_SIZE];
    while (remaining > 0)
    {
        int to_read = remaining > FILE_CHUNK_SIZE ? FILE_CHUNK_SIZE : remaining;
        int r = recv(clientes[idx_emisor].fd, chunk, to_read, 0);
        if (r <= 0)
            break; // error o cliente desconectado
        send(fd_dest, chunk, r, 0);
        remaining -= r;
    }
}

void desconectar_cliente(int idx)
{
    printf("Desconectado: %s\n", clientes[idx].nombre);
    CERRAR_SOCKET(clientes[idx].fd);
    clientes[idx].fd = -1;
    clientes[idx].nombre[0] = '\0';
    enviar_lista_usuarios();
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Uso: %s [PUERTO]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        printf("Error al iniciar Winsock: %d\n", WSAGetLastError());
        return 1;
    }
#endif

    int puerto = atoi(argv[1]);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr, cli_addr;
    socklen_t cli_len = sizeof(cli_addr);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(puerto);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_CLIENTS) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    inicializar_clientes();
    printf("Servidor escuchando en el puerto %d\n", puerto);

    fd_set readfds;

    while (1)
    {
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        int max_fd = server_fd;

        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clientes[i].fd != -1)
            {
                FD_SET(clientes[i].fd, &readfds);
                if (clientes[i].fd > max_fd)
                    max_fd = clientes[i].fd;
            }
        }

        if (select(max_fd + 1, &readfds, NULL, NULL, NULL) < 0)
        {
            perror("select");
            continue;
        }

        if (FD_ISSET(server_fd, &readfds))
        {
            int nuevo_fd = accept(server_fd, (struct sockaddr *)&cli_addr, &cli_len);
            if (nuevo_fd < 0)
                continue;

            int idx_libre = -1;
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (clientes[i].fd == -1)
                {
                    idx_libre = i;
                    break;
                }
            }

            if (idx_libre == -1)
            {
                char *msg = "Servidor lleno\n";
                send(nuevo_fd, msg, strlen(msg), 0);
                CERRAR_SOCKET(nuevo_fd);
                continue;
            }

            char nombre[NAME_SIZE] = {0};
            int bytes = recv(nuevo_fd, nombre, NAME_SIZE - 1, 0);
            if (bytes <= 0)
            {
                CERRAR_SOCKET(nuevo_fd);
                continue;
            }

            // Eliminar CR/LF final si lo hay
            nombre[bytes] = '\0';
            nombre[strcspn(nombre, "\r\n")] = '\0';

            if (nombre_duplicado(nombre))
            {
                char *msg = "Nombre inválido o duplicado\n";
                send(nuevo_fd, msg, strlen(msg), 0);
                CERRAR_SOCKET(nuevo_fd);
                continue;
            }

            strncpy(clientes[idx_libre].nombre, nombre, NAME_SIZE - 1);
            clientes[idx_libre].fd = nuevo_fd;

            printf("Conectado: %s\n", clientes[idx_libre].nombre);
            enviar_lista_usuarios();
        }

        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clientes[i].fd != -1 && FD_ISSET(clientes[i].fd, &readfds))
            {
                char buffer[BUFFER_SIZE] = {0};
                int bytes = recv(clientes[i].fd, buffer, BUFFER_SIZE - 1, 0);
                if (bytes <= 0)
                {
                    desconectar_cliente(i);
                    continue;
                }

                // Protocolo privado: PRIV|destino|texto
                if (strncmp(buffer, "PRIV|", 5) == 0)
                {
                    buffer[bytes] = '\0';
                    char *p = buffer + 5;
                    char *dst = strtok(p, "|");
                    char *msg = strtok(NULL, "\n");
                    // enviar_privado toma el índice del emisor, el nombre del destino y el texto
                    enviar_privado(i, dst, msg);
                    continue;
                }
                // Protocolo: FILE|destino|filename|size\n + datos
                if (strncmp(buffer, "FILE|", 5) == 0)
                {
                    // 1) Asegura el fin de cabecera
                    if (bytes < 6)
                        continue; // muy corto
                    // Busca el '\n' que termina la cabecera
                    char *nl = memchr(buffer, '\n', bytes);
                    if (!nl)
                    {
                        fprintf(stderr, "Cabecera de FILE incompleta\n");
                        continue;
                    }
                    int header_len = nl - buffer + 1;

                    // 2) Extrae y parsea la cabecera
                    char hdr[BUFFER_SIZE];
                    memcpy(hdr, buffer, header_len);
                    hdr[header_len - 1] = '\0'; // quitar '\n'
                    char *p = hdr + 5;
                    char *dest = strtok(p, "|");
                    char *fname = strtok(NULL, "|");
                    long fsize = atol(strtok(NULL, "\0"));

                    // 3) Busca el socket destino
                    int fd_dest = -1;
                    for (int j = 0; j < MAX_CLIENTS; j++)
                    {
                        if (clientes[j].fd != -1 &&
                            strcmp(clientes[j].nombre, dest) == 0)
                        {
                            fd_dest = clientes[j].fd;
                            break;
                        }
                    }
                    if (fd_dest < 0)
                    {
                        char *err = "ERROR|Usuario receptor no encontrado\n";
                        send(clientes[i].fd, err, strlen(err), 0);
                        continue;
                    }

                    // 4) Envía la cabecera ya formateada
                    char out_hdr[BUFFER_SIZE];
                    snprintf(out_hdr, sizeof(out_hdr),
                             "FILE|%s|%s|%ld\n",
                             clientes[i].nombre, fname, fsize);
                    send(fd_dest, out_hdr, strlen(out_hdr), 0);

                    // 5) Envía los datos “sobrantes” tras la cabecera
                    int leftover = bytes - header_len;
                    if (leftover > 0)
                    {
                        send(fd_dest, nl + 1, leftover, 0);
                    }
                    long remaining = fsize - leftover;

                    // 6) Lee y reenvía el resto de los datos
                    char chunk[FILE_CHUNK_SIZE];
                    while (remaining > 0)
                    {
                        int to_read = remaining > FILE_CHUNK_SIZE
                                          ? FILE_CHUNK_SIZE
                                          : remaining;
                        int r = recv(clientes[i].fd, chunk, to_read, 0);
                        if (r <= 0)
                            break;
                        send(fd_dest, chunk, r, 0);
                        remaining -= r;
                    }

                    continue;
                }

                buffer[bytes] = '\0';
                char *cmd = strtok(buffer, "|");
                if (cmd && strcmp(cmd, "TO") == 0)
                {
                    char *destino = strtok(NULL, "|");
                    char *mensaje = strtok(NULL, "");
                    if (destino && mensaje)
                        enviar_privado(i, destino, mensaje);
                }
            }
        }
    }

    CERRAR_SOCKET(server_fd);

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
