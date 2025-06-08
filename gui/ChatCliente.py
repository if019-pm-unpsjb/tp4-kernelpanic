import tkinter as tk
import threading

class ChatCliente:
    def __init__(self, master, socket_conexion, nombre):
        self.master = master
        self.master.title(f"Chat - {nombre}")
        self.master.geometry("400x400")
        self.socket = socket_conexion
        self.nombre = nombre

        self.text_area = tk.Text(master, state="disabled")
        self.text_area.pack(padx=10, pady=10, fill="both", expand=True)

        self.entry_mensaje = tk.Entry(master)
        self.entry_mensaje.pack(fill="x", padx=10)
        self.entry_mensaje.bind("<Return>", self.enviar_mensaje)

        self.running = True
        threading.Thread(target=self.recibir_mensajes, daemon=True).start()

    def enviar_mensaje(self, event=None):
        mensaje = self.entry_mensaje.get()
        if mensaje:
            try:
                self.socket.sendall(mensaje.encode())
                self.mostrar_mensaje(self.nombre, mensaje)
                self.entry_mensaje.delete(0, tk.END)
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
                self.mostrar_mensaje(self.nombre, data.decode())
            except Exception as e:
                self.mostrar_mensaje("Sistema", f"Error: {e}")
                self.running = False
                break

    def mostrar_mensaje(self, remitente, mensaje):
        self.text_area.config(state="normal")
        self.text_area.insert(tk.END, f"{remitente}: {mensaje}\n")
        self.text_area.config(state="disabled")
        self.text_area.see(tk.END)