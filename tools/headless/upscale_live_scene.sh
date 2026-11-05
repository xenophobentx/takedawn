#!/usr/bin/env bash
# upscale_live_scene.sh <rundir> [KEY=VALUE...]
#
# The upscaled menu pictures applied in the running game, with no restart
# (BO-311, src/game/upscale.c h_live, modtex.c bo3_modtex_live), with a window:
#
#   1. Start with the pack made (TAKEDAWN_GENERATED_MODS holds ai-upscaled-art),
#      Burnout 3 World Tour: Crash Nav (the world map and satellite maps are
#      pack textures). Shot live_on1.
#   2. Settings, Mods: "Remove the upscaled menu pictures", close. Shot
#      live_off: the original pictures, in the same process.
#   3. Settings, Mods: "Upscale menu pictures with AI", wait for
#      "Upscaled menu pictures: On." with the settings open, close. Shot
#      live_on2.
#
# Set TAKEDAWN_GENERATED_MODS and TAKEDAWN_UPSCALE_CACHE (the upscaler already
# fetched) in the environment: scratch copies; the pack is deleted and made
# again. TAKEDAWN_UPSCALE_OFFER=on:fast keeps step 3 to about a minute.
set -u
source "$(dirname "$0")/scene_lib.sh"
: "${TAKEDAWN_GENERATED_MODS:?a scratch generated-mods folder with a pack}"
: "${TAKEDAWN_UPSCALE_CACHE:?a scratch upscale cache with the upscaler}"
HOME_DIR="${HOME_DIR:-$(mktemp -d)}"
rd="$(cd "$(dirname "$0")/../.." && mkdir -p "$1" && cd "$1" && pwd)"
ov="$rd/ovtest.txt"; : > "$ov"
WINDOW=1 scene_start "$@" HOME="$HOME_DIR" RECOMP_SETTINGS=none RECOMP_OVERLAY_TEST="$ov" \
    TAKEDAWN_GENERATED_MODS="$TAKEDAWN_GENERATED_MODS" TAKEDAWN_UPSCALE_CACHE="$TAKEDAWN_UPSCALE_CACHE" \
    TAKEDAWN_UPSCALE_OFFER="${TAKEDAWN_UPSCALE_OFFER:-on:fast}" \
    RECOMP_DEBUG="${RECOMP_DEBUG:+$RECOMP_DEBUG,}texpack_log"
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
close_overlay() { ov "pad b"; sleep 1; st | grep -q '^open 1' && { ov "pad b"; sleep 1; }; }
swaps() { grep -ac '\[TEXPACK\] swap' "$D/log.txt"; }

to_main_menu
press a:400 0                   # Burnout 3 World Tour: Crash Nav
wait_flips 300
ov "shot $rd/live_on1.bmp"; sleep 1
s1=$(swaps); note "swaps so far: $s1"
[ "$s1" -gt 0 ] || bad "no pack texture swapped in at the start"

# 2. Remove, live.
open_mods
to_row 'Remove the upscaled menu pictures'
ov "pad a"; sleep 1
st | grep -q 'the original pictures are back' || bad "the row does not say the originals are back"
close_overlay
wait_flips 120
ov "shot $rd/live_off.bmp"; sleep 1
grep -aq '\[MOD\] takedawn.ai-upscaled-art off now' "$D/log.txt" || bad "no live 'off' line"

# 3. Make it again, live.
open_mods
to_row 'Upscale menu pictures with AI'
ov "pad a"
k=0
until st | grep -q 'Upscaled menu pictures: On. You can turn them off in the mod list'; do
    sleep 1; k=$((k + 1)); [ $k -gt 900 ] && { bad "not made"; break; }
done
st | grep 'menu pictures' | head -n 2
ov "shot $rd/live_done_row.bmp"; sleep 1
close_overlay
wait_flips 240
ov "shot $rd/live_on2.bmp"; sleep 1
s2=$(swaps); note "swaps now: $s2"
[ "$s2" -gt "$s1" ] || bad "nothing swapped in again after the live apply"
grep -aq '\[MOD\] takedawn.ai-upscaled-art on now' "$D/log.txt" || bad "no live 'on' line"
scene_end "upscale live"
[ $fail = 0 ] && note "PASS" || note "FAILED"
exit $fail
