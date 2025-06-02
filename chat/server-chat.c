#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define NAME_SIZE 32

typedef struct
{
    int fd;
    char nombre[NAME_SIZE];
} Cliente;

Cliente clientes[MAX_CLIENTS];

void enviar_lista_usuarios()
{
    char lista[BUFFER_SIZE] = "USERS|";
    int primero = 1;
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clientes[i].fd != -1)
        {
            if (!primero)
                strcat(lista, "|");
            strcat(lista, clientes[i].nombre);
            primero = 0;
        }
    }
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clientes[i].fd != -1)
        {
            send(clientes[i].fd, lista, strlen(lista), 0);
        }
    }
}

void enviar_privado(const char *remitente, const char *destino, const char *mensaje)
{
    char mensaje_formateado[BUFFER_SIZE];
    snprintf(mensaje_formateado, sizeof(mensaje_formateado), "FROM|%s|%s", remitente, mensaje);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clientes[i].fd != -1 && strcmp(clientes[i].nombre, destino) == 0)
        {
            send(clientes[i].fd, mensaje_formateado, strlen(mensaje_formateado), 0);
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        printf("Uso: %s [IP] [PUERTO]\n", argv[0]);
        return 1;
    }

    const char *ip = argv[1];
    int puerto = atoi(argv[2]);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_addr, cli_addr;
    socklen_t cli_len = sizeof(cli_addr);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(puerto);
    inet_pton(AF_INET, ip, &server_addr.sin_addr);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind");
        return 1;
    }

    if (listen(server_fd, MAX_CLIENTS) < 0)
    {
        perror("listen");
        return 1;
    }

    for (int i = 0; i < MAX_CLIENTS; i++)
        clientes[i].fd = -1;

    printf("Servidor escuchando en %s:%d\n", ip, puerto);

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

        // Nueva conexión
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
                close(nuevo_fd);
            }
            else
            {
                // Recibir nombre
                char buffer[NAME_SIZE];
                int bytes = recv(nuevo_fd, buffer, NAME_SIZE - 1, 0);
                if (bytes <= 0)
                {
                    close(nuevo_fd);
                    continue;
                }
                buffer[bytes] = '\0';

                clientes[idx_libre].fd = nuevo_fd;
                strncpy(clientes[idx_libre].nombre, buffer, NAME_SIZE - 1);
                clientes[idx_libre].nombre[NAME_SIZE - 1] = '\0';

                printf("Conectado: %s\n", clientes[idx_libre].nombre);
                enviar_lista_usuarios();
            }
        }

        // Mensajes de clientes
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clientes[i].fd != -1 && FD_ISSET(clientes[i].fd, &readfds))
            {
                char buffer[BUFFER_SIZE];
                int bytes = recv(clientes[i].fd, buffer, BUFFER_SIZE - 1, 0);
                if (bytes <= 0)
                {
                    printf("Desconectado: %s\n", clientes[i].nombre);
                    close(clientes[i].fd);
                    clientes[i].fd = -1;
                    enviar_lista_usuarios();
                    continue;
                }

                buffer[bytes] = '\0';

                // Parsear TO|destino|mensaje
                char *cmd = strtok(buffer, "|");
                if (cmd && strcmp(cmd, "TO") == 0)
                {
                    char *destino = strtok(NULL, "|");
                    char *mensaje = strtok(NULL, "");
                    if (destino && mensaje)
                    {
                        enviar_privado(clientes[i].nombre, destino, mensaje);
                    }
                }
            }
        }
    }

    close(server_fd);
    exit(EXIT_SUCCESS);
}
