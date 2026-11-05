#!/usr/bin/env python3
"""aries_logger.py: Takedawn's stand-in for EA's Aries lobby, as Burnout 3's
DirtySock speaks it (BO-134). The protocol notebook: the C server that ships
is written from what this one learns.

A separate program: the game talks to it only over sockets. Written from
what the game sends (the wire format below) and from what the game's code
reads back, not from any other server's code. MIT, like the rest of this
repository.

Wire format: a 12-byte header, then the body.
    cmd   4 ASCII bytes, e.g. "@dir"
    code  4 bytes (0 in a request; a reply's status, e.g. "new7")
    len   big-endian u32, the whole packet including the header
    body  KEY=VALUE lines, NUL-terminated

Every packet is logged. A command with a handler below gets its answer; any
other gets an empty success (same cmd, code 0, empty body), so each run
shows the next request the game makes.

    python3 tools/lobby/aries_logger.py [--ports 21810] [--log FILE]
                                        [--addr IP]   # handed out in redirects
"""
import argparse
import asyncio
import datetime
import itertools
import sys

LOG = sys.stdout
ADDR = "127.0.0.1"
PING_SECS = 20


def log(*a):
    ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
    print(ts, *a, file=LOG, flush=True)


def parse_body(body: bytes):
    text = body.split(b"\0", 1)[0].decode("latin-1")
    out = {}
    for tok in text.replace("\r", "\n").replace("\t", "\n").split("\n"):
        tok = tok.strip()
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    return text, out


def packet(cmd: bytes, body: str = "", code: bytes = b"\0\0\0\0") -> bytes:
    b = body.encode("latin-1") + b"\0" if body else b""
    return cmd + code + (12 + len(b)).to_bytes(4, "big") + b


def kv(d):
    return "".join(f"{k}={v}\n" for k, v in d.items())


# ── state ─────────────────────────────────────────────────────────────

ROOMS = {1: "Takedawn", 2: "Burnout"}
_ids = itertools.count(1)
_game_ids = itertools.count(1)
SESSIONS = set()
GAMES = {}


class Session:
    def __init__(self, writer, port):
        self.writer = writer
        self.port = port
        self.peer = writer.get_extra_info("peername")[0]
        self.uid = next(_ids)
        self.name = ""
        self.laddr = self.peer
        self.room = 0
        self.game = 0
        self.userparams = ""

    def send(self, cmd, body="", code=b"\0\0\0\0"):
        self.writer.write(packet(cmd, body, code))
        log(f"[{self.port}] -> {self.name or self.peer} {cmd.decode('latin-1')!r}"
            + (f" code={code!r}" if code != b"\0\0\0\0" else "") + f" body={body!r}")


class Game:
    def __init__(self, host, f):
        if not host.room:
            host.room = 1
        self.ident = next(_game_ids)
        self.name = f.get("NAME", host.name)
        self.password = f.get("PASS", "")
        self.params = f.get("PARAMS", "")
        self.sysflags = f.get("SYSFLAGS", "0")
        self.custflags = f.get("CUSTFLAGS", "0")
        self.minsize = f.get("MINSIZE", "2")
        self.maxsize = f.get("MAXSIZE", "6")
        # ROOM is the room the client thinks it is in; without a "move" first
        # (Quick Match) it is not a room id at all.
        r = int(f.get("ROOM", "0") or 0) if f.get("ROOM", "").lstrip("-").isdigit() else 0
        self.room = r if r in ROOMS else (host.room or 1)
        self.players = [host]

    def record(self):
        # HOST (0x773794 -> record +0x38): the host's persona; LobbyApi
        # leaves a game whose record has none within 1000 frames (0x1B9FE0).
        r = {"IDENT": self.ident, "NAME": self.name, "HOST": self.players[0].name if self.players else "",
             "PARAMS": self.params,
             "ROOM": self.room, "CUSTFLAGS": self.custflags, "SYSFLAGS": self.sysflags,
             "COUNT": len(self.players), "MINSIZE": self.minsize, "MAXSIZE": self.maxsize,
             "NUMPART": 1, "PARTSIZE0": self.maxsize, "PARTPARAMS0": "", "WHEN": "2004.9.7-0:00:00",
             "SEED": self.ident * 7919}
        for i, p in enumerate(self.players):
            r[f"OPID{i}"] = p.uid
            r[f"OPPO{i}"] = p.name
            r[f"ADDR{i}"] = p.peer
            r[f"LADDR{i}"] = p.laddr
            r[f"MADDR{i}"] = ""
            r[f"OPPART{i}"] = 0
            r[f"OPPARAM{i}"] = p.userparams
            r[f"OPFLAG{i}"] = 0
        return kv(r)


def who(s):
    # "+who": the server's record of this user; the title keeps it as "self"
    # (LobbyApi state 1 waits for it after the persona is chosen).
    return kv({"I": s.uid, "N": s.name, "M": s.name, "F": "", "A": s.peer, "LA": s.laddr,
               "P": 0, "S": "", "X": "", "G": s.game, "AT": "", "CL": 511, "LV": 1049601,
               "MD": 0, "R": ROOMS.get(s.room, ""), "RI": s.room, "RT": 0, "RM": 0, "RF": 0,
               "US": 0, "HW": 0, "RP": 0, "MA": "", "WI": 0, "WT": 0})


def room_rec(rid):
    count = sum(1 for s in SESSIONS if s.room == rid)
    # F letters are bits ('C' = bit 3: a room games can be created in).
    return kv({"I": rid, "N": ROOMS[rid], "H": "Takedawn", "D": ROOMS[rid], "F": "C",
               "T": count, "L": 50})


def in_room(rid):
    return [s for s in SESSIONS if s.room == rid]


# ── handlers: (session, fields) -> reply body, (code, body), or None ──

def h_dir(s, f):
    # The directory hands the client to the lobby: here, ourselves.
    return kv({"ADDR": ADDR, "PORT": s.port, "SESS": 1, "MASK": "f" * 32})


def h_addr(s, f):
    s.laddr = f.get("ADDR", s.peer)
    return ""


def h_news(s, f):
    # NAME=7 is the server configuration; the reply code must be "new7"
    # (0x216120). Keys the title parses: 0x1BA6A0..0x1BA780.
    name = f.get("NAME", "7")
    return (b"new" + name.encode()[:1],
            kv({"BUDDY_SERVER": ADDR, "BUDDY_PORT": s.port + 1,
                "TOSAC_URL": f"http://{ADDR}/tos.txt", "NEWS_URL": f"http://{ADDR}/news.txt"}))


def h_sele(s, f):
    s.send(b"sele", kv({"MORE": 0, "SLOTS": 32, "STATS": 0}))
    for rid in ROOMS:
        s.send(b"+rom", room_rec(rid))
    return None


def h_auth(s, f):
    # Any name logs in; the account is the gamertag (0x21B2A0 reads NAME and
    # PERSONAS, then the title sends "pers").
    s.name = f.get("GTAG") or f.get("NAME") or f"Player{s.uid}"
    return kv({"NAME": s.name, "PERSONAS": s.name, "MAIL": "", "BORN": "19700101", "GEND": "M",
               "FROM": "US", "LANG": "en", "SPAM": "NN", "SINCE": "2004.9.7-0:00:00",
               "LAST": "2004.9.7-0:00:00", "TOS": 1})


def h_pers(s, f):
    s.name = f.get("PERS") or f.get("GTAG") or s.name
    s.send(b"pers", kv({"PERS": s.name, "LKEY": "0123456789abcdef0123456789abcdef",
                        "LAST": "2004.9.7-0:00:00", "PLAST": "2004.9.7-0:00:00", "LOC": "enUS",
                        "A": s.peer, "LA": s.laddr, "IDLE": 60000}))
    s.send(b"+who", who(s))
    return None


def h_move(s, f):
    old, s.room = s.room, int(f.get("IDENT", f.get("NAME", 0)) or 0)
    s.send(b"move", kv({"IDENT": s.room, "NAME": ROOMS.get(s.room, ""), "COUNT": len(in_room(s.room))}))
    s.send(b"+who", who(s))
    for rid in {old, s.room} - {0}:
        for o in SESSIONS:
            o.send(b"+rom", room_rec(rid))
    return None


def h_gsea(s, f):
    games = list(GAMES.values())
    s.send(b"gsea", kv({"COUNT": len(games)}))
    for g in games:
        s.send(b"+gam", g.record())
    return None


def broadcast_game(g):
    # "+gam" fills every client's game list (the host too: its LobbyApi
    # waits up to 600 frames for its own game there, 0x1B9EE0, then gives
    # up); "+mgm" is "your game" for the players in it.
    # "+agm" is the list LobbyApi itself searches for the game it is in
    # (list 5, 0x2189D4); "+gam" the room's game list (list 4, 0x218AF8).
    # A record without NAME removes the game from either list.
    rec = g.record()
    for o in set(in_room(g.room)) | set(g.players):
        o.send(b"+gam", rec)
        o.send(b"+agm", rec)
    for p in g.players:
        p.send(b"+mgm", rec)
        # The self record's G is the game the user is in; LobbyApi drops a
        # game whose self record does not name it within 200 frames (0x1B9AD0).
        p.send(b"+who", who(p))


def h_gcre(s, f):
    g = Game(s, f)
    GAMES[g.ident] = g
    s.game = g.ident
    s.send(b"gcre", g.record())
    broadcast_game(g)
    return None


def h_gjoi(s, f):
    g = GAMES.get(int(f.get("IDENT", 0) or 0))
    if not g and f.get("NAME"):   # Quick Match joins by the game's name
        g = next((x for x in GAMES.values() if x.name == f["NAME"]), None)
    if "USERPARAMS" in f:
        s.userparams = f["USERPARAMS"]
    if not g:
        return (b"gjoi", "")  # TODO: the title's "no such game" code
    if s not in g.players:
        g.players.append(s)
    s.game = g.ident
    s.send(b"gjoi", g.record())
    broadcast_game(g)
    return None


def leave(s):
    g = GAMES.get(s.game)
    s.game = 0
    if not g:
        return
    if not s.writer.is_closing():
        s.send(b"+who", who(s))
    if s in g.players:
        g.players.remove(s)
    if not g.players:
        del GAMES[g.ident]
        for o in in_room(g.room):
            o.send(b"+gam", kv({"IDENT": g.ident}))
            o.send(b"+agm", kv({"IDENT": g.ident}))
    else:
        broadcast_game(g)


def h_glea(s, f):
    leave(s)
    return ""


def h_gdel(s, f):
    g = GAMES.pop(s.game, None)
    if g:
        for p in g.players:
            p.game = 0
    return ""


def h_gset(s, f):
    g = GAMES.get(s.game)
    if g:
        for k, a in (("PARAMS", "params"), ("SYSFLAGS", "sysflags"), ("CUSTFLAGS", "custflags"),
                     ("MAXSIZE", "maxsize"), ("MINSIZE", "minsize")):
            if k in f:
                setattr(g, a, f[k])
        if "USERPARAMS" in f:
            s.userparams = f["USERPARAMS"]
        s.send(b"gset", g.record())
        broadcast_game(g)
        return None
    return ""


HANDLERS = {
    b"@dir": h_dir, b"addr": h_addr, b"news": h_news, b"sele": h_sele, b"auth": h_auth,
    b"pers": h_pers, b"move": h_move, b"gsea": h_gsea, b"gcre": h_gcre, b"gjoi": h_gjoi,
    b"glea": h_glea, b"gdel": h_gdel, b"gset": h_gset,
}


async def handle(reader, writer, port):
    s = Session(writer, port)
    SESSIONS.add(s)
    log(f"[{port}] connect from {s.peer}")
    buf = b""

    async def pinger():
        # The client drops a session that has been quiet for a minute; a
        # server "~png" keeps it (the client answers it).
        n = 0
        while True:
            await asyncio.sleep(PING_SECS)
            n += 1
            try:
                writer.write(packet(b"~png", f"REF={n}\nTIME=1"))
                await writer.drain()
            except Exception:
                return
    ping_task = asyncio.create_task(pinger())
    try:
        while True:
            data = await reader.read(65536)
            if not data:
                break
            buf += data
            while len(buf) >= 12:
                n = int.from_bytes(buf[8:12], "big")
                if n < 12 or n > 1 << 20:
                    log(f"[{port}] not an Aries header: {buf[:64]!r}")
                    buf = b""
                    break
                if len(buf) < n:
                    break
                pkt, buf = buf[:n], buf[n:]
                cmd, code, body = pkt[:4], pkt[4:8], pkt[12:]
                if cmd == b"~png":
                    continue
                text, fields = parse_body(body)
                log(f"[{port}] <- {s.name or s.peer} {cmd.decode('latin-1')!r} code={code.hex()} body={text!r}")
                h = HANDLERS.get(cmd)
                out = h(s, fields) if h else ""
                if out is None:
                    pass
                elif isinstance(out, tuple):
                    s.send(cmd, out[1], out[0])
                else:
                    s.send(cmd, out)
                    if not h:
                        log(f"[{port}]    (unknown {cmd.decode('latin-1')!r}: empty success)")
                await writer.drain()
    except ConnectionError as e:
        log(f"[{port}] {e}")
    ping_task.cancel()
    leave(s)
    SESSIONS.discard(s)
    log(f"[{port}] closed {s.name or s.peer}")
    writer.close()


async def main():
    global LOG, ADDR
    ap = argparse.ArgumentParser()
    ap.add_argument("--ports", default="21810")
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--addr", default="127.0.0.1")
    ap.add_argument("--log")
    a = ap.parse_args()
    ADDR = a.addr
    if a.log:
        LOG = open(a.log, "a", buffering=1)
    servers = []
    for p in (int(x) for x in a.ports.split(",") if x):
        servers.append(await asyncio.start_server(
            lambda r, w, p=p: handle(r, w, p), a.bind, p))
        log(f"listening on {a.bind}:{p}")
    await asyncio.gather(*(s.serve_forever() for s in servers))


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
