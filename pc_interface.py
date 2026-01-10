import socket
import struct

PORT = 5005

FMT = "<6s h H"
SIZE = struct.calcsize(FMT)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("0.0.0.0", PORT))
print("Listening on UDP Port",PORT)

while True:
    data, addr = sock.recvfrom(2048)
    if len(data) == SIZE:
            mac, tilt, moist = struct.unpack(FMT, data)
            mac_str = ":".join(f"{b:02x}" for b in mac)
            print(f"From {addr} | scr={mac_str} tilt={tilt} moist={moist}")
    else:
          print(f"Got {len(data)} bytes from {addr}")