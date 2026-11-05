# crowd: fake players for a test lobby

`crowd.py` plays hundreds of fake players against a Takedawn lobby
(`tools/lobby/aries_server.c`). Each one sends what the real game sends:
sign-in, room list, QUICK PLAY, CREATE GAME (public with a code), private
games, JOIN WITH CODE, BROWSE, car and ready, the host's lock and start,
the lobby connection dropping at the race start, rank and the stay-together
re-host after the race, kicks, leaving. A few throw-away connections send bad
input. Python 3.8+, standard library only; one process handles 500 players.

At the end it prints latency percentiles per request, refusals by request and
answer code, errors, the lobby process's memory, CPU and open files (when it
runs on the same machine), and every **protocol anomaly**: an answer the real
client would not cope with (a game record without HOST, a search that offers a
full or started game, a list record with addresses, an unknown answer code,
an answer out of order, a fake player seeing a real game). The exit status is
1 when there was an anomaly or the lobby died.

## Never the public lobby

Fake games on the public server show up in real players' QUICK PLAY and in the
player counts on the main menu. crowd refuses `vps4954` and 79.172.222.137
(by name and by every address a name resolves to), and any other public
address unless `--allow-public`. It also stops when the lobby does not answer
`TKCROWD=1`, which only a lobby started with `--crowd-test` does.

## Fake and real players on one lobby

With `--crowd-test` the lobby keeps the two kinds apart. A session that sends
`TKCROWD=1` in `addr` is a fake player; the games it makes are fake games.
Searches, the room's game pushes, game updates and joins (by name, by ident,
with a code) only ever reach the same kind. A real copy's QUICK PLAY never
lands in a fake game and a fake player never joins a real copy's lobby, where
it could not answer the peer-to-peer traffic and would be kicked. Without the
flag the lobby ignores `TKCROWD` and behaves as the public server does.

What is still shared: player counts (`TKONLINE`, `TKWAITING`, the stats page),
the server's CPU and memory, the rate limits, the session and game tables. A
real copy in the mixed run therefore sees the fake players in the menu counts,
but not their games.

## Running it

On ubuntu (`ssh ubuntu`), next to a lobby built from the same branch:

    gcc -O2 -o takedawn-lobby tools/lobby/main.c tools/lobby/aries_server.c -lpthread
    tools/lobby/crowd/run_local.sh ./takedawn-lobby 31866 runs/load200 --players 200 --duration 600

`run_local.sh` starts the lobby on 127.0.0.1 with `--crowd-test`, runs the
crowd, stops the lobby, and leaves `lobby.log`, `crowd.out` and `crowd.json`.

Next to a soak (real headless copies on a local lobby): start that lobby with
`--crowd-test` and point the crowd at its port, from the same machine:

    takedawn-lobby --port 31866 --bind 127.0.0.1 --public 127.0.0.1 --crowd-test &
    # the copies: RECOMP_ONLINE_SERVER=127.0.0.1:31866
    python3 tools/lobby/crowd/crowd.py --port 31866 --players 200 --duration 3600

From another machine on the LAN, start the lobby with `--max-per-ip 0` (it
allows 64 connections from one address; loopback is not counted) and give
`--host`.

The crowd itself is light (200 players are a few percent of one core), so it
can run next to headless copies on the same machine. Do not run it during a
performance measurement or while the owner plays.

## Settings

| option | default | what |
|---|---|---|
| `--players` | 50 | fake players |
| `--duration`, `--ramp` | 600, 60 s | run length; players arrive over the ramp |
| `--hosts` | 0.3 | share of choices that host a game |
| `--mix-quick/create/private` | 0.5/0.35/0.15 | what a host makes |
| `--code-joins`, `--browse-joins` | 0.2, 0.1 | joiners with a code / by BROWSE; the rest QUICK PLAY |
| `--wrong-codes` | 0.05 | code joins with a code no game has |
| `--lobby-secs` | 20,60 | a host waits this long for players before it starts |
| `--races-per-hour` | off | sets `--lobby-secs` from a target race rate |
| `--race-secs` | 300,420 | away for the race (real 3-lap race and results); 60,180 for more churn |
| `--leave-after-race` | 0.25 | a player leaves its group after a race (else stay together) |
| `--kicks` | 0.05 | per second, a host kicks one guest (once per lobby) |
| `--churn`, `--offline-secs` | 0.1, 10,60 | signs out after a round, comes back later |
| `--bad-per-hour` | 0.5 | bad-input connections per player per hour |
| `--think-secs` | 2,10 | on the online page before a choice |
| `--json-out` | | the summary as JSON |
| `--server-pid` | the process on `--port` | the lobby process to watch (Linux) |
| `--first`, `--prefix` | 0, Crowd | two crowds on one lobby need different ranges |

The bad input: a request before sign-in, a field that fails validation, a
wrong game code, a broken header, an unknown request, a body over 8 KB, 150
searches at once, half a packet. The lobby should close the connection for the
first, second, fourth and sixth, answer the others, and throttle the flood.

## Race wait (BO-365)

`--racewait` turns on the client side of BO-365: when a QUICK PLAY search
comes back empty with `TKRACEID`, the player polls `gsea TKWAITFOR=<id>` every
3 s, accepts `TKWAITSTATE=racing|lobby|open|full|gone` (anything else is an
anomaly) and joins the first listed game on `open`. Against a lobby without
BO-365 nothing changes. With `--crowd-test` a race keeps the kind of the game
that raced (`race_t.crowd`): a fake race is offered only to fake players, a
real one only to real copies, a successor is linked only to a race of its own
kind, and the room's `TKRACING` counts the real races (test_lobby part
`racecrowd`).

## Notes

- Latencies are measured in the crowd's event loop; the summary prints the
  loop's own lag. If it is high, the numbers say more about the crowd than the
  lobby.
- Fake players use `TKID=1` (synthetic addresses), unique names
  (`Crowd0000`...), and an XUID that carries their own local address.
