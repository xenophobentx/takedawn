#!/usr/bin/env bash
# upscale_row_scene.sh <rundir> [KEY=VALUE...]
#
# The Mods page's "Upscale menu pictures" rows (BO-311, BO-327; src/game/upscale.c,
# docs/upscale-art.md) with a window (the overlay needs one), driven through
# RECOMP_OVERLAY_TEST:
#
#   1. With a pack made (TAKEDAWN_GENERATED_MODS holds ai-upscaled-art): the
#      rows say "Upscale menu pictures again (made: ...)" and "Remove the
#      upscaled menu pictures". Shot overlay_made.
#   2. Remove: the pack folder is gone, the first row offers it again.
#   3. Start: the row turns into "Stop upscaling menu pictures (N%: ...)"; a
#      press stops it: "last try: stopped". Shots overlay_running,
#      overlay_stopped.
#
# Set TAKEDAWN_GENERATED_MODS and TAKEDAWN_UPSCALE_CACHE (with the upscaler
# already fetched, so step 3 downloads nothing) in the environment: a scratch
# copy, the pack is deleted. A scratch HOME and no settings file.
set -u
source "$(dirname "$0")/scene_lib.sh"
: "${TAKEDAWN_GENERATED_MODS:?a scratch generated-mods folder with a pack}"
: "${TAKEDAWN_UPSCALE_CACHE:?a scratch upscale cache with the upscaler}"
HOME_DIR="${HOME_DIR:-$(mktemp -d)}"
rd="$(cd "$(dirname "$0")/../.." && mkdir -p "$1" && cd "$1" && pwd)"
ov="$rd/ovtest.txt"; : > "$ov"
WINDOW=1 scene_start "$@" HOME="$HOME_DIR" RECOMP_SETTINGS=none RECOMP_OVERLAY_TEST="$ov" \
    TAKEDAWN_GENERATED_MODS="$TAKEDAWN_GENERATED_MODS" TAKEDAWN_UPSCALE_CACHE="$TAKEDAWN_UPSCALE_CACHE"
fail=0
bad() { note "FAIL: $*"; fail=1; }
ov() { echo "$1" >> "$ov"; sleep 0.4; }
st() {
    local k=0
    rm -f "$rd/ovstate.txt"
    echo "state $rd/ovstate.txt" >> "$ov"
    until [ -s "$rd/ovstate.txt" ] || [ $k -gt 100 ]; do sleep 0.1; k=$((k + 1)); done
    cat "$rd/ovstate.txt" 2>/dev/null
}
row_of() { st | grep -v '^\(open\|page\|offer\|paused\) ' | grep -n "$1" | head -n 1 | cut -d: -f1; }
sel_row() { st | grep -v '^\(open\|page\|offer\|paused\) ' | grep -n '^>' | head -n 1 | cut -d: -f1; }
to_row() {
    local want cur k=0; want=$(row_of "$1"); [ -n "$want" ] || { bad "no row '$1'"; return 1; }
    cur=$(sel_row)
    while [ "${cur:-0}" != "$want" ]; do
        [ "${cur:-0}" -lt "$want" ] && ov "pad down" || ov "pad up"
        cur=$(sel_row); k=$((k + 1))
        [ $k -gt 60 ] && { bad "could not reach row '$1'"; return 1; }
    done
}
open_mods() {
    ov "pad back+start"; sleep 1
    local k=0
    until st | grep -q '^page Mods'; do ov "pad rb"; k=$((k + 1)); [ $k -gt 7 ] && { bad "no Mods page"; return 1; }; done
}

to_main_menu
pack="$TAKEDAWN_GENERATED_MODS/ai-upscaled-art"
grep -aq '\[TEXPACK\] pack takedawn.ai-upscaled-art' "$D/log.txt" || bad "the pack was not loaded at the start"

# 1. Made.
open_mods
st | grep -q 'Upscale menu pictures again (made: ' || bad "no 'again' row"
st | grep -q 'Remove the upscaled menu pictures' || bad "no 'Remove' row"
to_row 'Upscale menu pictures again'
ov "shot $rd/overlay_made.bmp"; sleep 1

# 2. Remove.
to_row 'Remove the upscaled menu pictures'
ov "pad a"; sleep 1
[ -d "$pack" ] && bad "the pack is still there"
st | grep -q 'Upscale menu pictures with AI' || bad "no offer after removing"
st | grep -q 'Remove the upscaled' && bad "the Remove row stayed"

# 3. Start, then stop.
to_row 'Upscale menu pictures with AI'
ov "pad a"
k=0
until st | grep -q 'Stop upscaling menu pictures ([1-9]'; do
    sleep 0.5; k=$((k + 1)); [ $k -gt 120 ] && { bad "no progress in the row"; break; }
done
st | grep 'Stop upscaling' | head -n 1
ov "shot $rd/overlay_running.bmp"; sleep 1
to_row 'Stop upscaling menu pictures'
ov "pad a"
k=0
until st | grep -q 'last try: stopped'; do
    sleep 0.5; k=$((k + 1)); [ $k -gt 60 ] && { bad "not stopped"; break; }
done
ov "shot $rd/overlay_stopped.bmp"; sleep 1
st | grep 'Upscale menu pictures' | head -n 1
ov "pad b"; sleep 1
scene_end "upscale rows"
[ $fail = 0 ] && note "PASS" || note "FAILED"
exit $fail
