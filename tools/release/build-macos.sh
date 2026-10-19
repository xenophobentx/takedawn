#!/usr/bin/env bash
# build-macos.sh: Takedawn.app and its disk image, on an Apple silicon Mac.
#
#   tools/release/build-macos.sh [--no-dmg] [--no-setup]
#
#   dist/<version>/Takedawn-<version>-macos-arm64.dmg
#   build/release/macos/Takedawn.app          (the same app, unpacked)
#
# The app (the layout docs/installer.md gives for the setup window):
#
#   Takedawn.app/Contents/
#     Info.plist
#     MacOS/Takedawn           the game (CFBundleExecutable). Installs the
#                              data itself: Takedawn --install <image|folder>
#     MacOS/takedawn-setup     the setup window (Qt), which the game opens
#                              when its data is not installed
#     Frameworks/              libSDL2-2.0.0.dylib (sdl2-compat) and
#                              libSDL3.0.dylib, built here from the pinned
#                              sources; QtCore, QtGui, QtWidgets, QtDBus
#     PlugIns/                 Qt's cocoa platform and macOS style
#     Resources/Takedawn.icns  from assets/icon/takedawn-macos.svg
#     Resources/qt.conf
#     Resources/licenses/      THIRD_PARTY.txt, the licence texts, SOURCE.txt
#
# The setup window (tools/install/gui) is built here against the pinned
# qt.io kit (pins.env: QT_VERSION; fetched into the cache with aqtinstall)
# and deployed with tools/install/gui/packaging/deploy-qt-macos.sh.
# --no-setup, or TAKEDAWN_SETUP=0, builds the app without it.
#
# Nothing is signed with an identity and nothing is notarised (decided). On
# Apple silicon a program without any signature does not start, so the linker
# gives each binary an ad hoc one by itself. That alone is not enough for an
# app: a linker-signed executable inside a bundle that has resources fails
# verification ("code has no resources but signature indicates they must be
# present"), and macOS reports a downloaded copy of such an app as damaged,
# with no way to open it. So the bundle is sealed ad hoc (codesign -s -: no
# certificate, no identity, no Apple service involved), which leaves the
# ordinary "not verified" prompt the player can pass once (docs/release.md).
# TAKEDAWN_MACOS_SEAL=0 skips the seal.
#
# Needs: Xcode Command Line Tools, cmake, ninja, pkg-config, and at build time
# Homebrew's libepoxy and openssl@3 (the toolkit's configure asks for both;
# the program links neither: -dead_strip_dylibs).
source "$(dirname "$0")/common.sh"
source "$REPO/tools/release/pins.env"
[ "$(uname -s)" = Darwin ] && [ "$(uname -m)" = arm64 ] || die "build-macos.sh runs on an Apple silicon Mac"
MAKE_DMG=1; WITH_SETUP="${TAKEDAWN_SETUP:-1}"
for a in "$@"; do case "$a" in --no-dmg) MAKE_DMG=0 ;; --no-setup) WITH_SETUP=0 ;; *) die "unknown option $a" ;; esac; done

assert_gen
B="$WORK/macos"
P="$DEPS/macos-arm64-$MACOS_DEPLOYMENT_TARGET"      # the libraries' prefix
export MACOSX_DEPLOYMENT_TARGET="$MACOS_DEPLOYMENT_TARGET"
t0=$(date +%s)

# ── the pinned SDL ───────────────────────────────────────────────────
fetch() {  # <url> <sha256> -> path
    local f="$DEPS/dl/$(basename "$1")"
    mkdir -p "$DEPS/dl"
    [ -s "$f" ] || curl -fL --retry 3 -o "$f" "$1" >&2
    [ "$(sha256_of "$f")" = "$2" ] || die "$(basename "$f"): sha256 is not the pinned one"
    printf '%s' "$f"
}
dep_stamp="SDL3 $SDL3_VERSION $SDL3_SHA256 sdl2-compat $SDL2COMPAT_VERSION $SDL2COMPAT_SHA256 target $MACOS_DEPLOYMENT_TARGET"
if [ "$(cat "$P/.stamp" 2>/dev/null)" != "$dep_stamp" ]; then
    say "building SDL3 $SDL3_VERSION and sdl2-compat $SDL2COMPAT_VERSION (macOS $MACOS_DEPLOYMENT_TARGET+)"
    rm -rf "$P" "$DEPS/src-macos"; mkdir -p "$P" "$DEPS/src-macos/SDL3" "$DEPS/src-macos/sdl2-compat"
    tar -xf "$(fetch "$SDL3_URL" "$SDL3_SHA256")" -C "$DEPS/src-macos/SDL3" --strip-components 1
    tar -xf "$(fetch "$SDL2COMPAT_URL" "$SDL2COMPAT_SHA256")" -C "$DEPS/src-macos/sdl2-compat" --strip-components 1
    common=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
            -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOS_DEPLOYMENT_TARGET"
            -DCMAKE_INSTALL_PREFIX="$P" -DCMAKE_INSTALL_NAME_DIR=@rpath
            -DCMAKE_BUILD_WITH_INSTALL_NAME_DIR=ON
            -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON -DCMAKE_INSTALL_RPATH=@loader_path)
    nice -n "$NICE" cmake -S "$DEPS/src-macos/SDL3" -B "$DEPS/src-macos/SDL3-build" "${common[@]}" \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_TEST_LIBRARY=OFF \
        > "$DEPS/src-macos/SDL3.log" 2>&1 || die "SDL3 configure failed ($DEPS/src-macos/SDL3.log)"
    nice -n "$NICE" cmake --build "$DEPS/src-macos/SDL3-build" -j"$JOBS" >> "$DEPS/src-macos/SDL3.log" 2>&1 \
        || die "SDL3 build failed ($DEPS/src-macos/SDL3.log)"
    cmake --install "$DEPS/src-macos/SDL3-build" >> "$DEPS/src-macos/SDL3.log" 2>&1
    nice -n "$NICE" cmake -S "$DEPS/src-macos/sdl2-compat" -B "$DEPS/src-macos/sdl2-compat-build" "${common[@]}" \
        -DCMAKE_PREFIX_PATH="$P" -DSDL2COMPAT_TESTS=OFF -DSDL2COMPAT_STATIC=OFF \
        > "$DEPS/src-macos/sdl2-compat.log" 2>&1 || die "sdl2-compat configure failed ($DEPS/src-macos/sdl2-compat.log)"
    nice -n "$NICE" cmake --build "$DEPS/src-macos/sdl2-compat-build" -j"$JOBS" >> "$DEPS/src-macos/sdl2-compat.log" 2>&1 \
        || die "sdl2-compat build failed ($DEPS/src-macos/sdl2-compat.log)"
    cmake --install "$DEPS/src-macos/sdl2-compat-build" >> "$DEPS/src-macos/sdl2-compat.log" 2>&1
    printf '%s' "$dep_stamp" > "$P/.stamp"
fi

# ── the game ─────────────────────────────────────────────────────────
say "configuring (arm64, macOS $MACOS_DEPLOYMENT_TARGET+, Metal only)"
mkdir -p "$B"
# Metal is the Mac's backend: the Vulkan one (a MoltenVK test bed) is left
# out, and with it glslang, SPIRV-Tools and the Vulkan loader.
cmake -S "$REPO" -B "$B/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBO3_RELEASE=ON -DBO3_REGION="$BO3_REGION" \
    -DXBOXRECOMP_DIR="$TOOLKIT" -DBO3_BIN_DIR="$B/bin" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOS_DEPLOYMENT_TARGET" \
    -DXBOXRECOMP_VULKAN=OFF -DSDL2_DIR="$P/lib/cmake/SDL2" -DSDL3_DIR="$P/lib/cmake/SDL3" \
    -DCMAKE_EXE_LINKER_FLAGS="-Wl,-dead_strip_dylibs" > "$B/configure.log" 2>&1 \
    || { tail -20 "$B/configure.log" >&2; die "configure failed ($B/configure.log)"; }
say "building (-j$JOBS, nice $NICE)"
tb=$(date +%s)
nice -n "$NICE" cmake --build "$B/build" --target burnout3 -j"$JOBS" > "$B/build.log" 2>&1 \
    || { tail -30 "$B/build.log" >&2; die "build failed ($B/build.log)"; }
say "build took $(( $(date +%s) - tb )) s"
if grep -q "Wunguarded-availability" "$B/build.log"; then
    grep -c "Wunguarded-availability" "$B/build.log" | sed 's/^/!! API-availability warnings: /' >&2
    die "the build uses APIs newer than macOS $MACOS_DEPLOYMENT_TARGET without a guard ($B/build.log)"
fi

# ── the bundle ───────────────────────────────────────────────────────
APP="$B/$APP_TITLE.app"; C="$APP/Contents"
rm -rf "$APP"; mkdir -p "$C/MacOS" "$C/Frameworks" "$C/Resources"
cp "$B/bin/takedawn" "$C/MacOS/Takedawn"
# Real files, not links: sdl2-compat opens SDL3 by the name libSDL3.dylib
# beside itself, the SDL2 link is by its install name.
cp "$P/lib/libSDL2-2.0.0.dylib" "$C/Frameworks/libSDL2-2.0.0.dylib"
cp "$P/lib/libSDL3.0.dylib" "$C/Frameworks/libSDL3.0.dylib"
ln -s libSDL3.0.dylib "$C/Frameworks/libSDL3.dylib"

HAVE_SETUP=no
if [ "$WITH_SETUP" = 1 ]; then
    QT="$(qt_kit mac desktop clang_64)"
    say "building the setup window (Qt $QT_VERSION from $QT)"
    icon_py=(); [ -n "${TAKEDAWN_PYTHON:-}" ] && icon_py=(-DTAKEDAWN_ICON_PYTHON="$TAKEDAWN_PYTHON")
    cmake -S "$REPO/tools/install/gui" -B "$B/setup" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$QT" -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOS_DEPLOYMENT_TARGET" ${icon_py[@]+"${icon_py[@]}"} \
        > "$B/setup-configure.log" 2>&1 || { tail -20 "$B/setup-configure.log" >&2; die "setup window: configure failed"; }
    nice -n "$NICE" cmake --build "$B/setup" --target takedawn-setup -j"$JOBS" > "$B/setup-build.log" 2>&1 \
        || { tail -30 "$B/setup-build.log" >&2; die "setup window: build failed ($B/setup-build.log)"; }
    "$REPO/tools/install/gui/packaging/deploy-qt-macos.sh" "$B/setup/takedawn-setup" "$QT" "$C" arm64 \
        > "$B/setup-deploy.log" 2>&1 || { tail -20 "$B/setup-deploy.log" >&2; die "setup window: Qt deployment failed"; }
    say "setup window: $(tail -1 "$B/setup-deploy.log"), $(du -sh "$C/Frameworks" | cut -f1) in Frameworks"
    HAVE_SETUP=yes
else
    say "the app is built without the setup window (--no-setup)"
fi

ICONSET="$B/Takedawn.iconset"; rm -rf "$ICONSET"; mkdir -p "$ICONSET"
for pair in 16:icon_16x16 32:icon_16x16@2x 32:icon_32x32 64:icon_32x32@2x 128:icon_128x128 \
            256:icon_128x128@2x 256:icon_256x256 512:icon_256x256@2x 512:icon_512x512 1024:icon_512x512@2x; do
    render_icon "$REPO/assets/icon/takedawn-macos.svg" "${pair%%:*}" "$ICONSET/${pair#*:}.png"
done
iconutil -c icns "$ICONSET" -o "$C/Resources/Takedawn.icns"

if [ "$HAVE_SETUP" = yes ]; then copy_licenses "$C/Resources/licenses" qt; else copy_licenses "$C/Resources/licenses"; fi

sed -e "s/@VERSION@/$VERSION/g" -e "s/@BUNDLE_ID@/$MACOS_BUNDLE_ID$ID_SUFFIX/g" -e "s/@APP_TITLE@/$APP_TITLE/g" \
    -e "s/@MIN_OS@/$MACOS_DEPLOYMENT_TARGET/g" \
    "$REPO/tools/release/macos/Info.plist.in" > "$C/Info.plist"
plutil -lint "$C/Info.plist" >/dev/null || die "Info.plist is not valid"

# ── audit: nothing from outside the bundle or the system ─────────────
while IFS= read -r f; do
    file -b "$f" | grep -q "Mach-O" || continue
    bad="$(otool -L "$f" | tail -n +2 | awk '{print $1}' \
           | grep -v -E '^(/System/|/usr/lib/|@rpath/|@loader_path/|@executable_path/)' || true)"
    [ -z "$bad" ] || die "$(basename "$f") links outside the bundle: $bad"
    lipo -archs "$f" | grep -qx "arm64" || die "$(basename "$f") is not arm64 only"
    minos="$(vtool -show-build "$f" | sed -n 's/^ *minos //p' | head -1)"
    [ "$(printf '%s\n%s\n' "$minos" "$MACOS_DEPLOYMENT_TARGET" | sort -V | tail -1)" = "$MACOS_DEPLOYMENT_TARGET" ] \
        || die "$(basename "$f") needs macOS $minos, the target is $MACOS_DEPLOYMENT_TARGET"
done < <(find "$C/MacOS" "$C/Frameworks" "$C/PlugIns" -type f 2>/dev/null)
otool -L "$C/MacOS/Takedawn" | grep -q "@rpath/libSDL2-2.0.0.dylib" || die "Takedawn does not link the bundled SDL2"
if [ "${TAKEDAWN_MACOS_SEAL:-1}" = 1 ]; then
    codesign --force --deep --sign - "$APP" > "$B/codesign.log" 2>&1 || die "ad hoc seal failed ($B/codesign.log)"
    codesign --verify --deep --strict "$APP" || die "the sealed bundle does not verify"
    say "bundle sealed ad hoc (no identity, not notarised)"
else
    say "bundle not sealed: only the linker's ad hoc signatures"
fi
codesign -dv "$C/MacOS/Takedawn" 2>&1 | grep -q "^Authority=" && die "takedawn carries an identity's signature"

scratch_run "$B/scratch-home" "$C/MacOS/Takedawn" --version > "$B/version.txt" || die "the bundled takedawn does not start"
check_version "$B/version.txt" "$APP_TITLE.app"

# ── the disk image ───────────────────────────────────────────────────
if [ "$MAKE_DMG" = 1 ]; then
    mkdir -p "$DIST"
    DMG="$DIST/Takedawn-$VERSION$PKG_TAG-macos-arm64.dmg"
    STAGE="$B/dmg"; rm -rf "$STAGE" "$DMG"; mkdir -p "$STAGE"
    cp -R "$APP" "$STAGE/"
    ln -s /Applications "$STAGE/Applications"
    sed -e "s/@VERSION@/$VERSION/g" -e "s/@DISC@/$DISC_NAME/g" -e "s/@APP_TITLE@/$APP_TITLE/g" "$REPO/tools/release/macos/READ-ME-FIRST.txt" > "$STAGE/Read me first.txt"
    hdiutil create -volname "$APP_TITLE $VERSION" -srcfolder "$STAGE" -fs HFS+ -format UDZO \
        -imagekey zlib-level=9 -ov "$DMG" > "$B/hdiutil.log" 2>&1 || die "hdiutil failed ($B/hdiutil.log)"
    cp "$B/version.txt" "$DIST/version-macos.txt"
    say "$(basename "$DMG"): $(( $(stat -f %z "$DMG") / 1024 / 1024 )) MB, sha256 $(sha256_of "$DMG")"
fi
say "macOS done in $(( $(date +%s) - t0 )) s (setup window: $HAVE_SETUP)"
