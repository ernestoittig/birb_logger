import socket, struct, os
from datetime import datetime

PUERTO = 5000
CARPETA = "audios"
os.makedirs(CARPETA, exist_ok=True)

def recibir_exacto(conn, n):
    datos = bytearray()
    while len(datos) < n:
        parte = conn.recv(min(4096, n - len(datos)))
        if not parte:
            raise ConnectionError("la placa cerró la conexión antes de terminar")
        datos += parte
    return bytes(datos)

s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("0.0.0.0", PUERTO))
s.listen(1)
print(f"Escuchando en el puerto {PUERTO}... (Ctrl+C para salir)")

while True:
    conn, addr = s.accept()
    conn.settimeout(15)
    try:
        tam = struct.unpack("<I", recibir_exacto(conn, 4))[0]
        audio = recibir_exacto(conn, tam)
        nombre = datetime.now().strftime("%Y_%m_%d_%H_%M_%S") + ".raw"
        ruta = os.path.join(CARPETA, nombre)
        with open(ruta, "wb") as f:
            f.write(audio)
        conn.sendall(b"OK\n")
        print(f"[{addr[0]}] guardado {ruta} ({tam} bytes)")
    except Exception as e:
        print("Error:", e)
    finally:
        conn.close()

