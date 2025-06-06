#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>             // para SIGCHLD
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

// Versión simplificada de tus estructuras:
struct tftp_data {
    uint16_t opcode;  // 3 = DATA
    uint16_t block;   // número de bloque
    char     data[512];
};

struct tftp_ack {
    uint16_t opcode;  // 4 = ACK
    uint16_t block;
};

// Prototipos de tus funciones (las implementaciones quedan igual que antes, salvo que
// reciben el socket “hijo” como segundo parámetro)
void tftp_rrq(char buffer[], int sockfd, struct sockaddr_in client_addr, socklen_t addr_len, int bytes_recv);
void tftp_wrq(char buffer[], int sockfd, struct sockaddr_in client_addr, socklen_t addr_len);
void send_ack(int sockfd, struct sockaddr_in *client_addr, socklen_t addr_len, uint16_t block);


#define PORT_BASE   28002
#define MAX_BUFFER  516

// Lleva la cuenta de qué puerto usará el siguiente proceso hijo.
// Empieza en PORT_BASE+1 y va subiendo.
static uint16_t next_port = PORT_BASE + 1;

int main(int argc, char *argv[])
{
    int sockfd_base;
    struct sockaddr_in server_addr_base, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[MAX_BUFFER];

    // 1) Crear socket UDP “base” (padre) en PORT_BASE
    sockfd_base = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_base < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr_base, 0, sizeof(server_addr_base));
    server_addr_base.sin_family = AF_INET;
    server_addr_base.sin_addr.s_addr = INADDR_ANY;
    server_addr_base.sin_port = htons(PORT_BASE);

    if (bind(sockfd_base, (struct sockaddr *)&server_addr_base, sizeof(server_addr_base)) < 0) {
        perror("bind");
        close(sockfd_base);
        exit(EXIT_FAILURE);
    }

    printf("Servidor TFTP concurrente escuchando en el puerto %d...\n", PORT_BASE);

    // 2) Para evitar procesos zombies:
    signal(SIGCHLD, SIG_IGN);

    // 3) Bucle principal: solo recibe RRQ/WRQ y hace fork
    while (1) {
        int bytes_recv = recvfrom(
            sockfd_base, buffer, sizeof(buffer), 0,
            (struct sockaddr *)&client_addr, &addr_len
        );
        if (bytes_recv < 0) {
            perror("recvfrom en padre");
            continue;
        }

        printf("Padre: solicitud de %s:%d (bytes=%d)\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), bytes_recv);

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            continue;  // sigo esperando más peticiones
        }

        if (pid == 0) {
            // --- Proceso hijo: atiende la transferencia particular ---
            close(sockfd_base);  // cierre del socket “base” heredado

            // Creo un socket UDP nuevo para la transferencia:
            int sockfd_hijo = socket(AF_INET, SOCK_DGRAM, 0);
            if (sockfd_hijo < 0) {
                perror("socket hijo");
                exit(EXIT_FAILURE);
            }

            // Bindeo el socket hijo en un puerto incremental:
            struct sockaddr_in server_addr_hijo;
            memset(&server_addr_hijo, 0, sizeof(server_addr_hijo));
            server_addr_hijo.sin_family = AF_INET;
            server_addr_hijo.sin_addr.s_addr = INADDR_ANY;
            server_addr_hijo.sin_port = htons(next_port);

            printf("Hijo %d: enlazando en puerto %d...\n", getpid(), next_port);
            next_port++;
            if (bind(sockfd_hijo, (struct sockaddr *)&server_addr_hijo, sizeof(server_addr_hijo)) < 0) {
                perror("bind hijo");
                close(sockfd_hijo);
                exit(EXIT_FAILURE);
            }

            // Leo el opcode del buffer inicial (primeros 2 bytes, en big-endian)
            uint16_t opcode_net = *(uint16_t *)buffer;
            uint16_t opcode = ntohs(opcode_net);

            if (opcode == 1) {
                // RRQ: “read request”
                tftp_rrq(buffer, sockfd_hijo, client_addr, addr_len, bytes_recv);
            }
            else if (opcode == 2) {
                // WRQ: “write request”
                tftp_wrq(buffer, sockfd_hijo, client_addr, addr_len);
            }
            else {
                printf("Hijo %d: opcode no soportado: %d\n", getpid(), opcode);
            }

            // Termina la transferencia, cierro el socket hijo y salgo:
            close(sockfd_hijo);
            printf("Hijo %d: terminando transferencia, salgo.\n", getpid());
            exit(EXIT_SUCCESS);
        }
        else {
            // --- Proceso padre: simplemente ignora, vuelve a esperar más solicitudes ---
            // No hacemos close(sockfd_base) aquí, porque lo seguirá usando.
        }
    }

    // (nunca llegaremos acá en un servidor en ejecución normal)
    close(sockfd_base);
    return 0;
}

//Read Request
void tftp_rrq(char buffer[], int sockfd, struct sockaddr_in client_addr, socklen_t addr_len, int bytes_recv)
{
    char *filename = &buffer[2];
    char *mode = filename + strlen(filename) + 1;
    printf("Received RRQ for file: %s, mode: %s\b \n", filename, mode);

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
            fprintf(stderr, "Invalid ACK received\b \n");
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
    printf("Received WRQ for file: %s, mode: %s\b \n", filename, mode);

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
    printf("ACK 0 enviado\b \n");


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
        printf("Bloque %d recibido (%d bytes)\n", block, data_size);

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
