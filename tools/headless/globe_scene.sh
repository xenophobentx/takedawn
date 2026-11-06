#!/usr/bin/env bash
# globe_scene.sh <rundir> [KEY=VALUE...]
#
# Crash Nav's globe and the 4:3 pictures around it, one game process
# (BO-385, GitHub #124; docs/widescreen-hud.md, display.pictures). Shots
# (CAPTURE=1): globe (Select Region, the globe at rest over North America),
# zoom (the flight from the globe to the USA map, ZOOM_AT flips after the runtime took A,
# default 60), city (Select Location), then through Single Event to the
# loading screen of the first race: loading.
#
# With display.hud_fix at 16:9 and display.pictures blur or black the globe
# has its 4:3 size and shape in the middle (the 2D camera's clip x, scaled
# for sub_000F31E0 only), the menu films, the loading picture and the zoom
# are 4:3 as before; at stretch the globe is the title's own. Its lines are
# one guest pixel wide at every render scale (toolkit nv2a_wide_lines.c):
# RECOMP_DEBUG=fb_dump_host=1 also writes each shot at its host size
# (<rundir>/fb_host_flip_NNNNN.bmp, renamed to shots/<name>_host.bmp).
set -u
source "$(dirname "$0")/scene_lib.sh"
scene_start "$@"

# host <name>: the host-size frame of the shot just taken, if one was made.
host() {
    local tgt; tgt=$(awk -F'\t' -v n="$1" '$1 == n { print $4 }' "$D/shots.tsv" | tail -n 1)
    local src; src=$(printf '%s/fb_host_flip_%05u.bmp' "$D" "$tgt")
    [ -n "$tgt" ] && [ -f "$src" ] && mv "$src" "$D/shots/$1_host.bmp"
    rm -f "$D"/fb_host_flip_*.bmp
}

to_main_menu
press a:400 0               # Burnout 3 World Tour: Crash Nav, Select Region
shot globe "$(flip)" 240 0; host globe
press a:400 0               # the USA: the flight to its map
shot zoom "$(anchor 'PAD: live "' "$PADN")" "${ZOOM_AT:-60}" 0; host zoom
wait_flips 120
shot city "$(flip)" 20 0; host city
press b:400 120             # back to the globe
press b:400 120             # the main menu
press down:200 20
press a:400 90              # Single Event: Select Mode
press a:400 90              # Race: Select Options
press a:400 0               # Select Car
wait_log 'CAR1[.]bgv' "car select (CAR1.bgv)"
wait_flips 150
press a:400 0               # the first car: the loading screen
shot loading "$(flip)" 40 0; host loading
scene_end "at the loading screen"
