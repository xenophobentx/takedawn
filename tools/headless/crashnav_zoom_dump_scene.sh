#!/usr/bin/env bash
# crashnav_zoom_dump_scene.sh <rundir> [KEY=VALUE...]
#
# Crash Nav's zoom (BO-314): main menu > Burnout 3 World Tour (Select Region,
# the globe), A on the region: the globe zooms to the region, the region to
# the city, and the city lands on Select Location (the map). With
# display.hud_fix at 16:9 every step of the zoom must keep the map's shape
# the way display.crashnav_map draws the map at Select Location (stretched:
# the title's columns; native: 4:3 with the menu), with no jump when the
# zoom lands.
#
# With CAPTURE=1 every flip of the zoom is dumped (ZOOM_FLIPS, default 420,
# from just before the A on the region) as <rundir>/zoom/fb_flip_NNNNN.bmp,
# and the globe as the shot crashnav_globe. BO3_HUD_DUMP=<file> in the environment: a dump
# of the 2D scenes every DUMP_EVERY (default 10) flips of the zoom, tagged
# with the flip.
set -u
source "$(dirname "$0")/scene_lib.sh"
scene_start "$@"
to_main_menu
press a:400 0               # Burnout 3 World Tour: Crash Nav, Select Region
shot crashnav_globe "$(flip)" 240 0
n="${ZOOM_FLIPS:-420}"
# The dumps are asked for before the press (a flip already shown cannot be
# dumped): from 10 flips ahead, the globe's last frames included.
f0=$(( $(flip) + 10 ))
if [ "${CAPTURE:-0}" = 1 ]; then
    echo "$f0-$(( f0 + n ))" >> "$D/dumpat.txt"
fi
press a:400 0               # the region (USA): the zoom
note "zoom from flip $f0"
end=$(( f0 + n ))
while [ "$(flip)" -lt "$end" ]; do
    if [ -n "${BO3_HUD_DUMP:-}" ]; then
        echo "flip $(flip)" > "$BO3_HUD_DUMP.req"
    fi
    wait_flips "${DUMP_EVERY:-10}"
done
wait_flips 2
if [ "${CAPTURE:-0}" = 1 ]; then
    mkdir -p "$D/zoom"
    for (( k = f0; k <= end; k++ )); do
        src=$(printf '%s/fb_flip_%05u.bmp' "$D" "$k")
        [ -f "$src" ] && mv "$src" "$D/zoom/"
    done
fi
scene_end "zoom done, at Select Location"
