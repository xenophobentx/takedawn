# Online play

Burnout 3's online mode works in Takedawn again. EA's servers closed long
ago, so a lobby server stands in for them. It only introduces the players:
the races run directly between the players' own copies of the game.

Online play is new. If something goes wrong, a bug report (System > Create
bug report) carries the online session and game ids, so I can find the same
moment in the server's log.

## Turning it on

Settings > System > Online play:

- Internet (default): the online pages use the lobby server in the
  Server row below it. By default that is the community server the project runs
  (`vps4954.rackhostvps.com`, TCP 21810); nothing needs to be set. To play
  on another server, such as one you run yourself (below), press A on the
  row and type its name or address, optionally with `:port` (`online.server`
  in `settings.toml`). Y in the editor puts the default back. A name is
  looked up when you first go online, not when the game starts.
- Off: the game behaves like a console with no network cable. A saved
  Off stays Off.
- Local network: no internet server, for a LAN party. The first copy
  started on the network runs a lobby itself; the others find it on their
  own.

Internet is on from the first start, so there is nothing to turn on. The
game binds no port and asks the router for nothing until you open an online
page (the main menu line only looks up the server and asks it for the player
count, an outgoing connection like any web page); a change here
applies after a restart. There is no account to create and no password.

Your name online is the Online name row (`online.name` in
`settings.toml`). Takedawn picks a random one (Racer1234) the first time;
press A on the row to type your own (letters and digits, up to 15). Your
computer's account name is never used.

The Game code row makes your games private: a game you create while it
holds a code is seen only by players with the same code in that row. Empty
(the default) means public games.

## Playing

Pick TAKEDAWN ONLINE in the main menu, then your name. The main menu line
shows how many players are online and how many games are waiting. The online
page has four lines:

- QUICK PLAY puts you in the fullest open game, or makes one. A game
  made this way starts by itself: a 30-second countdown runs once two players
  are in, and the race starts when every guest has a car. Before the race
  everyone votes for the track: three tracks and Random, for 15 seconds or
  until everyone has voted.
- CREATE GAME makes a game with a five-letter code, shown in the lobby's
  top line (CODE 7K2MX). Send the code to your friends. The game is open to
  anyone else too. The host starts the race with Start.
- JOIN WITH CODE turns the page into a code entry. Type the code and
  press A on it to join that game. With no open game of that code the entry
  comes back with the code still typed and says so.
- BROWSE GAMES lists the games. The open ones come first, then the ones
  racing, with the race's time (NAME - RACING 1:20), then the full ones
  (NAME - FULL). A on an open game joins it. A on a racing one waits for its
  race to end and then joins the game. A full one says it is full. With no
  game at all you are back on the online page, which says NO GAMES RIGHT NOW.
  B goes back.

In a game lobby the top line says what the game is waiting for: WAITING FOR
PLAYERS 2/6, the players still picking a car, HOST: PRESS START WHEN READY or
STARTING IN 20 S. A player who is removed is told why, for example REMOVED:
COULD NOT CONNECT TO THE HOST.

### Waiting for a race

If every game is already racing, QUICK PLAY waits for
that race to end and puts you in the game when it is made again. The line at
the bottom says RACE IN PROGRESS, with the players and the race's time, then
THE RACE IS OVER - JOINING THE GAME... (or how many are ahead of you). If
the game is full after the race, or nobody makes it again, the line says so
and QUICK PLAY makes a game of its own. JOIN WITH CODE and a racing game in
BROWSE GAMES wait the same way. B leaves.

### The settings menu online

While you are online (on the online pages, in
a lobby or in a race), opening the settings doesn't pause the game. It keeps
running with your input held, so the others keep hearing from your copy. The
menu's header says so. Offline it pauses as before.

## After a race

On the original console an online game ended with the race: after the
results and the points everyone was back in the room, and the host had to
make a new game. In Takedawn the players of a game stay together. After the
results the screen says RETURNING TO THE LOBBY... while the game saves and
signs in again, the host's game is made again under the same name with the
same settings, and the other players join it by themselves. Everyone keeps
the car and colour of the race and is in the same game lobby again. Points
and takedowns go to your profile as usual; the pages that count them up are
not shown.

- Nobody leaves the results alone: Continue To Lobby shows the game's own
  WAITING FOR OTHER PLAYERS until everyone has pressed it (its CANCEL takes
  you back to the results).
- After that every copy saves and signs in by itself. A guest that is back
  first waits for the host's game as long as the host is still on its way:
  the line at the bottom says WAITING FOR THE HOST TO COME BACK with the time
  waited, then THE HOST IS BACK. The guest joins by itself. B leaves, to the
  online page.
- A host that is back first waits for its guests the same way: the lobby's
  line says WAITING FOR 1 MORE FROM THE RACE, and a QUICK PLAY game's
  countdown starts only when they are there (or have left).
- If the host quits on the results, the others are told at once: THE HOST
  LEFT - THE GAME IS OVER, and they go to the online page. A host that never
  comes back (its game closed, the connection lost) is given up about three
  minutes after everyone else is back: THE HOST DID NOT COME BACK.
- If the lobby server cannot be reached after the race (for example a LAN
  game whose first copy quit, since the lobby runs inside that copy), the game's
  own connection message comes up after about 25 seconds, and you go to the
  online page.
- A game with a code keeps its code: the new game has the same one, so your
  friends can still join it with the code.
- A game with a password is not made again: everyone goes to the room as
  on the console.
- If the autosave is off or fails, the game's own question comes up as usual;
  the others wait while it is answered.

If the host leaves, the game ends. It doesn't pass to another player yet. The
online messages (the lobby's top line, the waiting lines) are in English in
every language.

## What goes over the network

- To the lobby server (TCP 21810; the community server unless you set
  another): your online name, your copy's local and public addresses, and
  the games you create or join.
- To the other players in your game: the race itself, directly (UDP).
- Nothing else goes out, and no Microsoft or EA server is contacted.

## Running a lobby server

You do not need one to play: the default server is there for everyone. Run
your own for a private group, a LAN party across networks, or when the
community server is down.

The server is `takedawn-lobby`, a small program with no dependencies
(`tools/lobby/`, MIT), and anyone can run one.

Build and run on Linux:

```
cmake -S tools/lobby -B build/lobby && cmake --build build/lobby
./build/lobby/takedawn-lobby --public 203.0.113.7
```

Or as a container:

```
docker build -t takedawn-lobby tools/lobby
docker run -d --name takedawn-lobby --restart unless-stopped \
    --network host takedawn-lobby --public 203.0.113.7
```

`--public` is the address players use to reach the server: an IPv4 address or
a host name. A name is looked up when the server starts (it refuses to start
if the name does not resolve to an IPv4 address) and again every 10 minutes;
a change is logged and used from then on, and a failed lookup keeps the last
address. Players are always handed the dotted address, since the game reads
it as one. Without `--public` the server takes its machine's address on the
default route, which is right on a machine with a public address and wrong
inside a container or behind a router. `--verbose` logs every request.

### Limits

| Flag | Variable | Default | What it limits |
|---|---|---|---|
| `--max-sessions N` | `TAKEDAWN_LOBBY_MAX_SESSIONS` | 4096 | players connected at once |
| `--max-games N` | `TAKEDAWN_LOBBY_MAX_GAMES` | 2048 | games open at once |
| `--max-players N` | `TAKEDAWN_LOBBY_MAX_PLAYERS` | 8 | players in one game (2 to 8; the game itself asks for 6) |
| `--max-per-ip N` | | 64 | connections from one address (0: any number; local test addresses are not counted). An address at its limit is not served for 10 seconds: its next connections are closed without an answer. Players behind one shared address (CGNAT, a campus) count together |
| `--relay-port N` | | off | the UDP relay for pairs that cannot connect directly (the game's default is 21811). Off unless given. Open that UDP port to the internet |
| `--idle-secs S` | | 90 | a connection that sent nothing for S seconds is dropped (0: never). The game answers the server's ping every 20 seconds, so a live player never goes quiet |

A connection that has not signed in 25 seconds after it opened is dropped too.
Every connection has TCP keep-alive on. A copy of the game that runs a lobby for
its own network gets these defaults as well, and accepts only players on the
local network.

A flag wins over its variable. The server sizes its tables from these numbers
when it starts: about 0.4 KB for a session and 0.6 KB for a game, so the
defaults take about 3 MB, and a full server a few MB more for the buffers
(each session has an input buffer of at most 12 KB and an output queue of at
most 1 MB; a player whose queue is full does not read and is dropped). It
waits on all connections with `poll()`, so the only limits are memory and the
process's open-file limit: each connection is one file. The server raises its
soft limit to what the sessions need when the hard limit allows it, and if it
cannot it lowers `--max-sessions` and says so in the log. Under systemd set
`LimitNOFILE=8192` (see the unit below); with `docker run`, `--ulimit nofile=8192`.

When a limit is reached the game is told so: a new connection gets "server
full" and is closed, and creating or joining a game gets "full". The server
log says which limit it was (one line every ten seconds at most, with the
number of connections turned away). A player that stops reading from its
connection is dropped instead of holding up the others. A game search returns
at most 100 games (the game asks for 500; it does not need more).
`--stats S` logs the number of sessions and games, and today's sign-ins, every S seconds.

### What the server checks

Every request is checked before anything from it is stored or passed on to
the other players' copies of the game, and a request that fails ends the
connection (the title's own parser is not fed what it could not have sent):

- an online name is 1 to 15 printable ASCII characters, no `=`; a game's name
  1 to 32, a password up to 31;
- `PARAMS`, `USERPARAMS`, `CUSTFLAGS` are lists of decimal 32-bit integers
  (the car and track numbers inside are not checked against the game's tables);
  `SYSFLAGS` one such number; the player limits 2 to 8;
- an address is four decimal numbers. The player's own address (`ADDR`, and the
  one inside its XUID) is believed when it is the connection's own, a private or
  carrier-grade-NAT one, or, for a player on this machine, a loopback one;
  otherwise the connection's address is used, so a player cannot send the others'
  race traffic to a third party;
- a room is one the server has (`move` to another is refused); a game is made in the
  room its maker is in, and a player does not change room while in a game;
- a request must be finished: whole requests are what keep a session alive (a request
  trickled in a byte at a time does not), and an unfinished one is dropped after 15 s;
- `gset KICK=<name>` from the host removes that player from its game (the player is
  told like a guest whose host left; the others get the shorter list). The title
  sends it, after a private `mesg` (answered with an empty success, nothing delivered),
  for a player whose peer link does not come up, and repeats it until the player is
  gone from the member list; a repeat that changes nothing
  (a `mesg`, a kick of a player who is no member) is answered at once and does not use the
  flood bucket (it has its own, 200 a second; more is ignored silently); a server that ignores it is flooded and drops the host;
- after the host starts a game, `gset` changes nothing and sends no member list, and a
  second `gsta` does nothing; `gdel` tells every player, the host too;
- before sign-in only the sign-in requests are answered; a request over the rate
  of 20 a second (a burst of 60 is allowed) is ignored, and a connection that
  keeps it up is dropped;
- only the host changes a game's settings; a guest changes its own car and
  colour. "Started" (flag bit 19) is only ever set by the server, when the host
  starts the game. A game that has started cannot be joined;
- a search or a room's game list shows no player's address (`0.0.0.0`); the
  players of a game get the addresses of the others and a per-game random secret
  (`TKSECRET`, 32 hex digits, from the system's random number generator), which
  they use to sign their peer-to-peer hello packets. The secret is never in a
  search, a room list, the logs or the statistics.

### Game codes, game order and the status counts

Takedawn adds a few keys of its own to the lobby protocol. The game's own
parser skips keys it does not know, and so does the server, so an old game
against a new server, or a new game against an old server, just does not use
them.

Private games: `TKCODE=`. A game code is 3 to 15 letters and digits, not
case sensitive (the server upper-cases it). The Takedawn overlay sends it with
three requests:

- `gcre` with `TKCODE=KZ7MQ` creates a private game. The code is stored as the
  game's password. The game is announced to nobody but its players (not in the
  room's `+gam`/`+agm` pushes), is never in a search without the code, and is
  not named on the public page: the page says only how many private games
  there are.
- `gcre` with `TKCODE=KZ7MQ` and `TKPUBLIC=1` creates a public game that also
  has the code. It is announced, listed and joined like any public game, and a
  search or join with the code finds it too (a join with the code needs no
  `PASS`). Its name is logged, its code is not.
- `gsea` with `TKCODE=KZ7MQ` lists only the open games made with that code,
  private or public (a search without a code never lists a private game). Quick Match, which
  searches and then joins the first game by name, therefore lands in the
  friend's game.
- `gjoi` with `TKCODE=KZ7MQ` joins it, by `IDENT` or by `NAME`. Without the
  code, or with a wrong one, the answer is "gone", the same as for no game at
  all.

A session also gives its code with `move` (when it enters a room) and `sele`;
the server remembers the last code a session sent (an empty `TKCODE=` clears it).
A private game is then pushed (`+gam`, `+agm`, and the "gone" record when it
closes) to the sessions in its room that gave the same code, and only to them,
without addresses or secret. A player who gives the code after the game was
created is sent the open private games with that code as soon as it enters the
room, since the title's JOIN GAME screen shows pushes and does not search again.

A private game is joined only with `TKCODE` (a guess in `PASS` is not the code), and
`TKCODE` does not touch the password of a public game. Two things are limited: a
strike is a join that asks for a private game without its code, or a code that
no private game has (in a search, a join or a `move`); a code that a game has is no
strike even when that game is full or under way. More than 20 strikes a minute end
the session and the address is not served for a minute. And a session looks a code
up (a `gsea` or a `move` with a code) at most once a second; a faster search gets an
empty answer. A push of private games on `move` holds at most 100 games.
Codes are never logged (`--verbose` shows
`TKCODE=***`), and a private game is logged as "created a private game" without
its name. A code is a shared secret among friends: whoever has it joins.

In the game's menus. CREATE GAME makes a new five-character code from
`src/game/codeentry.c` (A-Z and 2-9 without I and O) and goes straight to the title's
create state. Its game is public (`TKPUBLIC=1` with the code), so QUICK PLAY and
BROWSE GAMES on other copies find it and a friend can still JOIN WITH CODE; a code
from the System page's Game code row still makes a private game. The room is entered behind its mode page, because the title takes a
`gcre` answer only from inside a room. B on that page goes back to the online page.
The lobby's top line shows the code in front of the status (`CODE 7K2MX - WAITING FOR
THE HOST`). A game's code ends on the room page and the online page. JOIN WITH
CODE turns the online page's own list into a code entry and then runs the title's
Quick Match with that code. With no open game of that code the page comes back
with the code still typed and `NO OPEN GAME WITH CODE ...`. QUICK PLAY and BROWSE
GAMES go back to the System page's Game code row (usually none). The main menu's
TAKEDAWN ONLINE line and the online page's line show `TKONLINE` and `TKWAITING`
from a background `@dir` probe (`TKPROBE=1`, no sign-in) every 30 seconds and when a
page comes up. A probe is never a signed-in session, so it does not count itself. LAN
shows `LOCAL NETWORK`, and with no answer from the server the line is just
`RACE ONLINE`.

Players in a race. Every player closes the lobby connection at the race start,
so a racer is no session. A session that ends within `RACE_HOLD_SECS` of its game's
`gsta` stays in `TKONLINE` until that time from the `gsta` is over, or until the same
name signs in again, from any address, so nobody counts twice. The time is 720 s: the
title goes up to 5 laps, a lap measured 102 to 120 s, and the way from the finish to
the sign-in 36 to 51 s, which makes 651 s, and 10% on top. A Crash event ends long
before that and its players sign in again; only a crash or a quit in a race lasts
the whole time. A start with one player keeps nobody, and one address keeps at most
`--max-per-ip` racers. The public page's `players` and the stats files count the same
way, and the log's stats line shows both (`0 signed in, 2 in a race`).

Entering a room. The title's JOIN GAME screen shows the room's list and the
pushes that arrive; it does not search again. So on a `move` into a room (not into
the room the player is already in, and at most once every 5 seconds) the server
pushes the open public games that are already there as `+gam` and `+agm`, fullest
first, then oldest, at most 100. Games with a password, under the host's start
lock, under way or full are left out. The records have no addresses and no
secret. Private games are never in this list; a player who gave their code gets
those with the same code (see above).

Order of games. A search without a code lists the open public games fullest
first, then oldest first, so players who press Quick Match one after another end
up in the same game. The public page lists waiting games in the same order.

Counts for the overlay: `TKONLINE=`, `TKWAITING=`. Every room record (`+rom`,
sent after `sele`, after `move` and whenever the counts change, at most every
30 seconds) carries two more lines: `TKONLINE=<players signed in or racing>` and
`TKWAITING=<public games a stranger could join now>` (not full, not started,
not private). The overlay reads them from the lobby stream; the game ignores
them.

Waiting for a race: `TKWAITFOR`, `TKRACING`, `TKWAITSTATE` (BO-365). A started
game is gone from the server seconds after its `gsta` (everyone closes the lobby
connection), so the server keeps a race record of it: its host (name and address), the
players who started, its code, its kick list. Only a game started with two or more
players and no password (a code is fine) gets one. When the host signs in again and
makes a game (the stay-together re-host after the race), that game is linked to the race
as its successor (`game N is game M again` in the log) and takes the kick list over. A
race is over when its host is back and has made no game for 60 s, when the others are
back and the host is not for 60 s, when its host joins another game, or after
`RACE_HOLD_SECS`; then the log says `race of game M ... over: nobody made it again (why)`.
When the successor itself starts, the waiters still waiting follow to the new race.

- A `gsea` that lists nothing and has no `TKWAITFOR` carries the best race to wait for:
  `TKRACING=<races>`, `TKRACEID`, `TKRACENAME`, `TKRACEPLAYERS`, `TKRACEMAX`,
  `TKRACESECS` (seconds since the start). Most players first, then the oldest. Only
  races with room, not kicked from, and under the code rules: with a code only that
  code's races, without one only public ones.
- `gsea` with `TKWAITFOR=<race ident>` (1 to 2^31-1, anything else ends the connection)
  is a waiter's poll. The answer has `TKWAITSTATE=racing|lobby|open|full|gone`,
  `TKWAITPLAYERS`, `TKWAITSECS`, `TKWAITQUEUE` (waiters before this one, in arrival
  order) and `TKWAITNAME` once the successor exists, and the usual list of open games,
  with the successor first when this waiter has a seat in it and left out while it has
  none. `open`: the successor is in this answer; `lobby`: it exists, but the seats are
  taken by the racers coming back (theirs are kept for 25 s after it is made) or by
  waiters before this one; `full`: no seat after that; `gone`: over, unknown (a server
  restart) or a private race asked for without its code (never a strike).
- A `gjoi` into a successor by someone who did not race in it gets `full` while there
  is no free seat for its place in the queue: that is how the racers keep their seats.
  The client reads it as "keep waiting".
- Any search without `TKWAITFOR`, a `gjoi`, `gcre`, `glea` or `gdel` ends a wait. A code
  whose game is racing exists for the strike count, so waiting with a code is safe.
- `+rom` carries `TKRACING=<races a stranger could wait for>` next to `TKONLINE` and
  `TKWAITING`. `stats.json`'s `games_racing` counts the public race records.
- Old clients skip all of these keys; their answers are what they were.

### Player identity: `TKID`, `TKADDR`, `TKRADDRn`, `TKRLA`

Players in different homes can have the same private address (192.168.1.100). A client that sends
`TKID=1` in its `addr` request gets a synthetic title address from 10.64.0.0/16,
unique among the sessions on the server (the `addr` answer carries it as `TKADDR=`). The pool is walked round robin from a random place chosen when the server starts, so a restart never hands out the same first addresses again.
Other capable players see that address as the player's address: in `LADDRn` of the
game record, in `LA` and in the address inside the XUID of `+usr`/`+who` (the name
hash, the port offset and the user index stay). The real local address is in
`TKRADDRn` (game record) and `TKRLA` (`+usr`/`+who`); the public address is `ADDRn`/`A`
as before. A player's own entry is as it sent it. Legacy clients (no `TKID`) see real
addresses, except for a capable member whose real address another member shares, who
is shown by the synthetic address. A legacy player cannot join a game that holds a
member with the same real address, the same XUID, or the same address as a member's
synthetic one (answer `full`). A leased address: when a capable session ends, its synthetic address is kept for 30
minutes for the same player coming back from the same public address (the gamertag
comes from `TKNAME=` in `addr`; a client that does not send it is matched by the
local address it claims). The same address is then handed out again, so the other
players' routes to it stay right across a re-sign-in ("address reused for <name>"
in the log, never an address). Nobody else gets a leased address; if the pool is
full the oldest leases go first. At most as many leases as sessions. A player's address, XUID and name are frozen while the
player is in a game. The full description for client authors is
`.handoff/online-identity-spec.md`.

### Correlation ids: `TKSID`, `TKGID` (BO-340)

So that a player's bug report finds the server's lines, the server hands out two random ids.
They are not made from an address or a name, and they are not in the statistics or on the
public page.

- `TKSID=<8 hex digits>` in the `addr` answer (every client gets it; one that does not know
  the key skips it). It is the session's id: from the system's random number generator,
  made when the connection is accepted, unique among the live sessions, and new for every
  connection. Every log line about a session starts with `sid=<hex>` after the time:
  `20:47:58 sid=d8fa84fd IdA created game 1 gid=aec4ed3eaf2b 'IdGame'`.
- `TKGID=<12 hex digits>` in the records only a game's own players get (`+mgm` and the
  `gcre`, `gjoi`, `gset`, `gsta` answers, `+ses`), never in `+gam`/`+agm`, a search or the
  statistics. It is a game's id, random and different after a restart (the `IDENT` counts
  up from 1 again). Log lines about a game carry `gid=<hex>` next to the `IDENT`.

The client logs both (the `[ONLINE]` line) and puts the session id and the last game id into
the bug report. Find what the server saw with
`journalctl -u takedawn-lobby | grep sid=d8fa84fd`.

How long the lines last: the lobby logs to the systemd journal and nothing else keeps them.
With the defaults the journal is limited by size, not by age: it may use up to 10% of its
file system, at most 4 GiB (`SystemMaxUse=`), and the oldest entries go first. If
`/var/log/journal` does not exist (`Storage=auto`) it is kept only in `/run` and is gone
at a reboot. `MaxRetentionSec=` is off. So how many days it covers depends on how busy the
server is and how big the disk is; on a quiet lobby it is months, on a busy one days. Check
with `journalctl --disk-usage`, `journalctl --list-boots` and
`journalctl -u takedawn-lobby | head -1` (the oldest line kept). Ask players to send the
report soon after a problem; to keep more, create `/var/log/journal` and set
`MaxRetentionSec=` / `SystemMaxUse=` in `/etc/systemd/journald.conf`.

### Statistics

The server counts how it is used, so the people who run it can see whether it
is worth keeping up. It stores and logs no address and no player name in the
statistics. They are counts:

- connected sessions now, and the day's peak (connections, and players who
  signed in);
- sign-ins, and unique players, per day;
- games created per day by mode (Single Race, Road Rage, Team Crush, Double
  Impact, Party Crash), games started per day (also by mode), and how many
  players were in each game when it started (1 to 8);
- race results reported (the game's `rank` report, one per player and race);
- connections turned away because the server was full;
- race waits (a player waiting for a race under way): begun, joined the next
  game, the race gone, the race started again full;
- sign-ins and unique players per country.

The country is counted in aggregate from the connection's IP address, looked up
in an offline database on the server; the IP address is not stored.

Unique players are counted with a hash of the persona name under a random
salt made each UTC midnight and kept in memory only; the hashes are never
written, and both are dropped at the next midnight. The count is approximate:
one player under two names counts twice, and after a restart on the same
day a player seen before and after counts twice.

Nothing is written unless the server is given a directory:

```
takedawn-lobby --public 203.0.113.7 --stats-dir /var/lib/takedawn-lobby
```

(or `TAKEDAWN_LOBBY_STATS_DIR`). It holds two files:

- `daily.jsonl`: one JSON line per UTC day, appended when the day ends (or at
  the first start after a day the server was down for), never rewritten;
- `current.json`: today so far, rewritten every minute and when the server
  stops, with `sessions_now` and `players_now` added. A restart on the same
  day carries on from it.

A day looks like this (shortened):

```
{"date":"2026-10-31","connections":412,"refused_full":0,"signins":131,"unique_players":47,
 "peak_sessions":9,"peak_players":6,"games_created":38,"created_single_race":30,
 "created_road_rage":3,"created_team_crush":1,"created_double_impact":1,
 "created_party_crash":2,"created_other":1,"games_started":25,"started_single_race":21,...,
 "race_results":44,"start_players_1":1,"start_players_2":19,"start_players_3":3,...}
```

The country needs a [DB-IP Lite](https://db-ip.com/db/download/ip-to-country-lite)
country CSV (CC BY 4.0, a new one every month), given with `--geoip FILE` (or
`TAKEDAWN_LOBBY_GEOIP`). Only its IPv4 lines are used and nothing is looked up
over the network. Without the file every country is `??`. A day's line then
ends with `"countries":{"DE":[3,7],"HU":[5,12],"??":[1,1]}`: unique players and
sign-ins per country code.

Read them over ssh, for example
`jq -c '[.date,.unique_players,.games_started]' /var/lib/takedawn-lobby/daily.jsonl`.
The server has no way to ask for them over the network.

### Public page

The server can write a small public page for a web server to serve: a list of
the games people can join, how many players are online, today's totals, and the
last 30 days. The lobby itself serves no HTTP. It writes two files into a folder:

- `stats.json`, every 20 seconds: players and games now (`games_private` counts
  private games; their names and codes are never in the file), today's totals and
  games by mode, the last 30 days (from `daily.jsonl`), and up to 50 open games
  (waiting ones first). For each game: its name (what the host typed in the
  game; the game suggests the host's online name, so a name there may be a
  player's pseudonym; at most 32 printable characters, with `< > & ' " \` and
  non-ASCII still written as `\u` escapes), the
  mode, players and the player limit, `waiting` or `racing`, and whether it has a
  password. No player is listed, and there is no address, no password and no secret.
- `index.html`, written at start: one file of plain HTML, CSS and a little
  script (about 15 KB, nothing loaded from elsewhere), in Takedawn's colours. It
  fetches `stats.json` (a relative address, so it works under any path) every 20
  seconds and shows the game names as text. The source is
  `tools/lobby/stats_page.html`; `tools/lobby/embed_page.py` rebuilds
  `stats_page.h`, the copy inside the program.

### Owner's page

`--admin-token-file FILE` (or `TAKEDAWN_LOBBY_ADMIN_TOKEN_FILE`) names a file
holding one line of 20 to 64 letters, digits, `-` and `_`. The server then also
writes a folder with that name inside the public folder: `index.html` (charts
of every day since the start, by day, week or month, and every country),
`history.jsonl` (a copy of `daily.jsonl`) and `now.json` (today with every
country, and the live counts, every 20 seconds). The folder's name is the
secret: the server makes the public folder `0711` so it cannot be listed, the
web server must not list folders either, and the name is never logged. With the relay on, `now.json` and the page also show its counters: packets and bytes carried, and games relayed. The
page loads nothing from elsewhere and asks search engines not to index it.
Its source is `tools/lobby/admin_page.html` (`admin_page.h` inside the program).

The folder is `--public-dir DIR` (or `TAKEDAWN_LOBBY_PUBLIC_DIR`), by default
`<stats-dir>/public`, so with `--stats-dir` the page is on. It is created with
mode 0755 and the files 0644, so a web server running as another user can read it.

Under systemd use `--public-dir /run/takedawn-lobby` with
`RuntimeDirectory=takedawn-lobby` and `RuntimeDirectoryMode=0755`. With
`DynamicUser=yes`, systemd keeps `StateDirectory=` under `/var/lib/private`, a
directory only root may enter, so a web server could not reach
`/var/lib/takedawn-lobby/public` even though the folder itself is world-readable.
A runtime directory is a normal folder in `/run`. Nothing is lost when it goes
away: the server writes the page again when it starts, and the 30-day history
comes from `daily.jsonl` in the state directory.

A Caddy site that serves it at `https://example.org/takedawn/` (the web server only
reads the folder; `handle_path` strips the prefix, and the redirect makes the
page's relative `stats.json` resolve under `/takedawn/`):

```
example.org {
    redir /takedawn /takedawn/ permanent
    handle_path /takedawn/* {
        root * /run/takedawn-lobby
        @stats path /stats.json
        header @stats Cache-Control "no-cache, must-revalidate"
        @static not path /stats.json
        header @static Cache-Control "public, max-age=3600"
        file_server
    }
}
```

### Under systemd

A unit that runs the server as a throwaway user, with a state directory for
the statistics and a file limit that fits the defaults
(`tools/lobby/takedawn-lobby.service`):

```
[Unit]
Description=Takedawn online lobby
After=network-online.target
Wants=network-online.target

[Service]
ExecStart=/usr/local/bin/takedawn-lobby --public lobby.example.org --stats 300 --stats-dir /var/lib/takedawn-lobby --public-dir /run/takedawn-lobby
DynamicUser=yes
StateDirectory=takedawn-lobby
RuntimeDirectory=takedawn-lobby
RuntimeDirectoryMode=0755
LimitNOFILE=8192
MemoryMax=256M
Restart=always
RestartSec=2
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
PrivateDevices=yes
ProtectKernelTunables=yes
ProtectKernelModules=yes
ProtectControlGroups=yes
LockPersonality=yes
MemoryDenyWriteExecute=yes
RestrictNamespaces=yes
RestrictRealtime=yes
RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX
CapabilityBoundingSet=
SystemCallFilter=@system-service

[Install]
WantedBy=multi-user.target
```

`StateDirectory=` makes `/var/lib/takedawn-lobby` writable for the dynamic
user although `ProtectSystem=strict` makes the rest read-only;
`RuntimeDirectory=` does the same for `/run/takedawn-lobby`, the public page
(readable by all, see above). The server log
goes to the journal (`journalctl -u takedawn-lobby`); it names the players
who sign in and leave, but not the address they came from, and any control
character in a name is shown as `?`. `--verbose` adds the addresses and every
request (never a password, an XUID or a game secret), so leave it off on a
public server. A sign-in is counted once per connection in the statistics,
however often a player repeats it.

### Ports

| Port | Protocol | Who opens it | What for |
|---|---|---|---|
| 21810 | TCP | the server | the lobby: sign-in, rooms, games |
| 21809 | UDP | nothing (LAN only) | the local-network lobby's announcement, a broadcast that never leaves the network |
| 21811 | UDP | the server (only with `--relay-port`) | the relay for pairs that cannot connect directly |
| 31000 | UDP | the player who hosts a game | the race between the copies (the game's port 1000; see below) |

The server needs TCP 21810 open to the internet, and UDP 21811 as well when it
runs the relay (`--relay-port 21811`). On a home connection, forward those ports
on the router to the machine running the server and allow
it in that machine's firewall.

### Players behind routers

Two copies in a game first try to reach each other directly. Both sides send
a few small "hello" packets. These open a path through most home routers even
without forwarding, and show each copy which address the other one's packets
come from. Two players in the same house (the same public address) use the
local network; players in different houses use their public addresses. The game
accepts hellos and race packets only from players the lobby told it about.

**The relay (0.3.1).** A pair that has not reached each other directly within
2 seconds goes through the game server's relay instead: UDP 21811 on the
server. The direct path stays whenever it works, and it is the one with the
lowest ping. A relayed link carries the same packets over a detour through the
server, which costs the extra distance to the server and back, typically 10 to
60 ms in Europe. Each pair decides on its own, so one game can have direct and
relayed links side by side. The relay is on only when the server's operator
started it with `--relay-port` (see Limits above and the owner's page below); on a
server without it, a pair that cannot connect directly stays unconnected.

**Ports for the lowest ping.** The player who creates a game should have UDP
31000 and 36000 reachable, and a player who joins should have the same two
open on their router. Then even two players behind strict routers connect
directly. While a copy hosts a game in Internet mode, Takedawn asks the router
to forward those ports with UPnP, ten minutes at a time, renewed while it
hosts, and removes the forwarding when it joins someone else's game or quits
(`TAKEDAWN_UPNP=0` stops it asking). Without UPnP, forward UDP 31000 and 36000
on the router to the computer running the game. A player who has not opened
anything still joins: the relay carries the link, at the cost above.

**When a link does not come up.** If a player cannot be reached for 15
seconds, the title's own timer takes over: the host removes the player whose
link failed, and that player gets a box saying their connection did not come
up. The same happens when two guests cannot reach each other: the title picks one
of them, and the box names the reason as a failed connection, not a host's
choice. It lists the ports to open and the relay address to check. The 15 seconds are
the title's own, not something Takedawn added.

The game's own UDP ports below 10000 (1000, 3074, 6000) are moved up by
30000 on a computer (`TAKEDAWN_PORT_OFFSET`), since ordinary programs may not
use the low ones there; every Takedawn copy does the same, so they still find
each other.

Platform notes: on macOS 15 and later the system asks for the
Local Network permission the first time (LAN mode and UPnP need it). On
Android, Wi-Fi drops broadcast and multicast packets unless an app holds a
multicast lock, so LAN mode's lobby search and UPnP may not work there yet;
Internet mode through a server does.

## For developers

- Client: the toolkit's `src/kernel/xnet_hle.c` (the title's XNet sockets as
  host sockets, a cable, an address) and the port's `src/game/online.c` (the
  title's XNet and XOnline entry points, a signed-in user, LAN mode).
- Server: `tools/lobby/aries_server.c` (the protocol, as the game speaks it);
  `tools/lobby/aries_logger.py` is the notebook it was learnt with: it logs
  every request and answers an unknown one with an empty success.
- `tools/lobby/stats_page.html` is the public page; `embed_page.py` puts it
  (with the icon) into `stats_page.h`, which `main.c` includes. Run it after
  changing the page (`--html OUT` also writes the finished page for a browser).
- `tools/lobby/fake_client.py` is a scripted second player for tests.
- After a race (`src/game/stay.c`): the title disconnects from the lobby at
  the race start and signs in again after it. The title's message pages on
  the way still run, and while they do, the string slots they read hold one
  text. The room state then runs the points' closing step, and the host's
  runs the room's own CREATE GAME with the last game's request, a guest's
  the Quick Match limited to that game's name. The car page's result goes
  through the car state's own exit. `[STAY]` lines in the log;
  `tools/headless/td4_stay.sh` with `td4_host.sh` plays races in a row on
  two copies (modes `stay`, `hostquit`, `guestquit`, `guestyes`,
  `hostleave`).
- Online lines in every log (BO-340): a handful per session, tagged `[NET]`,
  written even with Diagnostic logging off, so a bug report's log has them:
  the lobby session (`lobby session sid=...`), the sign-in and why a sign-in or
  a game was refused, a game made or joined or over (`gid=...`), each peer
  linked or not, every message shown to the player, and the lobby closing the
  connection. No address is in them. The session id (`sid`) and the last game
  id (`gid`) are also in the report's `system.txt`. On the server,
  `journalctl -u takedawn-lobby | grep sid=<id>` finds the same session
  (`docs/online.md`, "Correlation ids"). With `TAKEDAWN_NET_TRACE=1` the
  `[ONLINE]` state line carries `sid=` and `gid=` too.
- `TAKEDAWN_NET_TRACE=1` logs every network call of the game, including the
  address chosen for each player and why; `TAKEDAWN_LOCAL_IP` pins a copy to
  one local address.
- Tests on one network: `TAKEDAWN_NET_FORCE_PUBLIC=1` sends to the other
  players' public addresses even when they are ours (through the router's NAT
  loopback); `TAKEDAWN_NET_ID` gives a copy its own player address without
  binding to it, so several copies on one machine (each with its own
  `TAKEDAWN_PORT_OFFSET`) are separate players; `TAKEDAWN_UPNP_LEASE` and
  `TAKEDAWN_UPNP_DESC` set the forwarding's lease and name;
  `tools/online/upnp_ctl` lists and removes test forwardings by name.
- `TAKEDAWN_NET_OPEN=1` hands the game datagrams from anyone, with the sender check off.
- `TAKEDAWN_NET_SIM=delay=150,jitter=50,loss=2,seed=7` (a test switch, off when
  unset and absent from release builds, like `TAKEDAWN_NET_WATCH`) makes the network to every other player bad, on this copy only. `delay` adds
  to the round trip in ms, half on each datagram out and half on each one in, so
  on two copies the two add up. `jitter` moves each direction by up to half
  its value either way, so the round trip moves by up to +/-`jitter`, and
  datagrams can swap order. `loss` drops that percent, in each direction on its
  own. It covers the players' UDP. The lobby's TCP connection does not go through
  it, and neither do hellos on their way out.
- `TAKEDAWN_NET_WATCH=1` logs one `[NET] watch peer` line a second per player:
  the title's own ping, its offset to that player, the round trip and loss
  measured below, and how the other car moved. `tools/online/bot/lat_pair.sh`
  plays two copies through rounds with these switches and
  `tools/online/bot/lat_sum.py` sums a run up.
- The copies ping each other once a second on the title's sockets: `TKPG`, 32
  bytes signed with the game's secret like the hellos, kept away from the title.
  A copy says in its hello that it answers, and a copy that did not say so gets no
  pings, so older copies never see one. The title's XNetQos block carries the
  result (`src/game/peerqos.h`); before the first answer it holds fixed values.
  The lobby's connection bars are the title's own and come from its own ping
  message, not from XNetQos.
- When the lobby sends a game secret (`TKSECRET=`, 32 hex digits, in the
  records only the game's players get), the hellos carry a SipHash tag under
  it and a hello without the right tag is refused.
- `tools/lobby/test_fuzz.c` throws random and hostile requests and raw bytes at
  the server's request handler and checks its books after every step; build it
  with the sanitizers (`gcc -O1 -g -fsanitize=address,undefined -o test_fuzz
  test_fuzz.c -lpthread && ./test_fuzz 400000 1`).
- `tools/lobby/test_lobby.c` tests the statistics (with a fake clock: the daily
  rollover, a restart, a day the server was down) and the session limit over
  real sockets: `gcc -O2 -o test_lobby test_lobby.c -lpthread && ./test_lobby`
  (or `cmake --build build/lobby --target test_lobby`).
  `tools/lobby/stress_lobby.py` signs in thousands of fake players to a server
  you started on the same machine (never a public one).
- `tools/lobby/crowd/crowd.py` plays hundreds of fake players the way the game
  does (QUICK PLAY, CREATE GAME, codes, races, stay together, kicks, bad input)
  and reports latency, refusals and protocol anomalies. Only against a test
  lobby started with `--crowd-test`, which keeps the fake games and real copies'
  games apart; see `tools/lobby/crowd/README.md`.

### Online soak (before a release)

The soak races two headless copies on one Linux machine back to back, in the
same two processes, with the game's own AI driving both
(`RECOMP_DEBUG=autopilot=1`). Start it on the test machine with
`tools/online/soak_ubuntu.sh BUILD RACES TAG`. It runs as a systemd user
unit, so restarting the machine you started it from does not end it.

The script takes the test machine lock, starts a lobby server with
`--stats 60`, puts the Host and the Guest in one game and runs
`tools/online/soak_remote.sh`. It logs both results of every race, the
memory and open files of both games and of the server, and the packets each
copy sent and received. With `TAKEDAWN_NET_TRACE=1` the copies also log
every car's position, and `tools/online/drawn.py HOST_LOG GUEST_LOG`
compares where each copy draws the other car with that car's own position.
The run stops on a missing result, a dead process, or a `stop` file in its
folder.
