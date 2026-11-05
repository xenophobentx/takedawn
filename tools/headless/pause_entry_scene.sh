#!/usr/bin/env bash
# pause_entry_scene.sh <rundir> [KEY=VALUE...]
#
# interface.menu_entry in the race's pause menu (BO-133, src/game/
# menuentry.c): RESUME, RESTART, AUDIO, CONTROLLERS, TAKEDAWN SETTINGS, QUIT.
# Needs a window (WINDOW=1 is set here): without one there is no overlay and
# the menu is the title's.
#
#   1. Single Event > Race (as to_race.sh), after the GO: Start; down to the
#      fifth line: shot pause_entry.
#   2. A: the overlay opens ("[MENU] pause: TAKEDAWN SETTINGS: opening the
#      overlay", the overlay's "open:" line).
#   3. The overlay closed (RECOMP_OVERLAY=file=..., the scripted pad does not
#      reach it): the title is still on its pause menu (shot pause_after),
#      down once more is QUIT (shot pause_quit).
#   4. Start: the race goes on (shot race_on).
#
# A scratch HOME, no settings file; the saves copied as in every scene.
set -u
source "$(dirname "$0")/scene_lib.sh"
HOME_DIR="${HOME_DIR:-$(mktemp -d)}"
rd="$(cd "$(dirname "$0")/../.." && mkdir -p "$1" && cd "$1" && pwd)"
rm -f "$rd/overlay_toggle"
WINDOW=1 scene_start "$@" HOME="$HOME_DIR" RECOMP_SETTINGS=none RECOMP_OVERLAY="file=$rd/overlay_toggle"
fail=0
bad() { note "FAIL: $*"; fail=1; }

to_main_menu
to_single_event
press a:400 90; press right:200 25; press down:200 20; press right:200 25; press a:400 90
wait_log 'CAR1[.]bgv' "car select"
press a:400 0
wait_log "$(track_re crash1)" "race begins"
wait_flip $(( $(anchor "$(track_re crash1)") + $(flips 900) ))
press start:300 120
grep -aq '\[MENU\] pause: TAKEDAWN SETTINGS listed' "$D/log.txt" || bad "not listed in the pause menu"
press down:200 25; press down:200 25; press down:200 25; press down:200 40
SHOT_WINDOW=0 shot pause_entry "$(flip)" 20 0

# A: the overlay opens and pauses the host; waits on the log, in wall time.
PADN=$(( PADN + 1 )); echo "a:400" >> "$D/pad.txt"
t=$(date +%s)
until grep -aq 'open: the title is paused' "$D/log.txt"; do
    alive; [ $(( $(date +%s) - t )) -gt 30 ] && break; sleep 0.25
done
grep -aq '\[MENU\] pause: TAKEDAWN SETTINGS: opening the overlay' "$D/log.txt" || bad "A did not ask for the overlay"
grep -aq 'open: the title is paused' "$D/log.txt" || bad "the overlay did not open"
sleep 2
: > "$rd/overlay_toggle"
f=$(flip); t=$(date +%s)
until [ "$(flip)" -gt $(( f + $(flips 60) )) ]; do
    alive; [ $(( $(date +%s) - t )) -gt 30 ] && { bad "the title did not run on after the overlay closed"; break; }
    sleep 0.25
done
SHOT_WINDOW=0 shot pause_after "$(flip)" 30 0
press down:200 40
SHOT_WINDOW=0 shot pause_quit "$(flip)" 20 0
press start:300 180
SHOT_WINDOW=0 shot race_on "$(flip)" 60 0
[ "$fail" = 0 ] && note "pause entry: listed, opened the overlay, the pause menu intact"
scene_end "pause entry scene done (fail=$fail)"
exit $fail
