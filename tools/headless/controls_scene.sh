#!/usr/bin/env bash
# controls_scene.sh <rundir> [KEY=VALUE...]
#
# BO-106: input.control_scheme. The PS2 driving layout holds only while the
# title takes driving input; the front end, the pause menu, the event intro
# and the results read the pad as always. A pad script stands in for a
# controller (BO3_CTL_SCRIPTED=1; without it a scripted pad keeps the Xbox
# layout whatever the setting), and the title's own state and what it read
# are logged every frame (BO3_CTL_LOG, BO3_DRIVE_LOG; src/game/controls.c).
#
#   SCHEME=ps2 (default) | xbox    the layout under test
#   EVENT=race (default) | crash   Single Event > Race, or > Crash (Trailer
#                                  Trash): its captions, crash and results
#   FACE=nintendo                  input.face_layout = nintendo as well. The
#                                  script is a controller here, so its
#                                  buttons are positions: "a" is the bottom
#                                  button (the title's B), "b" the right one
#                                  (the title's A). The menus are then driven
#                                  with the right-hand button, and in the PS2
#                                  layout the bottom one must still accelerate
#   SCRIPTED=0                     the script is a script again, not a
#                                  controller: with SCHEME=ps2 the run must
#                                  then be the Xbox layout throughout
#
# The same presses in both schemes: "a" is the PS2 accelerator and the Xbox
# boost, "rt" the Xbox accelerator and the PS2 crashbreaker.
#
#   race:   a (held 4 s)  a+start (pause with the button still down)
#           a (Resume)  rt  start (pause)  down x4, a, up, a (Quit > Yes)
#   crash:  a (leave the captions)  the scheme's accelerator (a or rt, held
#           to the junction)  then the results: a (Continue)
#
# PASS/FAIL per check in <rundir>/scene.txt and controls.txt; exit 1 on a
# failure.
set -u
source "$(dirname "$0")/scene_lib.sh"
rd="$1"; shift
SCHEME="${SCHEME:-ps2}"; EVENT="${EVENT:-race}"
abs="$(cd "$(dirname "$0")/../.." && mkdir -p "$rd" && cd "$rd" && pwd)"
AS="$SCHEME"; [ "${SCRIPTED:-1}" = 0 ] && AS=xbox      # what the checks expect
FACE="${FACE:-xbox}"
# With SCRIPTED=0 no face layout reaches the script either: its "a" is the
# title's A whatever the setting.
MIRROR=0; [ "$FACE" = nintendo ] && [ "${SCRIPTED:-1}" != 0 ] && MIRROR=1
scene_start "$rd" BO3_CTL_LOG="$abs/ctl.txt" BO3_DRIVE_LOG="$abs/drive.txt" \
    BO3_CTL_SCRIPTED="${SCRIPTED:-1}" RECOMP_INPUT_CONTROL_SCHEME="$SCHEME" \
    RECOMP_INPUT_FACE_LAYOUT="$FACE" "$@"
# press names the title's buttons (scene_lib.sh's menus do): on a pad with
# the Nintendo layout the title's A is the right-hand button, which a script
# calls "b". pos_press names a position and is passed as it is.
eval "pos_press() $(declare -f press | tail -n +2)"
press() {
    local b="$1"
    if [ "$MIRROR" = 1 ]; then
        b=$(printf '%s' "$b" | awk -F: -v OFS=: '{ n = split($1, k, "+"); o = ""
            for (i = 1; i <= n; i++) { t = k[i]
                if (t == "a") t = "b"; else if (t == "b") t = "a"
                else if (t == "x") t = "y"; else if (t == "y") t = "x"
                o = o (i > 1 ? "+" : "") t }
            $1 = o; print }')
    fi
    pos_press "$b" "${2:-45}"
}
# wait_ctl <regex> <n> <what>: until BO3_CTL_LOG has n lines matching.
wait_ctl() {
    local t=$(date +%s)
    until [ "$(grep -cE "$1" "$D/ctl.txt" 2>/dev/null || true)" -ge "$2" ]; do
        alive
        [ $(( $(date +%s) - t )) -gt "$SCENE_TIMEOUT" ] && die "no '$3' in the controls log"
        sleep 0.2
    done
    note "ctl: $3 (flip $(flip))"
}
to_main_menu
to_single_event
if [ "$EVENT" = crash ]; then
    press down:200 20; press down:200 20; press down:200 20     # Crash
    press a:400 90              # Junction Selection
    press down:200 20           # 1. Trailer Trash
    press a:400 90              # Select Car
    wait_log 'CAR1[.]bgv' "car select (CAR1.bgv)"
    press a:400 0
    wait_log 'streamed[.]dat' "junction streamed in"
    wait_log "$(track_re crash2)" "captions (crash2.rws)"
    b=$(anchor "$(track_re crash2)")
    shot ctl_captions "$b" 200
    wait_flip $(( b + $(flips 300) ))
    press a:400 60              # Continue: the countdown
    acc=a; [ "$AS" = xbox ] && acc=rt
    pos_press "$acc:14000" 0    # the scheme's accelerator, to the junction
    wait_log 'crash1[.]rws' "impact (crash1.rws)"
    c=$(anchor 'crash1[.]rws')
    shot ctl_crash "$c" 60
    wait_ctl 'p1_results 00000000 -> 00000001' 1 "results"
    wait_flips 300
    shot ctl_results "$(flip)" 20
    press a:300 0               # Continue: back to the front end
    wait_ctl 'module_vt 003A9ACC -> 003A9E7C' 1 "front end"
    wait_flips 120
    shot ctl_after "$(flip)" 20
else
    press a:400 90              # Race: Select Options
    press right:200 25
    press down:200 20
    press right:200 25
    press a:400 90              # Select Car
    wait_log 'CAR1[.]bgv' "car select (CAR1.bgv)"
    press a:400 0
    wait_log 'streamed[.]dat' "track streamed in"
    wait_log "$(track_re crash1)" "race begins (tracks crash1.rws)"
    a=$(anchor "$(track_re crash1)")
    wait_flip $(( a + 300 ))    # the GO
    # The presses queue up and fire one after another (200 ms apart), so the
    # waits are on the title's own state in the log, not on the press.
    # The bottom button: the PS2 accelerator. (Xbox scheme: the title's A,
    # the boost, wherever the face layout puts it.)
    drive=pos_press; [ "$AS" = xbox ] && drive=press
    $drive a:4000 0
    $drive a+start:600 0        # pause with the button still down
    wait_ctl 'paused_by FFFFFFFF -> 00000000' 1 "paused"
    wait_flips 150              # the pause must still be up
    shot ctl_pause "$(flip)" 20
    press a:300 0               # Resume
    wait_ctl 'paused_by 00000000 -> FFFFFFFF' 1 "resumed"
    shot ctl_resumed "$(flip)" 20
    press rt:2500 0
    press start:200 0           # pause
    wait_ctl 'paused_by FFFFFFFF -> 00000000' 2 "paused again"
    wait_flips 60
    for p in down:150 down:150 down:150 down:150 a:300; do press "$p" 0; done
    wait_flips 240              # Quit: "are you sure", the cursor on No
    press up:150 0
    press a:300 0               # Yes: the front end
    wait_ctl 'module_vt 003A9ACC -> 003A9E7C' 1 "front end"
    wait_flips 120
    shot ctl_frontend "$(flip)" 20
fi
python3 "$(dirname "$0")/controls_check.py" "$AS" "$EVENT" "$D/ctl.txt" "$D/drive.txt" "$([ "$MIRROR" = 1 ] && echo nintendo || echo xbox)" > "$D/controls.txt"
rc=$?
while IFS= read -r l; do note "$l"; done < "$D/controls.txt"
STOP_AT_END=1 scene_end "controls scene ($SCHEME, $EVENT, face $FACE)"
exit $rc
