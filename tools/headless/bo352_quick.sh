#!/usr/bin/env bash
# bo352_quick.sh <rundir> [KEY=VALUE...]
#
# BO-352 (src/game/xboxlive.c + onlinenav.c): QUICK PLAY. With nobody
# else online the navigator hosts a game and waits in the lobby; alone, the
# "STARTING IN N S" countdown must not begin. B (or Start) cancels and gives
# the menu back.
#
# PASS/FAIL in <rundir>/scene.txt; shots in <rundir>/shots/.
set -u
source "$(dirname "$0")/scene_lib.sh"
HOME_DIR="${HOME_DIR:-$(mktemp -d)}"
rd="$1"; shift
fail=0
bad() { note "FAIL: $*"; fail=1; }

wait_menulog() {
    local t=$(date +%s)
    until grep -aq "$1" "$D/log.txt" 2>/dev/null; do
        alive
        [ $(( $(date +%s) - t )) -gt "$SCENE_TIMEOUT" ] && die "no '$1' in the log"
        sleep 0.25
    done
    note "log: $1"
}

scene_start "$rd" HOME="$HOME_DIR" RECOMP_SETTINGS=none TAKEDAWN_QUIT_MENU=1 \
    TAKEDAWN_ONLINE=internet TAKEDAWN_GAMERTAG=SceneA1 "$@"
to_main_menu
# Enter the online page, accept the sign-in notice and select QUICK PLAY.
press down:200 20; press down:200 20; press down:200 20
press a:400 90
t=$(date +%s)
until grep -aq 'online: four lines listed' "$D/log.txt" 2>/dev/null; do
    alive
    [ $(( $(date +%s) - t )) -gt "$SCENE_TIMEOUT" ] && die 'online page not shown'
    press a:400 40
done
press a:400 90
wait_menulog 'on: quick play'

# the navigator drives through sign-in, Quick Match (nobody online: it hosts),
# the room, create game, name, mode, car, into the lobby. Wait until it has
# selected the car, then give the lobby a little while to draw.
t=$(date +%s)
until grep -aq 'press a on car: this car' "$D/log.txt" 2>/dev/null; do
    alive
    [ $(( $(date +%s) - t )) -gt 120 ] && die "the navigator never reached the car select"
    sleep 0.25
done
note "navigator selected the car (hosting)"
wait_flips 240
alive
SHOT_WINDOW=0 shot quick_lobby "$(flip)" 20 0

# alone in the lobby: no "STARTING IN N S" countdown
sleep 3
alive
grep -aq '\[ONLINENAV\] STARTING IN' "$D/log.txt" && bad "STARTING IN shown while alone"

# B cancels: the navigator stops and hands the menu back
press b:300 90
wait_menulog 'stopped: B: the player cancelled'
alive
grep -aq 'on: quick play' "$D/log.txt" || bad "the navigator did not start with quick play"
STOP_AT_END=1 scene_end "quick scene (fail=$fail)"
if [ "$fail" = 0 ]; then note "PASS: bo352_quick"; else note "FAIL: bo352_quick"; fi
exit $fail
