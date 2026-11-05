# scene_lib.sh: sourced by the reference-scene drivers (to_menu.sh,
# to_race.sh, to_crash.sh, to_garage.sh, to_intro.sh).
#
# A driver starts the game headless, reads its log, and presses buttons
# through RECOMP_PAD_LIVE. Every wait is on the log: a file the title opens,
# the runtime's line for a press it took, or a number of flips from the
# runtime's RECOMP_FLIP_MARK line ("[GPU] flip N T ms", one a flip). The
# title steps its menus and its simulation once a frame, so a wait counted
# in flips holds at any frame rate: the CPU rasteriser and the Deck under
# Proton wait the same number of frames as Metal at 60. (Below 60 fps the
# title can take more than one simulation step in a frame, so gameplay after
# the player's first input is not frame-exact across backends; the menus
# and everything before the GO are.) Wall time only bounds a wait
# (SCENE_TIMEOUT seconds) so a stuck run fails instead of hanging.
#
# Environment (all optional):
#   BIN=path            the executable (default ./bin/burnout3), run from the
#                       repo folder with --headless
#   LAUNCH='cmd ...'    the whole launch command instead, run with bash -c in
#                       GAME_DIR; it inherits the RECOMP_* variables. On the
#                       Deck deck_env.sh sets it (umu-run, Proton)
#   GAME_DIR=dir        where LAUNCH runs (default: the repo folder)
#   GAME_PATH_PREFIX=   prefix for the paths handed to the game: Z: under
#                       Proton (Wine maps Z: to /), empty on the Mac
#   SAVES=dir           the reference saves, copied to <game dir>/saves before
#                       every run (default <repo>/saves, or the main
#                       checkout's when this is a worktree;
#                       none: use what is there; empty: no saves at all, the
#                       first-boot screens). Never copied onto itself.
#   CAPTURE=1           golden frames: shot() dumps frames (adds
#                       RECOMP_D3D11_DUMP for the D3D11 backend)
#   SCENE_TIMEOUT=s     the longest any single wait may take (default 900)
#   SCENE_FLIPS=log     count flips by RECOMP_FLIP_LOG, for a build without
#                       RECOMP_FLIP_MARK (toolkit before bo3/scenes)
#   SCENE_RATE=Hz       the flips a second the waits are counted for (see
#                       flips below; default 60, or with graphics.native_rate
#                       the rate of RECOMP_FLIP_PACING=host:<Hz>)
#   STOP_AT_END=1       stop the game when the driver is done
#   WINDOW=1            run in a window instead of headless (frame-time and
#                       on-screen checks; the drivers work the same)
#   RECOMP_RENDER_SHADER_CACHE=dir   the shader cache of the run (default
#                       <game dir>/build/shader-cache, kept between runs;
#                       a fresh directory makes a cold run, none no cache)
#
# The saves: the profile flow depends on what is on the emulated disk (one
# profile, Profile 1, on the reference saves). The game reads saves/ in its
# working folder and writes to it while it runs (the cache partitions), so
# each run starts from a fresh copy of the reference.

SCENE_TIMEOUT="${SCENE_TIMEOUT:-900}"

# scene_default_saves: where the reference saves are when SAVES is not set:
# <repo>/saves, or the main checkout's saves/ when this repo is a linked
# worktree (saves/ and runs/ are git-ignored, so a worktree has none).
scene_default_saves() {
    local r="${REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}" m
    if [ ! -d "$r/saves" ]; then
        m="$(git -C "$r" worktree list --porcelain 2>/dev/null | sed -n '1s/^worktree //p')"
        [ -n "$m" ] && [ -d "$m/saves" ] && { echo "$m/saves"; return; }
    fi
    echo "$r/saves"
}

# cam_flips <n>: <n> flips of a crash camera as the title ships it (30 a
# second, two simulation steps each), as flips of this run: twice as many
# with graphics.crash_60fps on (RECOMP_GFX_CRASH_60FPS=1 in the environment
# or after the run directory; BO-111, docs/crash60.md), where a flip is one
# step. A wait counted in flips holds at any frame rate only while the title
# takes the same steps per flip; inside a crash camera the switch halves
# them, so "900 flips after the impact" is 30 s of the game without it and
# 15 s with it. Offsets into a camera go through this. A settings file that
# turns the switch on is not seen: give the variable.
cam_flips() {
    if [ "${SCENE_CRASH60:-${RECOMP_GFX_CRASH_60FPS:-0}}" = 1 ] || [ "$(scene_rate)" != 60 ]; then
        echo $(( $1 * 2 )); else echo "$1"; fi
}

# scene_rate: the flips a second of the run's menus, races and loads. 60 as
# the title ships them; with graphics.native_rate (the "[SIMRATE] native
# frame rate" line at boot) the display's rate, which headless is
# RECOMP_FLIP_PACING=host:<Hz>: a picture is a step there, menus and loads
# included, so the same wait in seconds takes N/60 times the flips.
# SCENE_RATE=<Hz> sets it. Decided once, after the first flip, by
# scene_rate_set, which the waits below call in the driver's own shell (a
# $(...) is a subshell, where the decision would be lost).
scene_rate_set() {
    [ -n "${SCENE_RATE:-}" ] && return
    [ "$(flip)" -gt 0 ] || return
    local hz=60 p
    if grep -aq '\[SIMRATE\] native frame rate' "$D/events.txt" 2>/dev/null; then
        p=$(sed -n 's/^RECOMP_FLIP_PACING=host:\([0-9]*\).*/\1/p' "$D/env.txt" 2>/dev/null)
        [ -n "$p" ] && [ "$p" -gt 61 ] && hz=$p
    fi
    SCENE_RATE=$hz
    [ "$hz" != 60 ] && note "native frame rate: waits counted for $hz flips a second"
    return 0
}
scene_rate() { echo "${SCENE_RATE:-60}"; }

# flips <n>: n flips as the title ships them (60 a second), as flips of this
# run (scene_rate). press, press_until, wait_flips and shot's offset go
# through it; a driver's own wait_flip $(( a + n )) gives $(flips n).
flips() { echo $(( ($1 * $(scene_rate) + 30) / 60 )); }

# scene_start <rundir> [KEY=VALUE...]: start the game, return at once.
scene_start() {
    REPO="${REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
    D="$(cd "$REPO" && mkdir -p "$1" && cd "$1" && pwd)"; shift
    rm -f "$D"/log.txt "$D"/out.txt "$D"/pid "$D"/dumpat.txt "$D"/fb_*.bmp \
          "$D"/scene.txt "$D"/events.txt "$D"/tail.pid "$D"/shots.tsv
    rm -rf "$D/shots"
    : > "$D/pad.txt"
    PADN=0
    local kv0
    for kv0 in "$@"; do
        case "$kv0" in RECOMP_GFX_CRASH_60FPS=*) SCENE_CRASH60="${kv0#*=}" ;; esac
    done
    # A fresh copy of the reference saves for every run (see the top).
    local saves="${SAVES:-$(scene_default_saves)}" gd="${GAME_DIR:-$REPO}"
    if [ "$saves" = empty ]; then
        # No profile at all: the title's first-boot screens (to_first_race.sh).
        rm -rf "$gd/saves" && mkdir -p "$gd/saves"
    elif [ "$saves" != none ] && [ -d "$saves" ]; then
        if [ "$(cd "$saves" && pwd -P)" != "$(cd "$gd" && mkdir -p saves && cd saves && pwd -P)" ]; then
            rm -rf "$gd/saves" && cp -R "$saves" "$gd/saves"
        fi
    fi
    local gp="${GAME_PATH_PREFIX:-}"
    local cap=()
    if [ "${CAPTURE:-0}" = 1 ]; then
        # fb_flip_NNNNN.bmp: the flips dumpat.txt asks for (Metal and the
        # CPU rasteriser from guest memory, D3D11 from its back buffer).
        # fb_NNN.bmp / fb_NNNN.bmp: the periodic dumps that come with
        # either variable, removed at the end.
        cap=(RECOMP_FB_DUMP="$gp$D/fb_" RECOMP_FB_DUMP_AT="@$gp$D/dumpat.txt"
             RECOMP_D3D11_DUMP="$gp$D/fb_")
    fi
    (
        cd "${GAME_DIR:-$REPO}" || exit 1
        export RECOMP_PB_BACKEND="${RECOMP_PB_BACKEND:-metal}" RECOMP_HEADLESS=1 \
               RECOMP_PAD_LIVE="$gp$D/pad.txt" RECOMP_STDIO_LOG="$gp$D/log.txt" \
               RECOMP_FLIP_MARK=1 \
               RECOMP_SAVE_DIR="${RECOMP_SAVE_DIR:-$PWD/saves}" \
               RECOMP_RENDER_SHADER_CACHE="${RECOMP_RENDER_SHADER_CACHE:-$PWD/build/shader-cache}"
        # Never the per-user save folder, nor the per-user shader cache: a
        # test run must not write the player's key list (BO-93).
        # A build from before flipmark: the full flip log has the same head.
        [ "${SCENE_FLIPS:-mark}" = log ] && export RECOMP_FLIP_LOG=1
        for kv in ${cap[@]+"${cap[@]}"} "$@"; do export "$kv"; done
        env | grep -E '^(RECOMP_|DXVK|PROTON|WINE)' | sort > "$D/env.txt"
        if [ -n "${LAUNCH:-}" ]; then
            exec bash -c "$LAUNCH" > "$D/out.txt" 2>&1
        elif [ "${WINDOW:-0}" = 1 ]; then
            unset RECOMP_HEADLESS
            exec "${BIN:-./bin/burnout3}" > "$D/out.txt" 2>&1
        else
            exec "${BIN:-./bin/burnout3}" --headless > "$D/out.txt" 2>&1
        fi
    ) &
    echo $! > "$D/pid"; disown 2>/dev/null || true
    T0=$(date +%s)
    # The lines the waits read, filtered out of the log as it grows: a
    # crash run's log passes 50 MB (the NV2A lines), and grepping all of it
    # several times a second would load the machine being measured.
    local gpid; gpid=$(cat "$D/pid")
    : > "$D/events.txt"
    (
        ( tail -n +1 -F "$D/log.txt" 2>/dev/null & echo $! > "$D/tail.pid"; wait ) \
            | grep -a --line-buffered -E \
            '\[GPU\] flip [0-9]+ |\[FILE\]|PAD: live "|dumped |fb_dump_at|\[AUTOPILOT\]|\[AUDIO-WAV\] t=|\[SIMRATE\] native' \
            >> "$D/events.txt" &
        while kill -0 "$gpid" 2>/dev/null; do sleep 2; done
        sleep 2; kill "$(cat "$D/tail.pid")" 2>/dev/null
    ) > /dev/null 2>&1 &
    note "start backend=${RECOMP_PB_BACKEND:-metal} pid=$(cat "$D/pid")"
}

note() { echo "$(date +%H:%M:%S) $*" | tee -a "$D/scene.txt" >&2; }
die() { note "FAIL: $*"; [ "${STOP_AT_END:-0}" = 1 ] && scene_stop; exit 1; }
alive() { kill -0 "$(cat "$D/pid")" 2>/dev/null || die "game exited"; }

# The newest flip number in the log (0 before the first).
flip() {
    local n
    n=$(tail -c 4096 "$D/events.txt" 2>/dev/null | grep -ao '\[GPU\] flip [0-9]*' \
        | tail -n 1 | awk '{print $3}')
    echo "${n:-0}"
}

# anchor <regex> [nth]: the flip the log was at when it printed the nth line
# matching regex (default the first): the last "[GPU] flip" line above it.
# Empty if there is no such line yet.
# The regex goes in through the environment, not -v: awk takes escapes out
# of a -v value ("\\\\" becomes "\\"), so a pattern with a backslash in it
# meant one thing to wait_log's grep and another here.
anchor() {
    ANCHOR_RE="$1" awk -v nth="${2:-1}" 'BEGIN { re = ENVIRON["ANCHOR_RE"] }
        /\[GPU\] flip [0-9]+ / { for (i = 1; i < NF; i++) if ($i == "flip") { f = $(i + 1); break } }
        $0 ~ re { if (++k == nth) { print f + 0; exit } }' "$D/events.txt" 2>/dev/null
}

# track_re <name>: the log line of the title opening one of its track files,
# D:\\tracks\\<name>.rws. The title's paths have backslashes; this matches
# either separator, in wait_log and anchor alike (a pattern written with a
# literal "[.]" or "/" between the folder and the name never matches).
track_re() { printf '%s' "[\\\\/]tracks[\\\\/]$1[.]rws"; }

# count <regex>: lines matching regex so far.
count() { grep -acE "$1" "$D/events.txt" 2>/dev/null || true; }

# wait_log <regex> [what]: until the log has a line matching regex.
wait_log() {
    local t=$(date +%s)
    until grep -aqE "$1" "$D/events.txt" 2>/dev/null; do
        alive
        [ $(( $(date +%s) - t )) -gt "$SCENE_TIMEOUT" ] && die "no '${2:-$1}' after ${SCENE_TIMEOUT}s"
        sleep 0.25
    done
    note "log: ${2:-$1} (flip $(anchor "$1"))"
}

# wait_count <regex> <n>: until regex has matched n lines.
wait_count() {
    local t=$(date +%s)
    until [ "$(count "$1")" -ge "$2" ]; do
        alive
        [ $(( $(date +%s) - t )) -gt "$SCENE_TIMEOUT" ] && die "'$1' not seen $2 times"
        sleep 0.25
    done
}

# wait_flip <n>: until the flip log reaches flip n.
wait_flip() {
    local t=$(date +%s)
    until [ "$(flip)" -ge "$1" ]; do
        alive
        [ $(( $(date +%s) - t )) -gt "$SCENE_TIMEOUT" ] && die "flip $1 not reached (at $(flip))"
        sleep 0.1
    done
}

# wait_flips <n>: n flips from now.
wait_flips() { scene_rate_set; wait_flip $(( $(flip) + $(flips "$1") )); }

# press <button[:hold]> [flips]: append the press, wait until the runtime
# has taken it ('PAD: live "<press>"'), then wait <flips> flips (default 45)
# for the screen it leads to. The runtime holds the button for <hold> ms and
# releases it 200 ms before the next press, so the next press never lands in
# the same pad report.
press() {
    PADN=$(( PADN + 1 ))
    echo "$1" >> "$D/pad.txt"
    wait_count 'PAD: live "' "$PADN"
    local f; f=$(anchor 'PAD: live "' "$PADN")
    note "press $1 (flip $f)"
    scene_rate_set
    wait_flip $(( f + $(flips "${2:-45}") ))
}

# press_until <button[:hold]> <regex> [flips]: press, and again every
# <flips> flips (default 120), until the log shows regex. For a screen that
# ignores input until its animation is done.
press_until() {
    local t=$(date +%s)
    until grep -aqE "$2" "$D/events.txt" 2>/dev/null; do
        [ $(( $(date +%s) - t )) -gt "$SCENE_TIMEOUT" ] && die "no '$2' after pressing $1"
        press "$1" 0
        local f=$(( $(flip) + $(flips "${3:-120}") ))
        until [ "$(flip)" -ge "$f" ] || grep -aqE "$2" "$D/events.txt"; do alive; sleep 0.1; done
    done
    note "log: $2 (flip $(anchor "$2"))"
}

# shot <name> <anchor-flip> <offset> [window]: with CAPTURE=1, dump the frame
# of flip anchor+offset as <rundir>/shots/<name>.bmp, and the <window>
# (default SHOT_WINDOW, 4) flips either side as <rundir>/shots/<name>/<k>.bmp (k = -4..4), and
# list it in shots.tsv. golden.py compares the centre and, when that is off,
# the window: a movie or a load that runs on its own clock can land a frame
# or two away from the anchor's count. The flips must still be ahead of the
# game; ask early. Without CAPTURE it only waits for the flip.
#
# The game dumps flip N before it prints "[GPU] flip N", so once the flip
# log is past the window, the files are complete.
shot() {
    local name=$1 a=$2 off w=${4:-${SHOT_WINDOW:-4}} tgt
    scene_rate_set
    off=$(flips "$3"); tgt=$(( a + off ))
    local now; now=$(flip)
    if [ "${CAPTURE:-0}" != 1 ]; then wait_flip "$tgt"; return; fi
    [ "$now" -ge $(( tgt - w )) ] && die "shot $name: flip $(( tgt - w )) asked for at flip $now"
    echo "$(( tgt - w ))-$(( tgt + w ))" >> "$D/dumpat.txt"
    wait_flip $(( tgt + w + 1 ))
    mkdir -p "$D/shots/$name"
    local k src
    for (( k = -w; k <= w; k++ )); do
        src=$(printf '%s/fb_flip_%05u.bmp' "$D" $(( tgt + k )))
        [ -f "$src" ] || continue
        if [ "$k" = 0 ]; then mv "$src" "$D/shots/$name.bmp"
        else mv "$src" "$D/shots/$name/$k.bmp"; fi
    done
    [ -f "$D/shots/$name.bmp" ] || note "shot $name: flip $tgt was not dumped"
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$a" "$off" "$tgt" \
        "${RECOMP_PB_BACKEND:-metal}" "$w" >> "$D/shots.tsv"
    note "shot $name: flip $tgt (anchor $a + $off, window $w)"
}

# scene_stop: end the run (the drivers leave the game running otherwise).
scene_stop() {
    local p; p=$(cat "$D/pid" 2>/dev/null) || return 0
    if [ -n "${STOP:-}" ]; then bash -c "$STOP"; fi
    pkill -P "$p" 2>/dev/null; kill "$p" 2>/dev/null
    rm -f "$D"/fb_[0-9]*.bmp
    note "stopped (flip $(flip), $(( $(date +%s) - T0 )) s)"
}

# The common start: attract screen, profile screen, Profile 1 loaded, the
# main menu with the cursor on Burnout 3 World Tour.
#
#   _FEMain.xmv     the attract loop: the title screen is up
#   SaveMeta.xbx    START was taken: "Select option: Load Profile" reads the
#                   saves
#   Profile 1       A was taken: the Load Profile list reads Profile 1 for
#                   its details (name, playing time); nothing is loaded yet
#
# START is pressed until the saves are read and A until the list is up: the
# screens take no input while they slide in, and a press there is lost,
# which is how the old drivers stalled on the profile screen (and why a
# fixed "down" could land on Profile 2). Then A loads the profile ("Load
# successful", no log line) and A again leaves the message.
#
# Shots: menu_attract (the attract loop over _FEMain.xmv), menu_main.
to_main_menu() {
    # START through the intro film to the title screen instead of waiting it
    # out (SKIP_INTRO=0: wait). The shots below count from _FEMain.xmv, which
    # comes after the film either way.
    if [ "${SKIP_INTRO:-1}" != 0 ]; then
        wait_log 'Titles30[.]xmv' "intro film (Titles30.xmv)"
        press_until start:300 '_FEMain[.]xmv' 60
    fi
    wait_log '_FEMain[.]xmv' "attract (_FEMain.xmv)"
    shot menu_attract "$(anchor '_FEMain[.]xmv')" 150 12
    press_until start:600 'SaveMeta[.]xbx' 180
    wait_flips 90
    press_until a:400 'Profile 1' 150
    wait_flips 60
    press a:400 120         # load Profile 1
    press a:400 0           # Load successful > Continue: main menu
    # The menu's background is the attract movie still playing, so the shot
    # is anchored on the movie, not on the press: its frame is the same at
    # every run only counted from _FEMain.xmv. The movie keeps its own
    # clock, so a loaded machine can put it a few frames off: a wider window.
    shot menu_main "$(anchor '_FEMain[.]xmv')" 800 12
    note "main menu"
}

# Main menu > Single Event: Select Mode (Race, Time Attack, Road Rage, Crash).
to_single_event() {
    press down:200 20
    press a:400 90
    note "single event: select mode"
}

# The end of a driver: leave the game running (its pid is in <rundir>/pid),
# or with STOP_AT_END=1 stop it.
scene_end() {
    note "$1 (flip $(flip), $(( $(date +%s) - T0 )) s)"
    rm -f "$D"/fb_[0-9]*.bmp
    [ "${STOP_AT_END:-0}" = 1 ] && scene_stop
    return 0
}
