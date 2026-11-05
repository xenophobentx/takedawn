#!/usr/bin/env bash
# bo352_mainmenu.sh <rundir> [KEY=VALUE...]
#
# The main menu keeps the title's five lines and appends QUIT in either
# online mode. A on each title line still leaves and B returns.
#
# PASS/FAIL in <rundir>/scene.txt; shots in <rundir>/shots/. Needs
# TAKEDAWN_QUIT_MENU=1 (the lines are only listed headless then).
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

# A on the title's five lines leaves the main menu, B brings it back. The
# onlinebot probe logs the screen changes ("screen main-menu ->" / "-> main-menu").
run_nav() {
    scene_start "$rd/nav" HOME="$HOME_DIR" RECOMP_SETTINGS=none TAKEDAWN_QUIT_MENU=1 \
        TAKEDAWN_ONLINE=internet TAKEDAWN_GAMERTAG=SceneA1 RECOMP_DEBUG=onlinebot=probe "$@"
    to_main_menu
    wait_menulog 'main menu: QUIT listed'
    local left right i t
    for i in 0 1 2 3 4; do
        left=$(grep -ac 'screen main-menu ->' "$D/log.txt" 2>/dev/null || true)
        right=$(grep -ac '\-> main-menu' "$D/log.txt" 2>/dev/null || true)
        press a:400 60
        t=$(date +%s)
        until [ "$(grep -ac 'screen main-menu ->' "$D/log.txt" 2>/dev/null || true)" -gt "$left" ]; do
            alive; [ $(( $(date +%s) - t )) -gt 90 ] && break; sleep 0.2
            # Xbox Live (line 3) first shows the title's own notice on the
            # main menu (a popup, no screen change): A accepts it
            [ $(( $(date +%s) - t )) -eq 12 ] && { note "line $i: no screen change yet, A for a notice"; press a:400 60; t=$((t - 1)); }
        done
        [ "$(grep -ac 'screen main-menu ->' "$D/log.txt" 2>/dev/null || true)" -gt "$left" ] \
            || bad "A on the title's line $i did not leave the main menu"
        wait_flips 300                                  # the submenu draws (the World Tour map loads tracks)
        press b:300 60
        t=$(date +%s)
        until [ "$(grep -ac '\-> main-menu' "$D/log.txt" 2>/dev/null || true)" -gt "$right" ]; do
            alive; [ $(( $(date +%s) - t )) -gt 90 ] && break; sleep 0.2
        done
        [ "$(grep -ac '\-> main-menu' "$D/log.txt" 2>/dev/null || true)" -gt "$right" ] \
            || bad "B after the title's line $i did not return to the main menu"
        wait_flips 90                                   # the cursor regains focus
        [ $i -lt 4 ] && press down:200 20
    done
    note "the title's five lines all leave the main menu and B returns"
    STOP_AT_END=1 scene_end "main menu navigation"
}

run_on() {
    scene_start "$rd/on" HOME="$HOME_DIR" RECOMP_SETTINGS=none TAKEDAWN_QUIT_MENU=1 \
        TAKEDAWN_ONLINE=internet TAKEDAWN_GAMERTAG=SceneA1 "$@"
    to_main_menu
    wait_menulog 'main menu: QUIT listed'
    press down:200 20; press down:200 20; press down:200 20; press down:200 20; press down:200 20
    SHOT_WINDOW=0 shot mainmenu_quit_online "$(flip)" 20 0
    grep -aq 'main menu: QUIT listed' "$D/log.txt" || bad "online: QUIT not listed"
    STOP_AT_END=1 scene_end "main menu (online): QUIT"
}

run_off() {
    scene_start "$rd/off" HOME="$HOME_DIR" RECOMP_SETTINGS=none TAKEDAWN_QUIT_MENU=1 "$@"
    to_main_menu
    wait_menulog '\[MENU\] main menu: QUIT listed'
    press down:200 20; press down:200 20; press down:200 20; press down:200 20; press down:200 20
    SHOT_WINDOW=0 shot mainmenu_quit "$(flip)" 20 0
    grep -aq 'QUICK PLAY ONLINE' "$D/log.txt" && bad "offline: obsolete QUICK PLAY ONLINE listed"
    grep -aq '\[MENU\] main menu: QUIT listed' "$D/log.txt" || bad "offline: QUIT not listed"
    STOP_AT_END=1 scene_end "main menu (offline): QUIT, no QUICK PLAY ONLINE"
}

run_nav "$@"
run_on "$@"
run_off
if [ "$fail" = 0 ]; then note "PASS: bo352_mainmenu"; else note "FAIL: bo352_mainmenu"; fi
exit $fail
