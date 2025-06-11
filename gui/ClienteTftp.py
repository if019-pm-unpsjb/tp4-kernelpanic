import os
import socket


def tftp_put(filepath, server_data, mostrar_mensaje):
    import os
    import socket

    filename = os.path.basename(filepath)
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(3)

        ip_servidor, puerto_servidor = server_data
        mode = b"octet"
        wrq = b"\x00\x02" + filename.encode() + b"\x00" + mode + b"\x00"
        sock.sendto(wrq, (ip_servidor, puerto_servidor))

        # Esperamos el primer ACK (block 0), y guardamos la dirección real del servidor
        try:
            ack, server_addr = sock.recvfrom(516)
        except socket.timeout:
            mostrar_mensaje("Sistema", "Timeout esperando primer ACK")
            return

        opcode, ack_block = int.from_bytes(ack[:2], 'big'), int.from_bytes(ack[2:4], 'big')
        if opcode != 4 or ack_block != 0:
            mostrar_mensaje("Sistema", f"ACK inesperado: {ack}")
            return

        with open(filepath, "rb") as f:
            block = 1
            while True:
                data = f.read(512)
                packet = b"\x00\x03" + block.to_bytes(2, 'big') + data
                sock.sendto(packet, server_addr)

                try:
                    ack, addr = sock.recvfrom(516)
                except socket.timeout:
                    mostrar_mensaje("Sistema", f"Timeout esperando ACK del bloque {block}")
                    return

                opcode, ack_block = int.from_bytes(ack[:2], 'big'), int.from_bytes(ack[2:4], 'big')
                if opcode != 4 or ack_block != block:
                    mostrar_mensaje("Sistema", f"ACK inesperado en bloque {block}: {ack}")
                    return

                block += 1
                if len(data) < 512:
                    break  # Último bloque

        sock.close()
        mostrar_mensaje("Sistema", f"Archivo {filename} enviado.")
    except Exception as e:
        mostrar_mensaje("Sistema", f"Error en TFTP PUT: {e}")


def tftp_get(filename, server_data, mostrar_mensaje=None):
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(3)

        ip_servidor, puerto_servidor = server_data
        mode = b"octet"
        rrq = b"\x00\x01" + filename.encode() + b"\x00" + mode + b"\x00"
        sock.sendto(rrq, (ip_servidor, puerto_servidor))

        ruta_guardado = os.path.join(os.getcwd(), "recibido_" + filename)
        with open(ruta_guardado, "wb") as f:
            expected_block = 1
            while True:
                try:
                    data, addr = sock.recvfrom(516)
                except socket.timeout:
                    if mostrar_mensaje:
                        mostrar_mensaje("Sistema", "Timeout esperando DATA")
                    return
                opcode = int.from_bytes(data[:2], 'big')
                block = int.from_bytes(data[2:4], 'big')
                if opcode != 3 or block != expected_block:
                    continue
                f.write(data[4:])
                ack = b"\x00\x04" + block.to_bytes(2, 'big')
                sock.sendto(ack, addr)
                if len(data[4:]) < 512:
                    break
                expected_block += 1
        sock.close()
        if mostrar_mensaje:
            mensaje_exito = (
                f"Archivo '{filename}' recibido exitosamente.\n"
                f"Guardado en: {os.path.abspath(ruta_guardado)}"
            )
            mostrar_mensaje("Sistema", mensaje_exito)
    except Exception as e:
        if mostrar_mensaje:
            mostrar_mensaje("Sistema", f"Error en TFTP GET: {e}")
