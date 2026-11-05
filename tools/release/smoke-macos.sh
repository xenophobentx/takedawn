#!/usr/bin/env bash
# smoke-macos.sh: run the macOS disk image the way a player's Mac would,
# without touching this user's own Takedawn folders.
#
#   tools/release/smoke-macos.sh [<dmg>] [--keep]
#
# Mounts the image read-only, and with HOME set to a scratch folder runs the
# app from the mounted volume:
#
#   1. takedawn --version                the build says what this checkout says
#      takedawn-setup's screenshot mode  the window draws, with the image's Qt
#   2. takedawn --headless               no data yet: exit status 3,
#                                        RESULT FAIL data not-installed
#   3. takedawn --install <disc> --check then the install itself into the
#      scratch HOME (TAKEDAWN_DISC; skipped without one), .complete written
#   4. a headless start driven to the main menu (tools/headless/scene_lib.sh)
#      with the reference saves (TAKEDAWN_REF_SAVES, default ./saves or the
#      main checkout's), in the scratch folder
#
# Prints one line per check: "OK <what>" or "NOT RUN <what>: <why>"; fails on
# the first check that goes wrong. One game at a time: it waits (15 minutes
# at most) while another takedawn/burnout3 is running, and with TESTHOST_LOCK
# set the boot runs under the shared lock ("local"). The scratch folder (2.5 GB after the install)
# is removed at the end unless --keep.
source "$(dirname "$0")/common.sh"
# A release build is quiet unless told (the toolkit's xr_log.h, BO-125): the
# checks below read the game's log, so it is switched on for every run here.
export RECOMP_LOG_DIAG=1
DMG=""; KEEP=0
for a in "$@"; do case "$a" in --keep) KEEP=1 ;; *) DMG="$a" ;; esac; done
DMG="${DMG:-$DIST/Takedawn-$VERSION-macos-arm64.dmg}"
[ -f "$DMG" ] || die "no disk image at $DMG"
assert_gen

T="${TAKEDAWN_SMOKE_DIR:-$WORK/smoke-macos}"
MNT="$T/mnt"
hdiutil detach "$MNT" -quiet 2>/dev/null || true
rm -rf "$T"; mkdir -p "$T/home" "$T/run" "$MNT"
cleanup() {
    hdiutil detach "$MNT" -quiet 2>/dev/null || hdiutil detach "$MNT" -force -quiet 2>/dev/null || true
    [ "$KEEP" = 1 ] || rm -rf "$T/home" "$T/run/saves" "$T/run/build"
}
trap cleanup EXIT
hdiutil attach "$DMG" -readonly -nobrowse -mountpoint "$MNT" -quiet || die "the image does not mount"
EXE="$MNT/Takedawn.app/Contents/MacOS/Takedawn"
[ -x "$EXE" ] || die "no Takedawn.app/Contents/MacOS/Takedawn in the image"
[ -L "$MNT/Applications" ] || die "no Applications link in the image"
codesign --verify --deep --strict "$MNT/Takedawn.app" 2>/dev/null \
    && echo "OK   bundle signature verifies (ad hoc)" || echo "NOT RUN bundle seal: the app is linker-signed only"
scratch_home "$T/home"          # never the real profile (common.sh)
run() { env -i HOME="$T/home" PATH=/usr/bin:/bin TMPDIR="${TMPDIR:-/tmp}" "$@"; }
# The same with dyld's list of loaded images on stderr. Not through env:
# /usr/bin/env is a protected program and macOS drops DYLD_* on the way in.
run_dyld() { ( HOME="$T/home" DYLD_PRINT_LIBRARIES=1 "$@" ); }
loaded_outside() {  # <dyld output>: images from neither the system nor the image
    sed -n 's/^dyld\[[0-9]*\]: <[^>]*> //p' "$1" | grep -v -E '^(/System/|/usr/lib/)' | grep -v "^$MNT/" || true
}

run "$EXE" --version > "$T/version.txt" || die "--version failed"
check_version "$T/version.txt" "dmg" >/dev/null
echo "OK   --version from the mounted image: $(sed -n 1p "$T/version.txt"), regen $GEN_KEY, patches $GEN_PATCHES"

# Nothing but the bundle and the system is loaded.
( cd "$T/run" && run_dyld "$EXE" --version > /dev/null 2> "$T/dyld.txt" )
grep -q "^dyld.*$MNT/Takedawn.app/Contents/Frameworks/libSDL2" "$T/dyld.txt" || die "dyld did not report the image's SDL2 being loaded"
libs="$(loaded_outside "$T/dyld.txt")"
[ -z "$libs" ] || die "libraries from outside the image were loaded: $libs"
echo "OK   only system libraries and the image's own are loaded"

# The setup window, from the image: its screenshot mode draws the first
# pages (it then waits for a scripted installer that is not there and ends
# itself, which is not a failure here). Qt must come from the image.
SETUPEXE="$MNT/Takedawn.app/Contents/MacOS/takedawn-setup"
if [ -x "$SETUPEXE" ]; then
    # From a folder without game data: in a checkout the game would answer
    # "the data is in the working directory" and the window has nothing to ask.
    ( cd "$T/run" && run_dyld "$SETUPEXE" --program "$EXE" --size 780x790 --shots "$T/shots" > "$T/setup.txt" 2>&1 || true )
    [ -s "$T/shots/setup-03-about.png" ] || { tail -5 "$T/setup.txt" >&2; die "the setup window drew nothing"; }
    libs="$(loaded_outside "$T/setup.txt")"
    [ -z "$libs" ] || die "the setup window loaded libraries from outside the image: $libs"
    grep -q "QtWidgets.framework" "$T/setup.txt" || die "the setup window did not load the image's Qt"
    grep -q -- "--data-status" "$T/setup.txt" || die "the setup window did not ask the game for its data status"
    echo "OK   setup window from the image: drew its pages with the image's Qt, asked the game for the data status"
else
    echo "NOT RUN setup window: the image has none"
fi

set +e
( cd "$T/run" && run "$EXE" --headless > "$T/nodata.txt" 2>&1 ); st=$?
set -e
[ "$st" = 3 ] && grep -q "RESULT FAIL data not-installed" "$T/nodata.txt" \
    || die "without data: exit status $st, expected 3 and RESULT FAIL data not-installed"
echo "OK   without data: exit status 3, RESULT FAIL data not-installed"

DISC="${TAKEDAWN_DISC:-}"
if [ -z "$DISC" ] || [ ! -e "$DISC" ]; then
    echo "NOT RUN install and boot: no disc image (TAKEDAWN_DISC)"
    exit 0
fi
run "$EXE" --install "$DISC" --check > "$T/check.txt" 2>&1 || { tail -5 "$T/check.txt" >&2; die "--install --check failed"; }
grep -q "^RESULT OK" "$T/check.txt" || die "--install --check gave no RESULT OK"
echo "OK   --install --check: $(grep -a '^STEP disc OK' "$T/check.txt" | head -1 | cut -c1-110)"
ti=$(date +%s)
run nice -n "$NICE" "$EXE" --install "$DISC" > "$T/install.txt" 2>&1 || { tail -5 "$T/install.txt" >&2; die "--install failed"; }
# The disc's own program installs it into its own folder (src/game/package.h:
# a European disc's under "Takedawn PAL"): the root is the one it reports.
ROOT="$(sed -n 's/^RESULT OK //p' "$T/install.txt" | tail -1)"
case "$ROOT" in "$T/home/"*) ;; *) die "the install went to '$ROOT', not into the scratch HOME" ;; esac
[ -f "$ROOT/.complete" ] || die "no .complete marker in $ROOT"
got=""
for r in $PKG_REGIONS; do use_region "$r"; grep -q "$(manifest_id)" "$ROOT/.complete" && got="$(manifest_id) ($BUILD_ID)"; done
use_region "${PKG_REGIONS%% *}"
[ -n "$got" ] || die ".complete names no manifest of the package"
n="$(find "$ROOT/Burnout 3 Takedown" -type f | wc -l | tr -d ' ')"
echo "OK   install into the scratch HOME: $n files in ${ROOT#$T/home/}, $(( $(date +%s) - ti )) s, .complete names manifest $got"
# The folders of the port's earlier name: queries move nothing; a real
# install and a game start with its data move them (test-user-dirs.sh).
bash "$REPO/tools/release/test-user-dirs.sh" "$EXE" "$T/dirs" "$ROOT" > "$T/dirs.txt" 2>&1 \
    || { tail -5 "$T/dirs.txt" >&2; die "the folder-move test failed"; }
echo "OK   old-name folders: $(grep -c '^OK' "$T/dirs.txt") checks (queries move nothing; an install and a game start move them)"
# --data before or after the word it goes with (test-arg-order.sh).
bash "$REPO/tools/release/test-arg-order.sh" "$EXE" "$T/args" "$DISC" > "$T/args.txt" 2>&1 \
    || { tail -5 "$T/args.txt" >&2; die "the argument-order test failed"; }
tail -1 "$T/args.txt"

SAVES_REF="${TAKEDAWN_REF_SAVES:-}"
for c in "$REPO/saves" "$(git -C "$REPO" worktree list --porcelain | sed -n '1s/^worktree //p')/saves"; do
    [ -z "$SAVES_REF" ] && [ -d "$c/UDATA" ] && SAVES_REF="$c"
done
if [ -z "$SAVES_REF" ]; then
    echo "NOT RUN boot to the menu: no reference saves (TAKEDAWN_REF_SAVES)"
    exit 0
fi
boot() {
    env -i HOME="$T/home" PATH=/usr/bin:/bin:/usr/sbin TMPDIR="${TMPDIR:-/tmp}" \
        REPO="$REPO" GAME_DIR="$T/run" BIN="$EXE" SAVES="$SAVES_REF" STOP_AT_END=1 \
        SCENE_TIMEOUT="${SCENE_TIMEOUT:-300}" \
        nice -n "$NICE" bash -c 'source "$REPO/tools/headless/scene_lib.sh"; scene_start "$1"; to_main_menu; scene_end "at the main menu"' \
        boot "$T/run/scene"
}
# One game at a time on this Mac: wait for anybody else's to end.
waited=0
while pgrep -x "Takedawn(-pal|-ensv)?" >/dev/null || pgrep -x takedawn >/dev/null || pgrep -x burnout3 >/dev/null; do
    if [ "$waited" -ge "${TAKEDAWN_SMOKE_WAIT:-900}" ]; then
        echo "NOT RUN boot to the menu: another game instance was running for ${waited}s"
        exit 0
    fi
    sleep 10; waited=$((waited + 10))
done
if [ -n "${TESTHOST_LOCK:-}" ] && [ -x "$TESTHOST_LOCK" ]; then
    "$TESTHOST_LOCK" acquire local burnout3/release "release smoke run" 600 900 >/dev/null \
        || die "the local game lock is held by somebody else"
    trap '"$TESTHOST_LOCK" release local burnout3/release >/dev/null 2>&1; cleanup' EXIT
fi
tb=$(date +%s)
boot > "$T/boot.txt" 2>&1 || { tail -15 "$T/boot.txt" >&2; die "the headless boot did not reach the main menu ($T/run/scene)"; }
grep -q "main menu" "$T/run/scene/scene.txt" || die "the scene driver did not report the main menu"
grep -aq "\[DATA\] root: $T/home/" "$T/run/scene/out.txt" "$T/run/scene/log.txt" 2>/dev/null \
    || die "the game did not run from the installed data in the scratch HOME"
echo "OK   headless boot from the installed data to the main menu: $(( $(date +%s) - tb )) s, flip $(grep -ao 'flip [0-9]*' "$T/run/scene/scene.txt" | tail -1 | cut -d' ' -f2)"
