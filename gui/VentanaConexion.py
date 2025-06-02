import tkinter as tk
import socket
from ChatCliente import ChatCliente

class VentanaConexion:
    def __init__(self, master):
        self.master = master
        self.master.title("Conexión al Servidor")
        self.master.geometry("400x400")
        master.columnconfigure(0, weight=1)

        tk.Label(master, text="Su nombre:").grid(row=0, column=0, sticky="w", padx=20, pady=(10, 0))
        self.entry_nombre = tk.Entry(master)
        self.entry_nombre.grid(row=1, column=0, sticky="ew", padx=20)

        tk.Label(master, text="Dirección IP:").grid(row=2, column=0, sticky="w", padx=20, pady=(20, 0))
        self.entry_ip = tk.Entry(master)
        self.entry_ip.grid(row=3, column=0, sticky="ew", padx=20)

        tk.Label(master, text="Puerto:").grid(row=4, column=0, sticky="w", padx=20, pady=(10, 0))
        self.entry_puerto = tk.Entry(master)
        self.entry_puerto.grid(row=5, column=0, sticky="ew", padx=20)

        self.label_error = tk.Label(master, text="", fg="red")
        self.label_error.grid(row=6, column=0, padx=20, pady=(5, 0))

        tk.Button(master, text="Conectar", command=self.intentar_conexion).grid(row=7, column=0, pady=15)

    def intentar_conexion(self):
        ip = self.entry_ip.get()
        puerto = self.entry_puerto.get()
        nombre = self.entry_nombre.get()

        if not ip or not puerto or not nombre:
            self.label_error.config(text="IP, puerto y nombre son obligatorios.")
            return
        if not puerto.isdigit():
            self.label_error.config(text="Puerto inválido.")
            return

        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((ip, int(puerto)))
            s.sendall(nombre.encode())
            self.label_error.config(text="")
            self.master.destroy()
            ventana_chat = tk.Tk()
            ChatCliente(ventana_chat, s, nombre)
            ventana_chat.mainloop()
        except Exception as e:
            self.label_error.config(text=f"No se pudo conectar: {e}")