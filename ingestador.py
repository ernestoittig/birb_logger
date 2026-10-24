import os
import os.path as path
import signal
import sqlite3

import birdnet
from birdnet.acoustic.models.v3_0.model import AcousticModelV3_0

from watchdog.events import FileSystemEventHandler
from watchdog.observers import Observer

SPOOL_DIR = "./audios"
CONFIDENCE_CUTOFF = 0.5

def process_spooled_files(model: AcousticModelV3_0, db: sqlite3.Connection):
    filelist = [f for f in os.listdir(SPOOL_DIR) if path.isfile(path.join(SPOOL_DIR, f)) and f.endswith(".wav")]

    print(f"Processing {len(filelist)} files")

    cur = db.cursor()
    for file in filelist:
        predictions = model.predict(path.join(SPOOL_DIR, file))

        df = predictions.to_dataframe()
        print(df)
        df_high_confidence = df[df["confidence"] > CONFIDENCE_CUTOFF]
        if (len(df_high_confidence) == 0):
            print(f"Discarding {file}")
            os.unlink(path.join(SPOOL_DIR, file))
            continue

        cur.execute("INSERT INTO audios (datetime, sighting_data) VALUES(?, jsonb(?))", (file.removesuffix(".wav"), df_high_confidence.to_json()))

        print(f"Processed {file} ({predictions.n_species})")

        os.unlink(path.join(SPOOL_DIR, file))
    db.commit()

def ensure_schema(db: sqlite3.Connection):
    cur = db.cursor()
    cur.execute("""
    CREATE TABLE IF NOT EXISTS audios (
        audio_id INTEGER PRIMARY KEY AUTOINCREMENT,
        datetime TEXT NOT NULL,
        sighting_data BLOB NOT NULL
    );
    """)
    cur.fetchall()

def main():
    model = birdnet.load("acoustic", "3.0", "onnx", lang="es")
    db = sqlite3.connect("species.sqlite3", check_same_thread=False)
    ensure_schema(db)

    process_spooled_files(model, db)

    class AudiosEvenHandler(FileSystemEventHandler):
        def on_created(self, event):
            _ = event
            process_spooled_files(model, db)

    observer = Observer()
    observer.schedule(AudiosEvenHandler(), path=SPOOL_DIR)
    observer.start()

    try:
        signal.pause()
    except KeyboardInterrupt:
        print("\nStopping directory watcher...")
        observer.stop()
        db.close()

    observer.join()

if __name__ == "__main__":
    main()
