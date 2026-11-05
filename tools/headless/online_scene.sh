#!/usr/bin/env bash
# online_scene.sh <rundir> [KEY=VALUE...]
#
# BO-134 spike: start the game headless with the online layer on
# (TAKEDAWN_ONLINE=1 unless given), drive it to the main menu and leave it
# running for hand-driven steps: append a press to <rundir>/pad.txt
# ("down:200"), ask for a frame with "N-M" in <rundir>/dumpat.txt (CAPTURE=1).
# The [XNET] lines are in <rundir>/log.txt.
set -u
source "$(dirname "$0")/scene_lib.sh"
scene_start "$@"
to_main_menu
scene_end "at the main menu"
