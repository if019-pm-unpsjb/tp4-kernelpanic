import tkinter as tk
import threading
import tkinter.filedialog as fd
import os
import socket


class ChatCliente:
    def __init__(self, master, socket_conexion, nombre):
        self.master = master
        self.socket = socket_conexion
        self.nombre = nombre
        self.running = True
        self.usuario_actual = None
        self.historial = {}
        self.no_leidos = set()

        self.master.title(f"OpenChat - {nombre}")
        self.master.geometry("600x400")
        self.master.resizable(False, False)

        self.configurar_layout()
        self.crear_widgets()
        threading.Thread(target=self.recibir_mensajes, daemon=True).start()
        self.mostrar_mensaje("Sistema", f"Bienvenido {nombre}!\nSeleccioná un usuario para comenzar a chatear.")

    def configurar_layout(self):
        self.master.columnconfigure(0, weight=4)
        self.master.columnconfigure(1, weight=1)
        self.master.rowconfigure(1, weight=1)
        self.master.rowconfigure(2, weight=0)

    def crear_widgets(self):
        # Título usuarios
        self.label_chat = tk.Label(self.master, text="Chat", font=("Helvetica", 12, "bold"))
        self.label_chat.grid(row=0, column=0, sticky="nw", padx=(10, 0), pady=(5, 0))

        tk.Label(self.master, text="Usuarios", font=("Helvetica", 12, "bold")).grid(
            row=0, column=1, sticky="nw", padx=10, pady=(5, 0)
        )

        # Área de mensajes
        self.text_area = tk.Text(self.master, state="disabled", wrap="word")
        self.text_area.grid(row=1, column=0, sticky="nsew", padx=(10, 0), pady=(0, 10))
        self.text_area.tag_config("sistema", foreground="red")
        self.text_area.tag_config("normal", foreground="black")

        # Lista de usuarios
        self.lista_usuarios = tk.Listbox(self.master, exportselection=False)
        self.lista_usuarios.grid(row=1, column=1, sticky="nsew", padx=10, pady=(0, 10))
        self.lista_usuarios.bind("<<ListboxSelect>>", self.cambiar_conversacion)

        # Entrada de mensaje
        self.entry_mensaje = tk.Entry(self.master)
        self.entry_mensaje.grid(row=2, column=0, sticky="ew", padx=(10, 0), pady=(0, 10))
        self.entry_mensaje.bind("<Return>", self.enviar_mensaje)

        tk.Button(self.master, text="Enviar", command=self.enviar_mensaje).grid(
            row=2, column=1, sticky="ew", padx=10, pady=(0, 10)
        )

        tk.Button(self.master, text="Enviar Archivo", command=self.enviar_archivo).grid(
            row=3, column=1, sticky="ew", padx=10, pady=(0, 10)
        )

    def enviar_mensaje(self, event=None):
        mensaje = self.entry_mensaje.get().strip()
        if not mensaje:
            return

        seleccion = self.lista_usuarios.curselection()
        if not seleccion:
            self.mostrar_mensaje("Sistema", "Seleccioná un usuario para chatear.")
            return

        destinatario = self.lista_usuarios.get(seleccion[0])
        comando = f"TO|{destinatario}|{mensaje}"
        try:
            self.socket.sendall(comando.encode())
            self.entry_mensaje.delete(0, tk.END)
            self.agregar_a_historial(destinatario, f"Yo: {mensaje}")
            self.mostrar_historial(destinatario)
        except Exception as e:
            self.mostrar_mensaje("Error", f"No se pudo enviar el mensaje: {e}")

    def recibir_mensajes(self):
        while self.running:
            try:
                data = self.socket.recv(1024)
                if not data:
                    self.desconectar()
                    break

                mensaje = data.decode()

                if mensaje.startswith("USERS|"):
                    self.actualizar_lista_usuarios(mensaje)
                elif mensaje.startswith("FROM|"):
                    remitente, msg = mensaje.split("|", 2)[1:]
                    self.agregar_a_historial(remitente, f"{remitente}: {msg}")
                    if remitente == self.usuario_actual:
                        self.mostrar_historial(remitente)
                    else:
                        self.no_leidos.add(remitente)
                        self.marcar_usuario_no_leido(remitente)
                elif mensaje.startswith("FILE|"):
                    remitente, archivo = mensaje.split("|", 2)[1:]
                    self.mostrar_mensaje("Sistema", f"{remitente} quiere enviarte el archivo {archivo}.")
                    threading.Thread(
                            target=self.tftp_get,
                            args=(archivo,),
                            daemon=True).start()
                else:
                    self.mostrar_mensaje("Sistema", mensaje)
            except Exception as e:
                self.mostrar_mensaje("Sistema", f"Error: {e}")
                self.running = False
                break

    def desconectar(self):
        self.mostrar_mensaje("Sistema", "Desconectado del servidor.")
        self.running = False

    def actualizar_lista_usuarios(self, mensaje):
        nombres = [n for n in mensaje.split("|")[1:] if n != self.nombre]
        self.lista_usuarios.delete(0, tk.END)
        for nombre in nombres:
            self.lista_usuarios.insert(tk.END, nombre)
            if nombre not in self.historial:
                self.historial[nombre] = []

    def cambiar_conversacion(self, seleccion):
        seleccion = self.lista_usuarios.curselection()
        if seleccion:
            nuevo_usuario = self.lista_usuarios.get(seleccion[0])
            self.usuario_actual = nuevo_usuario
            self.label_chat.config(text=nuevo_usuario)
            self.mostrar_historial(nuevo_usuario)
            self.marcar_usuario_leido(nuevo_usuario)

    def mostrar_historial(self, usuario):
        self.text_area.config(state="normal")
        self.text_area.delete("1.0", tk.END)
        for linea in self.historial.get(usuario, []):
            self.text_area.insert(tk.END, f"{linea}\n")
        self.text_area.config(state="disabled")
        self.text_area.see(tk.END)

    def agregar_a_historial(self, usuario, mensaje):
        self.historial.setdefault(usuario, []).append(mensaje)

    def mostrar_mensaje(self, remitente, mensaje):
        self.text_area.config(state="normal")
        tag = "sistema" if remitente == "Sistema" else "normal"
        self.text_area.insert(tk.END, f"{remitente}: {mensaje}\n", tag)
        self.text_area.config(state="disabled")
        self.text_area.see(tk.END)

    def marcar_usuario_no_leido(self, usuario):
        try:
            index = self.lista_usuarios.get(0, tk.END).index(usuario)
            self.lista_usuarios.itemconfig(index, {'fg': 'red'})
        except ValueError:
            pass

    def marcar_usuario_leido(self, usuario):
        try:
            index = self.lista_usuarios.get(0, tk.END).index(usuario)
            self.lista_usuarios.itemconfig(index, {'fg': 'black'})
        except ValueError:
            pass

    def enviar_archivo(self):
        seleccion = self.lista_usuarios.curselection()
        if not seleccion:
            self.mostrar_mensaje("Sistema", "Seleccioná un usuario para enviar el archivo.")
            return

        filepath = fd.askopenfilename()
        if not filepath:
            return

        destinatario = self.lista_usuarios.get(seleccion[0])
        nombre_archivo = filepath.split("/")[-1]

        comando = f"FILE|{destinatario}|{nombre_archivo}"
        try:
            self.socket.sendall(comando.encode())
            self.mostrar_mensaje("Sistema", f"Enviando archivo {nombre_archivo}...")
            threading.Thread(
                    target=self.tftp_put,
                    args=(filepath,),
                    daemon=True).start()
        except Exception as e:
            self.mostrar_mensaje("Sistema", f"Error al enviar archivo: {e}")

    def tftp_get(self, filename):
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.settimeout(3)

            ip_servidor, puerto_servidor = self.socket.getpeername()
            mode = b"octet"
            rrq = b"\x00\x01" + filename.encode() + b"\x00" + mode + b"\x00"
            sock.sendto(rrq, (ip_servidor, puerto_servidor))

            ruta_guardado = os.path.join(os.getcwd(), "recibido_" + filename)
            with open(ruta_guardado, "wb") as f:
                expected_block = 1
                while True:
                    try:
                        data, addr = sock.recvfrom(516)
                    except Exception:
                        self.mostrar_mensaje("Sistema", "Timeout esperando DATA")
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
            self.mostrar_mensaje("Sistema", f"Archivo '{filename}' recibido correctamente.")
        except Exception as e:
            self.mostrar_mensaje("Sistema", f"Error en TFTP GET: {e}")

    def tftp_put(self, filepath):
        filename = os.path.basename(filepath)
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.settimeout(3)

            ip_servidor, puerto_servidor = self.socket.getpeername()
            mode = b"octet"
            wrq = b"\x00\x02" + filename.encode() + b"\x00" + mode + b"\x00"
            sock.sendto(wrq, (ip_servidor, puerto_servidor))

            try:
                ack, server_addr = sock.recvfrom(516)
            except Exception:
                self.mostrar_mensaje("Sistema", "Timeout esperando primer ACK")
                return

            opcode, ack_block = int.from_bytes(ack[:2], 'big'), int.from_bytes(ack[2:4], 'big')
            if opcode != 4 or ack_block != 0:
                self.mostrar_mensaje("Sistema", f"ACK inesperado: {ack}")
                return

            with open(filepath, "rb") as f:
                block = 1
                while True:
                    data = f.read(512)
                    packet = b"\x00\x03" + block.to_bytes(2, 'big') + data
                    sock.sendto(packet, server_addr)

                    try:
                        ack, addr = sock.recvfrom(516)
                    except Exception:
                        self.mostrar_mensaje("Sistema", f"Timeout esperando ACK del bloque {block}")
                        return

                    opcode, ack_block = int.from_bytes(ack[:2], 'big'), int.from_bytes(ack[2:4], 'big')
                    if opcode != 4 or ack_block != block:
                        self.mostrar_mensaje("Sistema", f"ACK inesperado en bloque {block}: {ack}")
                        return

                    block += 1
                    if len(data) < 512:
                        break

            sock.close()
            self.mostrar_mensaje("Sistema", f"Archivo {filename} enviado.")
        except Exception as e:
            self.mostrar_mensaje("Sistema", f"Error en TFTP PUT: {e}")
