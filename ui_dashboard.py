import sqlite3
import pandas as pd
import streamlit as st
import matplotlib.pyplot as plt
import os


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DB_PATH = os.path.join(SCRIPT_DIR, "mesh_readings.sqlite")


HISTORY_LIMIT = 500

#thresholds
TILT_MIN, TILT_MAX = -3000, 3000     
MOIST_MIN, MOIST_MAX = 0, 4095       

st.set_page_config(page_title="Pole Monitoring Dashboard", layout="wide")


def get_conn_readonly(db_path: str) -> sqlite3.Connection:
    uri = f"file:{db_path}?mode=ro"
    conn = sqlite3.connect(uri, uri=True, timeout=2.0)
    conn.execute("PRAGMA busy_timeout=2000;")
    return conn

def load_poles() -> list[str]:
    conn = get_conn_readonly(DB_PATH)
    rows = conn.execute("SELECT DISTINCT mac FROM readings ORDER BY mac;").fetchall()
    conn.close()
    return [r[0] for r in rows]

def load_latest(mac: str) -> dict | None:
    conn = get_conn_readonly(DB_PATH)
    row = conn.execute(
        "SELECT mac, ts, tilt, moist, src_ip FROM latest WHERE mac = ?;",
        (mac,),
    ).fetchone()
    conn.close()
    if not row:
        return None
    return {"mac": row[0], "ts": row[1], "tilt": row[2], "moist": row[3], "src_ip": row[4]}

def load_history(mac: str, limit: int) -> pd.DataFrame:
    conn = get_conn_readonly(DB_PATH)
    df = pd.read_sql_query(
        """
        SELECT ts, tilt, moist
        FROM readings
        WHERE mac = ?
        ORDER BY ts DESC
        LIMIT ?;
        """,
        conn,
        params=(mac, limit),
    )
    conn.close()

    # convert ts to datetime
    df["ts"] = pd.to_datetime(df["ts"], utc=True, errors="coerce")
    df = df.dropna(subset=["ts"])
    df = df.sort_values("ts")  # ascending time for plotting
    return df

def compute_flags(df: pd.DataFrame) -> pd.DataFrame:
    df = df.copy()
    df["tilt_flag"] = (df["tilt"] < TILT_MIN) | (df["tilt"] > TILT_MAX)
    df["moist_flag"] = (df["moist"] < MOIST_MIN) | (df["moist"] > MOIST_MAX)
    df["any_flag"] = df["tilt_flag"] | df["moist_flag"]
    return df


st.title("Utility Pole Health Monitoring – Dashboard")


poles = load_poles()
if not poles:
    st.warning("No poles found yet. Make sure the UDP listener is running and writing to the database.")
    st.stop()

selected_mac = st.selectbox("Select a pole (MAC)", poles, index=0)

latest = load_latest(selected_mac)
history = load_history(selected_mac, HISTORY_LIMIT)
history_flags = compute_flags(history)

colA, colB, colC = st.columns(3)

if latest:
    colA.metric("Selected Pole", selected_mac)
    colB.metric("Latest Tilt", f"{latest['tilt']}")
    colC.metric("Latest Moisture", f"{latest['moist']}")
    st.caption(f"Last reading timestamp (UTC): {latest['ts']} | Source IP: {latest['src_ip']}")
else:
    st.info("No latest row for this MAC yet (history exists, but latest table may not have updated).")

# Flags summary
flag_count = int(history_flags["any_flag"].sum())
if flag_count > 0:
    st.error(f"⚠️ {flag_count} flagged reading(s) in the last {len(history_flags)} samples (out of range).")
else:
    st.success("No out-of-range flags in the displayed history.")

# Graphs
left, right = st.columns(2)

with left:
    st.subheader("Tilt vs Time")
    fig = plt.figure()
    plt.plot(history_flags["ts"], history_flags["tilt"])
    plt.xlabel("Time (UTC)")
    plt.ylabel("Tilt")
    st.pyplot(fig, clear_figure=True)

with right:
    st.subheader("Moisture vs Time")
    fig = plt.figure()
    plt.plot(history_flags["ts"], history_flags["moist"])
    plt.xlabel("Time (UTC)")
    plt.ylabel("Moisture")
    st.pyplot(fig, clear_figure=True)

#History table
st.subheader("Recent History")
show_flags_only = st.checkbox("Show only flagged readings", value=False)

table_df = history_flags.copy()
table_df["ts"] = table_df["ts"].dt.strftime("%Y-%m-%d %H:%M:%S.%f UTC")

if show_flags_only:
    table_df = table_df[table_df["any_flag"]]

st.dataframe(
    table_df[["ts", "tilt", "moist", "tilt_flag", "moist_flag"]],
    use_container_width=True,
    height=350,
)

st.caption("Tip: Refresh the browser to update. If you want, I can add an auto-refresh toggle.")
