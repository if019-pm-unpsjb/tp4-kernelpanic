/*
 * Cliente TFTP en C con soporte de RRQ, WRQ y manejo de errores (códigos 1 y 6)
 *
 * Uso:
 *   Para lectura (RRQ):
 *     ./cliente2 -r <IP-servidor> <puerto> <archivo_remoto>
 *
 *   Para escritura (WRQ):
 *     ./cliente2 -w <IP-servidor> <puerto> <archivo_local>
 *
 * Ejemplos:
 *   ./cliente2 -r 127.0.0.1 1069 ejemplo.txt
 *   ./cliente2 -w 127.0.0.1 1069 subir.bin
 *
 * Este cliente:
 *   - Construye y envía RRQ/WRQ en modo \"octet\".(binario)
 *   - Intercambia DATA/ACK bloque a bloque.
 *   - Detecta y muestra paquetes ERROR (opcode=5, códigos 1 y 6).
 *   - Usa timeout de 5 segundos para recvfrom().
 *
 * Compilar:
 *   gcc -o cliente2 cliente2.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>

#define TFTP_PORT       1069
#define BLOCK_SIZE      512
#define MAX_PACKET_SIZE 516   /* 2 bytes opcode + 2 bytes block + 512 bytes datos */

#define OPCODE_RRQ   1
#define OPCODE_WRQ   2
#define OPCODE_DATA  3
#define OPCODE_ACK   4
#define OPCODE_ERROR 5

/* Códigos de error específicos */
#define TFTP_ERR_NOTFOUND     1
#define TFTP_ERR_EXISTS       6

/* Prototipos */
void usage(const char *progname);
void receive_file(const char *ip, int port, const char *filename);
void send_file(const char *ip, int port, const char *filename);

/* Construye y envía un ACK con número de bloque 'block' */
void send_ack(int sockfd, struct sockaddr_in *server_addr, socklen_t addr_len, uint16_t block) {
    uint8_t buf[4];
    uint16_t op_net   = htons(OPCODE_ACK);
    uint16_t block_net= htons(block);
    memcpy(buf + 0, &op_net,    2);
    memcpy(buf + 2, &block_net, 2);
    if (sendto(sockfd, buf, 4, 0, (struct sockaddr *)server_addr, addr_len) < 0) {
        perror("sendto ACK");
        exit(EXIT_FAILURE);
    }
}

/* Muestra mensaje de uso y sale */
void usage(const char *progname) {
    fprintf(stderr,
        "Uso:\n"
        "  %s -r <IP-servidor> <puerto> <archivo_remoto>   (descargar archivo)\n"
        "  %s -w <IP-servidor> <puerto> <archivo_local>    (subir archivo)\n",
        progname, progname);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        usage(argv[0]);
    }

    const char *mode = argv[1];
    const char *ip   = argv[2];
    int port         = atoi(argv[3]);
    const char *filename = argv[4];

    if (strcmp(mode, "-r") == 0) {
        receive_file(ip, port, filename);
    }
    else if (strcmp(mode, "-w") == 0) {
        send_file(ip, port, filename);
    }
    else {
        usage(argv[0]);
    }

    return 0;
}

/*
 * receive_file: envia RRQ para 'filename', recibe bloques DATA y 
 * responde con ACK; maneja ERROR(1).
 */
void receive_file(const char *ip, int port, const char *filename) {
    int sockfd;
    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);

    /* Crear socket UDP */
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    /* Configurar timeout de 5 segundos en recvfrom() */
    struct timeval tv = {10, 0};
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("setsockopt SO_RCVTIMEO");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* Llenar estructura de servidor */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(port);
    if (inet_aton(ip, &server_addr.sin_addr) == 0) {
        fprintf(stderr, "IP inválida: %s\n", ip);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* Construir paquete RRQ: [opcode=1][filename][0]['o''c''t''e''t'][0] */
    size_t name_len = strlen(filename);
    const char *mode_str = "octet";
    size_t mode_len = strlen(mode_str);
    size_t pkt_len = 2 + name_len + 1 + mode_len + 1;
    uint8_t *rrq = malloc(pkt_len);
    if (!rrq) {
        perror("malloc");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    uint16_t op_net = htons(OPCODE_RRQ);
    memcpy(rrq + 0, &op_net, 2);
    memcpy(rrq + 2, filename, name_len);
    rrq[2 + name_len] = 0;
    memcpy(rrq + 3 + name_len, mode_str, mode_len);
    rrq[3 + name_len + mode_len] = 0;

    /* Enviar RRQ */
    if (sendto(sockfd, rrq, pkt_len, 0, (struct sockaddr *)&server_addr, addr_len) < 0) {
        perror("sendto RRQ");
        free(rrq);
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    free(rrq);

    /* Abrir archivo local para escritura */
    FILE *file = fopen(filename, "wb");
    if (!file) {
        perror("fopen para escritura");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    uint16_t expected_block = 1;

    while (1) {
        uint8_t buf[MAX_PACKET_SIZE];
        ssize_t n = recvfrom(sockfd, buf, sizeof(buf), 0, (struct sockaddr *)&server_addr, &addr_len);
        if (n < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                fprintf(stderr, "Timeout esperando DATA\n");
            } else {
                perror("recvfrom DATA");
            }
            fclose(file);
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        /* Leer opcode y code_or_block */
        uint16_t opcode = ntohs(*(uint16_t *)(buf + 0));
        uint16_t code_or_block = ntohs(*(uint16_t *)(buf + 2));

        if (opcode == OPCODE_ERROR) {
            /* Error recibido */
            char *err_msg = (char *)(buf + 4);
            fprintf(stderr, "TFTP Error %d: %s\n", code_or_block, err_msg);
            fclose(file);
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        else if (opcode != OPCODE_DATA) {
            fprintf(stderr, "Paquete inesperado (opcode=%d)\n", opcode);
            fclose(file);
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        /* Es paquete DATA */
        uint16_t block = code_or_block;
        if (block != expected_block) {
            /* reenviar ACK del último bloque válido */
            send_ack(sockfd, &server_addr, addr_len, expected_block - 1);
            continue;
        }

        size_t data_len = n - 4;
        if (fwrite(buf + 4, 1, data_len, file) != data_len) {
            perror("fwrite");
            fclose(file);
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        /* Enviar ACK de este bloque */
        send_ack(sockfd, &server_addr, addr_len, block);

        if (data_len < BLOCK_SIZE) {
            /* último bloque recibido */
            printf("Descarga completa: %s (%zu bytes)\n", filename, ftell(file));
            break;
        }
        expected_block++;
    }

    fclose(file);
    close(sockfd);
}

/*
 * send_file: envia WRQ para 'filename', luego envía bloques DATA,
 * maneja ERROR(6) y espera ACK de cada bloque.
 */
void send_file(const char *ip, int port, const char *filename) {
    int sockfd;
    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);

    /* Abrir archivo local para lectura */
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("fopen para lectura");
        exit(EXIT_FAILURE);
    }

    /* Crear socket UDP */
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    /* Configurar timeout de 5 segundos en recvfrom() */
    struct timeval tv = {10, 0};
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("setsockopt SO_RCVTIMEO");
        fclose(file);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* Llenar estructura de servidor */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(port);
    if (inet_aton(ip, &server_addr.sin_addr) == 0) {
        fprintf(stderr, "IP inválida: %s\n", ip);
        fclose(file);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* Construir paquete WRQ: [opcode=2][filename][0]['o''c''t''e''t'][0] */
    size_t name_len = strlen(filename);
    const char *mode_str = "octet";
    size_t mode_len = strlen(mode_str);
    size_t pkt_len = 2 + name_len + 1 + mode_len + 1;
    uint8_t *wrq = malloc(pkt_len);
    if (!wrq) {
        perror("malloc");
        fclose(file);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    uint16_t op_net = htons(OPCODE_WRQ);
    memcpy(wrq + 0, &op_net, 2);
    memcpy(wrq + 2, filename, name_len);
    wrq[2 + name_len] = 0;
    memcpy(wrq + 3 + name_len, mode_str, mode_len);
    wrq[3 + name_len + mode_len] = 0;

    /* Enviar WRQ */
    if (sendto(sockfd, wrq, pkt_len, 0, (struct sockaddr *)&server_addr, addr_len) < 0) {
        perror("sendto WRQ");
        free(wrq);
        fclose(file);
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    free(wrq);

    /* Esperar respuesta: puede ser ACK(0) o ERROR */
    uint8_t buf[MAX_PACKET_SIZE];
    ssize_t n = recvfrom(sockfd, buf, sizeof(buf), 0, (struct sockaddr *)&server_addr, &addr_len);
    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            fprintf(stderr, "Timeout esperando ACK 0\n");
        } else {
            perror("recvfrom ACK 0 / ERROR");
        }
        fclose(file);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    uint16_t opcode = ntohs(*(uint16_t *)(buf + 0));
    uint16_t code_or_block = ntohs(*(uint16_t *)(buf + 2));

    if (opcode == OPCODE_ERROR) {
        /* ERROR recibido */
        char *err_msg = (char *)(buf + 4);
        fprintf(stderr, "TFTP Error %d: %s\n", code_or_block, err_msg);
        fclose(file);
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    else if (opcode != OPCODE_ACK || code_or_block != 0) {
        fprintf(stderr, "Respuesta inesperada: opcode=%d, block=%d\n", opcode, code_or_block);
        fclose(file);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* Si llegamos aquí, recibimos ACK(0). Enviar bloques DATA */
    uint16_t block_num = 1;
    while (1) {
        uint8_t data_pkt[4 + BLOCK_SIZE] = {0};
        size_t bytes_read = fread(data_pkt + 4, 1, BLOCK_SIZE, file);
        if (bytes_read < 0) {
            perror("fread");
            break;
        }

        uint16_t data_op_net = htons(OPCODE_DATA);
        uint16_t blk_net     = htons(block_num);
        memcpy(data_pkt + 0, &data_op_net, 2);
        memcpy(data_pkt + 2, &blk_net,    2);

        /* Enviar DATA blok N */
        size_t send_len = 4 + bytes_read;
        if (sendto(sockfd, data_pkt, send_len, 0, (struct sockaddr *)&server_addr, addr_len) < 0) {
            perror("sendto DATA");
            break;
        }

        /* Esperar ACK N o ERROR */
        n = recvfrom(sockfd, buf, sizeof(buf), 0, (struct sockaddr *)&server_addr, &addr_len);
        if (n < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                fprintf(stderr, "Timeout esperando ACK %d\n", block_num);
            } else {
                perror("recvfrom ACK / ERROR");
            }
            fclose(file);
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        opcode = ntohs(*(uint16_t *)(buf + 0));
        code_or_block = ntohs(*(uint16_t *)(buf + 2));

        if (opcode == OPCODE_ERROR) {
            char *err_msg = (char *)(buf + 4);
            fprintf(stderr, "TFTP Error %d: %s\n", code_or_block, err_msg);
            fclose(file);
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        else if (opcode != OPCODE_ACK || code_or_block != block_num) {
            fprintf(stderr, "ACK inválido: opcode=%d, block=%d\n", opcode, code_or_block);
            fclose(file);
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        if (bytes_read < BLOCK_SIZE) {
            /* último bloque enviado */
            printf("Subida completa: %s\n", filename);
            break;
        }
        block_num++;
    }

    fclose(file);
    close(sockfd);
}
