#!/usr/bin/env bash
# race_drive.sh <rundir> [KEY=VALUE...]
#
# A race the title's own AI drives for player 1 (RECOMP_DEBUG=autopilot=1,
# src/game/autopilot.c, BO-130): Single Event > Race, region USA, track
# Silver Lake Southbound (as to_race.sh), the first car. From the GO the
# AI has the car; the run waits for the finish and the results and writes
# <rundir>/result.txt with the placing and the race time the autopilot logs.
# Shots (CAPTURE=1): race_results.
#
# The same variables as every scene (scene_lib.sh): CAPTURE, SAVES,
# SCENE_TIMEOUT, STOP_AT_END, LAUNCH, GAME_PATH_PREFIX, ... On Android it
# runs through tools/android/scene-launch.sh like the other scenes (LAUNCH
# and GAME_PATH_PREFIX from that script's header); RECOMP_DEBUG goes to the
# app with the run's other RECOMP_* variables.
#
#   RACE_REGION=r RACE_TRACK=t
#                   the Region and Track rows' right presses (default 1 and 1:
#                   USA, Silver Lake Southbound); with BO3_UNLOCK_ALL=1 every
#                   region and track (BO-208: Europe is 2, its 15th is 2/15)
#   RACES=n         race the same race n times (default 1): Continue on the
#                   results, Retry, again; a soak (PICK=random: see below)
#   RACE_STOP=file  with RACES: stop after the race in which the file appears
#   RACE_LIMIT=s    the longest a race may take, GO to results (default 900;
#                   the AI is no racer: 3 laps of Silver Lake took 395 s)
#   PICK=random     a random soak: Region and Track stay on the menu's own
#                   "Random" (no right presses), and between races the driver
#                   goes back to the event menu instead of Retry, so every
#                   race rolls a new track. The track of each race (the
#                   streamed.dat the title opened, tracks\<region>\<track>)
#                   goes to result.txt and tracks.txt. Default (unset): the
#                   fixed Silver Lake and Retry, as before.
#   MIX=1           during each race, press Start (the pause menu, then
#                   Start again) and open and close the settings overlay
#                   (RECOMP_OVERLAY=file=, the scripted form of F1 or
#                   Back+Start) a few times; MIX_PLAN="flips:pause
#                   flips:overlay ..." sets when, in flips after the GO.
#                   The flips of each go to mix.txt in the run directory.
#   SAVE_AT_END=1   after the last race, Continue and the RACE RESULTS page's
#                   CONTINUE (the title's autosave): waits for the title to
#                   open Profile 1 to write it, then 300 flips for the save
#                   guard to put it in place ("[SAVE] Profile 1: replaced" in
#                   log.txt). BO-323: an imported profile saved back.
#   EVENTS="race roadrage"
#                   with PICK=random: the event types, taken in turn, race n
#                   being word ((n-1) mod count). race = Single Event > Race;
#                   roadrage = Road Rage with its default options.
set -u
source "$(dirname "$0")/scene_lib.sh"
dbg="${RECOMP_DEBUG:+$RECOMP_DEBUG,}autopilot=1"
ovl=()
if [ -n "${MIX:-}" ]; then
    # The settings overlay opens when this file appears (RECOMP_OVERLAY=file=,
    # the toolkit's scripted form of F1 / Back+Start, which the scripted pad
    # does not reach).
    rr="${REPO:-$(cd "$(dirname "$0")/../.." && pwd)}"
    of="$(cd "$rr" && mkdir -p "$1" && cd "$1" && pwd)/overlay_toggle"
    command -v cygpath >/dev/null 2>&1 && of=$(cygpath -m "$of")
    ovl=(RECOMP_OVERLAY="file=$of")
fi
scene_start "$@" RECOMP_DEBUG="$dbg" ${ovl[@]+"${ovl[@]}"}
to_main_menu
to_single_event
read -r -a evs <<< "${EVENTS:-race}"
if [ -z "${PICK:-}" ]; then
    press a:400 90              # Race: Select Options (Region, Track, Rivals)
    for (( k = 0; k < ${RACE_REGION:-1}; k++ )); do press right:200 25; done   # Region: Random > USA
    press down:200 20
    for (( k = 0; k < ${RACE_TRACK:-1}; k++ )); do press right:200 25; done    # Track: Random > Silver Lake Southbound
    press a:400 90              # Select Car
else
    [ "$PICK" = random ] || die "PICK=$PICK: only PICK=random"
    # Region and Track stay on the menu's "Random" (greyed Track: the
    # region's own random pick). Through the Race's options to its Select
    # Car; select_event below moves to the first of EVENTS from there.
    press a:400 90              # Race: Select Options
    press a:400 90              # Select Car
fi
wait_log 'CAR1[.]bgv' "car select (CAR1.bgv)"
# select_event <name>: from any event's Select Car (where the A-loop below
# lands: Select Mode opens on the last event's line, and Race and Road Rage
# are both reached by A from it), Back twice to Select Mode (Select Car >
# Options > Select Mode, the cursor where it was), up three times (the
# list does not wrap: that is Race from any line), then the event: race, A
# (its options), A (Select Car); roadrage, down down, A, A.
select_event() {
    case "$1" in race|roadrage) ;; *) die "EVENTS: unknown event $1 (race, roadrage)" ;; esac
    press b:300 60
    press b:300 60
    press up:200 20
    press up:200 20
    press up:200 20
    [ "$1" = roadrage ] && { press down:200 20; press down:200 20; }
    press a:400 90
    press a:400 90
}
[ -n "${PICK:-}" ] && select_event "${evs[0]}"
press a:400 0               # the first car: loading screen
wait_log "$(track_re crash1)" "race begins (tracks crash1.rws)"
grep -aq '\[AUTOPILOT\] on' "$D/log.txt" || die "no '[AUTOPILOT] on' line: a build without BO-130?"

# One race, or a soak of RACES races: after each result, the results
# screen's Continue, then RETRY under CONTINUE on the RACE RESULTS page
# after it, YES to "Are you sure you want to retry?", and the same race
# again. One line per race in result.txt; RACE_STOP=<file>: stop after the
# race in which that file appears.
races="${RACES:-1}"
: > "$D/result.txt"
: > "$D/mix.txt"
rm -f "$D/overlay_toggle"

# mix_race <n>: MIX=1, while race n is on: the pause menu (Start, Start) and
# the settings overlay (the toggle file) at flips after the GO, a few times a
# race (MIX_PLAN="flips:pause flips:overlay ...", default below). Each
# action's flips go to mix.txt (what, first flip, last flip) so that a
# frame-time summary can leave them out.
mix_race() {
    local n=$1 e item off act f0 t
    e=$(anchor '\[AUTOPILOT\] engaged' "$n")
    for item in ${MIX_PLAN:-1500:pause 4000:overlay 8000:pause 12000:overlay 16000:pause}; do
        off=${item%%:*}; act=${item#*:}
        t=$(date +%s)
        until [ "$(flip)" -ge $(( e + off )) ] || [ "$(count '\[AUTOPILOT\] result')" -ge "$n" ]; do
            alive
            [ $(( $(date +%s) - t )) -gt "${RACE_LIMIT:-900}" ] && die "mix: flip $(( e + off )) not reached (at $(flip))"
            sleep 0.5
        done
        [ "$(count '\[AUTOPILOT\] result')" -ge "$n" ] && return 0
        f0=$(flip)
        case "$act" in
        pause)
            press start:300 120     # the PAUSED menu
            press start:300 60      # resume
            ;;
        overlay)
            : > "$D/overlay_toggle"     # open
            t=$(date +%s)
            while [ -e "$D/overlay_toggle" ] && [ $(( $(date +%s) - t )) -lt 15 ]; do sleep 0.25; done
            sleep 4
            : > "$D/overlay_toggle"     # close
            t=$(date +%s)
            while [ -e "$D/overlay_toggle" ] && [ $(( $(date +%s) - t )) -lt 15 ]; do sleep 0.25; done
            wait_flips 60
            ;;
        esac
        echo "$act $f0 $(flip)" >> "$D/mix.txt"
        note "mix: $act (flips $f0 to $(flip))"
    done
}
n=0
while :; do
    n=$((n + 1))
    wait_count '\[AUTOPILOT\] engaged' "$n"
    [ -n "${MIX:-}" ] && mix_race "$n"
    SCENE_TIMEOUT="${RACE_LIMIT:-900}" wait_count '\[AUTOPILOT\] result' "$n"
    line=$(grep -a '\[AUTOPILOT\] result' "$D/events.txt" | sed -n "${n}p")
    ev=""
    if [ -n "${PICK:-}" ]; then
        # The track: the streamed.dat the title opened last before this
        # race's "engaged" (tracks\<region>\<track>), and the event.
        tr=$(awk -v n="$n" '/\[AUTOPILOT\] engaged/ { if (++k == n) { print t; exit } }
            /streamed[.]dat/ { t = $0 }' "$D/events.txt" | grep -aoE '[Tt]racks[\\/][^ ]*streamed[.]dat' | sed 's/[Tt]racks.//; s/.streamed.dat//; s/\\/\//g')
        ev=" [${evs[$(( (n - 1) % ${#evs[@]} ))]} ${tr:-?}]"
        echo "${tr:-?}" >> "$D/tracks.txt"
    fi
    echo "race $n$ev: ${line#*result }" >> "$D/result.txt"
    note "race $n$ev: ${line#*result }"
    # Shot (CAPTURE=1): race_results (race_results_<n> after the first),
    # 3000 flips after the line (the finish's slow motion and fly-by first).
    SHOT_WINDOW=0 shot "race_results$([ "$n" -gt 1 ] && echo "_$n")" \
        "$(anchor '\[AUTOPILOT\] result' "$n")" 3000 0
    [ "$n" -ge "$races" ] && break
    [ -n "${RACE_STOP:-}" ] && [ -e "$RACE_STOP" ] && { note "RACE_STOP: $RACE_STOP"; break; }
    if [ -n "${PICK:-}" ]; then
        # Back to the event menu, not Retry: Continue, RACE RESULTS >
        # CONTINUE (the autosave), then A through the result screens (their
        # number depends on the race: points, takedowns, a won car, a tip)
        # until the Select Car of the next race, which loads CAR1.bgv again.
        mark=$(( $(wc -l < "$D/events.txt") + 1 ))
        press a:400 300     # Continue: RACE RESULTS, CONTINUE / RETRY
        press a:400 300     # CONTINUE: the autosave
        k=0
        until tail -n +"$mark" "$D/events.txt" | grep -aq 'CAR1[.]bgv'; do
            k=$((k + 1)); [ "$k" -gt 40 ] && die "no Select Car after $k presses (a result screen that wants another button?)"
            press a:400 150
        done
        note "event menu: Select Car after $k presses"
        select_event "${evs[$(( n % ${#evs[@]} ))]}"
        SHOT_WINDOW=0 shot "next_$n" "$(flip)" 20 0
        press a:400 0
        continue
    fi
    press a:400 300         # Continue: RACE RESULTS, CONTINUE / RETRY
    press down:200 40       # RETRY
    press a:400 180         # "Are you sure you want to retry?", on NO
    press up:300 60         # YES
    SHOT_WINDOW=0 shot "retry_$n" "$(flip)" 20 0
    press a:400 0
done
if [ "${SAVE_AT_END:-0}" = 1 ]; then
    press a:400 300         # Continue: RACE RESULTS, CONTINUE / RETRY
    press a:400 300         # CONTINUE: the autosave
    wait_log 'Profile 1 [(]disp [^1]' "Profile 1 opened for writing"
    wait_flips 300
fi
scene_end "autopilot: $n race(s)"
