import tkinter as tk
import threading
import tkinter.filedialog as fd
from tkinter import messagebox
import os


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
                data = self.socket.recv(4096).decode()
                if not data:
                    self.desconectar()
                    break

                if data.startswith("USERS|"):
                    self.actualizar_lista_usuarios(data)
                elif data.startswith("FROM|"):
                    remitente, msg = data.split("|", 2)[1:]
                    self.agregar_a_historial(remitente, f"{remitente}: {msg}")
                    if remitente == self.usuario_actual:
                        self.mostrar_historial(remitente)
                    else:
                        self.no_leidos.add(remitente)
                        self.marcar_usuario_no_leido(remitente)
                elif data.startswith("FILE|"):
                    self.procesar_archivo_entrante(data)
                else:
                    self.mostrar_mensaje("Sistema", data)

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
            self.mostrar_mensaje("Sistema", "Seleccioná un usuario")
            return

        destinatario = self.lista_usuarios.get(seleccion[0])
        filepath = fd.askopenfilename(title="Seleccionar archivo")

        if filepath:
            try:
                filename = os.path.basename(filepath)
                filesize = os.path.getsize(filepath)

                # Enviar cabecera
                self.socket.sendall(f"FILE|{destinatario}|{filename}|{filesize}\n".encode())

                # Enviar archivo
                with open(filepath, 'rb') as f:
                    self.socket.sendfile(f)

                self.mostrar_mensaje("Sistema", f"Archivo enviado: {filename}")
                self.agregar_a_historial(destinatario, f"Yo: [Archivo: {filename}]")

            except Exception as e:
                self.mostrar_mensaje("Error", f"Error al enviar: {e}")

    def procesar_archivo_entrante(self, header):
        try:
            # Parsear cabecera: FILE|remitente|filename|filesize
            parts = header.split("|")
            remitente = parts[1]
            filename = parts[2]
            filesize = int(parts[3])

            # Confirmar recepción
            if not messagebox.askyesno("Archivo entrante", f"¿Recibir archivo {filename} ({filesize} bytes) de {remitente}?"):
                return

            # Recibir archivo
            save_path = fd.asksaveasfilename(
                title="Guardar archivo",
                initialfile=filename,
                defaultextension=os.path.splitext(filename)[1]
            )

            if save_path:
                with open(save_path, 'wb') as f:
                    remaining = filesize
                    while remaining > 0:
                        chunk = self.socket.recv(min(4096, remaining))
                        if not chunk:
                            raise ConnectionError("Conexión interrumpida")
                        f.write(chunk)
                        remaining -= len(chunk)

                self.mostrar_mensaje("Sistema", f"Archivo {filename} guardado")
                self.agregar_a_historial(remitente, f"{remitente}: [Archivo: {filename}]")

        except Exception as e:
            self.mostrar_mensaje("Error", f"Error con archivo: {e}")
