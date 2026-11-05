#!/usr/bin/env bash
# unlock_all_scene.sh <rundir> [KEY=VALUE...]
#
# The overlay's Unlock everything (BO-131, src/game/unlockall.c, the
# toolkit's xbox_OverlaySetCheats) with a window (the overlay needs one):
#
#   1. Main menu; the overlay (driven through RECOMP_OVERLAY_TEST), System
#      page, "Unlock everything": A arms it (its warning shows), A switches
#      it on. The title's saves now go to <save root>/cheat-session.
#      Shots overlay_armed (the warning, under the row) and overlay_cheat
#      (switched on), of the window, overlay included.
#   2. Driver Details > Profile > Save Profile: the title saves. The save in
#      the save root must not change; the copy must.
#   3. The overlay: A switches it off, "Save the progress...?" comes up;
#      KEEP=1 picks Yes (the save is kept in the save history first, then
#      the copy goes over it: it changes), else No (the save stays as it was;
#      the page says saving waits for a restart and the Restart row shows).
#
# The cheat rows are a tester's: they show only with RECOMP_DEBUG=cheats,
# which this script sets.
#
# The save root is a scratch copy of SAVES' UDATA (RECOMP_SAVE_DIR); a
# scratch HOME and no settings file.
set -u
source "$(dirname "$0")/scene_lib.sh"
HOME_DIR="${HOME_DIR:-$(mktemp -d)}"
rd="$(cd "$(dirname "$0")/../.." && mkdir -p "$1" && cd "$1" && pwd)"
root="$rd/saveroot"
rm -rf "$root" && mkdir -p "$root"
cp -R "${SAVES:-$(scene_default_saves)}/UDATA" "$root/"
ov="$rd/ovtest.txt"; : > "$ov"
WINDOW=1 scene_start "$@" HOME="$HOME_DIR" RECOMP_SETTINGS=none RECOMP_SAVE_DIR="$root" \
    RECOMP_OVERLAY_TEST="$ov" RECOMP_DEBUG="${RECOMP_DEBUG:+$RECOMP_DEBUG,}cheats"
fail=0
bad() { note "FAIL: $*"; fail=1; }
sum() { (cd "$1" && find UDATA -type f -print0 | sort -z | xargs -0 shasum) 2>/dev/null | shasum | cut -c1-12; }

# ov <line>: one overlay test line, then a moment for it to be taken.
ov() { echo "$1" >> "$ov"; sleep 0.4; }
# The overlay takes one test line a turn of the main loop, which is slow
# while the window is covered: wait for the state file to be written anew.
st() {
    local k=0
    rm -f "$rd/ovstate.txt"
    echo "state $rd/ovstate.txt" >> "$ov"
    until [ -s "$rd/ovstate.txt" ] || [ $k -gt 100 ]; do sleep 0.1; k=$((k + 1)); done
    cat "$rd/ovstate.txt" 2>/dev/null
}
# The rows of the page are the state file's lines after the header; the
# selected one starts with '>'.
row_of() { st | grep -v '^\(open\|page\|offer\|paused\) ' | grep -n "$1" | head -n 1 | cut -d: -f1; }
sel_row() { st | grep -v '^\(open\|page\|offer\|paused\) ' | grep -n '^>' | head -n 1 | cut -d: -f1; }
to_row() {
    local want cur k=0; want=$(row_of "$1"); [ -n "$want" ] || { bad "no row '$1'"; return 1; }
    cur=$(sel_row)
    while [ "${cur:-0}" != "$want" ]; do
        [ "${cur:-0}" -lt "$want" ] && ov "pad down" || ov "pad up"
        cur=$(sel_row); k=$((k + 1))
        [ $k -gt 40 ] && { bad "could not reach row '$1'"; return 1; }
    done
}
open_advanced() {
    ov "pad back+start"; sleep 1
    local k=0
    until st | grep -q '^page System'; do ov "pad rb"; k=$((k + 1)); [ $k -gt 7 ] && { bad "no System page"; return 1; }; done
}

to_main_menu
before=$(sum "$root")

# 1. On.
open_advanced
to_row 'Unlock everything: Off'
ov "pad a"
st | grep -q "Unlock everything: Off" || bad "armed row missing"
ov "shot $rd/overlay_armed.bmp"; sleep 1
ov "pad a"
st | grep -q 'Unlock everything: On' || bad "not switched on"
grep -aq 'cheats: the game.s saves go to' "$D/log.txt" || bad "the saves were not sent to the copy"
[ -d "$root/cheat-session/UDATA" ] || bad "no copy of the save"
ov "shot $rd/overlay_cheat.bmp"; sleep 1
ov "pad b"; sleep 1

# 2. Driver Details > Profile > Save Profile.
press down:200 20; press down:200 20; press down:200 20; press down:200 20
press a:400 90              # Driver Details
press down:200 20; press down:200 20; press down:200 20
press a:400 90              # Profile
SHOT_WINDOW=0 shot profile_page "$(flip)" 20 0
press down:200 20
press a:400 180             # Save Profile: the slots
press a:400 180             # Profile 1: "already exists ... Overwrite?", on NO
press up:300 90             # YES
SHOT_WINDOW=0 shot save_q "$(flip)" 20 0
press a:400 400
SHOT_WINDOW=0 shot save_done "$(flip)" 20 0
press a:400 120             # the message, if one waits for A
during=$(sum "$root"); copy=$(sum "$root/cheat-session")
[ "$during" = "$before" ] || bad "the save changed while the cheat was on"
[ "$copy" != "$before" ] || bad "the title's save did not reach the copy"
note "save root $before -> $during; copy $copy"

# 3. Off, and the question.
open_advanced
to_row 'Unlock everything: On'
ov "pad a"
st | grep -q 'Save the progress made with cheats on' || bad "no question after switching off"
if [ "${KEEP:-0}" = 1 ]; then to_row 'Yes: keep it'; else to_row 'No: throw it away'; fi
ov "pad a"
ov "shot $rd/overlay_after.bmp"; sleep 1
st > "$rd/ovstate_after.txt"
ov "pad b"; sleep 1
after=$(sum "$root")
if [ "${KEEP:-0}" = 1 ]; then
    [ "$after" = "$copy" ] || note "kept: save root $after, copy was $copy (equal only if the title saved a file)"
    [ -d "$root/cheat-session" ] && bad "the copy is still there after keeping"
    # The save as it was is kept in the save history before the copy goes
    # over it, and nothing is left beside the save's files.
    hist=$(grep -a 'cheats: the save before keeping is in' "$D/log.txt" | tail -n 1 | sed 's/.*is in //')
    [ -n "$hist" ] && [ -d "$hist/UDATA" ] || bad "no history copy of the save before keeping"
    [ -n "$hist" ] && note "history copy $hist: $(sum "$hist") (save before: $before)"
    [ -z "$(find "$root/UDATA" -name '*.~new')" ] || bad "a .~new file is left in the save"
else
    [ "$after" = "$before" ] || bad "the save changed after throwing away"
    # Saving waits for a restart: said on the page and at the foot, and the
    # restart offered.
    grep -q 'Progress is not being saved until you restart' "$rd/ovstate_after.txt" || bad "no line about saving after throwing away"
    grep -q 'Restart now to apply' "$rd/ovstate_after.txt" || bad "no Restart row after throwing away"
    grep -q 'Quit game (progress is not being saved until a restart)' "$rd/ovstate_after.txt" || bad "the Quit row does not say it"
fi
note "unlock all: before $before, during $during, after $after, copy $copy (fail=$fail)"
scene_end "unlock all scene done"
exit $fail
