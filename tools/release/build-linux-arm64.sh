#!/usr/bin/env bash
# build-linux-arm64.sh: Takedawn for Linux arm64, as one AppImage, built in
# the Steam Runtime sniper SDK aarch64 container with podman.
#
#   tools/release/build-linux-arm64.sh [--no-setup-window]
#
#   dist/<version>/Takedawn-<version>-linux-arm64.AppImage
#
# Mirrors build-linux.sh for aarch64. The build machine must be an aarch64
# Linux host with podman; set TAKEDAWN_LINUX_ARM64_HOST (ssh) and
# TAKEDAWN_LINUX_ARM64_DIR (default takedawn-build/release). The container
# image is SNIPER_SDK_IMAGE_ARM64 from pins.env.
#
# The menu pictures' upscaler (BO-311) is not included: no arm64 Linux binary
# is published for the pinned Real-ESRGAN release. The rest of the AppImage is
# identical to the x86_64 one.
source "$(dirname "$0")/common.sh"
source "$REPO/tools/release/pins.env"

WITH_SETUP="${TAKEDAWN_SETUP:-1}"
for a in "$@"; do case "$a" in --no-setup-window) WITH_SETUP=0 ;; *) die "unknown option $a" ;; esac; done
assert_gen_all
B="$WORK/linux-arm64"
HOST="${TAKEDAWN_LINUX_ARM64_HOST:-}"
LDIR="${TAKEDAWN_LINUX_ARM64_DIR:-takedawn-build/release}"
[ -n "$HOST" ] || [ "$(uname -m)" = aarch64 ] || die "set TAKEDAWN_LINUX_ARM64_HOST: the arm64 build needs an aarch64 Linux machine with podman"
t0=$(date +%s)
remote() { if [ -n "$HOST" ]; then ssh "$HOST" "$@"; else bash -c "$*"; fi; }
# The container program on the build machine: podman, or TAKEDAWN_LINUX_ARM64_CONTAINER
# (a path there; anything that takes podman's "run" and "pull" and "image
# inspect", such as a wrapper around docker).
CT="${TAKEDAWN_LINUX_ARM64_CONTAINER:-podman}"
RDEST="${HOST:+$HOST:}$LDIR"
NAME="Takedawn-$VERSION$PKG_TAG-linux-arm64.AppImage"

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
# The menu pictures' upscaler (BO-311) is omitted: no arm64 Linux binary is
# published for the pinned Real-ESRGAN release (v0.2.5.0 ships x86_64 only).
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
RT="$DEPS/dl/appimage-runtime-aarch64-${APPIMAGE_RUNTIME_ARM64_SHA256:0:12}"
mkdir -p "$DEPS/dl"
[ -s "$RT" ] || curl -fL --retry 3 -o "$RT" "$APPIMAGE_RUNTIME_ARM64_URL"
[ "$(sha256_of "$RT")" = "$APPIMAGE_RUNTIME_ARM64_SHA256" ] || die "the AppImage runtime is not the pinned one"
cp "$RT" "$STAGE/runtime-aarch64"

# ── the sources, on the build machine ────────────────────────────────
say "copying the sources and the generated code to ${HOST:+the build machine, }$LDIR"
remote "mkdir -p $LDIR/src/game $LDIR/src/toolkit $LDIR/out $LDIR/stage" || die "cannot reach the Linux machine"
git -C "$REPO" ls-files -z | rsync -a --from0 --files-from=- "$REPO/" "$RDEST/src/game/"
git -C "$TOOLKIT" ls-files -z | rsync -a --from0 --files-from=- "$TOOLKIT/" "$RDEST/src/toolkit/"
# every program's generated code (gen, gen.pal, gen.pal-ensv)
for r in $PKG_REGIONS; do
    use_region "$r"
    rsync -a --delete "$GEN/" "$RDEST/src/game/src/game/recomp/$(basename "$GEN")/"
done
use_region "${PKG_REGIONS%% *}"; assert_gen
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
SKEY="$SDL3_VERSION-$SDL2COMPAT_VERSION-${SNIPER_SDK_IMAGE_ARM64##*:}"; SKEY="${SKEY:0:32}-p$SPATCH"
SDIR="$LDIR/sdl-sniper/$SKEY"
remote "mkdir -p $SDIR/src $SDIR/prefix $SDIR/build"
rsync -a "$(fetch_pin "$SDL3_URL" "$SDL3_SHA256")" "$RDEST/sdl-sniper/$SKEY/src/SDL3.tar.gz"
rsync -a "$(fetch_pin "$SDL2COMPAT_URL" "$SDL2COMPAT_SHA256")" "$RDEST/sdl-sniper/$SKEY/src/sdl2-compat.tar.gz"
rsync -a "$REPO/tools/release/linux/build-sdl.sh" "$RDEST/sdl-sniper/$SKEY/build-sdl.sh"
rsync -a --delete "$REPO/tools/release/linux/patches/" "$RDEST/sdl-sniper/$SKEY/src/patches/"
sdl_build="if [ ! -f sdl-sniper/$SKEY/done ]; then
    echo 'building SDL $SDL3_VERSION and sdl2-compat $SDL2COMPAT_VERSION in the container (once)'
    $CT run --rm --security-opt label=disable -e JOBS=${LINUX_JOBS:-8} -e NICE=$NICE \\
        -v \"\$PWD/sdl-sniper/$SKEY/src\":/sdlsrc:ro -v \"\$PWD/sdl-sniper/$SKEY/prefix\":/sdlout \\
        -v \"\$PWD/sdl-sniper/$SKEY/build\":/sdlbuild -v \"\$PWD/sdl-sniper/$SKEY/build-sdl.sh\":/build-sdl.sh:ro \\
        '$SNIPER_SDK_IMAGE_ARM64' sh /build-sdl.sh && touch sdl-sniper/$SKEY/done
fi"
qt_mount=""; qt_build=""
if [ -n "$QTSRC" ]; then
    # Qt lives in a cache folder of its own on the build machine, keyed by the
    # source archive and the container image; it is built when that changes.
    QKEY="${QTBASE_SRC_SHA256:0:12}-${SNIPER_SDK_IMAGE_ARM64##*:}"; QKEY="${QKEY:0:25}"
    QDIR="$LDIR/qt-sniper/$QT_VERSION-$QKEY"
    remote "mkdir -p $QDIR/src $QDIR/prefix $QDIR/build"
    rsync -a "$QTSRC" "$RDEST/qt-sniper/$QT_VERSION-$QKEY/src/qtbase.tar.xz"
    rsync -a "$REPO/tools/release/linux/build-qt.sh" "$RDEST/qt-sniper/$QT_VERSION-$QKEY/build-qt.sh"
    qt_mount="-v \"\$PWD/qt-sniper/$QT_VERSION-$QKEY/prefix\":/qt:ro"
    qt_build="if [ ! -f qt-sniper/$QT_VERSION-$QKEY/done ]; then
    echo 'building Qt $QT_VERSION in the container (once; about a quarter of an hour)'
    $CT run --rm --security-opt label=disable -e JOBS=${LINUX_JOBS:-8} -e NICE=$NICE \\
        -v \"\$PWD/qt-sniper/$QT_VERSION-$QKEY/src\":/qtsrc:ro -v \"\$PWD/qt-sniper/$QT_VERSION-$QKEY/prefix\":/qtout \\
        -v \"\$PWD/qt-sniper/$QT_VERSION-$QKEY/build\":/qtbuild -v \"\$PWD/qt-sniper/$QT_VERSION-$QKEY/build-qt.sh\":/build-qt.sh:ro \\
        '$SNIPER_SDK_IMAGE_ARM64' sh /build-qt.sh && touch qt-sniper/$QT_VERSION-$QKEY/done
fi"
fi
remote "printf '%s\n' '$(build_commit_line "$REPO")' > $LDIR/src/game/BUILD_COMMIT;
        printf '%s\n' '$(build_commit_line "$TOOLKIT")' > $LDIR/src/toolkit/BUILD_COMMIT"
for r in $PKG_REGIONS; do
    use_region "$r"; assert_gen
    there="$(remote "cd $LDIR/src/game/src/game/recomp/$(basename "$GEN") && sha256sum recomp_* | LC_ALL=C sort -k2 | sha256sum | cut -d' ' -f1; sed -n 's/^key //p' regen.stamp")"
    [ "$(printf '%s' "$there" | sed -n 1p)" = "$GEN_TREE" ] || die "$r: the generated files on the build machine are not the stamped tree"
    [ "$(printf '%s' "$there" | sed -n 2p)" = "$GEN_KEY" ] || die "$r: the regen key on the build machine is not $GEN_KEY"
    say "$r: generated code on the build machine: tree and key match ($GEN_KEY)"
done
use_region "${PKG_REGIONS%% *}"; assert_gen

# ── build, AppDir, AppImage ──────────────────────────────────────────
script="set -e; cd $LDIR
$CT image inspect '$SNIPER_SDK_IMAGE_ARM64' >/dev/null 2>&1 || $CT pull '$SNIPER_SDK_IMAGE_ARM64'
$sdl_build
$qt_build
$CT run --rm --security-opt label=disable -e JOBS=${LINUX_JOBS:-8} -e NICE=$NICE -e BO3_REGION=$BO3_REGION -e BO3_REGIONS='$PKG_REGIONS' \
    -v \"\$PWD/src\":/src:ro -v \"\$PWD/stage\":/stage:ro -v \"\$PWD/out\":/out -v \"\$PWD/sdl-sniper/$SKEY/prefix\":/sdl:ro $qt_mount \
    '$SNIPER_SDK_IMAGE_ARM64' sh /src/game/tools/release/linux/in-container.sh
command -v mksquashfs >/dev/null || { echo 'mksquashfs (squashfs-tools) is needed on the build machine' >&2; exit 1; }
rm -f out/fs.squashfs out/$NAME
mksquashfs out/AppDir out/fs.squashfs -root-owned -noappend -comp zstd -Xcompression-level 19 -b 1M -quiet -no-progress
cat stage/runtime-aarch64 out/fs.squashfs > out/$NAME
chmod 755 out/$NAME
rm -f out/fs.squashfs
cd out
mkdir -p scratch-home && [ "\$PWD/scratch-home" != "\$HOME" ] || exit 9
env HOME="\$PWD/scratch-home" XDG_DATA_HOME= XDG_CACHE_HOME= ./$NAME --version > version-mounted.txt 2>version-mounted.err || echo 'NOT RUN mounted (no FUSE?)'
env HOME="\$PWD/scratch-home" XDG_DATA_HOME= XDG_CACHE_HOME= ./$NAME --appimage-extract-and-run --version > version.txt
for p in takedawn-pal takedawn-ensv; do
    env HOME="\$PWD/scratch-home" LD_LIBRARY_PATH=AppDir/usr/lib AppDir/usr/bin/\$p --version > version-\${p#takedawn-}.txt
done
ldd AppDir/usr/bin/takedawn AppDir/usr/bin/takedawn-pal AppDir/usr/bin/takedawn-ensv | grep 'not found' | grep -v -E 'libSDL2|libSDL3' && { echo 'a library is missing on this machine' >&2; exit 1; }
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
for r in $PKG_REGIONS; do
    use_region "$r"
    [ -n "$PROG_SUFFIX" ] || continue
    assert_gen
    rsync -a "$RDEST/out/version$PROG_SUFFIX.txt" "$B/version$PROG_SUFFIX.txt" \
        || die "the AppImage has no usr/bin/takedawn$PROG_SUFFIX"
    check_version "$B/version$PROG_SUFFIX.txt" "usr/bin/takedawn$PROG_SUFFIX on the build machine"
done
use_region "${PKG_REGIONS%% *}"; assert_gen
if rsync -a "$RDEST/out/version-mounted.txt" "$B/version-mounted.txt" 2>/dev/null && [ -s "$B/version-mounted.txt" ]; then
    check_version "$B/version-mounted.txt" "the AppImage, mounted"
fi
cp "$B/version.txt" "$DIST/version-linux-arm64.txt"
say "$NAME: $(( $(stat -c %s "$DIST/$NAME" 2>/dev/null || stat -f %z "$DIST/$NAME") / 1024 / 1024 )) MB, sha256 $(sha256_of "$DIST/$NAME")"
say "Linux arm64 done in $(( $(date +%s) - t0 )) s (setup window: $HAVE_SETUP)"
