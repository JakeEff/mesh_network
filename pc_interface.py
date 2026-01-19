import socket
import struct
import sqlite3
import os
from datetime import datetime, timezone



PORT = 5005
FMT = "<6s h H"
SIZE = struct.calcsize(FMT)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DB_PATH = os.path.join(SCRIPT_DIR, "mesh_readings.sqlite")

print("DB will be stored at:", DB_PATH)


def init_db(conn: sqlite3.Connection):
    conn.execute("""
    CREATE TABLE IF NOT EXISTS readings (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        ts TEXT NOT NULL,
        mac TEXT NOT NULL,
        tilt INTEGER NOT NULL,
        moist INTEGER NOT NULL,
        src_ip TEXT,
        src_port INTEGER
    );
    """)
    conn.execute("""
    CREATE TABLE IF NOT EXISTS latest (
        mac TEXT PRIMARY KEY,
        ts TEXT NOT NULL,
        tilt INTEGER NOT NULL,
        moist INTEGER NOT NULL,
        src_ip TEXT,
        src_port INTEGER
    );
    """)
    conn.execute("CREATE INDEX IF NOT EXISTS idx_readings_mac_ts ON readings(mac, ts);")
    conn.commit()

def mac_to_str(mac_bytes: bytes) -> str:
    return ":".join(f"{b:02x}" for b in mac_bytes)

conn = sqlite3.connect(DB_PATH)
conn.execute("PRAGMA journal_mode=WAL;")  
init_db(conn)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("0.0.0.0", PORT))
print("Listening on UDP Port", PORT)

while True:
    data, addr = sock.recvfrom(2048)

    if len(data) != SIZE:
        print(f"Got {len(data)} bytes from {addr}")
        continue

    mac, tilt, moist = struct.unpack(FMT, data)
    mac_str = mac_to_str(mac)

    ts = datetime.now(timezone.utc).isoformat()
    src_ip, src_port = addr

    
    conn.execute(
        "INSERT INTO readings (ts, mac, tilt, moist, src_ip, src_port) VALUES (?, ?, ?, ?, ?, ?)",
        (ts, mac_str, tilt, moist, src_ip, src_port),
    )

    
    conn.execute(
        """
        INSERT INTO latest (mac, ts, tilt, moist, src_ip, src_port)
        VALUES (?, ?, ?, ?, ?, ?)
        ON CONFLICT(mac) DO UPDATE SET
            ts=excluded.ts,
            tilt=excluded.tilt,
            moist=excluded.moist,
            src_ip=excluded.src_ip,
            src_port=excluded.src_port;
        """,
        (mac_str, ts, tilt, moist, src_ip, src_port),
    )

    conn.commit()
    print(f"From {addr} | src={mac_str} tilt={tilt} moist={moist}")
