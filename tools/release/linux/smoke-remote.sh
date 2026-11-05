#!/usr/bin/env bash
# smoke-remote.sh: the checks of tools/release/smoke-linux.sh, on the Linux
# machine itself. Everything is done with HOME (and so the game's data, save
# and cache folders) inside <scratch>; the machine's own session, Steam and
# the user's folders are not touched, and no window is put on its screen.
#
#   smoke-remote.sh <AppImage> <scratch dir> <manifest ids> <source copy> [<disc folder or image>] [<reference saves>]
#
# <manifest ids>: the package's discs' (comma-separated): the disc given is
# any of them, and its program installs it into its own folder
# (src/game/package.h; a European disc's under "takedawn pal").
#
# One line per check: "OK   <what>" or "NOT RUN <what>: <why>"; the first
# check that goes wrong ends it with "!! <what>" and status 1.
set -uo pipefail
APP="$(readlink -f "$1")"; T="$2"; MANIFEST="$3"; SRC="$4"; DISC="${5:-}"; SAVES="${6:-}"
die() { echo "!! $*" >&2; exit 1; }
rm -rf "$T"; mkdir -p "$T/home" "$T/run" "$T/x"
T="$(readlink -f "$T")"
# A clean environment: no display, no session, the scratch HOME.
[ "$T/home" != "$(readlink -f "${HOME:-/}")" ] || die "the scratch HOME is the real home: not running the program there"
run() { env -i HOME="$T/home" PATH=/usr/bin:/bin "$@"; }

run "$APP" --version > "$T/version.txt" 2>/dev/null || die "--version (mounted) failed"
echo "OK   --version, mounted: $(sed -n 1p "$T/version.txt")"
# The folders of the port's earlier name: queries move nothing, a real install does.
if [ -f "$(dirname "$0")/test-user-dirs.sh" ]; then
    bash "$(dirname "$0")/test-user-dirs.sh" "$APP" "$T/dirs" > "$T/dirs.txt" 2>&1 || { tail -5 "$T/dirs.txt" >&2; die "the folder-move test failed"; }
    echo "OK   old-name folders: $(grep -c '^OK' "$T/dirs.txt") checks (queries move nothing; a real --install moves them)"
fi
if [ -f "$(dirname "$0")/test-arg-order.sh" ]; then
    bash "$(dirname "$0")/test-arg-order.sh" "$APP" "$T/args" "$DISC" > "$T/args.txt" 2>&1 || { tail -5 "$T/args.txt" >&2; die "the argument-order test failed"; }
    tail -1 "$T/args.txt"
fi

# Without a display the program asks for no window: the lines for whoever started it.
( cd "$T/run" && run "$APP" > "$T/nodata.txt" 2> "$T/nodata.err" ); st=$?
[ "$st" = 3 ] && grep -q "RESULT FAIL data not-installed" "$T/nodata.txt" \
    || die "without data and without a display: exit status $st, expected 3 and RESULT FAIL data not-installed"
grep -q "opening the setup window" "$T/nodata.err" && die "the setup window was started without a display"
echo "OK   no data, no display: exit status 3, RESULT FAIL data not-installed, no window"

# The AppImage's contents, for the checks that start the window directly.
( cd "$T/x" && run "$APP" --appimage-extract > /dev/null 2>&1 ) || die "--appimage-extract failed"
D="$T/x/squashfs-root"
if [ -x "$D/usr/bin/takedawn-setup" ]; then
    # With a display the game hands over to the window (it becomes that
    # process). The display named here does not exist and Qt is told to draw
    # off screen, so nothing appears anywhere.
    ( cd "$T/run" && exec env -i HOME="$T/home" PATH=/usr/bin:/bin DISPLAY=:987 QT_QPA_PLATFORM=offscreen \
          "$APP" > "$T/handover.txt" 2> "$T/handover.err" ) &
    hp=$!
    ok=no
    for _ in $(seq 1 40); do
        sleep 0.25
        if pgrep -u "$(id -u)" -f "^[^ ]*/usr/bin/takedawn-setup --program [^ ]*/usr/bin/takedawn( |$)" > /dev/null; then ok=yes; break; fi
        kill -0 "$hp" 2>/dev/null || break
    done
    sleep 2
    pkill -u "$(id -u)" -f "^[^ ]*/usr/bin/takedawn-setup --program [^ ]*/usr/bin/takedawn( |$)" 2>/dev/null
    kill "$hp" 2>/dev/null; wait "$hp" 2>/dev/null
    [ "$ok" = yes ] && grep -q "opening the setup window" "$T/handover.err" \
        || { tail -5 "$T/handover.err" >&2; die "with a display the game did not hand over to the setup window"; }
    grep -i -E "could not|cannot|error" "$T/handover.err" | grep -v "RECOMP" | head -3 | sed 's/^/     note: /'
    echo "OK   no data, a display: the game became the setup window (inside the mounted AppImage)"

    # The programs the window starts (--data-status, --install --check, --build-shaders)
    # run on the image's own libraries, whatever the machine has: a stock desktop has
    # no SDL 3, and a child that lost the image's usr/lib dies with "Failed loading
    # SDL3 library" (a machine that does have one hides that, so the check looks at
    # which file each process loaded). The loader's own log says which.
    rm -f "$T"/ld.*
    ( cd "$T/run" && exec env -i HOME="$T/home" PATH=/usr/bin:/bin DISPLAY=:987 QT_QPA_PLATFORM=offscreen \
          LD_DEBUG=libs LD_DEBUG_OUTPUT="$T/ld" "$APP" > /dev/null 2> /dev/null ) &
    hp=$!
    for _ in $(seq 1 40); do
        sleep 0.5
        [ "$(grep -l "calling init: .*libSDL3" "$T"/ld.* 2>/dev/null | wc -l)" -ge 2 ] && break
        kill -0 "$hp" 2>/dev/null || break
    done
    sleep 1
    pkill -u "$(id -u)" -f "^[^ ]*/usr/bin/takedawn-setup --program [^ ]*/usr/bin/takedawn( |$)" 2>/dev/null
    kill "$hp" 2>/dev/null; wait "$hp" 2>/dev/null
    nsdl=$(grep -l "calling init: .*libSDL3" "$T"/ld.* 2>/dev/null | wc -l)
    foreign=$(grep -h "calling init: .*libSDL3" "$T"/ld.* 2>/dev/null | grep -v "/tmp/\.mount_\|squashfs-root" | head -1)
    if [ -n "$foreign" ]; then die "a program the setup window started loaded SDL 3 from the system, not from the image: $foreign"
    elif [ "$nsdl" -lt 2 ]; then echo "NOT RUN the setup window's children on the image's libraries (only $nsdl process(es) loaded SDL 3)"
    else echo "OK   the setup window's children ran on the image's own libraries ($nsdl processes loaded its SDL 3)"; fi
    rm -f "$T"/ld.*

    # The window's own pages, drawn off screen with the image's Qt.
    ( cd "$T/run" && run env LD_LIBRARY_PATH="$D/usr/lib" QT_QPA_PLATFORM=offscreen QT_DEBUG_PLUGINS=0 \
          "$D/usr/bin/takedawn-setup" --program "$D/usr/bin/takedawn" --size 780x790 --shots "$T/shots" > "$T/setup.txt" 2>&1 )
    [ -s "$T/shots/setup-03-about.png" ] || { tail -5 "$T/setup.txt" >&2; die "the setup window drew nothing (offscreen)"; }
    grep -q -- "--data-status" "$T/setup.txt" || die "the setup window did not ask the game for its data status"
    grep -q "Add Takedawn to my Steam library\|steam" "$T/setup.txt" || true
    echo "OK   setup window, off screen: drew its pages, asked the game for the data status"

    # The xcb plugin and the X11 helper libraries the image carries: under a
    # headless gamescope of its own (its Xwayland is the display; nothing is
    # shown on the machine's screen).
    if command -v gamescope > /dev/null 2>&1; then
        ( cd "$T/run" && env -i HOME="$T/home" PATH=/usr/bin:/bin XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" \
              timeout 60 gamescope --backend headless -W 1280 -H 800 -- \
              env LD_LIBRARY_PATH="$D/usr/lib" QT_QPA_PLATFORM=xcb QT_DEBUG_PLUGINS=1 \
              "$D/usr/bin/takedawn-setup" --program "$D/usr/bin/takedawn" --size 780x790 --shots "$T/shots-xcb" > "$T/setup-xcb.txt" 2>&1 )
        if [ -s "$T/shots-xcb/setup-03-about.png" ] && grep -q "libqxcb.so" "$T/setup-xcb.txt"; then
            echo "OK   setup window on X11 (a headless gamescope of its own): the xcb plugin loaded and drew"
        elif grep -q -i "could not load the Qt platform plugin\|xcb.*not found\|cannot open shared object" "$T/setup-xcb.txt"; then
            grep -i "cannot open shared object\|could not load" "$T/setup-xcb.txt" | head -4 >&2
            die "the xcb platform plugin does not load"
        else
            echo "NOT RUN setup window on X11: the headless gamescope did not come up ($(grep -v '^qt\.' "$T/setup-xcb.txt" | tail -1 | cut -c1-90))"
        fi
    else
        echo "NOT RUN setup window on X11: no gamescope on this machine"
    fi
    # Wayland: the platform plugin and its shell integration load against a
    # Wayland display (the same kind of headless gamescope, exposing one).
    # Whether a window appears on a real compositor is not something this
    # can see.
    if command -v gamescope > /dev/null 2>&1; then
        ( cd "$T/run" && env -i HOME="$T/home" PATH=/usr/bin:/bin XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" \
              timeout 25 gamescope --backend headless --expose-wayland -W 1280 -H 800 -- \
              env LD_LIBRARY_PATH="$D/usr/lib" QT_QPA_PLATFORM=wayland QT_DEBUG_PLUGINS=1 \
              "$D/usr/bin/takedawn-setup" --program "$D/usr/bin/takedawn" --size 780x790 --shots "$T/shots-wl" > "$T/setup-wl.txt" 2>&1 )
        if grep -q 'libqwayland.so" loaded library' "$T/setup-wl.txt" && grep -q 'libxdg-shell.so" loaded library' "$T/setup-wl.txt"; then
            if [ -s "$T/shots-wl/setup-03-about.png" ]; then echo "OK   setup window on Wayland: the plugin loaded and drew"
            else echo "OK   setup window on Wayland: the platform plugin and xdg-shell loaded (no window seen: gamescope maps none for a Wayland client)"; fi
        elif grep -q -i "cannot open shared object\|Could not load the Qt platform plugin" "$T/setup-wl.txt"; then
            grep -i "cannot open shared object\|could not load" "$T/setup-wl.txt" | head -4 >&2
            die "the Wayland platform plugin does not load"
        else
            echo "NOT RUN setup window on Wayland: no Wayland display came up"
        fi
    fi
else
    echo "NOT RUN setup window: the AppImage has none"
fi

if [ -z "$DISC" ] || [ ! -e "$DISC" ]; then
    echo "NOT RUN install and boot: no disc image or folder on this machine"
    exit 0
fi
run "$APP" --install "$DISC" --check > "$T/check.txt" 2>&1 || { tail -4 "$T/check.txt" >&2; die "--install --check failed"; }
grep -q "^RESULT OK" "$T/check.txt" || die "--install --check gave no RESULT OK"
echo "OK   --install --check: $(grep -a '^STEP disc OK' "$T/check.txt" | head -1 | cut -c1-100)"
t0=$(date +%s)
run nice -n 10 "$APP" --install "$DISC" > "$T/install.txt" 2>&1 || { tail -4 "$T/install.txt" >&2; die "--install failed"; }
ROOT="$(sed -n 's/^RESULT OK //p' "$T/install.txt" | tail -1)"
case "$ROOT" in "$T/home/"*) ;; *) die "the install went to '$ROOT', not into the scratch HOME" ;; esac
got=""
for m in $(printf '%s' "$MANIFEST" | tr ',' ' '); do
    [ -f "$ROOT/.complete" ] && grep -q "$m" "$ROOT/.complete" && got="$m"
done
[ -n "$got" ] || die "no .complete marker naming a manifest of the package ($MANIFEST) in $ROOT"
echo "OK   install into the scratch HOME: $(find "$ROOT/Burnout 3 Takedown" -type f | wc -l) files in ${ROOT#$T/home/}, $(( $(date +%s) - t0 )) s, .complete names manifest $got"

if [ -z "$SAVES" ] || [ ! -d "$SAVES/UDATA" ]; then
    echo "NOT RUN boot to the menu: no reference saves"
    exit 0
fi
if pgrep -x "takedawn(-pal|-ensv)?" > /dev/null || pgrep -x burnout3 > /dev/null; then
    echo "NOT RUN boot to the menu: another game instance is running on this machine"
    exit 0
fi
t0=$(date +%s)
env -i HOME="$T/home" PATH=/usr/bin:/bin REPO="$SRC" GAME_DIR="$T/run" BIN="$APP" SAVES="$SAVES" \
    STOP_AT_END=1 SCENE_TIMEOUT="${SCENE_TIMEOUT:-300}" RECOMP_PB_BACKEND=vulkan \
    nice -n 10 bash -c 'source "$REPO/tools/headless/scene_lib.sh"; scene_start "$1"; to_main_menu; scene_end "at the main menu"' \
    boot "$T/run/scene" > "$T/boot.txt" 2>&1 \
    || { tail -12 "$T/boot.txt" >&2; grep -a -i "vulkan\|error\|fatal" "$T/run/scene/out.txt" 2>/dev/null | head -8 >&2; die "the headless boot did not reach the main menu"; }
grep -q "main menu" "$T/run/scene/scene.txt" || die "the scene driver did not report the main menu"
grep -aq "\[DATA\] root: $T/home/" "$T/run/scene/out.txt" "$T/run/scene/log.txt" 2>/dev/null \
    || die "the game did not run from the installed data in the scratch HOME"
dev="$(grep -a -h -m1 -i "\[vulkan\] device up" "$T/run/scene/out.txt" "$T/run/scene/log.txt" 2>/dev/null | head -1 | cut -c1-90)"
echo "OK   headless boot from the installed data to the main menu: $(( $(date +%s) - t0 )) s, flip $(grep -ao 'flip [0-9]*' "$T/run/scene/scene.txt" | tail -1 | cut -d' ' -f2) (${dev:-Vulkan device line not found})"
