#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

#define TFTP_OPCODE_RRQ   1
#define TFTP_OPCODE_WRQ   2
#define TFTP_OPCODE_DATA  3
#define TFTP_OPCODE_ACK   4
#define TFTP_OPCODE_ERROR 5

#define TFTP_BLOCK_SIZE 512

struct tftp_packet {
    uint16_t opcode;
    union {
        struct {
            uint16_t block;
            char data[TFTP_BLOCK_SIZE];
        } data;
        struct {
            uint16_t block;
        } ack;
        struct {
            uint16_t error_code;
            char error_msg[TFTP_BLOCK_SIZE];
        } error;
        char filename_mode[TFTP_BLOCK_SIZE * 2];
    };
};

void send_file(const char *server_ip, int port, const char *filename) {
    int sockfd;
    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);
    FILE *file;
    int bytes_read;
    uint16_t block_num = 1;
    struct timeval timeout;

    // Crear socket UDP
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configurar timeout para el socket
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    // Construir paquete WRQ
    struct tftp_packet wrq_packet;
    wrq_packet.opcode = htons(TFTP_OPCODE_WRQ);
    snprintf(wrq_packet.filename_mode, sizeof(wrq_packet.filename_mode), "%s%c%s%c", 
             filename, 0, "octet", 0);

    // Enviar WRQ
    if (sendto(sockfd, &wrq_packet, strlen(filename) + strlen("octet") + 4, 0,
               (const struct sockaddr *) &server_addr, addr_len) < 0) {
        perror("sendto failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("WRQ enviado para el archivo: %s\n", filename);

    // Esperar ACK para el bloque 0
    struct tftp_packet ack_packet;
    int n = recvfrom(sockfd, &ack_packet, sizeof(ack_packet), 0,
                    (struct sockaddr *) &server_addr, &addr_len);
    
    if (n < 0) {
        perror("recvfrom failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (ntohs(ack_packet.opcode) != TFTP_OPCODE_ACK || ntohs(ack_packet.ack.block) != 0) {
        fprintf(stderr, "ACK incorrecto recibido\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("ACK 0 recibido. Comenzando transferencia...\n");

    // Abrir archivo para lectura
    file = fopen(filename, "rb");
    if (!file) {
        perror("fopen failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Enviar datos del archivo
    while (1) {
        struct tftp_packet data_packet;
        data_packet.opcode = htons(TFTP_OPCODE_DATA);
        data_packet.data.block = htons(block_num);

        bytes_read = fread(data_packet.data.data, 1, TFTP_BLOCK_SIZE, file);
        
        // Enviar paquete DATA
        if (sendto(sockfd, &data_packet, bytes_read + 4, 0,
                  (const struct sockaddr *) &server_addr, addr_len) < 0) {
            perror("sendto failed");
            fclose(file);
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        printf("Enviado bloque %d (%d bytes)\n", block_num, bytes_read);

        // Esperar ACK
        n = recvfrom(sockfd, &ack_packet, sizeof(ack_packet), 0,
                    (struct sockaddr *) &server_addr, &addr_len);
        
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("Timeout, reenviando bloque %d\n", block_num);
                continue; // Reintentar el mismo bloque
            } else {
                perror("recvfrom failed");
                fclose(file);
                close(sockfd);
                exit(EXIT_FAILURE);
            }
        }

        if (ntohs(ack_packet.opcode) != TFTP_OPCODE_ACK || 
            ntohs(ack_packet.ack.block) != block_num) {
            fprintf(stderr, "ACK incorrecto recibido para el bloque %d\n", block_num);
            fclose(file);
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        printf("ACK %d recibido\n", block_num);

        if (bytes_read < TFTP_BLOCK_SIZE) {
            break; // Fin del archivo
        }

        block_num++;
    }

    fclose(file);
    close(sockfd);
    printf("Transferencia completada exitosamente.\n");
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <direccion_servidor> <puerto> <archivo_a_enviar>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    const char *filename = argv[3];

    send_file(server_ip, port, filename);

    return 0;
}