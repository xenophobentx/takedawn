#!/usr/bin/env bash
# The gamertag page of the online path (SELECT GAMERTAG), as a player reaches it
# from the main menu: TAKEDAWN ONLINE, then the list. Shots of the list, then
# the cursor driven up as far as it goes and A pressed, and A again on what
# opens. Without the nodash change the list starts with CREATE NEW ACCOUNT /
# MANAGE ACCOUNTS and that A asks for the Xbox Dashboard; with it the list is
# the gamertag alone and up / A stay on the page or sign in.
#
#   tools/headless/nodash_scene.sh <rundir> [KEY=VALUE...]
#
# No server is needed for the page; TAKEDAWN_LOBBY points at a closed local
# port so nothing reaches the live lobby. NODASH_UPS (default 2) up presses;
# NODASH_YES=1 answers YES to whatever question the A opened. NODASH_LOBBY=
# host:port of a local lobby: the sign-in is followed to the online page.
# NODASH_THEN_NO=1: then down and A (NO on a question still up).
set -u
source "$(dirname "$0")/scene_lib.sh"
HOME_DIR="${HOME_DIR:-$(mktemp -d)}"
rd="$1"; shift
scene_start "$rd" HOME="$HOME_DIR" RECOMP_SETTINGS=none \
    TAKEDAWN_ONLINE=internet TAKEDAWN_GAMERTAG=NoDash1 TAKEDAWN_LOBBY="${NODASH_LOBBY:-127.0.0.1:38997}" \
    TAKEDAWN_PORT_OFFSET=38100 RECOMP_DEBUG=onlinebot=probe "$@"
to_main_menu
press down:200 20; press down:200 20; press down:200 20
press a:400 30
# the page slides in; the probe's screen line can come late or not at all
wait_flips 240
SHOT_WINDOW=0 shot gt_list "$(flip)" 20 0
i=0; while [ "$i" -lt "${NODASH_UPS:-2}" ]; do press up:200 20; i=$(( i + 1 )); done
SHOT_WINDOW=0 shot gt_up "$(flip)" 20 0
press a:400 60
SHOT_WINDOW=0 shot gt_after_a "$(flip)" 20 0
# NODASH_YES=1: up to YES first (the title's question opens on NO)
[ "${NODASH_YES:-0}" = 1 ] && press up:200 20
press a:400 120
alive
SHOT_WINDOW=0 shot gt_after_a2 "$(flip)" 20 0
wait_flips 120
alive
if [ "${NODASH_THEN_NO:-0}" = 1 ]; then
    # the question is still up (the toolkit refused the launch): NO, A
    press down:200 20
    press a:400 120
    alive
    SHOT_WINDOW=0 shot gt_then_no "$(flip)" 20 0
fi
if [ -n "${NODASH_LOBBY:-}" ]; then
    # a local lobby (build/lobby/takedawn-lobby --port N --bind 127.0.0.1):
    # the sign-in goes on to the online page
    t=$(date +%s)
    until grep -aq 'online: four lines listed' "$D/log.txt" 2>/dev/null; do
        alive
        [ $(( $(date +%s) - t )) -gt "$SCENE_TIMEOUT" ] && die 'online page not shown'
        press a:400 40
    done
    wait_flips 60
    SHOT_WINDOW=0 shot online_page "$(flip)" 20 0
fi
STOP_AT_END=1 scene_end 'gamertag page'
note 'PASS: nodash_scene (the game is still running)'
