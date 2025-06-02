import tkinter as tk
import threading

class ChatCliente:
    def __init__(self, master, socket_conexion, nombre):
        self.master = master
        self.master.title(f"Chat - {nombre}")
        self.master.geometry("600x400")
        self.socket = socket_conexion
        self.nombre = nombre
        self.running = True
        self.no_leidos = set()  # Usuarios con mensajes no leídos

        self.historial = {}  # <usuario>: [mensajes]
        self.usuario_actual = None

        # Layout
        self.master.columnconfigure(0, weight=4)
        self.master.columnconfigure(1, weight=1)
        self.master.rowconfigure(0, weight=1)
        self.master.rowconfigure(1, weight=0)

        # Área de texto
        self.text_area = tk.Text(master, state="disabled", wrap="word")
        self.text_area.grid(row=0, column=0, sticky="nsew", padx=(10, 0), pady=10)

        # Lista de usuarios
        self.lista_usuarios = tk.Listbox(master, exportselection=False)
        self.lista_usuarios.grid(row=0, column=1, sticky="nsew", padx=10, pady=10)
        self.lista_usuarios.bind("<<ListboxSelect>>", self.cambiar_conversacion)

        # Entrada de mensaje
        self.entry_mensaje = tk.Entry(master)
        self.entry_mensaje.grid(row=1, column=0, sticky="ew", padx=(10, 0), pady=(0, 10))
        self.entry_mensaje.bind("<Return>", self.enviar_mensaje)

        tk.Button(master, text="Enviar", command=self.enviar_mensaje).grid(row=1, column=1, sticky="ew", padx=10, pady=(0, 10))

        threading.Thread(target=self.recibir_mensajes, daemon=True).start()

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
                    self.mostrar_mensaje("Sistema", "Desconectado del servidor.")
                    self.running = False
                    break

                mensaje = data.decode()

                if mensaje.startswith("USERS|"):
                    self.actualizar_lista_usuarios(mensaje)
                elif mensaje.startswith("FROM|"):
                    partes = mensaje.split("|", 2)
                    if len(partes) == 3:
                        remitente, msg = partes[1], partes[2]
                        self.agregar_a_historial(remitente, f"{remitente}: {msg}")
                        
                        if remitente == self.usuario_actual:
                            self.mostrar_historial(remitente)
                        else:
                            self.no_leidos.add(remitente)
                            self.marcar_usuario_no_leido(remitente)
                else:
                    self.mostrar_mensaje("Sistema", mensaje)
            except Exception as e:
                self.mostrar_mensaje("Sistema", f"Error: {e}")
                self.running = False
                break

    def actualizar_lista_usuarios(self, mensaje):
        nombres = mensaje.split("|")[1:]
        nombres = [n for n in nombres if n != self.nombre]

        self.lista_usuarios.delete(0, tk.END)
        for nombre in nombres:
            self.lista_usuarios.insert(tk.END, nombre)

            # Iniciar historial vacío si es nuevo
            if nombre not in self.historial:
                self.historial[nombre] = []

    def cambiar_conversacion(self, event):
        seleccion = self.lista_usuarios.curselection()
        if seleccion:
            nuevo_usuario = self.lista_usuarios.get(seleccion[0])
            self.usuario_actual = nuevo_usuario
            self.mostrar_historial(nuevo_usuario)

        # Marcar como leído y cambiar color
        if nuevo_usuario in self.no_leidos:
            self.no_leidos.remove(nuevo_usuario)
            self.marcar_usuario_leido(nuevo_usuario)


    def mostrar_historial(self, usuario):
        self.text_area.config(state="normal")
        self.text_area.delete("1.0", tk.END)
        for linea in self.historial.get(usuario, []):
            self.text_area.insert(tk.END, f"{linea}\n")
        self.text_area.config(state="disabled")
        self.text_area.see(tk.END)

    def agregar_a_historial(self, usuario, mensaje):
        if usuario not in self.historial:
            self.historial[usuario] = []
        self.historial[usuario].append(mensaje)

    def mostrar_mensaje(self, remitente, mensaje):
        self.text_area.config(state="normal")
        self.text_area.insert(tk.END, f"{remitente}: {mensaje}\n")
        self.text_area.config(state="disabled")
        self.text_area.see(tk.END)

    def marcar_usuario_no_leido(self, usuario):
    # Busca índice en la lista
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