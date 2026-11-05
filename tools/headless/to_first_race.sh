#!/usr/bin/env bash
# to_first_race.sh <rundir> [KEY=VALUE...]
#
# Reference scene "first race": what a new player gets. No saves at all
# (SAVES=empty): the logos, the intro film skipped with START, "New Profile",
# the name as offered, not saved, then Single Event > Race > USA > Silver
# Lake Southbound (all a new profile has), the first car, the driving
# training film (115 s; A or START skips it, played out here), the race, and RACE_SECS
# seconds (default 40) of driving with steering, boost and the brake.
# Leaves the game running (pid in <rundir>/pid), or STOP_AT_END=1 stops it
# when the driving is over.
#
# With nothing to load, the profile screens open no file to wait on: each
# press is given 150 flips (a menu takes no input while it slides in). The
# shader pre-build's race scene (src/game/shaderbuild.c) goes the same way
# in-process; this one is the check from outside, with the real film.
set -u
source "$(dirname "$0")/scene_lib.sh"
SAVES=empty scene_start "$@"
wait_log 'Titles30[.]xmv' "intro film (Titles30.xmv)"
press_until start:300 '_FEMain[.]xmv' 60
wait_flip $(( $(anchor '_FEMain[.]xmv') + $(flips 300) ))
press start:600 150         # Select Option: New Profile
press a:400 150             # the keyboard, cursor on Done
press a:400 150             # save your profile? No
press a:400 150             # Autosave will be disabled: No
press up:200 30             # Yes
press a:400 240             # the main menu
note "main menu"
to_single_event
press a:400 120             # Race: Select Options
press right:200 30          # Region: Random > USA
press down:200 30
press right:200 30          # Track: Random > Silver Lake Southbound
press_until a:400 'CAR1[.]bgv' 150
note "car select (flip $(anchor 'CAR1[.]bgv'))"
wait_flip $(( $(anchor 'CAR1[.]bgv') + $(flips 200) ))
press a:400 0               # the first car: the training film, the load
wait_log 'C3_V1.streamed[.]dat' "Silver Lake streamed in"
wait_log "$(track_re crash1)" "race begins (tracks crash1.rws)"
wait_flip $(( $(anchor "$(track_re crash1)") + $(flips 300) ))
note "driving (flip $(flip))"
n=$(( ${RACE_SECS:-40} / 40 )); [ "$n" -lt 1 ] && n=1
for (( k = 0; k < n; k++ )); do
    press rt:8000 500
    press rt+lright:1500 100
    press rt+a:6000 380
    press rt+lleft:2500 160
    press lt:1500 100
    press rt+a+lright:4000 260
    press rt+b:5000 320
    press rt+a:8000 500
done
note "driven (flip $(flip))"
scene_end "first race"
