#!/usr/bin/env bash
# td3_host.sh <rundir> [KEY=VALUE...]
#
# The other copy of the Takedawn Online step-3 scenes: LAN, port offset 30860,
# with a title address of its own (HOST_ID; the second copy has CLI_ID): the
# title tells its peers apart by address, and two copies with one address never
# link, so a race could not start (the toolkit's TAKEDAWN_NET_ID, for two
# copies on one machine).
# It is a player too: QUICK PLAY (nobody online, so it creates a game), the car
# select, the lobby. It writes <rundir>/hostready when it sits in its lobby,
# presses what it finds in <rundir>/cmds.txt (one press per line, scene_lib
# syntax, e.g. start:400 to start the race) and stays until <rundir>/stop
# appears. Not under test; only the second copy is.
#
#   HOST_CREATE=1  CREATE GAME on the online page (straight to the title's mode
#                page), then A for the mode page and the car
#   HOST_NAV=1   the debug navigator (TAKEDAWN_ONLINENAV=quick) plays this copy,
#                menus, car, the start of the game (START 30 s after the second
#                player is ready) and the results; it is what starts a race
set -u
source "$(dirname "$0")/scene_lib.sh"
source "$(dirname "$0")/td3_lib.sh"
RP="$(cd "$(dirname "$0")/../.." && pwd)"
HOME_DIR="${HOME_DIR:-$(mktemp -d)}"
rd="$1"; shift
sv="$RP/build/td3/saves-$(basename "$rd")"
cp -R "$RP/build/td3/saves-ref" "$sv"
nav=()
[ "${HOST_NAV:-0}" = 1 ] && nav=(TAKEDAWN_ONLINENAV=quick)
SAVES=none scene_start "$rd" HOME="$HOME_DIR" RECOMP_SETTINGS=none TAKEDAWN_QUIT_MENU=1 \
    RECOMP_SAVE_DIR="$sv" \
    TAKEDAWN_ONLINE=lan TAKEDAWN_PORT_OFFSET=30860 TAKEDAWN_NET_ID="${HOST_ID:-10.77.0.1}" TAKEDAWN_NET_FORCE_PUBLIC=1 TAKEDAWN_GAMERTAG=HostA ${nav[@]+"${nav[@]}"} "$@"
if [ "${HOST_CREATE:-0}" = 1 ]; then
    to_online_page
    press down:200 20
    press a:100 0
    wait_game 'name page skipped' 120
    wait_flips 120
    press a:300 0                                   # Continue: create the game
    wait_game '\[NET\] game created' 60
    wait_flips 240
    press a:400 0                                   # this car
    wait_game '\[NET\] lobby line' 60
elif [ "${HOST_NAV:-0}" = 1 ]; then
    to_main_menu
    wait_game '\[NET\] lobby line' 200
else
    to_online_page
    press a:100 0
    wait_game 'QUICKPLAY\] flow state [0-9A-F]+ -> 590' 150
    wait_flips 60
    press a:400 0                                   # this car
    wait_game '\[NET\] lobby line' 60
fi
wait_flips 120
: > "$D/hostready"
note "host is in its lobby"
: > "$D/cmds.txt"; n=0
while [ ! -f "$D/stop" ]; do
    alive
    m=$(wc -l < "$D/cmds.txt")
    while [ "$n" -lt "$m" ]; do n=$((n + 1)); press "$(sed -n "${n}p" "$D/cmds.txt")" 0; done
    sleep 0.5
done
STOP_AT_END=1 scene_end "host done"
