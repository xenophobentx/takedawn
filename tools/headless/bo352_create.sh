#!/usr/bin/env bash
# bo352_create.sh <rundir> [KEY=VALUE...]
#
# BO-352 (src/game/xboxlive.c + onlinenav.c + lobbystatus.c): CREATE GAME. The
# navigator drives to the mode page and stops (the player picks mode, track and
# laps); the script then continues to the car and into the lobby, where the
# top message line shows the status line. It stays 5 minutes, sends the four
# LT + d-pad reactions (no peer: nothing breaks), and B leaves cleanly.
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

press_until_log() {
    local t=$(date +%s)
    until grep -aq "$1" "$D/log.txt" 2>/dev/null; do
        alive
        [ $(( $(date +%s) - t )) -gt "$SCENE_TIMEOUT" ] && die "no '$1' in the log"
        press a:400 40
    done
    note "log: $1"
}

scene_start "$rd" HOME="$HOME_DIR" RECOMP_SETTINGS=none TAKEDAWN_QUIT_MENU=1 \
    TAKEDAWN_ONLINE=internet TAKEDAWN_GAMERTAG=SceneA1 "$@"
to_main_menu
press down:200 20; press down:200 20; press down:200 20   # Xbox Live
press a:400 90
press_until_log 'online: four lines listed'

# down once: line 1 = CREATE GAME; A starts the navigator
press down:200 20
press a:400 90
wait_menulog 'on: create game'
# the navigator drives to the mode page and stops there
wait_menulog 'stopped: Create game: on the mode page'

# the player picks (defaults): continue past the mode page, then the car
press a:400 90
wait_menulog 'screen creating -> car'
wait_flips 120                          # the car page takes A only once it is up
press a:400 90
# into the lobby: wait for the lobby screen to draw, then shoot the top line
wait_menulog '> game-lobby'
wait_flips 240
alive
SHOT_WINDOW=0 shot create_lobby "$(flip)" 20 0

# stay 5 minutes: no crash, no garbage
t0=$(date +%s)
while [ $(( $(date +%s) - t0 )) -lt 300 ]; do
    alive
    sleep 5
    if [ $(( $(date +%s) - t0 )) -ge 120 ] && [ ! -f "$D/shots/create_2min.bmp" ]; then
        SHOT_WINDOW=0 shot create_2min "$(flip)" 20 0
    fi
done
alive
SHOT_WINDOW=0 shot create_5min "$(flip)" 20 0
note "5 minutes in the lobby without a crash"

# the reactions, LT + d-pad (no peer: nothing must break). The keys 1-5 are
# keyboard keys: a pad-live run does not read RECOMP_KEY_LIVE.
for d in up right left down; do press lt+$d:150 20; done
alive
note "reactions LT + d-pad pressed"

# B leaves the game cleanly (the navigator handed back control at the mode page)
press b:300 90
alive
grep -aq 'on: create game' "$D/log.txt" || bad "the navigator did not start with create game"
grep -aq 'stopped: Create game: on the mode page' "$D/log.txt" || bad "the navigator did not stop on the mode page"
STOP_AT_END=1 scene_end "create scene (fail=$fail)"
if [ "$fail" = 0 ]; then note "PASS: bo352_create"; else note "FAIL: bo352_create"; fi
exit $fail
