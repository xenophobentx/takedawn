#!/usr/bin/env bash
# td4_host.sh <rundir> [KEY=VALUE...]
#
# The hosting copy of the Takedawn Online step-4 scenes ("stay together after
# a race"): LAN, port offset HOST_OFFSET (default 30870), title address
# HOST_ID (default 10.77.7.1; two copies on one Mac need their own, see
# td3_host.sh). It is a player: QUICK PLAY (nobody online, so it creates the
# game), car select, the lobby. It writes <rundir>/hostready in its lobby,
# then takes commands from <rundir>/cmds.txt, one per line:
#   <press>              a press in scene_lib syntax (start:400, a:300 ...)
#   burst <name> <step> <n>   frames every <step> flips into burst-<name>/
#   quit                 end the game process (a host that crashed or quit)
# and stays until <rundir>/stop appears. CAPTURE=1 is implied. HOST_CODED=1:
# CREATE GAME, a game with a code (<rundir>/code), instead of QUICK PLAY.
# ONLINE_MODE=internet LOBBY_ADDR=127.0.0.1:port: a lobby server of its own, which
# outlives both copies (on a local network the first copy runs it).
set -u
source "$(dirname "$0")/scene_lib.sh"
source "$(dirname "$0")/td3_lib.sh"
source "$(dirname "$0")/td4_lib.sh"
RP="$(cd "$(dirname "$0")/../.." && pwd)"
HOME_DIR="${HOME_DIR:-$RP/build/td4/home-$(basename "$1")}"
mkdir -p "$HOME_DIR"
rd="$1"; shift
sv="$RP/build/td4/saves-$(basename "$rd")"
mkdir -p "$sv"
cp -cRf "$RP/build/td3/saves-ref/" "$sv/"
CAPTURE=1 SAVES=none scene_start "$rd" HOME="$HOME_DIR" RECOMP_SETTINGS=none TAKEDAWN_QUIT_MENU=1 \
    RECOMP_SAVE_DIR="$sv" \
    TAKEDAWN_ONLINE="${ONLINE_MODE:-lan}" TAKEDAWN_LOBBY="${LOBBY_ADDR:-}" TAKEDAWN_PORT_OFFSET="${HOST_OFFSET:-30870}" TAKEDAWN_NET_ID="${HOST_ID:-10.77.7.1}" \
    TAKEDAWN_NET_FORCE_PUBLIC=1 TAKEDAWN_GAMERTAG="${HOST_TAG:-HostS}" "$@"
to_online_page
if [ "${HOST_CODED:-0}" = 1 ]; then             # CREATE GAME: a game with a code
    press down:200 20
    press a:100 0
    wait_game 'name page skipped' 120
    wait_flips 120
    press a:300 0                               # the mode page's Continue
    wait_game '\[NET\] game created' 60
    wait_flips 240
    grep -a 'CREATE GAME chosen' "$D/log.txt" | tail -n 1 | sed 's/.*code \([A-Z0-9]*\).*/\1/' > "$D/code"
    note "the game's code: $(cat "$D/code")"
else
    qp_press
    wait_flips 60
fi
press a:400 0                                   # this car
wait_game '\[NET\] lobby line' 60
wait_flips 120
: > "$D/hostready"
note "host is in its lobby"
: > "$D/cmds.txt"; n=0
while [ ! -f "$D/stop" ]; do
    alive
    m=$(wc -l < "$D/cmds.txt")
    while [ "$n" -lt "$m" ]; do
        n=$((n + 1)); c=$(sed -n "${n}p" "$D/cmds.txt")
        case "$c" in
        burst\ *) set -- $c; burst "$2" "$3" "$4" ;;
        quit) note "host quits"; kill "$(cat "$D/pid")"; collect_bursts; exit 0 ;;
        *) echo "$c" >> "$D/pad.txt"; PADN=$(( PADN + 1 )); note "press $c" ;;
        esac
    done
    collect_bursts
    sleep 0.5
done
collect_bursts
STOP_AT_END=1 scene_end "host done"
