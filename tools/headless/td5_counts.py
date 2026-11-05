#!/usr/bin/env python3
# td5_counts.py HOST:PORT [seconds] [every]: what the lobby says, on its own
# socket, the way the game's probe asks (@dir with TKPROBE=1): one line
# "<time> TKONLINE=n TKWAITING=n" per change, so a run can be read against
# what its copies did. With no seconds: one answer and out.
import socket, struct, sys, time

def ask(host, port):
    s = socket.create_connection((host, port), 2)
    s.settimeout(2)
    body = b"TKPROBE=1\0"
    s.sendall(b"@dir" + b"\0\0\0\0" + struct.pack(">I", 12 + len(body)) + body)
    hdr = b""
    while len(hdr) < 12:
        hdr += s.recv(12 - len(hdr))
    n = struct.unpack(">I", hdr[8:12])[0] - 12
    data = b""
    while len(data) < n:
        data += s.recv(n - len(data))
    s.close()
    kv = dict(p.split("=", 1) for p in data.decode("latin1").replace("\0", "\n").split("\n") if "=" in p)
    return kv.get("TKONLINE"), kv.get("TKWAITING")

host, port = sys.argv[1].rsplit(":", 1)
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 0
every = float(sys.argv[3]) if len(sys.argv) > 3 else 1
t0 = time.time()
last = None
while True:
    try:
        now = ask(host, int(port))
    except OSError as e:
        now = ("no answer", str(e))
    if now != last:
        print(time.strftime("%H:%M:%S"), "TKONLINE=%s TKWAITING=%s" % now, flush=True)
        last = now
    if time.time() - t0 >= secs:
        break
    time.sleep(every)
