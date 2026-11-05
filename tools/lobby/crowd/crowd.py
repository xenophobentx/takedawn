#!/usr/bin/env python3
"""crowd.py: hundreds of fake players against a Takedawn TEST lobby (BO-366).

Every fake player speaks the lobby protocol the way the real game does (the
title's own requests plus the lines the toolkit and the game add: TKID,
TKNAME, TKCODE, TKPUBLIC, TKWHY), learnt from the client code and from
verbose lobby logs of real copies:

  sign-in   a directory connection (@tic, @dir, closed), then the lobby
            connection: addr (TKID=1, TKNAME), skey, news, sele, auth, pers,
            sviw, [rank after a race], move IDENT=1
  QUICK PLAY   gsea; join the first game by NAME; refused: search again, the
            third refusal or NO GAMES FOUND makes its own public game
  CREATE GAME  gcre with TKPUBLIC=1 and TKCODE (public, with a code)
  private      gcre with TKCODE only (the System page's Game code)
  JOIN WITH CODE  gsea TKCODE=, gjoi NAME= TKCODE=
  BROWSE       gsea, gjoi IDENT=
  in a game    gset USERPARAMS (car, ready); the host locks (SYSFLAGS bit 12)
            and sends gsta; on +ses every player closes its lobby connection
            (the race), signs in again after the race, sends rank, and the
            stay-together path re-hosts (gcre, same NAME) / re-joins (gsea +
            gjoi NAME=host)
  kicks     the host's mesg PRIV= + gset KICK= TKWHY=host, repeated
  leaving   host gdel, guest glea
  vandals   bad input at a low rate on throw-away connections

It must never be pointed at the public lobby: real players would see fake
games in QUICK PLAY and fake numbers on the main menu. Run the server with
--crowd-test (the fake players and real copies then do not see each other's
games) on a machine of our own. See README.md.

Python 3.8+, standard library only.
"""
import argparse
import asyncio
import collections
import ipaddress
import json
import os
import random
import socket
import string
import sys
import time

# ── safety ────────────────────────────────────────────────────────────

LIVE_NAMES = ("vps4954",)                  # the public lobby (vps4954.rackhostvps.com)
LIVE_IPS = {"79.172.222.137"}


def target_guard(host, i_know_live, allow_public):
    """Refuses the public lobby (by name and by every address the name has)
    and, unless allow_public, any address that is not loopback or private."""
    low = host.lower()
    try:
        infos = socket.getaddrinfo(host, None, socket.AF_INET, socket.SOCK_STREAM)
        addrs = sorted({i[4][0] for i in infos})
    except socket.gaierror as e:
        sys.exit(f"crowd: cannot resolve {host}: {e}")
    live = any(n in low for n in LIVE_NAMES) or any(a in LIVE_IPS for a in addrs)
    if live and not i_know_live:
        sys.exit(f"crowd: REFUSED: {host} ({', '.join(addrs)}) is the public Takedawn lobby. "
                 "Fake players there show up in real players' QUICK PLAY and menu counts. "
                 "Run a local lobby (takedawn-lobby --crowd-test) instead.")
    if not live and not allow_public:
        for a in addrs:
            ip = ipaddress.ip_address(a)
            if not (ip.is_loopback or ip.is_private):
                sys.exit(f"crowd: REFUSED: {host} -> {a} is a public address; a load run belongs on "
                         "a lobby of our own on loopback or the LAN (--allow-public to override)")
    return addrs


# ── wire ──────────────────────────────────────────────────────────────

Z4 = b"\0\0\0\0"
PUSHES = {"+gam", "+agm", "+mgm", "+who", "+usr", "+rom", "+ses", "+msg", "+snp", "~png"}
# reply codes a real client knows for each request; anything else is an anomaly
OK_CODES = {"news": {"new7"}, "gjoi": {"full", "gone", "pass"}, "gcre": {"full"}, "@dir": {"full"}}
BIT_LOCK = 0x1000
BIT_STARTED = 0x80000
SYSFLAGS = 262464                           # the title's gcre SYSFLAGS
CAR = "-1779744768,1532330907"              # the title's USERPARAMS car words (from real logs)
PARAMS = ["-362987520,0,0,16842788,50331648,-994223927,0,0,0,",
          "-994230272,0,0,16842788,50331648,-994223965,0,0,0,",
          "-1622913024,0,0,16842788,50331648,-994224003,0,0,0,"]
CODE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"   # the toolkit's (xl_code_make)


def packet(cmd, body="", code=Z4):
    b = body.encode("latin-1") + b"\0" if body else b""
    return cmd.encode("latin-1") + code + (12 + len(b)).to_bytes(4, "big") + b


def kv(body):
    d = {}
    for line in body.split("\n"):
        k, sep, v = line.partition("=")
        if sep:
            d[k] = v
    return d


def lines(*pairs):
    return "".join(f"{k}={v}\n" for k, v in pairs if v is not None)


# ── statistics ────────────────────────────────────────────────────────

class Stats:
    def __init__(self):
        self.lat = collections.defaultdict(list)        # op -> [ms]
        self.lat_iv = collections.defaultdict(list)     # this report interval
        self.refusals = collections.Counter()           # "op:code"
        self.errors = collections.Counter()
        self.anom = collections.Counter()
        self.examples = collections.defaultdict(list)
        self.count = collections.Counter()
        self.ops = 0
        self.loop_lag = []

    def time(self, op, ms, req=True):
        """req: one request and its answer (the per-interval numbers and
        ops/s); else a whole step of several (sign-in, start -> in a game)."""
        self.lat[op].append(ms)
        if req:
            self.lat_iv[op].append(ms)
            self.ops += 1

    def _ex(self, key, text):
        if len(self.examples[key]) < 5:
            self.examples[key].append(f"{time.strftime('%H:%M:%S')} {text}"[:400])

    def error(self, kind, text=""):
        self.errors[kind] += 1
        self._ex("E " + kind, text)

    def anomaly(self, kind, text=""):
        self.anom[kind] += 1
        self._ex("A " + kind, text)


def pct(v, p):
    if not v:
        return 0.0
    s = sorted(v)
    return s[min(len(s) - 1, int(p / 100.0 * len(s)))]


# ── server process (when the lobby runs on this machine) ─────────────

class ServerWatch:
    def __init__(self, pid):
        self.pid = pid
        self.samples = []          # (t, rss_kb, cpu_pct, fds)
        self.gone = False
        self._last = None
        self.hz = os.sysconf("SC_CLK_TCK") if hasattr(os, "sysconf") else 100

    def sample(self):
        if not self.pid or self.gone:
            return None
        try:
            with open(f"/proc/{self.pid}/stat") as f:
                st = f.read().rsplit(")", 1)[1].split()
            ticks = int(st[11]) + int(st[12])
            rss = 0
            with open(f"/proc/{self.pid}/status") as f:
                for ln in f:
                    if ln.startswith("VmRSS:"):
                        rss = int(ln.split()[1])
            fds = len(os.listdir(f"/proc/{self.pid}/fd"))
        except (OSError, IndexError, ValueError):
            self.gone = True
            return None
        now = time.monotonic()
        cpu = 0.0
        if self._last:
            dt = now - self._last[0]
            cpu = 100.0 * (ticks - self._last[1]) / self.hz / dt if dt > 0 else 0.0
        self._last = (now, ticks)
        self.samples.append((now, rss, cpu, fds))
        return rss, cpu, fds


def find_server_pid(port):
    """The pid listening on port (Linux /proc), or 0."""
    try:
        want = f":{port:04X}"
        inodes = set()
        for fn in ("/proc/net/tcp", "/proc/net/tcp6"):
            try:
                with open(fn) as f:
                    next(f)
                    for ln in f:
                        p = ln.split()
                        if p[1].endswith(want) and p[3] == "0A":
                            inodes.add(p[9])
            except OSError:
                pass
        if not inodes:
            return 0
        for pid in os.listdir("/proc"):
            if not pid.isdigit():
                continue
            try:
                for fd in os.listdir(f"/proc/{pid}/fd"):
                    l = os.readlink(f"/proc/{pid}/fd/{fd}")
                    if l.startswith("socket:[") and l[8:-1] in inodes:
                        return int(pid)
            except OSError:
                continue
    except Exception:
        pass
    return 0


# ── one lobby connection ─────────────────────────────────────────────

class Dropped(Exception):
    pass


class Conn:
    """A lobby connection: answers pings, checks every answer the way the
    title would need it, hands pushes to on_push, and times requests."""

    def __init__(self, crowd, player, label):
        self.c = crowd
        self.st = crowd.stats
        self.p = player
        self.label = label
        self.r = self.w = None
        self.task = None
        self.waiter = None              # [cmd, future, collect_n, collected, code, body]
        self.closed = False
        self.closing = False            # we close it (no "dropped" error)
        self.expect_eof = False         # a vandal waits for the server to hang up
        self.code_sent = False
        self.lenient = player is None

    async def open(self):
        t0 = time.monotonic()
        try:
            self.r, self.w = await asyncio.wait_for(
                asyncio.open_connection(self.c.host, self.c.port), self.c.args.timeout)
        except (OSError, asyncio.TimeoutError) as e:
            self.st.error("connect", f"{self.label}: {e!r}")
            raise Dropped("connect")
        self.st.time("connect", (time.monotonic() - t0) * 1000)
        self.task = asyncio.ensure_future(self._pump())

    def close(self):
        self.closing = True
        if self.w:
            try:
                self.w.close()
            except Exception:
                pass
        if self.task:
            self.task.cancel()

    async def _pump(self):
        buf = b""
        try:
            while True:
                d = await self.r.read(65536)
                if not d:
                    break
                buf += d
                while len(buf) >= 12:
                    n = int.from_bytes(buf[8:12], "big")
                    if n < 12:
                        self.st.anomaly("bad packet length", f"{self.label}: length {n}")
                        raise Dropped("framing")
                    if n > 65536:
                        self.st.anomaly("packet over 64 KB", f"{self.label}: {buf[:4]!r} {n}")
                    if len(buf) < n:
                        break
                    p, buf = buf[:n], buf[n:]
                    self._packet(p)
        except (ConnectionError, OSError, Dropped):
            pass
        except asyncio.CancelledError:
            return
        self.closed = True
        if not self.closing and not self.expect_eof:
            self.st.error("dropped by the server", f"{self.label} during {self.p.phase if self.p else '?'}")
        self._fail(Dropped("eof"))

    def _fail(self, exc):
        w = self.waiter
        if w and not w[1].done():
            w[1].set_exception(exc)

    def _packet(self, p):
        cmd = p[:4].decode("latin-1")
        code = p[4:8]
        raw = p[12:]
        if raw and raw[-1:] != b"\0":
            self.st.anomaly("body not NUL-terminated", f"{cmd} {raw[:80]!r}")
        body = raw.split(b"\0")[0]
        if any(ch < 0x20 and ch != 0x0A for ch in body) or len(body) > 8192:
            self.st.anomaly("control bytes or >8 KB in a body", f"{cmd} len {len(body)}")
        body = body.decode("latin-1")
        if cmd == "~png":
            self.send("~png", "REF=1\nTIME=30")
            self.st.count["pings"] += 1
            return
        w = self.waiter
        if w and w[2] is not None:            # collecting the +gam records after a gsea reply
            if cmd != "+gam":
                self.st.anomaly("gsea list cut by another packet", f"{cmd} after {len(w[3])}/{w[2]} records")
                self._resolve()
            else:
                w[3].append(kv(body))
                if len(w[3]) >= w[2]:
                    self._resolve()
                return
        if w and cmd == w[0]:
            scode = "" if code == Z4 else code.decode("latin-1", "replace")
            if scode and scode not in OK_CODES.get(cmd, ()):
                self.st.anomaly("unknown answer code", f"{cmd} code {scode!r}")
            w[4], w[5] = scode, body
            if cmd == "gsea" and not scode:
                d = kv(body)
                try:
                    n = int(d.get("COUNT", "x"))
                except ValueError:
                    self.st.anomaly("gsea without COUNT", body[:120])
                    n = 0
                if n > 0:
                    w[2] = n
                    return
            self._resolve()
            return
        if cmd in PUSHES:
            if self.p:
                self.p.on_push(self, cmd, kv(body), body)
            return
        if self.lenient:                 # a vandal that did not wait for its answers
            return
        if w:
            self.st.anomaly("answer out of order", f"waiting for {w[0]}, got {cmd}")
        else:
            self.st.anomaly("answer nobody asked for", cmd)

    def _resolve(self):
        w = self.waiter
        self.waiter = None              # the next packet is not part of this answer
        if w and not w[1].done():
            w[1].set_result((w[4], w[5], w[3]))

    def send(self, cmd, body="", code=Z4):
        if self.closed or not self.w:
            return
        try:
            self.w.write(packet(cmd, body, code))
        except Exception:
            pass

    async def req(self, cmd, body="", op=None, timeout=None):
        """Sends a request and waits for its answer: (code, body, records)."""
        if self.closed:
            raise Dropped("closed")
        fut = asyncio.get_running_loop().create_future()
        self.waiter = [cmd, fut, None, [], "", ""]
        t0 = time.monotonic()
        self.send(cmd, body)
        try:
            res = await asyncio.wait_for(fut, timeout or self.c.args.timeout)
        except asyncio.TimeoutError:
            self.st.error(f"timeout {cmd}", f"{self.label} {body[:80]!r}")
            self.waiter = None
            raise Dropped("timeout")
        finally:
            if self.waiter and self.waiter[1] is fut:
                self.waiter = None
        self.st.time(op or cmd, (time.monotonic() - t0) * 1000)
        if res[0] and not (cmd == "news" and res[0] == "new7"):
            self.st.refusals[f"{op or cmd}:{res[0]}"] += 1
        return res


# ── record checks: what the title needs from a game record ───────────

def check_record(st, rec, where, member):
    if rec.get("IDENT", "0") in ("0", "") or "NAME" not in rec:
        return                       # an empty record, or "IDENT=n" alone: off the lists
    try:
        n = int(rec.get("COUNT", "x"))
    except ValueError:
        st.anomaly("game record without COUNT", where)
        return
    names = [rec.get(f"OPPO{i}") for i in range(n)]
    if any(x is None for x in names) or f"OPPO{n}" in rec:
        st.anomaly("game record COUNT does not match its players", f"{where} COUNT={n}")
    if n and (not rec.get("HOST") or rec.get("HOST") != names[0]):
        st.anomaly("game record HOST missing or not player 0", f"{where} HOST={rec.get('HOST')!r}")
    if not rec.get("NAME"):
        st.anomaly("game record without NAME", where)
    if not member:
        leak = [k for k in rec if k.startswith("TKSECRET") or k.startswith("TKRADDR") or
                ((k.startswith("ADDR") or k.startswith("LADDR")) and rec[k] not in ("0.0.0.0", ""))]
        if leak:
            st.anomaly("address or secret in a list record", f"{where}: {leak[:3]}")


# ── one fake player ──────────────────────────────────────────────────

class Player:
    def __init__(self, crowd, n):
        self.c = crowd
        self.a = crowd.args
        self.st = crowd.stats
        self.n = n
        self.name = f"{crowd.args.prefix}{n:04d}"
        self.rng = random.Random(crowd.args.seed * 100003 + n)
        # a local address of its own (10.120.0.0 up: clear of the real copies' TAKEDAWN_NET_ID 10.77.x)
        self.laddr = f"10.{120 + n // 64000 % 100}.{n // 250 % 256}.{n % 250 + 2}"
        self.mid = "$%016x" % self.rng.getrandbits(64)
        self.mac = "$0254%08x" % self.rng.getrandbits(32)
        # the title's XUID: u[0..1] a name hash, u[2..3] the port offset, u[4..7] the
        # player's address; written as a 64-bit number, u[7] first (aries_server.c auth)
        ab = [int(x) for x in self.laddr.split(".")]
        self.xuid = "$" + "".join("%02x" % x for x in reversed(ab)) + "0000" + "%04x" % self.rng.getrandbits(16)
        self.conn = None
        self.phase = "offline"
        self.game = None             # the game record (dict) of the game we are in
        self.host = False
        self.code = ""               # the code this player uses now ("" none)
        self.events = asyncio.Queue()
        self.after_race = False
        self.mates = []
        self.race_ident = None

    # pushes
    def is_member(self, d):
        try:
            n = int(d.get("COUNT", "0") or 0)
        except ValueError:
            return False
        return any(d.get(f"OPPO{i}") == self.name for i in range(n))

    def on_push(self, conn, cmd, d, body):
        if conn is not self.conn:
            return
        if cmd in ("+gam", "+agm"):
            check_record(self.st, d, cmd, member=self.is_member(d))
            self.c.isolation_check(self, d, cmd)
        elif cmd == "+mgm":
            if d.get("IDENT", "0") == "0":
                self.events.put_nowait(("out", d.get("TKWHY", "")))
            else:
                check_record(self.st, d, "+mgm", member=True)
                if not self.is_member(d):
                    self.st.anomaly("+mgm of a game without us in it", f"{self.name} {d.get('IDENT')}")
                if self.game is not None and d.get("IDENT") == self.game.get("IDENT"):
                    self.game = d
        elif cmd == "+ses":
            self.events.put_nowait(("ses", d))

    def drain(self):
        while not self.events.empty():
            self.events.get_nowait()

    def bump(self, k, v=1):
        self.st.count[k] += v

    async def sleep(self, lo, hi=None):
        await asyncio.sleep(self.rng.uniform(lo, hi if hi is not None else lo))

    # sign in
    async def sign_in(self):
        self.phase = "sign-in"
        t0 = time.monotonic()
        d = Conn(self.c, self, f"{self.name}/dir")
        await d.open()
        try:
            await d.req("@tic", "RC4+MD5-V2")
            code, body, _ = await d.req("@dir", lines(("VERS", "FLM/A1"), ("SKU", "FLAME"), ("SLUS", "SLUS-FLAME"),
                                                      ("LANG", "en"), ("FROM", ""), ("MID", self.mac)))
            if code == "full":
                self.bump("server full at @dir")
                raise Dropped("full")
            dd = kv(body)
            if "ADDR" not in dd or "PORT" not in dd:
                self.st.anomaly("@dir without ADDR/PORT", body[:120])
            elif dd["PORT"] != str(self.c.port):
                self.c.note_once("dir port", f"@dir sends players to port {dd['PORT']} (crowd uses {self.c.port})")
        finally:
            d.close()
        c = Conn(self.c, self, self.name)
        await c.open()
        self.conn = c
        try:
            _, body, _ = await c.req("addr", lines(("ADDR", self.laddr), ("PORT", self.rng.randint(30000, 60000)),
                                                   ("TKID", 1), ("TKNAME", self.name),
                                                   ("TKCROWD", 1 if not self.a.as_real else None)))
            ad = kv(body)
            if not self.a.as_real and ad.get("TKCROWD") != "1":
                self.c.not_isolated()
            if "TKSID" not in ad:
                self.st.anomaly("addr answer without TKSID", body[:80])
            await c.req("skey", "SKEY=$5075626c6963204b6579\n")
            code, _, _ = await c.req("news", "NAME=7")
            sele = "MYGAME=1 GAMES=1 ROOMS=1 USERS=1 MESGS=1 STATS=1 RANKS=0"
            cl = self.code_lines("sele")
            await c.req("sele", sele + ("\n" + cl if cl else ""))
            code, body, _ = await c.req("auth", lines(
                ("VERS", "FLM/A1"), ("SKU", "FLAME"), ("SLUS", "SLUS-FLAME"), ("LANG", "en"), ("FROM", ""),
                ("MID", self.mid), ("GTAG", self.name), ("XUID", self.xuid), ("PURCHBAN", 0), ("XFLAGS", 0),
                ("MAC", self.mac), ("PASS", "$"), ("MASK", 1)))
            if kv(body).get("NAME") != self.name:
                self.st.anomaly("auth answer with another NAME", body[:80])
            await c.req("pers", lines(("GTAG", self.name), ("XUID", self.xuid), ("MID", self.mid),
                                      ("PURCHBAN", 0), ("XFLAGS", 0), ("MAC", self.mac)))
            await c.req("sviw", "VIEW=PS2\n")
            if self.after_race:
                await c.req("rank", self.rank_body())
                self.after_race = False
            await self.move()
        except Dropped:
            c.close()
            self.conn = None
            raise
        self.st.time("sign-in (all)", (time.monotonic() - t0) * 1000, req=False)
        self.bump("sign-ins")
        self.phase = "lobby"

    def rank_body(self):
        names = self.mates or [self.name]
        parts = [("WHEN", "2004.9.7 0:00:00"), ("REPT", self.name), ("AUTH", ""), ("VENUE", 0)]
        for i, nm in enumerate(names[:6]):
            parts += [(f"NAME{i}", nm), (f"TEAM{i}", i), (f"WEIGHT{i}", 0)]
        parts += [("SKU", "BURNOUT3"), ("POINTS", self.rng.randint(300, 1500)), ("CRASHDAMAGE", 0),
                  ("FAVVEH", 0), ("ONLINETAKEDOWNS", 0)]
        return lines(*parts)

    def code_lines(self, cmd):
        """The toolkit's TKCODE line: the code on gcre/gsea/gjoi/move/sele,
        an empty one on move/sele when a code went out before (clears it)."""
        if self.code:
            self.conn.code_sent = True
            return f"TKCODE={self.code}\n"
        if self.conn.code_sent and cmd in ("move", "sele"):
            self.conn.code_sent = False
            return "TKCODE=\n"
        return ""

    async def move(self, ident=1):
        await self.conn.req("move", (f"IDENT={ident}\n" if ident else "") + self.code_lines("move"),
                            op="move" if ident else "move out")

    def sign_out(self):
        if self.conn:
            self.conn.close()
        self.conn = None
        self.game = None
        self.phase = "offline"

    # the life of a player
    async def run(self):
        await asyncio.sleep(self.rng.uniform(0, self.a.ramp))
        while not self.c.stopping:
            try:
                if not self.conn:
                    await self.sign_in()
                await self.round()
            except Dropped:
                self.sign_out()
                await self.sleep(2, 6)
            except asyncio.CancelledError:
                self.sign_out()
                return
            except Exception as e:          # a crowd bug: count it, keep the player going
                self.st.error("crowd internal", f"{self.name}: {e!r}")
                self.sign_out()
                await self.sleep(5, 10)
            if not self.c.stopping and self.rng.random() < self.a.churn:
                self.bump("churn: signed out for a while")
                self.sign_out()
                await self.sleep(*self.a.offline_secs)

    async def round(self):
        """One go from the online page: host or join, then the races that
        follow (stay together) until the group breaks up."""
        await self.sleep(*self.a.think_secs)
        if self.c.stopping:
            return
        if self.rng.random() < self.a.hosts:
            mode = self.pick({"quick": self.a.mix_quick, "create": self.a.mix_create, "private": self.a.mix_private})
            await self.host_game(mode)
        else:
            r = self.rng.random()
            if r < self.a.code_joins:
                await self.join_code()
            elif r < self.a.code_joins + self.a.browse_joins:
                await self.quick_play(browse=True)
            else:
                await self.quick_play()

    def pick(self, weights):
        tot = sum(weights.values()) or 1
        x = self.rng.uniform(0, tot)
        for k, w in weights.items():
            x -= w
            if x <= 0:
                return k
        return k

    def new_code(self):
        return "".join(self.rng.choice(CODE_ALPHABET) for _ in range(5))

    # hosting
    async def host_game(self, mode, code=None):
        self.code = code if code is not None else (self.new_code() if mode in ("create", "private") else "")
        while not self.c.stopping:
            self.phase = "creating"
            params = self.rng.choice(PARAMS)
            body = lines(("NAME", self.name), ("PASS", ""), ("PARAMS", params), ("MINSIZE", 2), ("MAXSIZE", 6),
                         ("SYSFLAGS", SYSFLAGS), ("ROOM", 1), ("TKPUBLIC", 1 if mode == "create" else None))
            body += self.code_lines("gcre")
            code, rb, _ = await self.conn.req("gcre", body, op=f"gcre {mode}")
            if code:
                self.bump(f"gcre refused ({code})")
                self.code = ""
                await self.sleep(5, 15)
                return
            rec = kv(rb)
            check_record(self.st, rec, "gcre", member=True)
            if rec.get("HOST") != self.name:
                self.st.anomaly("gcre answer: not our game", rb[:100])
            self.game, self.host = rec, True
            self.drain()
            self.bump(f"games created: {mode}")
            if self.code:
                self.c.codes[self.code] = (self.name, mode)
            try:
                await self.conn.req("gset", f"NAME={self.name}\nUSERPARAMS={CAR},0,\n", op="gset car")
                started = await self.host_lobby()
            finally:
                if self.code in self.c.codes and self.c.codes[self.code][0] == self.name:
                    del self.c.codes[self.code]
            if not started:
                self.code = ""
                return
            # the race: the lobby connection is closed at the start
            await self.race()
            # stay together: sign in again and make the game again (same NAME, same kind)
            if self.c.stopping or self.rng.random() < self.a.leave_after_race:
                self.bump("host left after the race")
                self.code = ""
                return
            await self.sign_in()
            await self.sleep(*self.a.rehost_secs)
            self.bump("stay: re-host")
        self.code = ""

    async def host_lobby(self):
        """Waits for players, kicks now and then, starts. True: started."""
        self.phase = "hosting"
        t_open = time.monotonic()
        wait = self.rng.uniform(*self.a.lobby_secs)
        ready_sent = False
        kicked = False
        while not self.c.stopping:
            n = int(self.game.get("COUNT", "1") or 1)
            el = time.monotonic() - t_open
            if not ready_sent and el > 3:
                await self.conn.req("gset", f"NAME={self.name}\nUSERPARAMS={CAR},3,\n", op="gset ready")
                ready_sent = True
            if n >= 2 and not kicked and self.rng.random() < self.a.kicks and el > 5:
                victim = self.game.get(f"OPPO{self.rng.randint(1, n - 1)}")
                if victim:
                    await self.kick(victim)
                    kicked = True
            if (n >= 2 and el >= wait) or n >= 6:
                return await self.start_race()
            if el >= self.a.host_give_up:
                self.bump("host gave up waiting (gdel)")
                await self.conn.req("gdel", "", op="gdel")
                self.game, self.host = None, False
                return False
            if self.rng.random() < self.a.host_quits / 10.0:
                self.bump("host left its lobby (gdel)")
                await self.conn.req("gdel", "", op="gdel")
                self.game, self.host = None, False
                return False
            try:
                ev = await asyncio.wait_for(self.events.get(), 1.0)
                if ev[0] == "out":
                    self.st.anomaly("host got an empty game record", f"{self.name} TKWHY={ev[1]}")
                    self.game, self.host = None, False
                    return False
            except asyncio.TimeoutError:
                pass
        return False

    async def kick(self, victim):
        """The title's kick: mesg to the player, gset KICK with the reason,
        repeated a few times (it repeats until the player is off the list)."""
        self.bump("kicks sent")
        for i in range(3):
            await self.conn.req("mesg", f"PRIV={victim}\nTEXT={victim}\nATTR=CNP2\n", op="mesg")
            await self.conn.req("gset", f"NAME={self.name}\nKICK={victim}\nTKWHY=host\n",
                                op="gset kick" if i == 0 else "gset kick again")

    async def start_race(self):
        self.phase = "starting"
        f = int(self.game.get("SYSFLAGS", SYSFLAGS) or SYSFLAGS)
        await self.conn.req("gset", f"NAME={self.name}\nSYSFLAGS={f | BIT_LOCK}\n", op="gset lock")
        # one race for the whole group: the guests finish when the host does
        self.c.race_len[self.game.get("IDENT")] = self.rng.uniform(*self.a.race_secs)
        t0 = time.monotonic()
        code, body, _ = await self.conn.req("gsta", f"NAME={self.name}\n", op="gsta")
        rec = kv(body)
        if not (int(rec.get("SYSFLAGS", "0") or 0) & BIT_STARTED):
            self.st.anomaly("gsta answer without the started bit", body[:100])
        # the +ses push follows the answer
        try:
            while True:
                ev = await asyncio.wait_for(self.events.get(), self.a.timeout)
                if ev[0] == "ses":
                    break
        except asyncio.TimeoutError:
            self.st.error("no +ses after gsta", self.name)
            return False
        self.st.time("gsta -> +ses", (time.monotonic() - t0) * 1000, req=False)
        self.mates = [rec.get(f"OPPO{i}") for i in range(int(rec.get("COUNT", "0") or 0))]
        self.bump("races started (host)")
        self.c.races_started += 1
        self.race_ident = rec.get("IDENT")
        return True

    async def race(self):
        """The players close the lobby connection right after the start and
        come back when the race and the results are over."""
        self.phase = "racing"
        await asyncio.sleep(self.rng.uniform(0.2, 1.2))
        self.sign_out()
        self.phase = "racing"
        self.c.racing += 1
        try:
            ln = self.c.race_len.get(self.race_ident)
            if ln is None:
                ln = self.rng.uniform(*self.a.race_secs)
            # the same race; the results and the sign-in take each player a little more or less
            await asyncio.sleep(ln + self.rng.uniform(0, 8))
        finally:
            self.c.racing -= 1
        self.after_race = True
        self.bump("races finished")

    # joining
    async def quick_play(self, browse=False, refusals=0):
        kind = "browse" if browse else "quick"
        t0 = time.monotonic()
        self.code = ""
        while not self.c.stopping:
            self.phase = "searching"
            await self.move(0)                    # the online page: out of the room, search, into the room
            code, body, recs = await self.conn.req("gsea", "START=0\nCOUNT=500\n" + self.code_lines("gsea"),
                                                   op="gsea")
            self.check_search(recs, body, code_used=False)
            if not recs:
                d = kv(body)
                if self.a.racewait and d.get("TKRACEID"):
                    joined = await self.race_wait(d)
                    if joined:
                        return await self.guest_lobby(kind, t0)
                if browse:
                    self.bump("browse: nothing listed")
                    return
                # NO GAMES FOUND: QUICK PLAY makes its own public game
                self.bump("quick play: none found, creates")
                return await self.host_game("quick")
            g = recs[0] if not browse else self.rng.choice(recs)
            await self.move(1)
            self.phase = "joining"
            if browse:
                jb = lines(("IDENT", g.get("IDENT")), ("PASS", ""), ("USERPARAMS", "0,0,0,"))
            else:
                jb = lines(("NAME", g.get("NAME")), ("PASS", ""), ("USERPARAMS", "0,0,0,"))
            code, rb, _ = await self.conn.req("gjoi", jb + self.code_lines("gjoi"), op=f"gjoi {kind}")
            if not code:
                return await self.joined(rb, kind, t0)
            refusals += 1
            self.bump(f"join refused ({code})")
            if browse:
                return
            if refusals >= 3:                 # QP_REFUSE_MAX: the third refusal makes its own game
                self.bump("quick play: refused 3x, creates")
                return await self.host_game("quick")
            await self.sleep(0.5, 2)

    async def join_code(self):
        t0 = time.monotonic()
        live = list(self.c.codes.items())
        if not live or self.rng.random() < self.a.wrong_codes:
            self.code = self.new_code()
            wrong = True
        else:
            self.code, (owner, mode) = self.rng.choice(live)
            wrong = False
        try:
            self.phase = "searching"
            # the title's Quick Match with the code: out of the room, search, into the room, join
            # (a move into the room with a code is a code lookup too: at most one a second)
            await self.move(0)
            code, body, recs = await self.conn.req("gsea", "START=0\nCOUNT=500\n" + self.code_lines("gsea"),
                                                   op="gsea code")
            self.check_search(recs, body, code_used=True)
            if not recs:
                self.bump("code join: no game for the code" + (" (wrong code)" if wrong else ""))
                await self.move(1)
                return
            g = recs[0]
            await self.move(1)
            self.phase = "joining"
            code, rb, _ = await self.conn.req("gjoi", lines(("NAME", g.get("NAME")), ("PASS", ""),
                                                            ("USERPARAMS", "0,0,0,")) + self.code_lines("gjoi"),
                                              op="gjoi code")
            if code:
                self.bump(f"join refused ({code})")
                return
            await self.joined(rb, "code", t0)
        finally:
            self.code = ""

    def check_search(self, recs, body, code_used):
        for r in recs:
            check_record(self.st, r, "gsea", member=False)
            self.c.isolation_check(self, r, "gsea")
            try:
                if int(r.get("COUNT", 0)) >= int(r.get("MAXSIZE", 6)):
                    self.st.anomaly("search offered a full game", r.get("IDENT"))
                if int(r.get("SYSFLAGS", 0)) & BIT_STARTED:
                    self.st.anomaly("search offered a started game", r.get("IDENT"))
            except ValueError:
                self.st.anomaly("game record with a bad number", str(r)[:100])
        self.bump("searches")
        if not recs:
            self.bump("searches: empty")

    async def joined(self, rb, kind, t0):
        rec = kv(rb)
        check_record(self.st, rec, "gjoi", member=True)
        names = [rec.get(f"OPPO{i}") for i in range(int(rec.get("COUNT", "0") or 0))]
        if self.name not in names:
            self.st.anomaly("gjoi answer without us in it", rb[:100])
        self.game, self.host = rec, False
        self.drain()
        self.st.time(f"{kind}: start -> in a game", (time.monotonic() - t0) * 1000, req=False)
        self.bump(f"joins: {kind}")
        return await self.guest_lobby(kind, t0)

    async def guest_lobby(self, kind, t0):
        """In someone's lobby: car, ready, then the race or the way out."""
        while self.game is not None and not self.c.stopping:
            self.phase = "guest"
            host = self.game.get("HOST", "")
            await self.conn.req("gset", f"NAME={host}\nUSERPARAMS={CAR},1,\n", op="gset car")
            await self.sleep(1, 3)
            if self.game is None:
                break
            await self.conn.req("gset", f"NAME={host}\nUSERPARAMS={CAR},3,\n", op="gset ready")
            t_in = time.monotonic()
            while True:
                try:
                    ev = await asyncio.wait_for(self.events.get(), 1.0)
                except asyncio.TimeoutError:
                    ev = None
                if self.c.stopping:
                    return
                if ev and ev[0] == "ses":
                    self.mates = [self.game.get(f"OPPO{i}") for i in range(int(self.game.get("COUNT", "0") or 0))]
                    self.bump("races started (guest)")
                    self.race_ident = self.game.get("IDENT")
                    await self.race()
                    if self.rng.random() < self.a.leave_after_race:
                        self.bump("guest left after the race")
                        self.game = None
                        return
                    await self.sign_in()
                    if await self.stay_rejoin(host):
                        break               # in the host's new game: car, ready again
                    return
                if ev and ev[0] == "out":
                    why = ev[1] or "none"
                    self.bump(f"guest out of the game (TKWHY={why})")
                    self.game = None
                    return
                if time.monotonic() - t_in > self.a.guest_give_up:
                    self.bump("guest gave up waiting (glea)")
                    await self.conn.req("glea", f"NAME={self.name}\nFORCE=1\n", op="glea")
                    self.game = None
                    return
                if self.rng.random() < self.a.guest_quits / 10.0:
                    self.bump("guest left the lobby (glea)")
                    await self.conn.req("glea", f"NAME={self.name}\nFORCE=1\n", op="glea")
                    self.game = None
                    return

    async def stay_rejoin(self, host):
        """Stay together: the guest looks for the host's game made again and
        joins it by name, for up to stay_secs."""
        self.phase = "stay"
        t0 = time.monotonic()
        while time.monotonic() - t0 < self.a.stay_secs and not self.c.stopping:
            code, body, recs = await self.conn.req("gsea", "START=0\nCOUNT=500\n" + self.code_lines("gsea"),
                                                   op="gsea stay")
            self.check_search(recs, body, code_used=bool(self.code))
            if any(r.get("NAME") == host for r in recs):
                code, rb, _ = await self.conn.req("gjoi", lines(("NAME", host), ("PASS", ""), ("USERPARAMS", "0,0,0,"))
                                                  + self.code_lines("gjoi"), op="gjoi stay")
                if not code:
                    rec = kv(rb)
                    check_record(self.st, rec, "gjoi", member=True)
                    self.game = rec
                    self.drain()
                    self.st.time("stay: back -> in the new game", (time.monotonic() - t0) * 1000, req=False)
                    self.bump("stay: re-joined")
                    return True
                self.bump(f"stay: join refused ({code})")
            await asyncio.sleep(3)
        self.bump("stay: host's game never came")
        self.game = None
        return False

    async def race_wait(self, d):
        """BO-365 (behind --racewait): wait for a race in progress, poll with
        TKWAITFOR, join the host's game made again when it is open."""
        rid = d.get("TKRACEID")
        self.bump("racewait: started")
        t0 = time.monotonic()
        while time.monotonic() - t0 < 480 and not self.c.stopping:
            await asyncio.sleep(3)
            code, body, recs = await self.conn.req("gsea", f"START=0\nCOUNT=500\nTKWAITFOR={rid}\n" +
                                                   self.code_lines("gsea"), op="gsea waitfor")
            w = kv(body).get("TKWAITSTATE", "")
            if w not in ("racing", "lobby", "open", "full", "gone"):
                self.st.anomaly("racewait: unknown TKWAITSTATE", repr(w))
                return False
            self.bump(f"racewait poll: {w}")
            if w == "gone":
                return False
            if w == "open" and recs:
                code, rb, _ = await self.conn.req("gjoi", lines(("NAME", recs[0].get("NAME")), ("PASS", ""),
                                                                ("USERPARAMS", "0,0,0,")) + self.code_lines("gjoi"),
                                                  op="gjoi racewait")
                if not code:
                    self.game, self.host = kv(rb), False
                    self.drain()
                    self.st.time("racewait: wait -> in the game", (time.monotonic() - t0) * 1000, req=False)
                    self.bump("racewait: joined")
                    return True
                self.bump(f"racewait: join refused ({code})")
        self.bump("racewait: gave up")
        return False


# ── vandals: bad input at a low rate, each on its own connection ──────

async def vandal(crowd, n):
    st = crowd.stats
    rng = random.Random(crowd.args.seed * 7 + n)
    kind = rng.choice(["early", "field", "badcode", "garbage", "unknown", "oversize", "flood", "half"])
    name = f"{crowd.args.prefix[:2]}V{n % 100000:05d}"
    c = Conn(crowd, None, f"vandal/{kind}")
    try:
        await c.open()
    except Dropped:
        return
    outcome = "?"
    try:
        async def signin():
            await c.req("addr", f"ADDR=10.99.{n // 250 % 256}.{n % 250 + 2}\nPORT=1000\nTKID=1\nTKNAME={name}\n"
                        + ("" if crowd.args.as_real else "TKCROWD=1\n"), op="vandal addr")
            await c.req("sele", "MYGAME=1 GAMES=1", op="vandal sele")
            await c.req("auth", f"GTAG={name}\nPASS=$\nMASK=1\n", op="vandal auth")
            await c.req("pers", f"GTAG={name}\n", op="vandal pers")
        c.expect_eof = kind in ("early", "field", "garbage", "oversize", "half")
        if kind == "early":
            c.send("gsea", "START=0\nCOUNT=500\n")
        elif kind == "field":
            await signin()
            c.send("gcre", f"NAME={name}\nMAXSIZE=99\n")
        elif kind == "badcode":
            await signin()
            code, _, _ = await c.req("gjoi", f"NAME=Nobody\nPASS=\nTKCODE={''.join(rng.choice(CODE_ALPHABET) for _ in range(6))}\n",
                                     op="vandal wrong code")
            outcome = code or "ok"
        elif kind == "garbage":
            c.w.write(b"\xff\xfe\x00\x01" + Z4 + (5).to_bytes(4, "big"))
        elif kind == "unknown":
            await signin()
            code, _, _ = await c.req("zzzz", "WHAT=1\n", op="vandal unknown")
            outcome = code or "empty success"
        elif kind == "oversize":
            await signin()
            c.send("gsea", "X=" + "A" * 9000)
        elif kind == "flood":
            await signin()
            for _ in range(150):
                c.send("gsea", "START=0\nCOUNT=500\n")
            await asyncio.sleep(2)
            outcome = "closed" if c.closed else "still open"
        elif kind == "half":
            c.w.write(packet("addr", "ADDR=1.2.3.4\n")[:9])
            await asyncio.sleep(0.5)
            c.close()
            outcome = "sent half"
        if c.expect_eof and kind != "half":
            t0 = time.monotonic()
            while not c.closed and time.monotonic() - t0 < 30:
                await asyncio.sleep(0.2)
            outcome = "closed by the server" if c.closed else "NOT closed"
            if not c.closed:
                st.anomaly(f"vandal {kind}: the server kept the connection", "")
    except Dropped:
        outcome = "dropped"
    finally:
        c.close()
    st.count[f"vandal {kind}: {outcome}"] += 1


# ── the crowd ────────────────────────────────────────────────────────

class Crowd:
    def __init__(self, args):
        self.args = args
        self.host, self.port = args.host, args.port
        self.stats = Stats()
        self.stopping = False
        self.codes = {}               # live crowd game codes -> (host, mode): how friends hear of a code
        self.race_len = {}            # game IDENT -> seconds of its race (the group finishes together)
        self.racing = 0
        self.races_started = 0
        self.players = []
        self.notes = {}
        self.isolated = None
        self.watch = ServerWatch(args.server_pid or (find_server_pid(args.port) if args.host in
                                                     ("127.0.0.1", "localhost") else 0))
        self.t0 = time.monotonic()

    def note_once(self, key, text):
        if key not in self.notes:
            self.notes[key] = text
            print(f"crowd: note: {text}", flush=True)

    def not_isolated(self):
        if self.isolated is None:
            self.isolated = False
            msg = ("the lobby does not say TKCROWD=1: it is not running with --crowd-test, so real "
                   "players would see the fake games")
            if self.args.require_isolation:
                print(f"crowd: STOP: {msg}", flush=True)
                self.stopping = True
                self.stats.error("lobby not in --crowd-test mode", "")
            else:
                self.note_once("iso", msg)

    def isolation_check(self, p, rec, where):
        host = rec.get("HOST", "")
        if host and not host.startswith(self.args.prefix) and not self.args.as_real:
            self.stats.anomaly("isolation: a fake player saw a real game", f"{where} host {host!r}")

    def online(self):
        return sum(1 for p in self.players if p.conn is not None)

    def in_game(self):
        return sum(1 for p in self.players if p.game is not None and p.conn is not None)

    async def lag_probe(self):
        while not self.stopping:
            t = time.monotonic()
            await asyncio.sleep(0.1)
            self.stats.loop_lag.append((time.monotonic() - t - 0.1) * 1000)

    async def reporter(self):
        last_ops, last_t = 0, time.monotonic()
        while not self.stopping:
            await asyncio.sleep(self.args.report_every)
            now = time.monotonic()
            s = self.watch.sample()
            if self.watch.pid and self.watch.gone:
                print("crowd: THE LOBBY PROCESS IS GONE: stopping", flush=True)
                self.stats.error("server process gone", f"pid {self.watch.pid}")
                self.stopping = True
            iv = [x for v in self.stats.lat_iv.values() for x in v]
            self.stats.lat_iv.clear()
            line = (f"[{now - self.t0:6.0f}s] online {self.online():4d} in-game {self.in_game():4d} "
                    f"racing {self.racing:4d} codes {len(self.codes):3d} ops/s {(self.stats.ops - last_ops) / (now - last_t):6.1f} "
                    f"p50 {pct(iv, 50):6.1f} p99 {pct(iv, 99):7.1f} ms  err {sum(self.stats.errors.values())} "
                    f"anom {sum(self.stats.anom.values())}")
            if s:
                line += f"  server rss {s[0] / 1024:.1f} MB cpu {s[1]:.0f}% fds {s[2]}"
            print(line, flush=True)
            last_ops, last_t = self.stats.ops, now
        self.watch.sample()

    async def vandals(self):
        rate = self.args.bad_per_hour * self.args.players / 3600.0
        if rate <= 0:
            return
        rng = random.Random(self.args.seed)
        n = 0
        while not self.stopping:
            await asyncio.sleep(rng.expovariate(rate))
            n += 1
            asyncio.ensure_future(vandal(self, n))

    async def run(self):
        print(f"crowd: {self.args.players} players against {self.host}:{self.port} for {self.args.duration} s "
              f"(ramp {self.args.ramp} s), lobby pid {self.watch.pid or 'not watched'}", flush=True)
        self.watch.sample()
        self.players = [Player(self, i + self.args.first) for i in range(self.args.players)]
        tasks = [asyncio.ensure_future(p.run()) for p in self.players]
        side = [asyncio.ensure_future(self.reporter()), asyncio.ensure_future(self.lag_probe()),
                asyncio.ensure_future(self.vandals())]
        t_end = time.monotonic() + self.args.duration
        while time.monotonic() < t_end and not self.stopping:
            await asyncio.sleep(0.5)
        self.stopping = True
        for t in tasks + side:
            t.cancel()
        await asyncio.gather(*tasks, *side, return_exceptions=True)
        for p in self.players:
            p.sign_out()
        await asyncio.sleep(0.5)
        self.watch.sample()
        return self.summary()

    def summary(self):
        st = self.stats
        el = time.monotonic() - self.t0
        out = {"players": self.args.players, "seconds": round(el), "target": f"{self.host}:{self.port}",
               "isolated": self.isolated is not False, "races_started": self.races_started,
               "ops": st.ops, "latency_ms": {}, "refusals": dict(st.refusals), "errors": dict(st.errors),
               "anomalies": dict(st.anom), "counts": dict(st.count), "examples": dict(st.examples),
               "loop_lag_ms": {"p50": round(pct(st.loop_lag, 50), 1), "p99": round(pct(st.loop_lag, 99), 1),
                               "max": round(max(st.loop_lag or [0]), 1)}, "notes": self.notes}
        for op, v in sorted(st.lat.items()):
            out["latency_ms"][op] = {"n": len(v), "p50": round(pct(v, 50), 2), "p90": round(pct(v, 90), 2),
                                     "p99": round(pct(v, 99), 2), "max": round(max(v), 2)}
        w = self.watch
        if w.samples:
            out["server"] = {"pid": w.pid, "gone": w.gone,
                             "rss_mb_start": round(w.samples[0][1] / 1024, 1),
                             "rss_mb_max": round(max(s[1] for s in w.samples) / 1024, 1),
                             "rss_mb_end": round(w.samples[-1][1] / 1024, 1),
                             "cpu_pct_avg": round(sum(s[2] for s in w.samples[1:]) / max(1, len(w.samples) - 1), 1),
                             "cpu_pct_max": round(max(s[2] for s in w.samples), 1),
                             "fds_max": max(s[3] for s in w.samples)}
        return out


def print_summary(s):
    print("\n==== crowd summary ====")
    print(f"{s['players']} players, {s['seconds']} s, {s['target']}, isolated (--crowd-test): {s['isolated']}, "
          f"races started: {s['races_started']}, requests: {s['ops']}")
    if "server" in s:
        v = s["server"]
        print(f"server pid {v['pid']}: rss {v['rss_mb_start']} -> max {v['rss_mb_max']} -> {v['rss_mb_end']} MB, "
              f"cpu avg {v['cpu_pct_avg']}% max {v['cpu_pct_max']}%, fds max {v['fds_max']}"
              + ("  ** PROCESS GONE **" if v["gone"] else ""))
    print(f"crowd loop lag ms: p50 {s['loop_lag_ms']['p50']} p99 {s['loop_lag_ms']['p99']} max {s['loop_lag_ms']['max']}"
          "  (high lag inflates every latency below)")
    print(f"{'op':32s} {'n':>7s} {'p50':>8s} {'p90':>8s} {'p99':>8s} {'max':>8s}  (ms)")
    for op, v in s["latency_ms"].items():
        print(f"{op:32s} {v['n']:7d} {v['p50']:8.2f} {v['p90']:8.2f} {v['p99']:8.2f} {v['max']:8.2f}")
    for title, key in (("refusals", "refusals"), ("errors", "errors"), ("PROTOCOL ANOMALIES", "anomalies"),
                       ("counts", "counts")):
        d = s[key]
        print(f"-- {title}: {sum(d.values()) if d else 0}")
        for k, v in sorted(d.items()):
            print(f"   {v:8d}  {k}")
    if s["examples"]:
        print("-- examples")
        for k, v in sorted(s["examples"].items()):
            for e in v[:3]:
                print(f"   {k}: {e}")


def rng_pair(text):
    a, _, b = text.partition(",")
    return (float(a), float(b or a))


def main():
    ap = argparse.ArgumentParser(description="fake players against a Takedawn test lobby (never the public one)",
                                 formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=21810)
    ap.add_argument("--players", type=int, default=50)
    ap.add_argument("--first", type=int, default=0, help="number of the first player (two crowds: different ranges)")
    ap.add_argument("--prefix", default="Crowd", help="name prefix of the fake players (<= 10 characters)")
    ap.add_argument("--duration", type=float, default=600, help="seconds")
    ap.add_argument("--ramp", type=float, default=60, help="seconds over which the players arrive")
    ap.add_argument("--think-secs", type=rng_pair, default=(2, 10), help="min,max on the online page before a choice")
    ap.add_argument("--hosts", type=float, default=0.3, help="share of choices that host a game")
    ap.add_argument("--mix-quick", type=float, default=0.5, help="weight: hosts making a QUICK PLAY game")
    ap.add_argument("--mix-create", type=float, default=0.35, help="weight: CREATE GAME (public with a code)")
    ap.add_argument("--mix-private", type=float, default=0.15, help="weight: private game (code only)")
    ap.add_argument("--code-joins", type=float, default=0.2, help="share of joiners using JOIN WITH CODE")
    ap.add_argument("--browse-joins", type=float, default=0.1, help="share of joiners using BROWSE (gjoi IDENT)")
    ap.add_argument("--wrong-codes", type=float, default=0.05, help="share of code joins with a code no game has")
    ap.add_argument("--lobby-secs", type=rng_pair, default=(20, 60), help="min,max a host waits before starting")
    ap.add_argument("--races-per-hour", type=float, default=0,
                    help="if set: lobby-secs = 3600/N - race time (per group), overrides --lobby-secs")
    ap.add_argument("--race-secs", type=rng_pair, default=(300, 420),
                    help="min,max seconds away for a race (a real 3-lap race and its results: 340-410 s)")
    ap.add_argument("--rehost-secs", type=rng_pair, default=(5, 20), help="sign-in to re-host after a race")
    ap.add_argument("--stay-secs", type=float, default=40, help="a guest looks for its host's new game this long")
    ap.add_argument("--leave-after-race", type=float, default=0.25, help="chance a player leaves the group after a race")
    ap.add_argument("--host-give-up", type=float, default=150, help="a host alone closes its game after this")
    ap.add_argument("--guest-give-up", type=float, default=240, help="a guest leaves a lobby that never starts")
    ap.add_argument("--host-quits", type=float, default=0.01, help="chance per second a host leaves its lobby (x10)")
    ap.add_argument("--guest-quits", type=float, default=0.01, help="chance per second a guest leaves (x10)")
    ap.add_argument("--kicks", type=float, default=0.05, help="chance per second a host kicks (once per lobby)")
    ap.add_argument("--churn", type=float, default=0.1, help="chance a player signs out after a round")
    ap.add_argument("--offline-secs", type=rng_pair, default=(10, 60))
    ap.add_argument("--bad-per-hour", type=float, default=0.5, help="bad-input connections per player per hour")
    ap.add_argument("--timeout", type=float, default=10, help="seconds for an answer")
    ap.add_argument("--report-every", type=float, default=30)
    ap.add_argument("--json-out", help="write the summary as JSON here")
    ap.add_argument("--server-pid", type=int, default=0, help="watch this lobby process (default: the one on --port)")
    ap.add_argument("--racewait", action="store_true",
                    help="BO-365 servers: wait for a race in progress (TKRACEID / TKWAITFOR / TKWAITSTATE)")
    ap.add_argument("--no-require-isolation", dest="require_isolation", action="store_false",
                    help="run even if the lobby is not in --crowd-test mode (crowd-only runs)")
    ap.add_argument("--as-real", action="store_true",
                    help="do not send TKCROWD (the fake players look real; only with no real copy on the lobby)")
    ap.add_argument("--allow-public", action="store_true", help="allow a public address that is not the live lobby")
    ap.add_argument("--i-know-live", action="store_true", help=argparse.SUPPRESS)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    if len(a.prefix) > 10 or not a.prefix.isalnum():
        sys.exit("crowd: --prefix: up to 10 letters or digits (names are prefix + 4 digits, 15 at most)")
    if a.as_real:
        a.require_isolation = False
    if a.races_per_hour > 0:
        cyc = 3600.0 / a.races_per_hour
        mid = (a.race_secs[0] + a.race_secs[1]) / 2
        lo = max(10.0, cyc - mid - 30)
        a.lobby_secs = (lo, lo + 20)
    target_guard(a.host, a.i_know_live, a.allow_public)
    try:
        import resource
        soft, hard = resource.getrlimit(resource.RLIMIT_NOFILE)
        want = min(hard, max(soft, 4 * a.players + 256))
        if want > soft:
            resource.setrlimit(resource.RLIMIT_NOFILE, (want, hard))
    except (ImportError, ValueError, OSError):
        pass
    crowd = Crowd(a)
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    import signal
    for sig in (signal.SIGINT, signal.SIGTERM):        # end early: the summary still comes
        try:
            loop.add_signal_handler(sig, lambda: setattr(crowd, "stopping", True))
        except (NotImplementedError, RuntimeError):
            pass
    try:
        s = loop.run_until_complete(crowd.run())
    except KeyboardInterrupt:
        crowd.stopping = True
        s = crowd.summary()
    print_summary(s)
    if a.json_out:
        with open(a.json_out, "w") as f:
            json.dump(s, f, indent=1)
    bad = s["anomalies"] or s.get("server", {}).get("gone") or "lobby not in --crowd-test mode" in s["errors"]
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
