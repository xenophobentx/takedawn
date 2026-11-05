#!/usr/bin/env bash
# controls_stale_scene.sh <rundir> [KEY=VALUE...]
#
# GitHub #52, #111: the PS2 driving layout in the events after a crash event
# whose results camera reached its end. Three events in one process: Crash
# (Trailer Trash), waits on the results until p1_results goes to 1, Continue;
# Road Rage, the bottom button held from the GO, Quit; Race, the same. A pad
# script stands in for a controller (BO3_CTL_SCRIPTED=1). The byte stays 1
# through the last two, and they must still have the PS2 layout.
#
# PASS/FAIL per event in <rundir>/stale.txt; exit 1 on a failure.
set -u
source "$(dirname "$0")/scene_lib.sh"
rd="$1"; shift
abs="$(cd "$(dirname "$0")/../.." && mkdir -p "$rd" && cd "$rd" && pwd)"
scene_start "$rd" BO3_CTL_LOG="$abs/ctl.txt" BO3_CTL_SCRIPTED=1 \
    RECOMP_INPUT_CONTROL_SCHEME=ps2 RECOMP_INPUT_FACE_LAYOUT=xbox "$@"
wait_ctl() {
    local t=$(date +%s)
    until [ "$(grep -cE "$1" "$D/ctl.txt" 2>/dev/null || true)" -ge "$2" ]; do
        alive
        [ $(( $(date +%s) - t )) -gt "$SCENE_TIMEOUT" ] && die "no '$3' in the controls log"
        sleep 0.2
    done
    note "ctl: $3 (flip $(flip))"
}
go_re='p1_phase [0-9A-F]+ -> 00000002'
fe_re='module_vt 003A9ACC -> 003A9E7C'
# to_select_car: A until the front end has opened CAR1.bgv again.
to_select_car() {
    local k=0
    until tail -n +"$1" "$D/events.txt" | grep -aq 'CAR1[.]bgv'; do
        k=$((k + 1)); [ "$k" -gt 40 ] && die "no Select Car after $k presses"
        press a:400 150
    done
    note "Select Car after $k presses"
}
# pick_event <downs from Race>: Select Car > Options > Select Mode, up to Race
# (the list does not wrap), down to the event, its options, Select Car.
pick_event() {
    local mark=$(( $(wc -l < "$D/events.txt") + 1 )) i
    press b:300 60
    press b:300 60
    press up:200 20; press up:200 20; press up:200 20
    for (( i = 0; i < $1; i++ )); do press down:200 20; done
    press a:400 90
    press a:400 90
    until tail -n +"$mark" "$D/events.txt" | grep -aq 'CAR1[.]bgv'; do alive; sleep 0.2; done
    wait_flips 60
    press a:400 0               # the first car: loading
}
# drive <nth GO>: the bottom button, then Quit from the pause menu.
drive() {
    wait_ctl "$go_re" "$1" "GO $1"
    wait_flips 60
    press a:4000 0              # the PS2 accelerator
    wait_flips 60
    press start:200 0           # pause
    wait_ctl 'paused_by FFFFFFFF -> 00000000' "$(( $1 - 1 ))" "paused"
    wait_flips 60
    for p in down:150 down:150 down:150 down:150 a:300; do press "$p" 0; done
    wait_flips 240              # Quit: "are you sure", the cursor on No
    press up:150 0
    press a:300 0               # Yes
    wait_ctl "$fe_re" "$1" "front end"
    wait_flips 240
}
to_main_menu
to_single_event
press down:200 20; press down:200 20; press down:200 20     # Crash
press a:400 90              # Junction Selection
press down:200 20           # 1. Trailer Trash
press a:400 90              # Select Car
wait_log 'CAR1[.]bgv' "car select (CAR1.bgv)"
press a:400 0
wait_log 'streamed[.]dat' "junction streamed in"
wait_log "$(track_re crash2)" "captions (crash2.rws)"
wait_flip $(( $(anchor "$(track_re crash2)") + 300 ))
press a:400 60              # Continue: the countdown
press a:14000 0             # the PS2 accelerator, to the junction
wait_log 'crash1[.]rws' "impact (crash1.rws)"
wait_ctl 'p1_results 00000000 -> 00000001' 1 "results camera at its end"
mark=$(( $(wc -l < "$D/events.txt") + 1 ))
# Continue: the results take A some time after the camera has arrived.
k=0
until grep -qE "$fe_re" "$D/ctl.txt"; do
    k=$((k + 1)); [ "$k" -gt 40 ] && die "the results did not take Continue"
    wait_flips 300
    grep -qE "$fe_re" "$D/ctl.txt" || press a:300 0
done
note "ctl: front end after $k tries (flip $(flip))"
to_select_car "$mark"
pick_event 2                # Road Rage
drive 2
mark=$(( $(wc -l < "$D/events.txt") + 1 ))
to_select_car "$mark"
pick_event 0                # Race
drive 3
python3 "$(dirname "$0")/controls_stale_check.py" "$D/ctl.txt" > "$D/stale.txt"
rc=$?
while IFS= read -r l; do note "$l"; done < "$D/stale.txt"
STOP_AT_END=1 scene_end "controls stale scene"
exit $rc
