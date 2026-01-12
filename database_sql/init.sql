-- File to format and save data in SQL
-- By Greg Kirk for SMART Pole Capstone
-- Jan 12 2026

PRAGMA foreign_keys = ON;

-- One row per pole/device (MAC address)
CREATE TABLE IF NOT EXISTS devices (
  mac TEXT PRIMARY KEY,                           -- "aa:bb:cc:dd:ee:ff"
  first_seen_utc TEXT NOT NULL DEFAULT (datetime('now')),
  last_seen_utc  TEXT NOT NULL DEFAULT (datetime('now'))
);

-- Time-series readings (one row per received packet)
CREATE TABLE IF NOT EXISTS readings (
  id INTEGER PRIMARY KEY AUTOINCREMENT,

  mac TEXT NOT NULL,                              -- matches mac_str from Python
  received_utc TEXT NOT NULL DEFAULT (datetime('now')),  -- server receive time

  tilt INTEGER NOT NULL,                           -- from struct 'h'
  moisture INTEGER NOT NULL,                       -- from struct 'H'

  -- Future: device-provided timestamp (when you add it to the packet)
  device_time_utc TEXT NULL,

  FOREIGN KEY(mac) REFERENCES devices(mac) ON DELETE CASCADE,

  -- Optional sanity checks; adjust if needed
  CHECK (length(mac) = 17),                        -- "aa:bb:cc:dd:ee:ff" = 17 chars
  CHECK (moisture >= 0 AND moisture <= 65535)
);

-- Indexes for common queries (per pole over time)
CREATE INDEX IF NOT EXISTS idx_readings_mac_received
  ON readings(mac, received_utc DESC);

CREATE INDEX IF NOT EXISTS idx_readings_received
  ON readings(received_utc DESC);
