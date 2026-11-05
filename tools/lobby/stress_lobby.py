#!/usr/bin/env python3
"""stress_lobby.py: many fake players against a lobby server you started
yourself on this machine (BO-329). Never point it at a public server.

    takedawn-lobby --port 31810 --max-sessions 2000 --stats-dir /tmp/st &
    ulimit -n 8192
    python3 tools/lobby/stress_lobby.py --port 31810 --clients 2010 --hold 25

Every client signs in (addr, sele, auth, pers). A few move into the room,
create games in different modes, join, start and report a result. All of
them answer the server's pings and stay for --hold seconds, then leave.
It prints how many signed in, how many were turned away ("full"), how many
games were created and started, and how long the sign-ins took.
"""
import argparse
import asyncio
import time


def packet(cmd, body=""):
    b = body.encode("latin-1") + b"\0" if body else b""
    return cmd.encode() + b"\0\0\0\0" + (12 + len(b)).to_bytes(4, "big") + b


class Client:
    def __init__(self, n):
        self.n = n
        self.name = f"Stress{n:05d}"
        self.got = {}
        self.pings = 0
        self.full = False
        self.buf = b""
        self.r = self.w = None

    async def open(self, host, port):
        self.r, self.w = await asyncio.open_connection(host, port)

    async def pump(self):
        """Read everything the server sends; answer pings; note replies."""
        try:
            while True:
                d = await self.r.read(65536)
                if not d:
                    return
                self.buf += d
                while len(self.buf) >= 12:
                    n = int.from_bytes(self.buf[8:12], "big")
                    if len(self.buf) < n:
                        break
                    head, self.buf = self.buf[:12], self.buf[n:]
                    cmd, code = head[:4], head[4:8]
                    if cmd == b"@dir" and code == b"full":
                        self.full = True
                    elif cmd == b"~png":
                        self.pings += 1
                        self.w.write(packet("~png", "REF=1\nTIME=30"))
                    else:
                        self.got[cmd] = self.got.get(cmd, 0) + 1
        except (ConnectionError, asyncio.CancelledError):
            return

    def send(self, cmd, body=""):
        self.w.write(packet(cmd, body))

    async def wait(self, cmd, timeout=30.0):
        end = time.time() + timeout
        while self.got.get(cmd, 0) == 0 and not self.full and time.time() < end:
            await asyncio.sleep(0.02)
        return self.got.get(cmd, 0) > 0

    async def login(self):
        self.send("addr", f"ADDR=127.0.1.{self.n % 250 + 1}\nPORT=1000\n")
        self.send("sele", "MYGAME=1 GAMES=1 ROOMS=1 USERS=1 MESGS=1 STATS=1 RANKS=0")
        self.send("auth", f"VERS=FLM/A1\nSKU=FLAME\nGTAG={self.name}\nPASS=$\nMASK=1\n")
        ok = await self.wait(b"auth")
        if ok:
            self.send("pers", f"GTAG={self.name}\n")
            ok = await self.wait(b"pers")
        return ok


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=21810)
    ap.add_argument("--clients", type=int, default=2000)
    ap.add_argument("--hold", type=float, default=25.0)
    ap.add_argument("--games", type=int, default=20, help="pairs that create, join and start a game")
    a = ap.parse_args()
    if a.host not in ("127.0.0.1", "localhost"):
        raise SystemExit("stress_lobby.py only runs against a server on this machine")

    cs = [Client(i) for i in range(a.clients)]
    t0 = time.time()
    sem = asyncio.Semaphore(200)

    async def go(c):
        async with sem:
            try:
                await c.open(a.host, a.port)
            except OSError as e:
                c.err = e
                return
            c.task = asyncio.create_task(c.pump())
            c.ok = await c.login()

    await asyncio.gather(*(go(c) for c in cs))
    t1 = time.time()
    live = [c for c in cs if getattr(c, "ok", False)]
    full = [c for c in cs if c.full]
    print(f"connected {len(cs) - sum(1 for c in cs if hasattr(c, 'err'))}, signed in {len(live)}, "
          f"not signed in (turned away: the server answers 'full' and closes) {len(cs) - len(live)} (saw the answer: {len(full)}), sign-ins took {t1 - t0:.1f} s", flush=True)

    # Games: host and guest pairs among the signed-in clients; modes cycle 0..4.
    made = started = 0
    for g in range(min(a.games, len(live) // 2)):
        h, p = live[2 * g], live[2 * g + 1]
        h.send("move", "IDENT=1\n")
        h.send("gcre", f"NAME=G{g}\nPASS=\nPARAMS=1,2,{g % 5},3,\nMINSIZE=2\nMAXSIZE=2\n"
                       "SYSFLAGS=262464\nROOM=1\n")
        if await h.wait(b"gcre"):
            made += 1
            p.send("gjoi", f"NAME=G{g}\nPASS=\nUSERPARAMS=1,1,3,\n")
            await p.wait(b"gjoi")
            h.send("gsta")
            if await h.wait(b"gsta"):
                started += 1
            h.send("rank", "RANK=1\n")
            p.send("rank", "RANK=2\n")
    print(f"games created {made}, started {started}", flush=True)

    await asyncio.sleep(a.hold)
    pings = sum(c.pings for c in live)
    print(f"pings answered {pings} (about {len(live)} per 20 s)", flush=True)
    for c in cs:
        if c.w is not None:
            c.w.close()
    await asyncio.sleep(1)
    for c in cs:
        t = getattr(c, "task", None)
        if t:
            t.cancel()


asyncio.run(main())
