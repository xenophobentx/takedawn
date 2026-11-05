#!/usr/bin/env bash
# g2_drive.sh <rundir> [KEY=VALUE...]
#
# Gate G2 of the performance plan (docs/research/perf-plan-fable.md): one
# race the title's own AI drives (RECOMP_DEBUG=autopilot=1, as
# race_drive.sh), High refresh off, then the results' Continue and the
# RACE RESULTS page's CONTINUE, which writes the autosave. Two builds that
# compute the same give the same result line and the same save files.
#
# Writes <rundir>/g2.txt:
#   result <the autopilot's result line without its flip number>
#   save   <sha256 over the save's files (UDATA/) and their sha256>
#   parts  <the same over the emulated disk's partition images>
# and <rundir>/save.sha (one line a file), so two runs compare with diff.
# The flip numbers are left out: the loads take as many pictures as the
# machine needs, the simulation steps are what must agree.
#
#   RACE_REGION / RACE_TRACK   as race_drive.sh (default 1 / 1: USA, Silver
#                              Lake Southbound)
#   G2_SETTLE=n                flips to wait after the CONTINUE for the
#                              autosave to be written (default 900)
#   SAVES                      the reference saves (scene_lib.sh), the same
#                              for both builds
#   G2_DETSEED=1               RECOMP_DEBUG=detseed=1: the guest's clocks
#                              from fixed values at the start (the toolkit)
#   G2_LOCKSTEP=1              BO3_LOCKSTEP=1 (hirefresh.c): one step an
#                              Update, the random generator held until the race
# <rundir>/stephash.txt: BO3_STEPHASH's line a step (autopilot.c), for the
# step two runs part at (diff, or cmp).
set -u
source "$(dirname "$0")/scene_lib.sh"
dbg="${RECOMP_DEBUG:+$RECOMP_DEBUG,}autopilot=1"
REPO="${REPO:-$(cd "$(dirname "$0")/../.." && pwd)}"
hf="$(cd "$REPO" && mkdir -p "$1" && cd "$1" && pwd)/stephash.txt"
command -v cygpath >/dev/null 2>&1 && hf="$(cygpath -m "$hf")"
scene_start "$@" RECOMP_DEBUG="$dbg${G2_DETSEED:+,detseed=1}" RECOMP_GFX_HIGH_REFRESH=0 \
    BO3_LOCKSTEP="${G2_LOCKSTEP:-0}" BO3_STEPHASH="$hf"
to_main_menu
to_single_event
press a:400 90              # Race: Select Options (Region, Track, Rivals)
for (( k = 0; k < ${RACE_REGION:-1}; k++ )); do press right:200 25; done
press down:200 20
for (( k = 0; k < ${RACE_TRACK:-1}; k++ )); do press right:200 25; done
press a:400 90              # Select Car
wait_log 'CAR1[.]bgv' "car select (CAR1.bgv)"
press a:400 0               # the first car: loading screen
wait_log "$(track_re crash1)" "race begins (tracks crash1.rws)"
grep -aq '\[AUTOPILOT\] on' "$D/log.txt" || die "no '[AUTOPILOT] on' line"
wait_count '\[AUTOPILOT\] engaged' 1
SCENE_TIMEOUT="${RACE_LIMIT:-900}" wait_count '\[AUTOPILOT\] result' 1
line=$(grep -a '\[AUTOPILOT\] result' "$D/events.txt" | head -1)
line="${line#*result }"; line="${line% (flip*}"
note "result: $line"
wait_flip $(( $(anchor '\[AUTOPILOT\] result' 1) + 3000 ))   # the finish's slow motion and fly-by
press a:400 300             # Continue: RACE RESULTS, CONTINUE / RETRY
press a:400 "${G2_SETTLE:-900}"   # CONTINUE: the autosave
gd="${GAME_DIR:-$REPO}"
sd="${RECOMP_SAVE_DIR:-$gd/saves}"
scene_stop
sleep 3
# Every file of the save folder with its sha256, except the run's own
# (logs, the instance lock, crash reports, the timestamped save-history
# copies the port keeps). "save" is the hash of UDATA/ (the title's save:
# the profile, its meta and images); "parts" of the emulated disk's
# partition images (the title's cache partitions and the rest).
shacmd() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$@"; else shasum -a 256 "$@"; fi; }
( cd "$sd" && find . -type f ! -name '*.log' ! -name 'instance.lock' ! -path './logs/*' ! -path './crash/*' \
      ! -path './backup/*' ! -path './save-history/*' -print | LC_ALL=C sort | while IFS= read -r f; do
      printf '%s  %s\n' "$(shacmd "$f" | cut -d' ' -f1)" "$f"; done ) > "$D/save.sha"
hsave=$(grep '  ./UDATA/' "$D/save.sha" | shacmd | cut -d' ' -f1)
hparts=$(grep '  ./Partition' "$D/save.sha" | shacmd | cut -d' ' -f1)
printf 'result %s\nsave %s\nparts %s\n' "$line" "$hsave" "$hparts" > "$D/g2.txt"
rm -rf "$D/save" && cp -R "$sd" "$D/save"
note "g2: $(tr '\n' ' ' < "$D/g2.txt")"
