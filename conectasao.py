import socket, struct, os
from datetime import datetime

import struct

def create_stereo_wav_header(sample_rate, data_size=0, bits_per_sample=16):
    """
    Generates a 44-byte WAV PCM header for a stereo raw audio file.
    
    Args:
        sample_rate (int): The sampling frequency (e.g., 44100, 48000).
        data_size (int): The size of the raw PCM data in bytes. Set to 0 if unknown 
                         (or use 0xFFFFFFFF for streaming).
        bits_per_sample (int): Bit depth, usually 16, 24, or 32 for PCM.
        
    Returns:
        bytes: The 44-byte WAV header.
    """
    num_channels = 2  # Hardcoded for stereo
    byte_rate = sample_rate * num_channels * (bits_per_sample // 8)
    block_align = num_channels * (bits_per_sample // 8)
    
    # Pack the header using little-endian ('<') structure
    header = struct.pack(
        '<4sI4s4sIHHIIHH4sI',
        b'RIFF',
        36 + data_size,    # ChunkSize: 36 + SubChunk2Size
        b'WAVE',
        b'fmt ',
        16,                # Subchunk1Size: 16 for PCM
        1,                 # AudioFormat: 1 for uncompressed PCM
        num_channels,
        sample_rate,
        byte_rate,
        block_align,
        bits_per_sample,
        b'data',
        data_size          # Subchunk2Size: Size of the raw audio data
    )
    
    return header

PUERTO = 5000
PRE_CARPETA = "audios_pre"
CARPETA = "audios"

SAMPLE_RATE = 16000
BITS_PER_SAMPLE = 16

def main():
    os.makedirs(CARPETA, exist_ok=True)
    os.makedirs(PRE_CARPETA, exist_ok=True)

    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", PUERTO))
    s.listen(1)
    print(f"Escuchando en el puerto {PUERTO}... (Ctrl+C para salir)")

    while True:
        conn, addr = s.accept()
        conn.settimeout(15)
        try:
            nombre = datetime.now().strftime("%Y-%m-%dT%H:%M:%S") + ".wav"
            ruta = os.path.join(PRE_CARPETA, nombre)
            tam = 0
            raw_bytes = bytes()
            with open(ruta, "wb") as f:
                while True:
                    chunk = conn.recv(4096)
                    if not chunk:
                        break
                    tam += len(chunk)
                    raw_bytes += chunk
                header = create_stereo_wav_header(SAMPLE_RATE, bits_per_sample=BITS_PER_SAMPLE, data_size=tam)
                f.write(header)
                f.write(raw_bytes)

            conn.sendall(b"OK\n")
            ruta_final = os.path.join(CARPETA, nombre)
            os.rename(ruta, ruta_final)
            print(f"[{addr[0]}] guardado {ruta_final} ({tam} bytes)")
        except Exception as e:
            print("Error:", e)
        finally:
            conn.close()

if __name__ == "__main__":
    main()
