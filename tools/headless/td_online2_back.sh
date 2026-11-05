#!/usr/bin/env bash
set -u
source "$(dirname "$0")/scene_lib.sh"
HOME_DIR="${HOME_DIR:-$(mktemp -d)}"
rd="$1"; shift
scene_start "$rd" HOME="$HOME_DIR" RECOMP_SETTINGS=none TAKEDAWN_QUIT_MENU=1 TAKEDAWN_ONLINE=lan TAKEDAWN_PORT_OFFSET=30850 "$@"
to_main_menu
press down:200 20; press down:200 20; press down:200 20
press a:400 90
t=$(date +%s)
until grep -aq 'online: four lines listed' "$D/log.txt"; do alive; [ $(( $(date +%s)-t )) -gt "$SCENE_TIMEOUT" ] && die 'no online page'; press a:400 40; done
wait_flips 60
f=$(flip)
for i in $(seq 1 35); do echo $((f + 6*i)) >> "$D/dumpat.txt"; done
printf 'b:200\n' >> "$D/pad.txt"
wait_flip $((f + 220))
SHOT_WINDOW=0 shot after_back "$(flip)" 20 0
grep -aq 'Back uses sign-out Yes path' "$D/log.txt" || die 'direct path not reached'
[ "$(grep -ac 'main menu: QUIT listed' "$D/log.txt")" -ge 2 ] || die 'main menu not reached'
STOP_AT_END=1 scene_end 'direct Back capture'
note 'PASS: direct Back'
