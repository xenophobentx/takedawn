#!/usr/bin/env bash
# test-user-dirs.sh <program> <scratch dir> [<installed data root>]
#
# The folders of the port's earlier name (Burnout3, burnout3, xboxrecomp) are
# moved to the new one only by a game start that has its data and by a real
# --install; every query leaves them where they are (src/game/install.h).
# This runs <program> (takedawn, Takedawn.exe under Git Bash, the AppImage,
# a checkout's bin/burnout3) with a scratch profile under <scratch dir> that
# holds folders of the old name, and checks, command by command, that
# nothing moved; then that a real --install moves them, and, with an
# installed data root (a folder with .complete), that a game start does.
#
# Never against a real profile: the profile is HOME and XDG_* (APPDATA and
# LOCALAPPDATA on Windows) inside <scratch dir>, and the script refuses a
# scratch dir that is the user's own home.
set -u
# A release build is quiet unless told (the toolkit's xr_log.h, BO-125): the
# checks below read the game's log, so it is switched on for every run here.
export RECOMP_LOG_DIAG=1
PROG="${1:?program}"; T="${2:?scratch dir}"; DATA="${3:-}"; EXTRA=()
die() { echo "FAIL $*" >&2; exit 1; }
[ -x "$PROG" ] || die "no program $PROG"
mkdir -p "$T" && T="$(cd "$T" && pwd)" || die "no scratch dir"
[ "$T" != "$(cd "${HOME:-/}" && pwd)" ] || die "the scratch dir is the real home"
P="$T/profile"; rm -rf "$P" "$T/out"; mkdir -p "$P" "$T/out" "$T/empty" "$T/notadisc"
echo x > "$T/notadisc/file.txt"

case "$(uname -s)" in
MINGW*|MSYS*|CYGWIN*)
    win() { cygpath -w "$1"; }
    export APPDATA="$(win "$P/Roaming")" LOCALAPPDATA="$(win "$P/Local")"
    OLD_SAVE="$P/Roaming/Burnout3"; NEW_SAVE="$P/Roaming/Takedawn"
    OLD_DATA="$P/Local/Burnout3";   NEW_DATA="$P/Local/Takedawn"
    OLD_CACHE="$P/Local/xboxrecomp/cache"; NEW_CACHE="$P/Local/Takedawn/cache"
    run() { "$@"; } ;;
Darwin)
    win() { echo "$1"; }
    OLD_SAVE="$P/Library/Application Support/Burnout3"; NEW_SAVE="$P/Library/Application Support/Takedawn"
    OLD_DATA=""; NEW_DATA=""
    OLD_CACHE="$P/Library/Caches/burnout3"; NEW_CACHE="$P/Library/Caches/Takedawn"
    run() { env -i HOME="$P" PATH=/usr/bin:/bin TMPDIR="${TMPDIR:-/tmp}" ${EXTRA[@]+"${EXTRA[@]}"} "$@"; } ;;
*)
    win() { echo "$1"; }
    OLD_SAVE="$P/.local/share/burnout3"; NEW_SAVE="$P/.local/share/takedawn"
    OLD_DATA=""; NEW_DATA=""
    OLD_CACHE="$P/.cache/burnout3"; NEW_CACHE="$P/.cache/takedawn"
    run() { env -i HOME="$P" PATH=/usr/bin:/bin ${EXTRA[@]+"${EXTRA[@]}"} "$@"; } ;;
esac

seed() {    # the folders as an older build left them
    rm -rf "$P"; mkdir -p "$OLD_SAVE/UDATA" "$OLD_CACHE"
    echo 'saved = "old"' > "$OLD_SAVE/settings.toml"; echo save > "$OLD_SAVE/UDATA/profile.bin"
    echo cache > "$OLD_CACHE/vk_pipelines.bin"
    [ -z "$OLD_DATA" ] || { mkdir -p "$OLD_DATA/Game"; echo data > "$OLD_DATA/Game/readme.txt"; }
}
real_dir() { [ -d "$1" ] && [ ! -L "$1" ]; }
unmoved() {  # <what ran>
    real_dir "$OLD_SAVE" && [ -f "$OLD_SAVE/settings.toml" ] && [ -f "$OLD_SAVE/UDATA/profile.bin" ] \
        || die "$1: the old save folder is not as it was"
    [ -f "$OLD_CACHE/vk_pipelines.bin" ] || die "$1: the old shader cache is not as it was"
    [ ! -e "$NEW_SAVE/settings.toml" ] && [ ! -e "$NEW_CACHE/vk_pipelines.bin" ] || die "$1: something was moved to the new name"
    [ -z "$OLD_DATA" ] || [ -f "$OLD_DATA/Game/readme.txt" ] || die "$1: the old data folder is not as it was"
    echo "OK   $1: nothing moved"
}
moved() {    # <what ran>
    [ -f "$NEW_SAVE/settings.toml" ] && [ "$(cat "$NEW_SAVE/settings.toml")" = 'saved = "old"' ] \
        && [ -f "$NEW_SAVE/UDATA/profile.bin" ] || die "$1: the saves are not under the new name"
    [ -f "$NEW_CACHE/vk_pipelines.bin" ] || die "$1: the shader cache is not under the new name"
    real_dir "$OLD_SAVE" && die "$1: the old save folder is still a folder"
    [ -z "$OLD_DATA" ] || [ -f "$NEW_DATA/Game/readme.txt" ] || die "$1: the old data folder was not moved"
    echo "OK   $1: saves, settings and shader cache are under the new name, contents the same"
}

seed
( cd "$T/empty" && run "$PROG" --version ) > "$T/out/version.txt" 2>&1 || die "--version failed"
unmoved "--version"
# The PAL programs (Takedawn-pal, Takedawn-ensv; docs/pal.md) keep folders of their own and
# leave the old name's alone, since they are NTSC-U's: for them, the steps
# below that expect a move check that nothing moved.
PAL=0; tr -d '\r' < "$T/out/version.txt" | grep -q '^manifest .*(pal-' && PAL=1
after() {    # <what ran>
    if [ "$PAL" = 1 ]; then unmoved "$1 (PAL)"; else moved "$1"; fi
}
( cd "$T/empty" && run "$PROG" --help ) > "$T/out/help.txt" 2>&1; unmoved "--help"
( cd "$T/empty" && run "$PROG" --data-status ) > "$T/out/status.txt" 2>&1
if [ "$PAL" = 1 ]; then
    grep -q '^LOG data pending-move' "$T/out/status.txt" && { cat "$T/out/status.txt" >&2; die "--data-status names a move on the PAL program"; }
else
    grep -q '^LOG data pending-move .*[Bb]urnout3 -> ' "$T/out/status.txt" || { cat "$T/out/status.txt" >&2; die "--data-status does not say a move is pending"; }
fi
unmoved "--data-status (it names $(grep -c '^LOG data pending-move' "$T/out/status.txt") pending moves)"
( cd "$T/empty" && run "$PROG" --install "$(win "$T/notadisc")" --check ) > "$T/out/check.txt" 2>&1; unmoved "--install <source> --check"
( cd "$T/empty" && run "$PROG" --bug-report --out "$(win "$T/out")" ) > "$T/out/report.txt" 2>&1; unmoved "--bug-report"
( cd "$T/empty" && run "$PROG" --no-setup ) > "$T/out/nodata.txt" 2>&1; st=$?
[ "$st" = 3 ] || die "a start without data ended with status $st, not 3"
unmoved "a start without data (status 3)"
( cd "$T/empty" && run "$PROG" --build-shaders --status ) > "$T/out/sbstatus.txt" 2>&1; unmoved "--build-shaders --status"

# A real install moves them first, whatever becomes of the install itself
# (this source is no disc, so nothing is copied).
( cd "$T/empty" && run "$PROG" --install "$(win "$T/notadisc")" ) > "$T/out/install.txt" 2>&1
after "a real --install"
( cd "$T/empty" && run "$PROG" --data-status ) > "$T/out/status2.txt" 2>&1
grep -q '^LOG data pending-move' "$T/out/status2.txt" && die "--data-status still names a pending move after it"
( cd "$T/empty" && run "$PROG" --install "$(win "$T/notadisc")" ) > /dev/null 2>&1
after "a second --install (idempotent)"

# Both names there: nothing is merged, nothing written over.
if [ "$PAL" = 0 ]; then
    mkdir -p "$OLD_CACHE"; echo other > "$OLD_CACHE/vk_pipelines.bin"
    ( cd "$T/empty" && run "$PROG" --install "$(win "$T/notadisc")" ) > /dev/null 2>&1
    [ "$(cat "$OLD_CACHE/vk_pipelines.bin")" = other ] && [ "$(cat "$NEW_CACHE/vk_pipelines.bin")" = cache ] \
        || die "with both names there a file was moved or written over"
    echo "OK   both names there: each folder is left as it is"
fi

if [ -n "$DATA" ]; then
    [ -f "$DATA/.complete" ] || die "$DATA is not an installed data root (no .complete)"
    seed
    # A game start with its data: headless, stopped after a few seconds.
    # exec: the background job is the program itself, so it can be stopped
    # (under Git Bash a kill reaches only the shell; the Windows process id
    # is in /proc/<pid>/winpid).
    EXTRA=(TAKEDAWN_DATA="$(win "$DATA")" RECOMP_HEADLESS=1)
    case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) ( cd "$T/empty" && TAKEDAWN_DATA="$(win "$DATA")" RECOMP_HEADLESS=1 exec "$PROG" --headless ) > "$T/out/start.txt" 2>&1 & ;;
    Darwin) ( cd "$T/empty" && exec env -i HOME="$P" PATH=/usr/bin:/bin TMPDIR="${TMPDIR:-/tmp}" "${EXTRA[@]}" "$PROG" --headless ) > "$T/out/start.txt" 2>&1 & ;;
    *) ( cd "$T/empty" && exec env -i HOME="$P" PATH=/usr/bin:/bin "${EXTRA[@]}" "$PROG" --headless ) > "$T/out/start.txt" 2>&1 & ;;
    esac
    pid=$!
    for k in $(seq 1 40); do [ -f "$NEW_SAVE/settings.toml" ] && break; sleep 0.5; done
    sleep 2
    wp="$(cat /proc/$pid/winpid 2>/dev/null || true)"
    kill $pid 2>/dev/null; wait $pid 2>/dev/null
    [ -z "$wp" ] || taskkill //F //T //PID "$wp" > /dev/null 2>&1
    after "a game start with its data"
    [ "$PAL" = 1 ] || grep -q '^\[DIRS\]' "$T/out/start.txt" || die "the game start did not log the move"
else
    echo "NOT RUN a game start with its data (no installed data root given)"
fi
echo "RESULT OK"
