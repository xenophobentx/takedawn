#!/usr/bin/env python3
"""fake_client.py: a scripted second player for the Takedawn lobby (BO-134).

Logs in the way Burnout 3 does (addr, skey, news, sele, auth, pers) and can
join a game, so one real copy of the game can be tested against another
"player" without running two copies. It only speaks the lobby protocol;
it does not answer the game's peer-to-peer traffic.

    python3 tools/lobby/fake_client.py [--host 127.0.0.1] [--port 21810]
        [--name Racer2] [--join 1] [--stay 60]
"""
import argparse
import socket
import time


def packet(cmd, body=""):
    b = body.encode("latin-1") + b"\0" if body else b""
    return cmd.encode() + b"\0\0\0\0" + (12 + len(b)).to_bytes(4, "big") + b


def read_packets(sock, until=None, timeout=2.0):
    sock.settimeout(0.2)
    buf, out, end = b"", [], time.time() + timeout
    while time.time() < end:
        try:
            d = sock.recv(65536)
            if not d:
                break
            buf += d
        except socket.timeout:
            pass
        while len(buf) >= 12:
            n = int.from_bytes(buf[8:12], "big")
            if len(buf) < n:
                break
            p, buf = buf[:n], buf[n:]
            cmd, code, body = p[:4].decode("latin-1"), p[4:8], p[12:].split(b"\0")[0].decode("latin-1")
            print(f"<- {cmd} code={code!r} {body.replace(chr(10), ' ')[:200]}", flush=True)
            if cmd == "~png":
                sock.sendall(packet("~png", "REF=1\nTIME=30"))
            out.append((cmd, body))
            if until and cmd == until:
                return out
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=21810)
    ap.add_argument("--name", default="Racer2")
    ap.add_argument("--laddr", default="127.0.0.9")
    ap.add_argument("--join", type=int, default=0)
    ap.add_argument("--stay", type=int, default=60)
    a = ap.parse_args()
    s = socket.create_connection((a.host, a.port))

    def req(cmd, body="", until=None, timeout=2.0):
        print(f"-> {cmd} {body.replace(chr(10), ' ')}", flush=True)
        s.sendall(packet(cmd, body))
        return read_packets(s, until or cmd, timeout)

    req("addr", f"ADDR={a.laddr}\nPORT=1000\n")
    req("skey", "SKEY=$5075626c6963204b6579\n")
    req("news", "NAME=7")
    req("sele", "MYGAME=1 GAMES=1 ROOMS=1 USERS=1 MESGS=1 STATS=1 RANKS=0")
    req("auth", f"VERS=FLM/A1\nSKU=FLAME\nGTAG={a.name}\nPASS=$\nMASK=1\n")
    req("pers", f"GTAG={a.name}\n")
    req("move", "IDENT=1\n")
    req("gsea", "START=0\nCOUNT=500\n")
    if a.join:
        req("gjoi", f"IDENT={a.join}\nPASS=\n")
        req("gset", f"NAME={a.name}\nUSERPARAMS=-1779744768,1532330907,1,\n")
    read_packets(s, None, a.stay)
    req("glea", f"NAME={a.name}\nFORCE=1\n", timeout=1)
    s.close()


if __name__ == "__main__":
    main()
