#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>

#define BUFFER_SIZE 1024

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

    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    fd_set read_fds;

    // Crear socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Error al crear el socket");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    // Convertir IP a binario
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0)
    {
        perror("Dirección IP inválida");
        exit(EXIT_FAILURE);
    }

    // Conectarse al servidor
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Conexión fallida");
        exit(EXIT_FAILURE);
    }

    printf("Conectado al servidor. Escribe tus mensajes:\n");

    while (1)
    {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds); // Teclado
        FD_SET(sock, &read_fds);         // Socket

        int max_fd = sock > STDIN_FILENO ? sock : STDIN_FILENO;
        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0)
        {
            perror("select");
            exit(EXIT_FAILURE);
        }

        // Leer mensaje del servidor
        if (FD_ISSET(sock, &read_fds))
        {
            int bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0);
            
            if (bytes <= 0)
            {
                printf("Servidor desconectado.\n");
                break;
            }
            
            buffer[bytes] = '\0';
            printf("\n[Mensaje]: %s\n", buffer);
            printf("> ");
            fflush(stdout);
        }

        // Leer entrada del usuario
        if (FD_ISSET(STDIN_FILENO, &read_fds))
        {
            if (fgets(buffer, BUFFER_SIZE, stdin) != NULL)
            {
                if (send(sock, buffer, strlen(buffer), 0) < 0)
                {
                    perror("Error al enviar");
                    break;
                }
            }
        }
    }

    close(sock);
    exit(EXIT_SUCCESS);
}
