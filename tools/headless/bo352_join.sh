#!/usr/bin/env bash
# bo352_join.sh <rundir> [KEY=VALUE...]
#
# BO-352 (src/game/xboxlive.c + onlinenav.c): JOIN WITH CODE. The five-letter
# picker opens, left/right/up/down edit it, A joins with that code, and a code
# nobody has ends in the title's own "no games found" (no crash, no hang),
# then B comes back to the page.
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

# JOIN WITH CODE is line 2.
press down:200 20; press down:200 20
press a:400 90
wait_menulog 'the code picker is up'
SHOT_WINDOW=0 shot join_picker "$(flip)" 20 0

# type a code nobody has: right changes the character under the cursor
press right:200 15; press right:200 15; press right:200 15   # first char A -> D
press down:200 15; press down:200 15; press down:200 15; press down:200 15
press right:200 15; press right:200 15                        # fifth char A -> C
SHOT_WINDOW=0 shot join_typed "$(flip)" 20 0

# A: set the code and join (the navigator drives to Quick Match with it)
press a:400 90
wait_menulog 'code .* chosen'
wait_menulog 'on: join'
f0=$(flip); sleep 8; alive
[ "$(flip)" -gt "$f0" ] || bad "flips stopped after joining with a bad code"

# B back to the page (the navigator leaves it to the player)
press b:300 60
alive
grep -aq 'the code picker is up' "$D/log.txt" || bad "the picker never opened"
grep -aq 'code .* chosen' "$D/log.txt" || bad "A did not take the code"
grep -aq 'on: join' "$D/log.txt" || bad "the navigator did not start with join"
STOP_AT_END=1 scene_end "join scene (fail=$fail)"
if [ "$fail" = 0 ]; then note "PASS: bo352_join"; else note "FAIL: bo352_join"; fi
exit $fail
