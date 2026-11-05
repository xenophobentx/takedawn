#!/usr/bin/env bash
set -u
source "$(dirname "$0")/scene_lib.sh"
HOME_DIR="${HOME_DIR:-$(mktemp -d)}"
rd="$1"; shift
scene_start "$rd" HOME="$HOME_DIR" RECOMP_SETTINGS=none TAKEDAWN_QUIT_MENU=1 TAKEDAWN_ONLINE=lan TAKEDAWN_PORT_OFFSET=30850 TAKEDAWN_GAMERTAG=SceneA1 "$@"
to_main_menu
press down:200 20; press down:200 20; press down:200 20
press a:400 90
t=$(date +%s)
until grep -aq 'online: four lines listed' "$D/log.txt"; do alive; [ $(( $(date +%s)-t )) -gt "$SCENE_TIMEOUT" ] && die 'no online page'; press a:400 40; done
wait_flips 60
f=$(flip)
for i in $(seq 1 35); do echo $((f + 6*i)) >> "$D/dumpat.txt"; done
press a:200 30
press b:200 60
grep -aq 'stopped: B: the player cancelled' "$D/log.txt" || die 'no cancel'
grep -aq 'Back uses sign-out Yes path' "$D/log.txt" && die 'cancel signed out'
SHOT_WINDOW=0 shot after_cancel "$(flip)" 20 0
press a:400 90
t=$(date +%s)
until grep -aq 'press a on car: this car' "$D/log.txt"; do alive; [ $(( $(date +%s)-t )) -gt 120 ] && die 'retry did not reach car'; sleep .25; done
wait_flips 240
SHOT_WINDOW=0 shot retry_lobby "$(flip)" 20 0
STOP_AT_END=1 scene_end 'early cancel and retry'
note 'PASS: early cancel and retry'
