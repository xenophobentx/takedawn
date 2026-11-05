#!/usr/bin/env bash
# td3_quick.sh <rundir> <mode> [KEY=VALUE...]
#
# Takedawn Online step 3: QUICK PLAY on the second copy (LAN, offset 30850),
# with shots of every ~6th flip (about 100 ms) from the press of A on QUICK
# PLAY. CAPTURE=1 is implied. The other copy is td3_host.sh.
#
#   found    a game is up (the other copy): the press, then the title's own
#            search, join, car select (A, as a player) and lobby
#   none     nobody online: the title's search, the create branch, the creating
#            page, car select (A) and lobby as the host
#   leave    found, then B in the lobby and Yes to the title's own question
#   create   CREATE GAME (still the navigator): lobby select, room, create pages, for the prompt bars
#   ypress   Y on the online page and on the search page: nothing happens
#   noserver the other copy (HOST_RD, which runs the lobby) is stopped before
#            QUICK PLAY: the title's own answer, and where the player ends up
#   cancel   B on the search screen, then QUICK PLAY again until the lobby
#   race     found, then the host (td3_host.sh, HOST_RD=<its rundir>) starts the race;
#            both copies run RECOMP_DEBUG=autopilot=1 (give it after the mode), the
#            race loads and is driven to its end, A on the results until the
#            lobby is back; then B in the lobby, Yes, back on the online page
#
# The scene's own checks end in PASS/FAIL in <rundir>/scene.txt. The game is
# left running (pid in <rundir>/pid); STOP_AT_END=1 stops it.
set -u
source "$(dirname "$0")/scene_lib.sh"
source "$(dirname "$0")/td3_lib.sh"
RP="$(cd "$(dirname "$0")/../.." && pwd)"
HOME_DIR="${HOME_DIR:-$(mktemp -d)}"
rd="$1"; mode="$2"; shift 2
fail=0
bad() { note "FAIL: $*"; fail=1; }
sv="$RP/build/td3/saves-$(basename "$rd")"
cp -R "$RP/build/td3/saves-ref" "$sv"
CAPTURE=1 SAVES=none scene_start "$rd" HOME="$HOME_DIR" RECOMP_SETTINGS=none TAKEDAWN_QUIT_MENU=1 \
    RECOMP_SAVE_DIR="$sv" \
    TAKEDAWN_ONLINE=lan TAKEDAWN_PORT_OFFSET=30850 TAKEDAWN_NET_ID="${CLI_ID:-10.77.0.2}" TAKEDAWN_NET_FORCE_PUBLIC=1 TAKEDAWN_GAMERTAG=GuestB "$@"
to_online_page
case "$mode" in
found|none|race|leave)
    burst quick 6 ${BURST_N:-140}
    wait_flip "$BURST_F0"
    press a:100 0
    wait_game 'QUICKPLAY\] flow state [0-9A-F]+ -> 590' 120
    wait_flips ${CAR_WAIT:-90}                  # CAR_WAIT: flips on the car select before A
    press a:400 0                               # this car
    wait_game '\[NET\] lobby line' 60
    wait_flips 150
    if [ "$mode" = race ]; then
        lobbies=$(grep -ac '\[NET\] lobby line' "$D/log.txt")
        # the host (td3_host.sh, HOST_RD) starts the race with START; the
        # load follows: a frame every ~half second
        burst race 30 150
        wait_flip "$BURST_F0"
        echo "start:400" >> "${HOST_RD:?HOST_RD}/cmds.txt"
        note "the host presses START"
        wait_game '\[AUTOPILOT\] result' ${RACE_WAIT:-1500}
        wait_flips 120
        [ "${RACE_STOP_AT_RESULT:-0}" = 1 ] && { collect_bursts; exit 0; }
        post_race
    fi
    if [ "$mode" = leave ]; then
        burst leave 6 140
        wait_flip "$BURST_F0"
        press b:300 60                          # Back: the title's own question
        wait_flips 30
        press up:150 20                         # the question starts on NO
        press a:300 0                           # Yes
        wait_flips 300
    fi
    ;;
create)
    burst quick 6 140
    wait_flip "$BURST_F0"
    press down:200 20
    press a:100 0                               # CREATE GAME: the navigator, to the mode page
    wait_game 'stopped: Create game: on the mode page' 120
    wait_flips 120
    ;;
ypress)
    burst quick 6 60
    wait_flip "$BURST_F0"
    press y:200 40                              # Y on the online page: nothing
    press a:100 0                               # QUICK PLAY
    wait_game 'QUICKPLAY\] flow state [0-9A-F]+ -> 808' 30
    press y:200 0                               # Y on the search page: nothing
    wait_flips 400
    ;;
noserver)
    kill "$(cat "${HOST_RD:?HOST_RD}/pid")"
    wait_flips 120
    burst quick 6 140
    wait_flip "$BURST_F0"
    press a:100 0
    wait_flips 900
    ;;
cancel)
    burst quick 6 60
    wait_flip "$BURST_F0"
    press a:100 0
    wait_game 'QUICKPLAY\] flow state [0-9A-F]+ -> 808' 30
    wait_flips 12
    press b:150 0
    wait_game 'QUICKPLAY\] done: back on the online page' 30
    wait_flips 90
    burst again 6 140
    wait_flip "$BURST_F0"
    press a:100 0
    wait_game 'QUICKPLAY\] flow state [0-9A-F]+ -> 590' 120
    wait_flips 90
    press a:400 0
    wait_game '\[NET\] lobby line' 60
    wait_flips 150
    ;;
esac
alive
grep -a 'unknown screen' "$D/log.txt" && bad "unknown screen in the log"
[ "$mode" != create ] && grep -a '\[ONLINENAV\] press' "$D/log.txt" && bad "the navigator pressed a button"
collect_bursts
[ "${STOP_AT_END:-0}" = 1 ] && scene_stop
if [ "$fail" = 0 ]; then note "PASS: td3_quick $mode"; else note "FAIL: td3_quick $mode"; fi
exit $fail
