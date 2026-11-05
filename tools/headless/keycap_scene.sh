#!/usr/bin/env bash
# keycap_scene.sh <rundir> [KEY=VALUE...]
#
# The keyboard prompts follow the player's keys (BO-267, src/game/keycap.c, glyphs.c): in the main menu
# with the keyboard prompt set (input.glyphs = keyboard) the Select prompt is the cap of the Boost key.
#
#   1. Main menu, default keys: shot keycap_default (CAPTURE=1): the Space cap.
#   2. The overlay (driven through RECOMP_OVERLAY_TEST): Controls page, the Boost row, Enter, Z. The log has
#      "[OVERLAY]... key", settings.toml (a scratch one) has input.keys = "boost=Z".
#   3. The overlay closed: shot keycap_rebound, the Z cap, without a restart.
#   4. Space given back to Boost and the overlay closed: shot keycap_reset, the Space cap as in step 1.
#
# Needs a window (WINDOW=1), not under gamescope and not on a Steam Deck: there the Controls page has no
# key rows (BO-327). A scratch HOME and settings file: the player's are not read or written.
set -u
source "$(dirname "$0")/scene_lib.sh"
HOME_DIR="${HOME_DIR:-$(mktemp -d)}"
rd="$(cd "$(dirname "$0")/../.." && mkdir -p "$1" && cd "$1" && pwd)"
ov="$rd/overlay_test.txt"; : > "$ov"
scene_start "$@" HOME="$HOME_DIR" RECOMP_SETTINGS="$rd/settings.toml" RECOMP_INPUT_GLYPHS=keyboard \
    RECOMP_OVERLAY_TEST="$ov"
rm -f "$rd/settings.toml"
fail=0
bad() { note "FAIL: $*"; fail=1; }
ovl() { echo "$1" >> "$ov"; sleep 0.4; }
ovl_wait_flips() { local f; f=$(flip); local t; t=$(date +%s); until [ "$(flip)" -gt $(( f + $1 )) ]; do alive; [ $(( $(date +%s) - t )) -gt 60 ] && break; sleep 0.25; done; }

# F1, and wait until the overlay says it is open (a key sent while it is still loading its pictures is lost).
ovl_open() {
    local n; n=$(grep -ac 'open: the title is paused' "$D/log.txt" || true)
    ovl "key f1"
    local t; t=$(date +%s)
    until [ "$(grep -ac 'open: the title is paused' "$D/log.txt" || true)" -gt "$n" ]; do
        alive; [ $(( $(date +%s) - t )) -gt 30 ] && { bad "the overlay did not open"; break; }
        sleep 0.25
    done
    sleep 0.5
}

to_main_menu
SHOT_WINDOW=0 shot keycap_default "$(flip)" 30 0
ovl_open
# Controls is the fifth page (BO-327); its key rows follow the six settings and their title
for l in "key pgdn" "key pgdn" "key pgdn" "key pgdn" "key down" "key down" "key down" "key down" "key down" "key down" "key down" "key down" \
         "key enter" "key z" "state $rd/rows_after.txt"; do ovl "$l"; done
sleep 1.5
grep -a -q "Boost.* = Z" "$rd/rows_after.txt" || bad "the Boost row is not Z"
grep -a -q 'keys = "boost=Z"' "$rd/settings.toml" || bad "settings.toml has no boost=Z"
ovl "key f1"; ovl_wait_flips 60
SHOT_WINDOW=0 shot keycap_rebound "$(flip)" 30 0
ovl_open
for l in "key enter" "key space" "state $rd/rows_reset.txt"; do ovl "$l"; done   # Boost row still selected: Space back
sleep 1.5
grep -a -q "Boost.* = Space" "$rd/rows_reset.txt" || bad "the Boost row is not Space again"
ovl "key f1"
ovl_wait_flips 60
SHOT_WINDOW=0 shot keycap_reset "$(flip)" 30 0
[ "${fail}" = 0 ] && note "keycap scene: shots taken (default, rebound, reset)"
scene_end "keycap scene done (fail=$fail)"
exit $fail
