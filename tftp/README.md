# Documento de Procotolo TFTP v1.2

## Protocolo TFTP Personalizado (con Control de Errores)

**Fecha:** Junio 2025
**Autor:** I. Grilli

---

## 1. Objetivo

Definir un protocolo TFTP simplificado sobre UDP que incluya:

* Lectura (RRQ) de archivos existentes.
* Escritura (WRQ) de archivos nuevos.
* Manejo de errores comunes:

  * Error 1: File not found (archivo no existe en RRQ).
  * Error 6: File already exists (archivo ya existe en WRQ).
* Instrucciones para que un cliente pueda:

  1. Construir paquetes RRQ/WRQ.
  2. Procesar DATA/ACK.
  3. Detectar y mostrar paquetes ERROR (opcode 5).

Cualquier desarrollador podrá implementar un cliente compatible con `server-tftp.c`.

---

## 2. Tipos de Pakketos y Formatos

### 2.1. RRQ / WRQ

```
2 bytes   N bytes   1 byte   M bytes   1 byte
-------------------------------------------
| opcode | nombre | 0x00 | modo | 0x00 |
-------------------------------------------
```

* **opcode:** 1 (RRQ) o 2 (WRQ), en `uint16_t` (htons()).
* **nombre:** cadena ASCII, terminada en `0x00`.
* **modo:** siempre `"octet"`, terminada en `0x00`.

### 2.2. DATA

```
2 bytes   2 bytes   N bytes
---------------------------
| opcode | block | datos... |
---------------------------
```

* **opcode=3** (`htons(3)`).
* **block:** número de bloque (1, 2, ...), `htons(block)`.
* **datos:** hasta 512 bytes. Si N < 512 → fin de archivo.

### 2.3. ACK

```
2 bytes   2 bytes
----------------
| opcode | block |
----------------
```

* **opcode=4** (`htons(4)`).
* **block:** bloque confirmado. Para WRQ inicial, `block=0`.

### 2.4. ERROR

```
2 bytes   2 bytes    cadena ASCII   1 byte
----------------------------------------
|  0 | 5  | code | mensaje | 0x00 |
----------------------------------------
```

* **opcode=5** (`htons(5)`).
* **code:** código de error:
  - `1` = File not found (RRQ a archivo inexistente).
  - `6` = File already exists (WRQ a archivo existente).
* **mensaje:** texto explicativo, terminado en `0x00`.

---

## 3. Flujo de Operaciones

### 3.1. RRQ (Read Request)

1. **Cliente → Servidor:** RRQ

   * `opcode=1`, `nombre`, `0x00`, `"octet"`, `0x00`.
2. **Servidor recibe RRQ**:

   * `fopen(nombre, "rb")`:

     * Si `NULL` → enviar ERROR 1:
       • `err_buf[0..1] = htons(5)`
       • `err_buf[2..3] = htons(1)`
       • `err_buf[4..4+len(msg)-1] = "File not found"`
       • `err_buf[4+len(msg)] = 0x00`
       • `sendto(err_buf, 4 + len(msg) + 1, cliente)`
       • Cerrar socket y terminar.
     * Si existe el archivo → continuar.
3. **Enviar bloques DATA**:

   * Para cada bloque N:

     1. Leer hasta 512 bytes en buffer.
     2. `packet.opcode = htons(3); packet.block = htons(N)`.
     3. `sendto(packet, 4 + bytes_read, cliente)`.
     4. `recvfrom(ACK)`:

        * Si `opcode!=4` o `block!=N` → abortar.
     5. Si `bytes_read < 512` → último bloque: salir.
4. Cerrar archivo y socket.

### 3.2. WRQ (Write Request)

1. **Cliente → Servidor:** WRQ

   * `opcode=2`, `nombre`, `0x00`, `"octet"`, `0x00`.
2. **Servidor recibe WRQ**:

   * `access(nombre, F_OK)`:

     * Si `0` (el archivo existe) → enviar ERROR 6:
       • `err_buf[0..1] = htons(5)`
       • `err_buf[2..3] = htons(6)`
       • `err_buf[4..4+len(msg)-1] = "File already exists"`
       • `err_buf[4+len(msg)] = 0x00`
       • `sendto(err_buf, 4 + len(msg) + 1, cliente)`
       • Cerrar socket y terminar.
     * Si no existe → `fopen(nombre, "wb")`.
3. **Servidor envía ACK 0**: `opcode=4`, `block=0`.
4. **Cliente recibe ACK 0 → enviar bloques DATA**:

   * Leer hasta 512 bytes.
   * Armar `opcode=3`, `block=1`, copiar datos.
   * `sendto(DATA_1)` y esperar `recvfrom(ACK 1)`.
   * Repetir hasta `bytes_read < 512`:

     * Último DATA, luego `recvfrom(ACK N)` y cerrar.
5. **Servidor recibe DATA N**:

   * Si `opcode!=3` o `block!=esperado` → reenviar ACK del último bloque válido.
   * `fwrite(data, data_size, 1, file)`.
   * `send_ack(block)`.
   * Si `data_size < 512` → cerrar archivo y socket.

---

## 4. Ejemplos de Secuencias

### 4.1. RRQ con ERROR 1

**Cliente → Servidor**: RRQ "noExiste.txt"

**Servidor**:

```
fopen("noExiste.txt", "rb"); // retorna NULL
err_buf = [00 05 00 01 46 69 6c 65 20 6e 6f 74 20 66 6f 75 6e 64 00]
sendto(err_buf)
close(sockfd)
```

**Cliente** recibe:

```
opcode=5, code=1, mensaje="File not found"
Mostrar: "Error TFTP 1: File not found"
```

### 4.2. RRQ exitoso

**Cliente → Servidor**: RRQ "archivo.txt"
**Servidor**:

* `fopen` abre correctamente.
* `DATA 1 (512 bytes)` → Cliente envía `ACK 1`.
* `DATA 2 (<512 bytes)` → Cliente envía `ACK 2`.

### 4.3. WRQ con ERROR 6

**Cliente → Servidor**: WRQ "existe.bin"

**Servidor**:

```
access("existe.bin", F_OK) == 0
err_buf = [00 05 00 06 46 69 6c 65 20 61 6c 72 65 61 64 79 20 65 78 69 73 74 73 00]
sendto(err_buf)
close(sockfd)
```

**Cliente** recibe:

```
opcode=5, code=6, mensaje="File already exists"
Mostrar: "Error TFTP 6: File already exists"
```

### 4.4. WRQ exitoso

**Cliente → Servidor**: WRQ "nuevo.bin"
**Servidor**:

* `access(...)`: no existe → `fopen("nuevo.bin", "wb")`.
* Envía `ACK 0`.
  **Cliente** recibe `ACK 0` → envía `DATA 1`, etc.

---

## 5. Instrucciones para el Cliente

1. **Crear socket UDP** y, opcional, configurar timeout:

   ```c
   struct timeval tv = {5,0};
   setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
   ```
2. **Construir RRQ o WRQ**:

   ```c
   size_t len = 2 + strlen(nombre) + 1 + strlen("octet") + 1;
   uint8_t *pkt = malloc(len);
   uint16_t op = htons( (modo == "leer") ? 1 : 2 );
   memcpy(pkt, &op, 2);
   memcpy(pkt+2, nombre, strlen(nombre)); pkt[2+strlen(nombre)] = 0;
   memcpy(pkt+3+strlen(nombre), "octet", 5); pkt[3+strlen(nombre)+5] = 0;
   sendto(sockfd, pkt, len, 0, (struct sockaddr*)&server_addr, addr_len);
   ```
3. **Esperar respuesta con `recvfrom()`** y leer los primeros 4 bytes:

   ```c
   uint8_t buf[516];
   ssize_t n = recvfrom(sockfd, buf, sizeof(buf), 0, NULL, NULL);
   if (n < 0) { perror("timeout o error"); return; }
   uint16_t opcode = ntohs(*(uint16_t*)&buf[0]);
   uint16_t code_or_block = ntohs(*(uint16_t*)&buf[2]);

   if (opcode == 5) {
       // ERROR
       char *msg = (char*)(buf + 4);
       printf("TFTP Error %d: %s\n", code_or_block, msg);
       return;
   }
   // Si opcode == 3 (DATA) o 4 (ACK) → continuar flujo normal.
   ```
4. **Flujo RRQ**:

   * Si recibes DATA, con `block = code_or_block`:

     1. Guardar `buf+4` en disco.
     2. Enviar ACK con `opcode=4`, `block`.
     3. Si tamaño <512, fin.
5. **Flujo WRQ**:

   * Si recibes ACK 0: comenzar a enviar DATA 1, esperar ACK 1, etc.
   * Si recibes ACK `block`, enviar DATA `block+1`.
   * Si error o timeout, reintentar (hasta 5 veces) y abortar.

---

## 6. Conclusión

Esta versión simplificada describe:

* Cómo construir RRQ/WRQ.
* Cómo intercambiar DATA/ACK bloque a bloque.
* Cómo detectar y enviar errores:

  * ERROR 1: File not found.
  * ERROR 6: File already exists.
* Qué debe hacer un cliente para interpretar esos paquetes ERROR.

Con estos pasos, un cliente en cualquier lenguaje puede interoperar
con el servidor `server-tftp.c`, manejando transferencias exitosas y
condiciones de error de forma predecible.

Fin.

