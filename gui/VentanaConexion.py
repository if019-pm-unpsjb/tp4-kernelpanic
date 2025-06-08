import socket
import tkinter as tk
from ChatCliente import ChatCliente

class VentanaConexion:
    def __init__(self, master):
        self.master = master
        self.master.title("Conexión al Servidor")
        self.master.geometry("300x220")
        
        tk.Label(master, text="Su nombre:").pack(pady=(10, 0))
        self.entry_nombre = tk.Entry(master)
        self.entry_nombre.pack()

        tk.Label(master, text="Dirección IP:").pack(pady=(20, 0))
        self.entry_ip = tk.Entry(master)
        self.entry_ip.pack()

        tk.Label(master, text="Puerto:").pack(pady=(10, 0))
        self.entry_puerto = tk.Entry(master)
        self.entry_puerto.pack()

        self.label_error = tk.Label(master, text="", fg="red")
        self.label_error.pack(pady=(5, 0))

        tk.Button(master, text="Conectar", command=self.intentar_conexion).pack(pady=20)

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