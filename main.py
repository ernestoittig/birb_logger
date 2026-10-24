import os
import signal
import threading
import time

try:
    import ingestador
except Exception:
    ingestador = None

try:
    import conectasao
except Exception:
    conectasao = None

import server


def load_dotenv(dotenv_path=".env", override=False):
    try:
        if not os.path.isfile(dotenv_path):
            return
        with open(dotenv_path, "r", encoding="utf-8") as env_file:
            for line in env_file:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                if "=" in line:
                    key, val = line.split("=", 1)
                    key = key.strip()
                    val = val.strip()
                    if len(val) >= 2 and ((val[0] == '"' and val[-1] == '"') or (val[0] == "'" and val[-1] == "'")):
                        val = val[1:-1]
                    if key and (override or key not in os.environ):
                        os.environ[key] = val
    except Exception:
        pass


class ServiceWorker:
    def __init__(self, name, target):
        self.name = name
        self.target = target
        self.thread = None

    def start(self):
        self.thread = threading.Thread(target=self._run, name=self.name, daemon=True)
        self.thread.start()

    def _run(self):
        try:
            self.target()
        except Exception:
            pass

    def is_alive(self):
        return self.thread is not None and self.thread.is_alive()

    def join(self, timeout=1.0):
        if self.thread is not None:
            try:
                self.thread.join(timeout=timeout)
            except Exception:
                pass


def run_http_server():
    server.main()


def run_ingestador():
    if ingestador and hasattr(ingestador, "main"):
        ingestador.main()
    else:
        while True:
            time.sleep(1)


def run_conectasao():
    if conectasao and hasattr(conectasao, "main"):
        conectasao.main()
    else:
        while True:
            time.sleep(1)


def create_default_services():
    return [
        ServiceWorker("http_server", run_http_server),
        ServiceWorker("ingestador", run_ingestador),
        ServiceWorker("conectasao", run_conectasao),
    ]


def run_supervisor(services=None, stop_event=None, check_interval=0.2, dotenv_path=".env"):
    load_dotenv(dotenv_path)

    if stop_event is None:
        stop_event = threading.Event()

    if services is None:
        services = create_default_services()

    for service in services:
        service.start()

    try:
        while not stop_event.is_set():
            stop_event.wait(timeout=check_interval)
            if stop_event.is_set():
                break

            for service in services:
                if not service.is_alive() and not stop_event.is_set():
                    service.start()
    except KeyboardInterrupt:
        pass
    finally:
        stop_event.set()
        try:
            server.stop_server()
        except Exception:
            pass

        for service in services:
            service.join(timeout=0.1)


def main():
    load_dotenv(".env")
    stop_event = threading.Event()

    def handle_signal(sig_num, frame):
        stop_event.set()

    try:
        signal.signal(signal.SIGINT, handle_signal)
    except Exception:
        pass

    try:
        signal.signal(signal.SIGTERM, handle_signal)
    except Exception:
        pass

    run_supervisor(stop_event=stop_event)


if __name__ == "__main__":
    main()
