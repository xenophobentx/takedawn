#!/usr/bin/env bash
# crashnav_zoom_scene.sh <rundir> [KEY=VALUE...]
#
# The Crash Nav zoom (BO-311, the owner's Deck clip): main menu > Burnout 3
# World Tour (Select Region, the globe), A on the region: the camera flies
# from the globe to the region's satellite map and the city. With CAPTURE=1
# every frame of the flight is kept: shot crashnav_zoom (the frame
# ZOOM_AT flips after the press, default 60) and its ZOOM_WINDOW frames either
# side (default 60) in shots/crashnav_zoom/. With RECOMP_DEBUG=texpack_log
# the log says which pack textures were seen and swapped in, and when.
set -u
source "$(dirname "$0")/scene_lib.sh"
scene_start "$@"
to_main_menu
press a:400 0               # Burnout 3 World Tour: Crash Nav, Select Region
wait_flips 240
a=$(flip)
press a:400 0               # the region: the flight to its map
shot crashnav_zoom "$a" "${ZOOM_AT:-60}" "${ZOOM_WINDOW:-60}"
wait_flips 120
shot crashnav_city "$(flip)" 20 0
scene_end "crash nav zoom"
