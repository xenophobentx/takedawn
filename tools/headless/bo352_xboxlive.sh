#!/usr/bin/env bash
# Exercise the four online actions' layout, Browse and the code picker.
# PASS/FAIL and optional screenshots are written under the run directory.
set -u
source "$(dirname "$0")/scene_lib.sh"
HOME_DIR="${HOME_DIR:-$(mktemp -d)}"
rd="$1"; shift
wait_menulog() {
    local t=$(date +%s)
    until grep -aq "$1" "$D/log.txt" 2>/dev/null; do
        alive
        [ $(( $(date +%s) - t )) -gt "$SCENE_TIMEOUT" ] && die "no '$1' in the log"
        sleep 0.25
    done
}
scene_start "$rd" HOME="$HOME_DIR" RECOMP_SETTINGS=none TAKEDAWN_QUIT_MENU=1 \
    TAKEDAWN_ONLINE=internet TAKEDAWN_GAMERTAG=SceneA1 RECOMP_DEBUG=onlinebot=probe "$@"
to_main_menu
press down:200 20; press down:200 20; press down:200 20
press a:400 90
t=$(date +%s)
until grep -aq 'online: four lines listed' "$D/log.txt" 2>/dev/null; do
    alive
    [ $(( $(date +%s) - t )) -gt "$SCENE_TIMEOUT" ] && die 'online page not shown'
    press a:400 40
done
for item in quick create join browse; do
    SHOT_WINDOW=0 shot "online_$item" "$(flip)" 20 0
    [ "$item" = browse ] || press down:200 20
done
press a:400 90
wait_menulog 'screen xbox-live -> quick-match'
wait_flips 90
SHOT_WINDOW=0 shot browse_results "$(flip)" 20 0
press b:300 90
wait_menulog 'screen quick-match -> xbox-live'
# The title rebuilds the page with the cursor at zero.
press down:200 20; press down:200 20
press a:400 60
wait_menulog 'code picker is up'
SHOT_WINDOW=0 shot code_picker "$(flip)" 20 0
press b:300 60
STOP_AT_END=1 scene_end 'online page'
note 'PASS: bo352_xboxlive'
