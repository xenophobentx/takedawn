#!/usr/bin/env bash
# crashnav_zoom_window.sh <rundir> [KEY=VALUE...]
#
# The Crash Nav zoom with a window, at the window's resolution (BO-311): a
# burst of BURST window grabs (default 120, every presented frame) from the
# press on the region, through the overlay's test channel. The headless
# shots of crashnav_zoom_scene.sh are the guest's 640x480, too small to show
# a 4x texture.
set -u
source "$(dirname "$0")/scene_lib.sh"
HOME_DIR="${HOME_DIR:-$(mktemp -d)}"
rd="$(cd "$(dirname "$0")/../.." && mkdir -p "$1" && cd "$1" && pwd)"
ov="$rd/ovtest.txt"; : > "$ov"
WINDOW=1 scene_start "$@" HOME="$HOME_DIR" RECOMP_SETTINGS=none RECOMP_OVERLAY_TEST="$ov"
to_main_menu
press a:400 0               # Burnout 3 World Tour: Crash Nav, Select Region
wait_flips 240
mkdir -p "$rd/burst"
echo "burst ${BURST:-120} $rd/burst/z" >> "$ov"
press a:400 0               # the region: the flight to its map
wait_flips 300
scene_end "crash nav zoom (window)"
