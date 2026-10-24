import http.server
import json
import os
import re
import sqlite3
import urllib.parse
from collections import Counter


def init_db(db_path):
    try:
        connection = sqlite3.connect(db_path)
        cursor = connection.cursor()
        cursor.execute(
            """
            CREATE TABLE IF NOT EXISTS audios (
                audio_id INTEGER PRIMARY KEY AUTOINCREMENT,
                datetime TEXT NOT NULL,
                sighting_data BLOB NOT NULL
            )
            """
        )
        cursor.execute(
            """
            CREATE INDEX IF NOT EXISTS idx_audios_datetime_desc
            ON audios (datetime DESC, audio_id DESC)
            """
        )
        connection.commit()
        connection.close()
    except Exception:
        pass


def parse_sighting_data(raw_data, connection=None):
    if raw_data is None:
        return {}

    if isinstance(raw_data, dict):
        return raw_data

    if isinstance(raw_data, str):
        try:
            return json.loads(raw_data)
        except Exception:
            return {}

    if isinstance(raw_data, (bytes, bytearray, memoryview)):
        raw_bytes = bytes(raw_data)
        try:
            return json.loads(raw_bytes.decode("utf-8"))
        except Exception:
            pass

        if connection is not None:
            try:
                cursor = connection.cursor()
                cursor.execute("SELECT json(?)", (raw_bytes,))
                row = cursor.fetchone()
                if row and row[0]:
                    return json.loads(row[0])
            except Exception:
                pass

    return {}


def extract_species_list(data, reverse_order=True):
    if not isinstance(data, dict):
        return []

    species_mapping = data.get("species_name")
    if isinstance(species_mapping, dict):
        start_times = data.get("start_time", {})

        def sort_key(k):
            start_val = 0.0
            if isinstance(start_times, dict) and k in start_times:
                try:
                    start_val = float(start_times[k])
                except (ValueError, TypeError):
                    start_val = 0.0
            idx_val = int(k) if isinstance(k, int) or (isinstance(k, str) and k.isdigit()) else 0
            return (start_val, idx_val)

        sorted_keys = sorted(species_mapping.keys(), key=sort_key, reverse=reverse_order)
        return [str(species_mapping[k]) for k in sorted_keys if species_mapping.get(k)]

    if isinstance(species_mapping, list):
        items = [str(item) for item in species_mapping if item]
        return list(reversed(items)) if reverse_order else items

    if "data" in data and "columns" in data:
        columns = data.get("columns", [])
        if "species_name" in columns:
            name_idx = columns.index("species_name")
            rows = data.get("data", [])
            items = [str(r[name_idx]) for r in rows if len(r) > name_idx and r[name_idx]]
            return list(reversed(items)) if reverse_order else items

    return []


def get_latest_species(db_path, count=3):
    distinct_species = []
    seen = set()

    try:
        connection = sqlite3.connect(db_path)
        cursor = connection.cursor()
        cursor.execute("SELECT sighting_data FROM audios ORDER BY datetime DESC, audio_id DESC")

        for row in cursor:
            sighting_data = parse_sighting_data(row[0], connection)
            species_entries = extract_species_list(sighting_data, reverse_order=True)

            for species in species_entries:
                if species not in seen:
                    seen.add(species)
                    distinct_species.append(species)
                    if len(distinct_species) == count:
                        break

            if len(distinct_species) == count:
                break

        connection.close()
    except Exception:
        pass

    result = []
    if len(distinct_species) >= count:
        result = distinct_species[:count]
    elif len(distinct_species) == 2:
        result = [distinct_species[0], distinct_species[1], distinct_species[1]]
    elif len(distinct_species) == 1:
        result = [distinct_species[0], distinct_species[0], distinct_species[0]]
    else:
        result = ["", "", ""]

    return "\n".join(result)


def get_recent_audios(db_path, limit=10, offset=0):
    lines = []
    try:
        connection = sqlite3.connect(db_path)
        cursor = connection.cursor()
        cursor.execute(
            "SELECT audio_id, datetime FROM audios ORDER BY datetime DESC, audio_id DESC LIMIT ? OFFSET ?",
            (limit, offset),
        )
        for audio_id, audio_datetime in cursor.fetchall():
            lines.append(f"{audio_id} {audio_datetime}")
        connection.close()
    except Exception:
        pass

    return "\n".join(lines)


def get_audio_species(db_path, audio_id, limit=None, offset=0):
    lines = []
    try:
        connection = sqlite3.connect(db_path)
        cursor = connection.cursor()
        cursor.execute("SELECT sighting_data FROM audios WHERE audio_id = ?", (audio_id,))
        row = cursor.fetchone()
        if row:
            sighting_data = parse_sighting_data(row[0], connection)
            species_entries = extract_species_list(sighting_data, reverse_order=False)
            if offset > 0:
                species_entries = species_entries[offset:]
            if limit is not None and limit >= 0:
                species_entries = species_entries[:limit]
            lines = species_entries
        connection.close()
    except Exception:
        pass

    return "\n".join(lines)


def get_all_species_counts(db_path, limit=None, offset=0):
    counts = Counter()
    try:
        connection = sqlite3.connect(db_path)
        cursor = connection.cursor()
        cursor.execute("SELECT sighting_data FROM audios")
        for row in cursor:
            sighting_data = parse_sighting_data(row[0], connection)
            species_entries = extract_species_list(sighting_data, reverse_order=False)
            counts.update(species_entries)
        connection.close()
    except Exception:
        pass

    sorted_species = sorted(counts.items(), key=lambda item: (-item[1], item[0]))
    if offset > 0:
        sorted_species = sorted_species[offset:]
    if limit is not None and limit >= 0:
        sorted_species = sorted_species[:limit]

    lines = [f"{species}: {count}" for species, count in sorted_species]
    return "\n".join(lines)


class SpeciesRequestHandler(http.server.BaseHTTPRequestHandler):
    db_path = "species.sqlite3"

    def log_message(self, format, *args):
        pass

    def do_GET(self):
        try:
            parsed_url = urllib.parse.urlparse(self.path)
            path = parsed_url.path.rstrip("/")
            if path == "":
                path = "/"
            query_params = urllib.parse.parse_qs(parsed_url.query)

            if path in ("", "/", "/latest", "/latest_species", "/species/latest", "/species"):
                body = get_latest_species(self.db_path)
                self.send_plain_text(200, body)
                return

            if path in ("/recent_audios", "/audios", "/audios/recent"):
                limit = 10
                offset = 0
                if "limit" in query_params:
                    try:
                        limit = int(query_params["limit"][0])
                    except (ValueError, IndexError):
                        pass
                if "offset" in query_params:
                    try:
                        offset = int(query_params["offset"][0])
                    except (ValueError, IndexError):
                        pass
                body = get_recent_audios(self.db_path, limit=limit, offset=offset)
                self.send_plain_text(200, body)
                return

            match_audio = re.match(r"^/audios?/(\d+)/species$", path)
            if match_audio or path in ("/audio_species", "/audio/species"):
                audio_id = None
                if match_audio:
                    audio_id = int(match_audio.group(1))
                elif "audio_id" in query_params:
                    try:
                        audio_id = int(query_params["audio_id"][0])
                    except (ValueError, IndexError):
                        pass

                if audio_id is None:
                    self.send_plain_text(400, "Missing audio_id")
                    return

                limit = None
                offset = 0
                if "limit" in query_params:
                    try:
                        limit = int(query_params["limit"][0])
                    except (ValueError, IndexError):
                        pass
                if "offset" in query_params:
                    try:
                        offset = int(query_params["offset"][0])
                    except (ValueError, IndexError):
                        pass

                body = get_audio_species(self.db_path, audio_id=audio_id, limit=limit, offset=offset)
                self.send_plain_text(200, body)
                return

            if path in ("/all_species", "/species/all", "/species_counts"):
                limit = None
                offset = 0
                if "limit" in query_params:
                    try:
                        limit = int(query_params["limit"][0])
                    except (ValueError, IndexError):
                        pass
                if "offset" in query_params:
                    try:
                        offset = int(query_params["offset"][0])
                    except (ValueError, IndexError):
                        pass

                body = get_all_species_counts(self.db_path, limit=limit, offset=offset)
                self.send_plain_text(200, body)
                return

            self.send_plain_text(404, "Not Found")
        except Exception:
            self.send_plain_text(500, "Internal Server Error")

    def send_plain_text(self, status_code, body_text):
        try:
            body_bytes = body_text.encode("utf-8")
            self.send_response(status_code)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body_bytes)))
            self.end_headers()
            self.wfile.write(body_bytes)
        except Exception:
            pass


active_server = None


def create_server(host="0.0.0.0", port=8000, db_path="species.sqlite3"):
    init_db(db_path)
    handler_class = type("ConfiguredSpeciesRequestHandler", (SpeciesRequestHandler,), {"db_path": db_path})
    server = http.server.ThreadingHTTPServer((host, port), handler_class)
    return server


def stop_server():
    global active_server
    try:
        if active_server is not None:
            active_server.shutdown()
            active_server.server_close()
    except Exception:
        pass


def run_server(host="0.0.0.0", port=8000, db_path="species.sqlite3"):
    global active_server
    server = create_server(host=host, port=port, db_path=db_path)
    active_server = server
    try:
        server.serve_forever()
    except Exception:
        pass
    finally:
        try:
            server.server_close()
        except Exception:
            pass
        if active_server is server:
            active_server = None


def main():
    host = os.environ.get("HOST", "0.0.0.0")
    port_str = os.environ.get("PORT", os.environ.get("HTTP_PORT", "8000"))
    db_path = os.environ.get("DB_PATH", "species.sqlite3")

    try:
        port = int(port_str)
    except ValueError:
        port = 8000

    run_server(host=host, port=port, db_path=db_path)


if __name__ == "__main__":
    main()
