#!/usr/bin/env bash
# nonotice_burst.sh <rundir> [KEY=VALUE...]: the main menu's A on TAKEDAWN
# ONLINE to the TAKEDAWN ONLINE page, one frame every 6th flip
# (fb_flip_NNNNN.bmp, dumpat.txt; at most 64 items, so 60 frames), so every
# screen on the way can be looked at. No navigator: its probe rewrites
# dumpat.txt. The title's Xbox Live notice must not open (xboxlive.c
# xl_notice): the run fails without the skip's log line. SELECT GAMERTAG,
# the player's own sign-in, gets an A every 60 flips until the online page
# is built. Online settings come from the arguments (TAKEDAWN_ONLINE, the
# lobby...).
set -u
source "$(dirname "$0")/scene_lib.sh"
HOME_DIR="${HOME_DIR:-$(mktemp -d)}"
rd="$1"; shift
CAPTURE=1 scene_start "$rd" HOME="$HOME_DIR" RECOMP_SETTINGS=none TAKEDAWN_QUIT_MENU=1 \
    TAKEDAWN_GAMERTAG=NoNotice "$@"
to_main_menu
press down:200 20; press down:200 20; press down:200 20
f=$(flip)
for i in $(seq 1 60); do echo $((f + 6 * i)) >> "$D/dumpat.txt"; done
note "burst from flip $f"
press a:400 0
t=$(date +%s)
until grep -aq 'main menu: Xbox Live notice skipped' "$D/log.txt"; do
    alive
    [ $(( $(date +%s) - t )) -gt 60 ] && die 'the skip did not run'
    sleep 0.1
done
next=$(( $(flip) + 100 )); n=0
until grep -aq 'online: four lines listed' "$D/log.txt"; do
    alive
    [ $n -gt 5 ] && die 'no online page'
    if [ "$(flip)" -ge $next ]; then
        grep -aq 'online: four lines listed' "$D/log.txt" && break
        press a:400 0; n=$((n + 1)); next=$(( $(flip) + 60 ))
    fi
    sleep 0.1
done
wait_flip $(( f + 6 * 60 + 2 ))
STOP_AT_END=1 scene_end 'main menu to the online page'
note 'PASS: no notice'
