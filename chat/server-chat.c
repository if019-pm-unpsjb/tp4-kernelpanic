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

void enviar_privado(const char *remitente, const char *destino, const char *mensaje)
{
    char mensaje_formateado[BUFFER_SIZE];
    snprintf(mensaje_formateado, sizeof(mensaje_formateado), "FROM|%s|%s", remitente, mensaje);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clientes[i].fd != -1 && strcmp(clientes[i].nombre, destino) == 0)
        {
            send(clientes[i].fd, mensaje_formateado, strlen(mensaje_formateado), 0);
            return;
        }
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
    if (argc != 3)
    {
        printf("Uso: %s [IP] [PUERTO]\n", argv[0]);
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

    const char *ip = argv[1];
    int puerto = atoi(argv[2]);

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
    server_addr.sin_addr.s_addr = inet_addr(ip);

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
            if (bytes <= 0 || nombre_duplicado(nombre))
            {
                char *msg = "Nombre inválido o duplicado\n";
                send(nuevo_fd, msg, strlen(msg), 0);
                CERRAR_SOCKET(nuevo_fd);
                continue;
            }

            nombre[bytes] = '\0';
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

                buffer[bytes] = '\0';
                char *cmd = strtok(buffer, "|");
                if (cmd && strcmp(cmd, "TO") == 0)
                {
                    char *destino = strtok(NULL, "|");
                    char *mensaje = strtok(NULL, "");
                    if (destino && mensaje)
                        enviar_privado(clientes[i].nombre, destino, mensaje);
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
