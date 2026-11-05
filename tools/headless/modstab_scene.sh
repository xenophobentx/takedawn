#!/usr/bin/env bash
# modstab_scene.sh <rundir> [KEY=VALUE...]
#
# The Mods tab of the settings overlay (BO-316, src/game/modstab.c) and its sync with the game's own
# MODS page (modsmenu.c): three small mods in a scratch folder, one of them invalid.
#
#   1. Main menu; the overlay (RECOMP_OVERLAY_TEST), the Mods tab: rows dumped (modstab_rows_1.txt) and
#      the window shot (modstab_open.bmp, with the overlay on it).
#   2. The first mod turned off in the tab; there is no Apply step (BO-338), the choice is saved at once: the
#      rows say OFF*, its NEXT START tag is lit and the page says to restart; shot modstab_applied.bmp.
#   3. The overlay closed; Driver Details > Settings > MODS in the game: the same mod is OFF* there too
#      (shot mods_page, CAPTURE=1 with the game's own frame).
#
# Needs a window (WINDOW=1). A scratch HOME, settings and mod list: the player's are not read or written.
set -u
source "$(dirname "$0")/scene_lib.sh"
HOME_DIR="${HOME_DIR:-$(mktemp -d)}"
rd="$(cd "$(dirname "$0")/../.." && mkdir -p "$1" && cd "$1" && pwd)"
mods="$rd/mods"; ov="$rd/overlay_test.txt"; : > "$ov"
mkmod() {   # <folder> <id> <name> <version> <author> <description>
    mkdir -p "$mods/$1/files/Data"
    printf 'id = "%s"\nname = "%s"\nversion = "%s"\nauthor = "%s"\ndescription = "%s"\n' "$2" "$3" "$4" "$5" "$6" > "$mods/$1/mod.toml"
    printf 'x' > "$mods/$1/files/Data/$1.bin"
}
mkmod alpha alpha.road "Neon Road Textures" 1.2 "Ann Example" "Brighter road textures for the city tracks."
mkmod beta beta.menu "Slim Menu" 0.9 "B. Tester" "A thinner menu font."
mkdir -p "$mods/broken"; printf 'id = "gamma.broken"\n' > "$mods/broken/mod.toml"
scene_start "$@" HOME="$HOME_DIR" RECOMP_SETTINGS="$rd/settings.toml" TAKEDAWN_MODS="$mods" RECOMP_OVERLAY_TEST="$ov"
fail=0
bad() { note "FAIL: $*"; fail=1; }
ovl() { echo "$1" >> "$ov"; sleep 0.4; }
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
ovl_wait_flips() { local f t; f=$(flip); t=$(date +%s); until [ "$(flip)" -gt $(( f + $1 )) ]; do alive; [ $(( $(date +%s) - t )) -gt 60 ] && break; sleep 0.25; done; }

to_main_menu
ovl_open
# Display, Graphics, Game, Audio, Controls, Mods (BO-327): five pages on
for l in "key pgdn" "key pgdn" "key pgdn" "key pgdn" "key pgdn" "state $rd/modstab_rows_1.txt" "shot $rd/modstab_open.bmp"; do ovl "$l"; done
sleep 1
grep -a -q "Neon Road Textures" "$rd/modstab_rows_1.txt" || bad "the tab does not list the first mod"
grep -a -q "Slim Menu" "$rd/modstab_rows_1.txt" || bad "the tab does not list the second mod"
grep -a -q "Neon Road Textures.*\[NEXT START\]" "$rd/modstab_rows_1.txt" || bad "the mod's row has no NEXT START tag"
grep -a -q "Apply" "$rd/modstab_rows_1.txt" && bad "the tab still has an Apply row"
for l in "key enter" "state $rd/modstab_rows_2.txt" "shot $rd/modstab_applied.bmp"; do ovl "$l"; done
sleep 1
grep -a -q "Neon Road Textures.*OFF\*.*\[NEXT START\*\]" "$rd/modstab_rows_2.txt" || bad "turning the first mod off did not show OFF* and a lit NEXT START"
grep -a -q "Restart the game to use the saved mods" "$rd/modstab_rows_2.txt" || bad "the page did not say to restart after the saved change"
ovl "key f1"; ovl_wait_flips 60
# the game's own MODS page: Driver Details > Settings > MODS (the sixth line, below TAKEDAWN SETTINGS)
press down:200 20; press down:200 20; press down:200 20; press down:200 20
press a:400 90
press down:200 20; press down:200 20; press down:200 20; press down:200 20
press a:400 120
press down:200 20; press down:200 20; press down:200 20; press down:200 20; press down:200 20
press a:400 60
SHOT_WINDOW=0 shot mods_page "$(flip)" 30 0
[ "${fail}" = 0 ] && note "mods tab scene: tab listed the mods, the toggle saved at once, shots taken"
scene_end "mods tab scene done (fail=$fail)"
exit $fail
