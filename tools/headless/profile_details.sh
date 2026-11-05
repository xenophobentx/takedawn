#!/usr/bin/env bash
# profile_details.sh <rundir> [KEY=VALUE...]
#
# The profile the game loaded, as the player sees it (BO-323, a save brought
# over from the PS2 or an Xbox): attract, profile screen, main menu, then
# Driver Details, its shots taken from the moment the page is asked for
# (to_garage.sh counts from the attract film, which a fast machine passes
# before the shot is asked). Leaves the game on Driver Details, or
# STOP_AT_END=1.
#
# Shots (CAPTURE=1): menu_attract, menu_main, details, details_later.
set -u
source "$(dirname "$0")/scene_lib.sh"
scene_start "$@"
to_main_menu
press down:200 20; press down:200 20; press down:200 20; press down:200 20
press a:400 0               # Driver Details
shot details "$(flip)" 150 12
shot details_later "$(flip)" 240 12
scene_end "on Driver Details"
