#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

struct tftp_data
{
    uint16_t opcode; // Opcode
    uint16_t block;  // Block number
    char data[512];  // DataF
};

struct tftp_ack
{
    uint16_t opcode; // Opcode
    uint16_t block;  // Block number
};

void send_ack(int sockfd, struct sockaddr_in *client_addr, socklen_t addr_len, uint16_t block);
void tftp_rrq(char buffer[], int sockfd, struct sockaddr_in client_addr, socklen_t addr_len, int bytes_recv);
void tftp_wrq(char buffer[], int sockfd, struct sockaddr_in client_addr, socklen_t addr_len);

int main(int argc, char *argv[])
{
    int sockfd;

    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;  //Familia de direcciones IPv4.
    server_addr.sin_addr.s_addr = INADDR_ANY; //Escucha en todas las interfaces de red.
    server_addr.sin_port = htons(28002); // TFTP port

    //Vincula el socket a la dirección y puerto especificados. Si falla, se imprime un error y el programa finaliza.
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(sockfd);
        exit(EXIT_FAILURE);
    }


    printf("Servidor TFTP escuchando en el puerto %d...\n", ntohs(server_addr.sin_port));

    // Esperar RRQ del cliente
    char buffer[516];
    int bytes_recv = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &addr_len);
    if (bytes_recv < 0)
    {
        perror("recvfrom");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Solicitud recibida de %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    printf("Bytes recibidos: %d\n", bytes_recv);

    if (buffer[1] == 1) // Opcode for RRQ
        tftp_rrq(buffer, sockfd, client_addr, addr_len, bytes_recv);
    else if (buffer[1] == 2) // Opcode for WRQ
        tftp_wrq(buffer, sockfd, client_addr, addr_len);
    else
        printf("Solicitud no soportada (opcode = %d)\n", buffer[1]);

    close(sockfd);
    exit(EXIT_SUCCESS);
}

//Read Request
void tftp_rrq(char buffer[], int sockfd, struct sockaddr_in client_addr, socklen_t addr_len, int bytes_recv)
{
    char *filename = &buffer[2];
    char *mode = filename + strlen(filename) + 1;
    printf("Received RRQ for file: %s, mode: %s\b", filename, mode);

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
        bytes_recv = recvfrom(sockfd, buffer, sizeof(*buffer), 0, (struct sockaddr *)&client_addr, &addr_len);
        if (bytes_recv < 0)
        {
            perror("recvfrom ACK");
            break;
        }

        struct tftp_ack *ack = (struct tftp_ack *)buffer;
        if (ntohs(ack->opcode) != 4 || ntohs(ack->block) != block)
        {
            fprintf(stderr, "Invalid ACK received\b");
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
    printf("Archivo enviado exitosamente.\b");
}

void tftp_wrq(char buffer[], int sockfd, struct sockaddr_in client_addr, socklen_t addr_len)
{
    // 1. Extraer nombre de archivo y modo (netascii, octet, etc.)
    char *filename = &buffer[2];
    char *mode = filename + strlen(filename) + 1;
    printf("Received WRQ for file: %s, mode: %s\b", filename, mode);

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
    printf("ACK 0 enviado\b");


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
            fprintf(stderr, "Esperado DATA (3), recibido opcode %d\b", opcode);
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
        printf("Bloque %d recibido (%d bytes)\b", block, data_size);

        send_ack(sockfd, &client_addr, addr_len, block);

        if (data_size < sizeof(data_packet->data))
            break;

        expected_block++;
    }

    fclose(file);
    printf("Archivo recibido exitosamente.\b");
}

void send_ack(int sockfd, struct sockaddr_in *client_addr, socklen_t addr_len, uint16_t block)
{
    struct tftp_ack ack;
    ack.opcode = htons(4); // Opcode de ACK
    ack.block = htons(block);

    if (sendto(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)client_addr, addr_len) < 0)
    {
        perror("sendto");
        exit(EXIT_FAILURE);
    }
}