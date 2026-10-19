#!/usr/bin/env bash
# build-linux.sh: Takedawn for Linux x86-64 (desktop and Steam Deck), as one
# AppImage, built in the Steam Runtime sniper SDK container with podman.
#
#   tools/release/build-linux.sh [--no-setup-window]
#
#   dist/<version>/Takedawn-<version>-linux-x86_64.AppImage
#
# The container (pins.env: SNIPER_SDK_IMAGE, by digest) gives an old glibc
# (2.31) and the libraries the Deck's own runtime has, so one program runs on
# SteamOS, Bazzite and the desktop distributions. The build machine is
# TAKEDAWN_LINUX_HOST (ssh), working only under TAKEDAWN_LINUX_DIR in its home
# (default takedawn-build/release); without a host the script runs podman on
# this machine. What it does:
#
#   1. copies the two source trees (the files git tracks, as they are in the
#      working tree), the generated code and a BUILD_COMMIT file for each to
#      <dir>/src, and checks there that the generated files are the stamped
#      ones (the release build's configure checks it again);
#   2. runs tools/release/linux/in-container.sh in the container: the build
#      and the AppDir (usr/bin/takedawn, usr/lib with the libraries a system
#      may lack, the desktop file, the icon, the licences);
#   3. packs the AppDir with mksquashfs and the pinned AppImage runtime
#      (static, needs no libfuse on the player's machine);
#   4. runs the AppImage's --version on the build machine, mounted and
#      extracted, and brings the file back.
#
# podman is only ever asked to pull the pinned image and to run containers
# with --rm: no reset, no prune, no image removed. With TESTHOST_LOCK set the
# build holds the shared test-machine lock.
#
# The setup window (tools/install/gui) is built in the same container against
# a Qt built there once from the pinned source archive (pins.env,
# linux/build-qt.sh: the qt.io binaries need a newer glibc than the game
# does; kept in <dir>/qt-sniper), and goes into the AppDir with Qt, the xcb,
# Wayland and offscreen platform plugins and the X11 helper libraries the xcb
# plugin needs: usr/bin/takedawn-setup beside usr/bin/takedawn, which is where
# the game looks for it. --no-setup-window leaves it out.
source "$(dirname "$0")/common.sh"
source "$REPO/tools/release/pins.env"

WITH_SETUP="${TAKEDAWN_SETUP:-1}"
for a in "$@"; do case "$a" in --no-setup-window) WITH_SETUP=0 ;; *) die "unknown option $a" ;; esac; done
assert_gen
B="$WORK/linux"
HOST="${TAKEDAWN_LINUX_HOST:-}"
LDIR="${TAKEDAWN_LINUX_DIR:-takedawn-build/release}"
[ -n "$HOST" ] || [ "$(uname -s)" = Linux ] || die "set TAKEDAWN_LINUX_HOST: the Linux build needs a Linux machine with podman"
t0=$(date +%s)
remote() { if [ -n "$HOST" ]; then ssh "$HOST" "$@"; else bash -c "$*"; fi; }
RDEST="${HOST:+$HOST:}$LDIR"
NAME="Takedawn-$VERSION$PKG_TAG-linux-x86_64.AppImage"

# ── what goes into the AppDir beside the program, made here ──────────
STAGE="$B/stage"; rm -rf "$STAGE"; mkdir -p "$STAGE"
cp "$REPO/tools/release/linux/AppRun" "$STAGE/"
sed -e "s/^Name=Takedawn$/Name=$APP_TITLE/" "$REPO/tools/release/linux/takedawn.desktop" > "$STAGE/takedawn.desktop"
render_icon "$REPO/assets/icon/takedawn.svg" 256 "$STAGE/takedawn-256.png"
copy_licenses "$STAGE/licenses"
# The button prompts' artwork (assets/glyphs), rendered here: the container has
# no SVG renderer, and a release build does not go without the prompts.
mkdir -p "$STAGE/glyphs"
"${TAKEDAWN_PYTHON:-python3}" "$REPO/tools/glyphs/build.py" "$REPO/assets/glyphs" \
    "$STAGE/glyphs/bo3_glyph_art.c" "$STAGE/glyphs/bo3_glyph_art.h" > "$B/glyphs.log" 2>&1 \
    || { tail -5 "$B/glyphs.log" >&2; die "the button prompts could not be rendered (TAKEDAWN_PYTHON with resvg_py, or resvg)"; }
sed -e "s/@VERSION@/$VERSION/g" -e "s/@DISC@/$DISC_NAME/g" "$REPO/tools/release/linux/README.txt" > "$STAGE/README.txt"
HAVE_SETUP=no; QTSRC=""
if [ "$WITH_SETUP" = 1 ]; then
    # The setup window is built in the container against a Qt that is built
    # there too, once, from the pinned source archive (linux/build-qt.sh);
    # its icons are rendered here, where the SVG renderer is.
    QTSRC="$DEPS/dl/$(basename "$QTBASE_SRC_URL")"
    [ -s "$QTSRC" ] || curl -fL --retry 3 -o "$QTSRC" "$QTBASE_SRC_URL"
    [ "$(sha256_of "$QTSRC")" = "$QTBASE_SRC_SHA256" ] || die "the Qt source archive is not the pinned one"
    mkdir -p "$STAGE/icons"
    for s in 16 32 64 128 256 512; do render_icon "$REPO/assets/icon/takedawn.svg" "$s" "$STAGE/icons/icon-$s.png"; done
    printf '%s' "$QT_VERSION" > "$STAGE/qt-version"
    rm -rf "$STAGE/licenses"; copy_licenses "$STAGE/licenses" qt
    HAVE_SETUP=yes
fi
RT="$DEPS/dl/appimage-runtime-x86_64-${APPIMAGE_RUNTIME_SHA256:0:12}"
mkdir -p "$DEPS/dl"
[ -s "$RT" ] || curl -fL --retry 3 -o "$RT" "$APPIMAGE_RUNTIME_URL"
[ "$(sha256_of "$RT")" = "$APPIMAGE_RUNTIME_SHA256" ] || die "the AppImage runtime is not the pinned one"
cp "$RT" "$STAGE/runtime-x86_64"

# ── the sources, on the build machine ────────────────────────────────
say "copying the sources and the generated code to ${HOST:+the build machine, }$LDIR"
remote "mkdir -p $LDIR/src/game $LDIR/src/toolkit $LDIR/out $LDIR/stage" || die "cannot reach the Linux machine"
git -C "$REPO" ls-files -z | rsync -a --from0 --files-from=- "$REPO/" "$RDEST/src/game/"
git -C "$TOOLKIT" ls-files -z | rsync -a --from0 --files-from=- "$TOOLKIT/" "$RDEST/src/toolkit/"
rsync -a --delete "$GEN/" "$RDEST/src/game/src/game/recomp/gen/"
rsync -a --delete "$STAGE/" "$RDEST/stage/"
# SDL: the pinned sources, built once in the container (linux/build-sdl.sh).
fetch_pin() {  # <url> <sha256> -> path in the cache
    local f="$DEPS/dl/$(basename "$1")"
    [ -s "$f" ] || curl -fL --retry 3 -o "$f" "$1" >&2
    [ "$(sha256_of "$f")" = "$2" ] || die "$(basename "$f"): sha256 is not the pinned one"
    printf '%s' "$f"
}
# Our patches to it (linux/patches/sdl3-*.patch) are part of the key: a new
# or changed patch builds SDL again.
SPATCH="$(cat "$REPO"/tools/release/linux/patches/sdl3-*.patch 2>/dev/null | sha256_of /dev/stdin | cut -c1-8)"
SKEY="$SDL3_VERSION-$SDL2COMPAT_VERSION-${SNIPER_SDK_IMAGE##*:}"; SKEY="${SKEY:0:32}-p$SPATCH"
SDIR="$LDIR/sdl-sniper/$SKEY"
remote "mkdir -p $SDIR/src $SDIR/prefix $SDIR/build"
rsync -a "$(fetch_pin "$SDL3_URL" "$SDL3_SHA256")" "$RDEST/sdl-sniper/$SKEY/src/SDL3.tar.gz"
rsync -a "$(fetch_pin "$SDL2COMPAT_URL" "$SDL2COMPAT_SHA256")" "$RDEST/sdl-sniper/$SKEY/src/sdl2-compat.tar.gz"
rsync -a "$REPO/tools/release/linux/build-sdl.sh" "$RDEST/sdl-sniper/$SKEY/build-sdl.sh"
rsync -a --delete "$REPO/tools/release/linux/patches/" "$RDEST/sdl-sniper/$SKEY/src/patches/"
sdl_build="if [ ! -f sdl-sniper/$SKEY/done ]; then
    echo 'building SDL $SDL3_VERSION and sdl2-compat $SDL2COMPAT_VERSION in the container (once)'
    podman run --rm --security-opt label=disable -e JOBS=${LINUX_JOBS:-8} -e NICE=$NICE \\
        -v \"\$PWD/sdl-sniper/$SKEY/src\":/sdlsrc:ro -v \"\$PWD/sdl-sniper/$SKEY/prefix\":/sdlout \\
        -v \"\$PWD/sdl-sniper/$SKEY/build\":/sdlbuild -v \"\$PWD/sdl-sniper/$SKEY/build-sdl.sh\":/build-sdl.sh:ro \\
        '$SNIPER_SDK_IMAGE' sh /build-sdl.sh && touch sdl-sniper/$SKEY/done
fi"
qt_mount=""; qt_build=""
if [ -n "$QTSRC" ]; then
    # Qt lives in a cache folder of its own on the build machine, keyed by the
    # source archive and the container image; it is built when that changes.
    QKEY="${QTBASE_SRC_SHA256:0:12}-${SNIPER_SDK_IMAGE##*:}"; QKEY="${QKEY:0:25}"
    QDIR="$LDIR/qt-sniper/$QT_VERSION-$QKEY"
    remote "mkdir -p $QDIR/src $QDIR/prefix $QDIR/build"
    rsync -a "$QTSRC" "$RDEST/qt-sniper/$QT_VERSION-$QKEY/src/qtbase.tar.xz"
    rsync -a "$REPO/tools/release/linux/build-qt.sh" "$RDEST/qt-sniper/$QT_VERSION-$QKEY/build-qt.sh"
    qt_mount="-v \"\$PWD/qt-sniper/$QT_VERSION-$QKEY/prefix\":/qt:ro"
    qt_build="if [ ! -f qt-sniper/$QT_VERSION-$QKEY/done ]; then
    echo 'building Qt $QT_VERSION in the container (once; about a quarter of an hour)'
    podman run --rm --security-opt label=disable -e JOBS=${LINUX_JOBS:-8} -e NICE=$NICE \\
        -v \"\$PWD/qt-sniper/$QT_VERSION-$QKEY/src\":/qtsrc:ro -v \"\$PWD/qt-sniper/$QT_VERSION-$QKEY/prefix\":/qtout \\
        -v \"\$PWD/qt-sniper/$QT_VERSION-$QKEY/build\":/qtbuild -v \"\$PWD/qt-sniper/$QT_VERSION-$QKEY/build-qt.sh\":/build-qt.sh:ro \\
        '$SNIPER_SDK_IMAGE' sh /build-qt.sh && touch qt-sniper/$QT_VERSION-$QKEY/done
fi"
fi
remote "printf '%s\n' '$(build_commit_line "$REPO")' > $LDIR/src/game/BUILD_COMMIT;
        printf '%s\n' '$(build_commit_line "$TOOLKIT")' > $LDIR/src/toolkit/BUILD_COMMIT"
there="$(remote "cd $LDIR/src/game/src/game/recomp/gen && sha256sum recomp_* | LC_ALL=C sort -k2 | sha256sum | cut -d' ' -f1; sed -n 's/^key //p' regen.stamp")"
[ "$(printf '%s' "$there" | sed -n 1p)" = "$GEN_TREE" ] || die "the generated files on the build machine are not the stamped tree"
[ "$(printf '%s' "$there" | sed -n 2p)" = "$GEN_KEY" ] || die "the regen key on the build machine is not $GEN_KEY"
say "generated code on the build machine: tree and key match ($GEN_KEY)"

# ── build, AppDir, AppImage ──────────────────────────────────────────
script="set -e; cd $LDIR
podman image exists '$SNIPER_SDK_IMAGE' || podman pull '$SNIPER_SDK_IMAGE'
$sdl_build
$qt_build
podman run --rm --security-opt label=disable -e JOBS=${LINUX_JOBS:-8} -e NICE=$NICE -e BO3_REGION=$BO3_REGION \
    -v \"\$PWD/src\":/src:ro -v \"\$PWD/stage\":/stage:ro -v \"\$PWD/out\":/out -v \"\$PWD/sdl-sniper/$SKEY/prefix\":/sdl:ro $qt_mount \
    '$SNIPER_SDK_IMAGE' sh /src/game/tools/release/linux/in-container.sh
command -v mksquashfs >/dev/null || { echo 'mksquashfs (squashfs-tools) is needed on the build machine' >&2; exit 1; }
rm -f out/fs.squashfs out/$NAME
mksquashfs out/AppDir out/fs.squashfs -root-owned -noappend -comp zstd -Xcompression-level 19 -b 1M -quiet -no-progress
cat stage/runtime-x86_64 out/fs.squashfs > out/$NAME
chmod 755 out/$NAME
rm -f out/fs.squashfs
cd out
mkdir -p scratch-home && [ "\$PWD/scratch-home" != "\$HOME" ] || exit 9
env HOME="\$PWD/scratch-home" XDG_DATA_HOME= XDG_CACHE_HOME= ./$NAME --version > version-mounted.txt 2>version-mounted.err || echo 'NOT RUN mounted (no FUSE?)'
env HOME="\$PWD/scratch-home" XDG_DATA_HOME= XDG_CACHE_HOME= ./$NAME --appimage-extract-and-run --version > version.txt
ldd AppDir/usr/bin/takedawn | grep 'not found' | grep -v -E 'libSDL2|libSDL3' && { echo 'a library is missing on this machine' >&2; exit 1; }
sha256sum $NAME | cut -d' ' -f1 | sed 's/^/RESULT OK /'"
say "building in the sniper container (this takes a few minutes)"
if [ -n "$HOST" ] && [ -n "${TESTHOST_LOCK:-}" ] && [ -x "$TESTHOST_LOCK" ]; then
    "$TESTHOST_LOCK" run "$HOST" burnout3/release "release build (Linux AppImage)" 2400 -- \
        ssh "$HOST" "$script" > "$B/remote.log" 2>&1 \
        || { tail -25 "$B/remote.log" >&2; die "the Linux build failed ($B/remote.log)"; }
else
    remote "$script" > "$B/remote.log" 2>&1 || { tail -25 "$B/remote.log" >&2; die "the Linux build failed ($B/remote.log)"; }
fi
grep -a -E "^(building SDL|SDL built|SDL_|building Qt|Qt built|build took|glibc needed|carried|setup window|NOT RUN|RESULT)" "$B/remote.log" | sed 's/^/    /'

mkdir -p "$DIST"
rsync -a "$RDEST/out/$NAME" "$DIST/$NAME"
rsync -a "$RDEST/out/version.txt" "$B/version.txt"
rsync -a "$RDEST/out/carried.txt" "$B/carried.txt"
want="$(grep -a '^RESULT OK' "$B/remote.log" | awk '{print $3}')"
[ "$(sha256_of "$DIST/$NAME")" = "$want" ] || die "the fetched AppImage is not the one built"
check_version "$B/version.txt" "the AppImage on the build machine"
if rsync -a "$RDEST/out/version-mounted.txt" "$B/version-mounted.txt" 2>/dev/null && [ -s "$B/version-mounted.txt" ]; then
    check_version "$B/version-mounted.txt" "the AppImage, mounted"
fi
cp "$B/version.txt" "$DIST/version-linux.txt"
say "$NAME: $(( $(stat -f %z "$DIST/$NAME" 2>/dev/null || stat -c %s "$DIST/$NAME") / 1024 / 1024 )) MB, sha256 $(sha256_of "$DIST/$NAME")"
say "Linux done in $(( $(date +%s) - t0 )) s (setup window: $HAVE_SETUP)"
