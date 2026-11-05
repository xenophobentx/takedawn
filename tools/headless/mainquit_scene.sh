#!/usr/bin/env bash
# mainquit_scene.sh <rundir> [KEY=VALUE...]
#
# The main menu's QUIT line (BO-301, src/game/mainquit.c): the menu lists a
# sixth line below Driver Details, and A on it closes the game the way the
# window's close button does.
#
#   1. Main menu, down five times to the sixth line: shot mainquit_menu
#      (CAPTURE=1); the log has "[MENU] main menu: QUIT listed".
#   2. Up and down once more at the end: the cursor stays on the last line.
#   3. A: the log has "[MENU] main menu: QUIT chosen" and "[QUIT] asked for",
#      the process ends by itself within QUIT_WAIT seconds (default 40), and
#      the title took no other page ("[FILE]" lines after the press: none of
#      a page change is asserted, only that the process ends).
#   With TAKEDAWN_QUIT_MENU=0 only step 1 is done and the line must not be
#   listed.
#
# Needs a window (WINDOW=1), or TAKEDAWN_QUIT_MENU=1 to list the line in a
# headless run too: without either there is no window to close, so the line is
# not listed (checked as such).
#
# A scratch HOME and no settings file (RECOMP_SETTINGS=none): the player's
# settings are not read or written; the saves are copied as in every scene.
set -u
source "$(dirname "$0")/scene_lib.sh"
HOME_DIR="${HOME_DIR:-$(mktemp -d)}"
scene_start "$@" HOME="$HOME_DIR" RECOMP_SETTINGS=none
fail=0
bad() { note "FAIL: $*"; fail=1; }

to_main_menu
press down:200 20; press down:200 20; press down:200 20; press down:200 20
press down:200 30
SHOT_WINDOW=0 shot mainquit_menu "$(flip)" 20 0
listed=$(grep -ac '\[MENU\] main menu: QUIT listed' "$D/log.txt" || true)
if [ "${TAKEDAWN_QUIT_MENU:-}" = 0 ] || { [ "${WINDOW:-0}" != 1 ] && [ "${TAKEDAWN_QUIT_MENU:-}" != 1 ]; }; then
    [ "$listed" = 0 ] || die "listed without a window or with the switch off"
    scene_end "main menu quit off: shot taken"
    exit 0
fi
[ "$listed" -ge 1 ] || bad "QUIT was not listed"

press down:200 20; press down:200 20         # at the end: stays on the last line
# A: the game may end before the press's own wait (press would call that a
# crash), so the line goes in by hand.
PADN=$(( PADN + 1 )); echo "a:400" >> "$D/pad.txt"
pid=$(cat "$D/pid"); t=$(date +%s)
until ! kill -0 "$pid" 2>/dev/null; do
    [ $(( $(date +%s) - t )) -gt "${QUIT_WAIT:-40}" ] && { bad "the game did not end after QUIT"; break; }
    sleep 0.25
done
grep -aq '\[MENU\] main menu: QUIT chosen' "$D/log.txt" || bad "no '[MENU] main menu: QUIT chosen' line"
grep -aq '\[QUIT\] asked for: the main menu' "$D/log.txt" || bad "no '[QUIT] asked for' line"
[ "${fail}" = 0 ] && note "main menu QUIT: the game ended by itself after $(( $(date +%s) - t )) s"
scene_end "main menu quit scene done (fail=$fail)"
exit $fail
