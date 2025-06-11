/* PARA PROBAR LA CONCURRENCIA DE MANERA LOCAL SE PUEDE REALIZAR
    EL SIGUIENTE SCRIPT!
(
    echo "mode octet"
    echo "put 1.txt"
    sleep 1
) | tftp 127.0.0.1 28002 &

(
    echo "mode octet"
    echo "put 2.txt"
    sleep 1
) | tftp 127.0.0.1 28002 &

*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>            
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>

struct tftp_data {
    uint16_t opcode;  // 3 = DATA
    uint16_t block;   // número de bloque
    char     data[512];
};

struct tftp_ack {
    uint16_t opcode;  // 4 = ACK
    uint16_t block;
};

void tftp_rrq(char buffer[], int sockfd, struct sockaddr_in client_addr, socklen_t addr_len, int bytes_recv);
void tftp_wrq(char buffer[], int sockfd, struct sockaddr_in client_addr, socklen_t addr_len);
void send_ack(int sockfd, struct sockaddr_in *client_addr, socklen_t addr_len, uint16_t block);

void sigchld_handler(int signo) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

#define MAX_BUFFER  516

// Lleva la cuenta de qué puerto usará el siguiente proceso hijo.
// Empieza en PORT_BASE+1 y va subiendo.

int main(int argc, char *argv[]) {
    if (argc != 2) 
    {
      printf("Uso: %s [PUERTO]\n", argv[0]);
      exit(EXIT_FAILURE);
    }

    const int PORT_BASE = atoi(argv[1]);
    uint16_t next_port = PORT_BASE + 1;
    int server_fd, opt = 1;
    struct sockaddr_in server_addr, client_addr;
    char buffer[MAX_BUFFER];
    socklen_t addr_len = sizeof(client_addr);

    // Manejar hijos zombie
    signal(SIGCHLD, sigchld_handler);

    // Crear socket UDP principal
    if ((server_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Permitir reutilización inmediata de puerto
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    #ifdef SO_REUSEPORT
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    #endif

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT_BASE);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    printf("Servidor TFTP escuchando en puerto %d\n", PORT_BASE);

    while (1) {
        int bytes_recv = recvfrom(server_fd, buffer, sizeof(buffer), 0,
                                 (struct sockaddr *)&client_addr, &addr_len);
        if (bytes_recv < 0) {
            perror("recvfrom");
            continue;
        }

        // Solo RRQ (1) o WRQ (2)
        uint16_t opcode = ntohs(*(uint16_t *)buffer);

         int child_port = 0;
        // El padre reserva el puerto antes de forkear
        child_port = next_port++;


        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            continue;
        }
        if (pid == 0) {  // Proceso hijo
    close(server_fd);

    int child_sock, bind_ok = 0, retries = 0;
    do {
        child_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (child_sock < 0) {
            perror("socket hijo");
            exit(EXIT_FAILURE);
        }

        setsockopt(child_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        #ifdef SO_REUSEPORT
        setsockopt(child_sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
        #endif

        struct sockaddr_in child_addr;
        memset(&child_addr, 0, sizeof(child_addr));
        child_addr.sin_family = AF_INET;
        child_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        child_addr.sin_port = htons(child_port);   // <-- Usá el valor que heredó del padre

        if (bind(child_sock, (struct sockaddr *)&child_addr, sizeof(child_addr)) == 0) {
            bind_ok = 1;
        } else {
            close(child_sock);
            if (retries++ > 100) {
                fprintf(stderr, "No se pudo bindear un puerto para el hijo\n");
                exit(EXIT_FAILURE);
            }
        }
    } while (!bind_ok);

    printf("Hijo atendiendo cliente en puerto %d (PID %d)\n", child_port, getpid());

    // El hijo atiende SOLO a este cliente
    if (opcode == 1) {
        tftp_rrq(buffer, child_sock, client_addr, addr_len, bytes_recv);
    } else if (opcode == 2) {
        tftp_wrq(buffer, child_sock, client_addr, addr_len);
    } else {
        printf("Opcode desconocido: %u\n", opcode);
    }

    close(child_sock);
    exit(0);
        // El padre sigue escuchando
    }
}
    // Nunca llega aquí
    close(server_fd);
    return 0;
}


//Read Request
void tftp_rrq(char buffer[], int sockfd, struct sockaddr_in client_addr, socklen_t addr_len, int bytes_recv)
{
    char *filename = &buffer[2];
    char *mode = filename + strlen(filename) + 1;
    printf("Received RRQ for file: %s, mode: %s\n", filename, mode);

  /*   FILE *file = fopen(filename, "rb");
    if (!file)
    {
        perror("fopen");
        close(sockfd);
        exit(EXIT_FAILURE);
    } */
   //---
    FILE *file = fopen(filename, "rb");
            if (file == NULL) {
        /* fopen falló → construyo y envío paquete ERROR(1) en este mismo bloque */
        uint8_t err_buf[4 + 16 + 1];
        uint16_t op_net = htons(5);   /* opcode = 5 (ERROR) */
        memcpy(err_buf + 0, &op_net, 2);

        uint16_t code_net = htons(1); /* código = 1 (File not found) */
        memcpy(err_buf + 2, &code_net, 2);

        /* Mensaje ASCII corto: “File not found” */
        const char *msg = "File not found";
        memcpy(err_buf + 4, msg, strlen(msg));
        /* err_buf[4 + strlen(msg)] ya está a '\0' */
        
        /* Enviar por UDP de inmediato */
        sendto(sockfd, err_buf, 4 + strlen(msg) + 1, 0,
            (struct sockaddr *)&client_addr, addr_len);

        close(sockfd);
        return;
    }
//--

    uint16_t block = 1;

    while (1)
    {
        struct tftp_data packet;
        memset(&packet, 0, sizeof(packet));
        packet.opcode = htons(3); // Opcode for DATA
        packet.block = htons(block);

        int bytes_read = fread(packet.data, 1, sizeof(packet.data), file);
        int packet_size = bytes_read + 4; // 2 bytes for opcode and 2 bytes for block

        if (sendto(sockfd, &packet, packet_size, 0, (struct sockaddr *)&client_addr, addr_len) < 0)
        {
            perror("sendto");
            break;
        }

        //espera el ACK del cliente...
        bytes_recv = recvfrom(sockfd, buffer, MAX_BUFFER, 0, (struct sockaddr *)&client_addr, &addr_len);
        if (bytes_recv < 0)
        {
            perror("recvfrom ACK");
            break;
        }

        struct tftp_ack *ack = (struct tftp_ack *)buffer;
        if (ntohs(ack->opcode) != 4 || ntohs(ack->block) != block)
        {
            fprintf(stderr, "Invalid ACK received\n");
            break;
        }

        //Si se leyó menos de 512 bytes, 
        //significa que fue el último bloque del archivo, 
        //y se rompe el bucle.
        if (bytes_read < sizeof(packet.data))
            break;

        block++;
    }

    fclose(file);
    printf("Archivo enviado exitosamente.\n");
}

void tftp_wrq(char buffer[], int sockfd, struct sockaddr_in client_addr, socklen_t addr_len)
{
    // 1. Extraer nombre de archivo y modo (netascii, octet, etc.)
    char *filename = &buffer[2];
    char *mode = filename + strlen(filename) + 1;
    printf("Received WRQ for file: %s, mode: %s\n", filename, mode);

    /* 1) Verificar si el archivo YA existe */
      if (access(filename, F_OK) == 0)
    {
        /* Construir y enviar paquete ERROR(6, "File already exists") en línea */
        uint8_t err_buf[4 + 20 + 1] = { 0 };
        uint16_t op_net   = htons(5);  /* opcode = 5 (ERROR) */
        memcpy(err_buf + 0, &op_net, 2);
        uint16_t code_net = htons(6);  /* código = 6 (File already exists) */
        memcpy(err_buf + 2, &code_net, 2);
        const char *msg = "File already exists";
        size_t msg_len = strlen(msg);
        memcpy(err_buf + 4, msg, msg_len);
        /* err_buf[4 + msg_len] ya está a 0 por la inicialización */

        sendto(sockfd,
               err_buf,
               4 + msg_len + 1,
               0,
               (struct sockaddr *)&client_addr,
               addr_len);

        close(sockfd);
        return;
    }

    // 2. Abrir (o crear) el archivo en modo binario para escritura
    FILE *file = fopen(filename, "wb");
    if (!file)
    {
        perror("fopen");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // 3. Enviar ACK0 para indicar al cliente que comience a mandar datos
    send_ack(sockfd, &client_addr, addr_len, 0);
    //printf("ACK 0 enviadoo \b \n");


    // 4. Inicializar el número de bloque esperado:
    //    el primer bloque de datos que llegará será el bloque #1
    uint16_t expected_block = 1;
    
    // 5. Bucle de recepción de datos hasta que se complete la transferencia
    while (1)
    {
        char buffer_data[516] = {0};
        int bytes_recv = recvfrom(sockfd, buffer_data, sizeof(buffer_data), 0, (struct sockaddr *)&client_addr, &addr_len);
        if (bytes_recv < 0)
        {
            perror("recvfrom DATA");
            break;
        }

        // 5.a) Interpretar lo recibido como un paquete TFTP DATA
        struct tftp_data *data_packet = (struct tftp_data *)buffer_data;

        uint16_t opcode = ntohs(data_packet->opcode);
        uint16_t block = ntohs(data_packet->block);

        if (opcode != 3)
        {
            fprintf(stderr, "Esperado DATA (3), recibido opcode %d\n", opcode);
            break;
        }

        if (block != expected_block)
        {
            fprintf(stderr, "Bloque inesperado: %d, esperado: %d\b", block, expected_block);
            send_ack(sockfd, &client_addr, addr_len, expected_block - 1);
            continue;
        }

        int data_size = bytes_recv - 4; // 2 bytes for opcode and 2 bytes for block
        fwrite(data_packet->data, 1, data_size, file);
        printf("Bloque %d recibido (%d bytes)\n", block, data_size);

        send_ack(sockfd, &client_addr, addr_len, block);

        if (data_size < sizeof(data_packet->data))
            break;

        expected_block++;
    }
    fclose(file);
    printf("Archivo recibido exitosamente.\n");
}

void send_ack(int sockfd, struct sockaddr_in *client_addr, socklen_t addr_len, uint16_t block)
{
    struct tftp_ack ack;
    ack.opcode = htons(4); // Opcode de ACK
    ack.block = htons(block);
    

    if (sendto(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)client_addr, addr_len) < 0)
    {
        perror("sendto");
        printf("ack: %ld", sizeof(ack));
        exit(EXIT_FAILURE);
    }
}
