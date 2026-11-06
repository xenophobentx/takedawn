#!/usr/bin/env bash
# to_crash.sh <rundir> [KEY=VALUE...]
#
# Reference scene "crash" and "crash cam": Single Event > Crash, junction
# 1 (Trailer Trash, at Silver Lake: C3_V1), the first car. The Crash
# Training video plays first (once a launch, unskippable while profiles do
# not save), then the junction's intro flyby, the captions, the countdown;
# the accelerator is held from the countdown, the car hits the trailers,
# the crash cam ("Impact Time") follows, then Crash Results. Leaves the game
# running at Crash Results (pid in <rundir>/pid), or STOP_AT_END=1.
#
# Shots (CAPTURE=1): crash_intro, crash_caption, crash_cam, crash_results.
#
# The route is the one BO-25's crash.sh and the render agent's to_crash.sh
# took by the clock; here every step waits on the log.
set -u
source "$(dirname "$0")/scene_lib.sh"
scene_start "$@"
to_main_menu
to_single_event
press down:200 20; press down:200 20; press down:200 20     # Crash
press a:400 90              # Junction Selection (Random, 1. Trailer Trash, ...)
press down:200 20           # 1. Trailer Trash
press a:400 90              # Select Car
wait_log 'CAR1[.]bgv' "car select (CAR1.bgv)"
press a:400 0               # the first car: Crash Training video, loading
wait_log 'TrC_ENG[.]xmv' "Crash Training video (TrC_ENG.xmv)"
wait_log 'C3_V1.streamed[.]dat' "junction streamed in (C3_V1 streamed.dat)"
shot crash_intro "$(anchor 'C3_V1.streamed[.]dat')" 240     # the intro flyby
# The flyby ends (C3_v1\crash3.rws), a short load, then crash2.rws and the
# captions ("Create traffic carnage..."), which wait for A.
wait_log "$(track_re crash2)" "captions (crash2.rws)"
b=$(anchor "$(track_re crash2)")
shot crash_caption "$b" 200
wait_flip $(( b + $(flips 300) ))
press a:400 60              # the countdown, 3-2-1
press rt:600000 0           # held from the countdown: the start is the GO
wait_log 'C3_v1.crash1[.]rws' "impact (C3_v1 crash1.rws)"
c=$(anchor 'C3_v1.crash1[.]rws')
# Offsets into the camera are in the title's 30 Hz flips (cam_flips: twice
# as many flips with graphics.crash_60fps on).
shot crash_cam "$c" "$(cam_flips 60)"       # Impact Time, the slow-motion crash cam
shot crash_results "$c" "$(cam_flips 900)"
scene_end "at Crash Results"
