#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define MAX_CLIENTS 2
#define BUFFER_SIZE 1024
#define NAME_SIZE 32

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        printf("Uso: [ip del servidor] [puerto]\n");
        exit(EXIT_FAILURE);
    }

    const int port = atoi(argv[2]);
    if (port <= 0 || port > 65535)
    {
        fprintf(stderr, "Error: Puerto inválido '%s'. Debe estar entre 1 y 65535.\n", argv[2]);
        exit(EXIT_FAILURE);
    }

    const char *ip = argv[1];

    int server_fd, client_fd[2] = {-1, -1}, new_socket;

    char nombres[MAX_CLIENTS][NAME_SIZE] = {"", ""}; // Nombres de los clientes

    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE];
    fd_set readfds;

    // Crear socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Fallo en socket");
        exit(EXIT_FAILURE);
    }

    // Configurar dirección (configuracion de la estructura del socket)
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &address.sin_addr) <= 0)
    {
        perror("Dirección IP inválida");
        exit(EXIT_FAILURE);
    }

    // Asociar socket a dirección
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("Fallo en bind");
        exit(EXIT_FAILURE);
    }

    // Escuchar conexiones
    if (listen(server_fd, 2) < 0)
    {
        perror("Fallo en listen");
        exit(EXIT_FAILURE);
    }

    printf("Servidor esperando a dos clientes...\n");

    while (1)
    {
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        int max_fd = server_fd;

        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (client_fd[i] != -1)
            {
                FD_SET(client_fd[i], &readfds);
                if (client_fd[i] > max_fd)
                    max_fd = client_fd[i];
            }
        }

        if (select(max_fd + 1, &readfds, NULL, NULL, NULL) < 0)
        {
            perror("Error en select");
            exit(EXIT_FAILURE);
        }

        // Nueva conexión entrante
        if (FD_ISSET(server_fd, &readfds))
        {
            new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
            if (new_socket < 0)
            {
                perror("Fallo en accept");
                continue;
            }

            // Aceptar hasta 2 clientes
            int assigned = 0;
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (client_fd[i] == -1)
                {
                    client_fd[i] = new_socket;
                    printf("Cliente %d conectado\n", i + 1);
                    assigned = 1;
                    break;
                }
            }

            if (!assigned)
            {
                char *msg = "Servidor ocupado. Intente más tarde.\n";
                send(new_socket, msg, strlen(msg), 0);
                close(new_socket);
            }
        }

        // Comunicación entre clientes
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (client_fd[i] != -1 && FD_ISSET(client_fd[i], &readfds))
            {
                int bytes = recv(client_fd[i], buffer, BUFFER_SIZE - 1, 0);
                if (bytes <= 0)
                {
                    printf("Cliente %d desconectado\n", i + 1);
                    close(client_fd[i]);
                    client_fd[i] = -1;

                    // Cierra la conexión del otro cliente también
                    int other = (i + 1) % 2;
                    if (client_fd[other] != -1)
                    {
                        char *msg = "El otro usuario se desconectó.\n";
                        send(client_fd[other], msg, strlen(msg), 0);
                        close(client_fd[other]);
                        client_fd[other] = -1;
                    }
                }
                else
                {
                    buffer[bytes] = '\0';
                    int other = (i + 1) % 2;

                    if (client_fd[other] != -1)
                        send(client_fd[other], buffer, strlen(buffer), 0);
                }
            }
        }
    }

    exit(EXIT_SUCCESS);
}
