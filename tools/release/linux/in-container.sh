#!/bin/sh
# in-container.sh: the part of tools/release/build-linux.sh that runs inside
# the Steam Runtime sniper SDK container.
#
#   /src/game, /src/toolkit   the two source trees (with BUILD_COMMIT files)
#   /stage                    AppRun, the desktop file, icons and licences,
#                             prepared by build-linux.sh
#   /qt                       the qt.io kit for the setup window (optional)
#   /out                      build tree (kept between runs) and the AppDir
#
#   /sdl                      SDL 3 and sdl2-compat, built by build-sdl.sh
#
# Builds the release program with the SDK's clang against that SDL and the
# SDK's libepoxy, OpenSSL, Vulkan headers and static glslang, the setup window
# against the Qt kit mounted at /qt (when there is one), and lays out
# /out/AppDir. Libraries the program needs are sorted three ways: the ones
# every system has (glibc, libstdc++, the Vulkan loader, X11/GL/Wayland: never
# carried), the ones a system may lack (carried, from this image), and
# anything else, which fails the build until it is put on one of the lists.
set -eu
JOBS="${JOBS:-4}"
B=/out/build
A=/out/AppDir

cmake -S /src/game -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBO3_RELEASE=ON -DBO3_REGION="${BO3_REGION:-NTSC-U}" \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DXBOXRECOMP_DIR=/src/toolkit -DBO3_BIN_DIR="$B/bin" \
    -DSDL2_DIR=/sdl/lib/cmake/SDL2 -DSDL3_DIR=/sdl/lib/cmake/SDL3 \
    -DBO3_GLYPHS_ART_DIR=/stage/glyphs \
    -DCMAKE_EXE_LINKER_FLAGS="-Wl,--as-needed" > /out/configure.log 2>&1 \
    || { tail -20 /out/configure.log >&2; echo "configure failed" >&2; exit 1; }
start=$(date +%s)
nice -n "${NICE:-10}" cmake --build "$B" --target burnout3 -j"$JOBS" > /out/build.log 2>&1 \
    || { tail -30 /out/build.log >&2; echo "build failed" >&2; exit 1; }
echo "build took $(( $(date +%s) - start )) s"

rm -rf "$A"
mkdir -p "$A/usr/bin" "$A/usr/lib" "$A/usr/share/applications" \
         "$A/usr/share/icons/hicolor/256x256/apps" "$A/usr/share/licenses/takedawn"
cp "$B/bin/takedawn" "$A/usr/bin/takedawn"

# Never carried: what every system has and must match its own kernel, drivers
# and display server. (The Wayland client libraries are the compositor's side
# of a protocol and come with any system that has Wayland at all; libxkbcommon
# must be the system's too, because it reads the system's keyboard layouts and
# an old copy does not understand a new system's.)
SYSTEM='^(linux-vdso|ld-linux-x86-64|libc|libm|libdl|libpthread|librt|libutil|libresolv|libgcc_s|libstdc\+\+|libatomic|libvulkan|libGL|libGLX|libGLdispatch|libEGL|libOpenGL|libdrm|libgbm|libX11|libxcb|libXau|libXdmcp|libXext|libXrender|libbsd|libmd|libz|libexpat|libuuid|libfontconfig|libfreetype|libpng16|libbrotli[a-z]*|libglib-2\.0|libgthread-2\.0|libgobject-2\.0|libgio-2\.0|libgmodule-2\.0|libffi|libmount|libblkid|libselinux|libpcre[0-9]*|libpcre2-8|libdbus-1|libsystemd|liblzma|libzstd|liblz4|libgcrypt|libgpg-error|libcap|libwayland-client|libwayland-cursor|libwayland-egl|libgraphite2|libharfbuzz|libxkbcommon|libxkbcommon-x11|libX11-xcb|libSM|libICE)\.so'
# Carried from this image when the program or the window needs them: the X11
# helper libraries Qt's xcb plugin wants that a desktop may lack. (SDL and Qt
# are carried too, from /sdl and /qt: built in this image from pinned source.)
CARRY='^(libcrypto|libssl|libepoxy|libxcb-[a-z0-9-]+|libdouble-conversion|libb2|libpcre2-16|libmd4c|libts)\.so'
: > /out/carried.txt
: > /out/unknown.txt
carry() {  # <path of a library>: copy it (the real file, under its soname) and note its package
    name=$(basename "$1")
    [ -f "$A/usr/lib/$name" ] && return 0
    cp -L "$1" "$A/usr/lib/$name"
    case "$1" in
        /qt/*) echo "$name Qt $(cat /stage/qt-version 2>/dev/null), built from source in this image" >> /out/carried.txt; return 0 ;;
        /sdl/*) echo "$name $(readlink -f "$1" | sed 's/.*\.so\.//'), built from the pinned source in this image" >> /out/carried.txt; return 0 ;;
    esac
    pkg=$(dpkg -S "$(readlink -f "$1")" 2>/dev/null | head -1 | cut -d: -f1 || true)
    echo "$name ${pkg:-?} $(dpkg-query -W -f='${Version}\n' "${pkg:-none}" 2>/dev/null | head -n 1)" >> /out/carried.txt
    if [ -n "$pkg" ] && [ -f "/usr/share/doc/$pkg/copyright" ]; then
        cp "/usr/share/doc/$pkg/copyright" "$A/usr/share/licenses/takedawn/$pkg-copyright.txt"
    fi
}
# closure <file>...: everything the files link, sorted onto the lists; what is
# carried is looked at in turn.
closure() {
    for f in "$@"; do
        LD_LIBRARY_PATH=/qt/lib:/sdl/lib ldd "$f" 2>/dev/null | while read -r name arrow path rest; do
            case "$name" in
                /*) name=$(basename "$name") ;;
                statically|not) continue ;;     # "statically linked", "not a dynamic executable"
            esac
            case "$path" in
                /qt/lib/*|/sdl/lib/*) [ -f "$A/usr/lib/$name" ] || { carry "$path"; closure "$path"; }; continue ;;
            esac
            if echo "$name" | grep -q -E "$SYSTEM"; then continue; fi
            if echo "$name" | grep -q -E "$CARRY"; then
                [ -f "$path" ] || { echo "$name (not in the image)" >> /out/unknown.txt; continue; }
                [ -f "$A/usr/lib/$name" ] || { carry "$path"; closure "$path"; }
                continue
            fi
            echo "$name (needed by $(basename "$f"))" >> /out/unknown.txt
        done
    done
}
ldd "$B/bin/takedawn" > /out/ldd.txt
closure "$B/bin/takedawn"
# sdl2-compat opens SDL3 by name at run time; ldd does not show it.
carry /sdl/lib/libSDL3.so.0; closure /sdl/lib/libSDL3.so.0
[ -f "$A/usr/lib/libSDL2-2.0.so.0" ] || { echo "takedawn does not link the SDL2 built for it" >&2; exit 1; }

# ── the setup window, when a Qt kit was given ────────────────────────
if [ -d /qt/lib ]; then
    S=/out/setup
    # g++ 10, as Qt itself was built with (the image's clang 11 crashes on
    # Qt 6.10's headers).
    cmake -S /src/game/tools/install/gui -B "$S" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_COMPILER=g++-10 -DCMAKE_PREFIX_PATH=/qt -DTAKEDAWN_ICON_DIR=/stage/icons \
        -DTAKEDAWN_SETUP_GAMEPAD=ON -DSDL2_DIR=/sdl/lib/cmake/SDL2 -DSDL3_DIR=/sdl/lib/cmake/SDL3 \
        -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON '-DCMAKE_INSTALL_RPATH=$ORIGIN/../lib' > /out/setup-configure.log 2>&1 \
        || { tail -20 /out/setup-configure.log >&2; echo "setup window: configure failed" >&2; exit 1; }
    nice -n "${NICE:-10}" cmake --build "$S" --target takedawn-setup -j"$JOBS" > /out/setup-build.log 2>&1 \
        || { tail -30 /out/setup-build.log >&2; echo "setup window: build failed" >&2; exit 1; }
    # The controller in the window is SDL's, the one the game carries.
    LD_LIBRARY_PATH=/qt/lib:/sdl/lib ldd "$S/takedawn-setup" | grep -q 'libSDL2-2.0.so.0 => /sdl/' \
        || { echo "setup window: it does not link the SDL2 built for the game" >&2; exit 1; }
    cp "$S/takedawn-setup" "$A/usr/bin/takedawn-setup"
    printf '[Paths]\nPrefix = ..\nPlugins = plugins\n' > "$A/usr/bin/qt.conf"
    # X11 (also what gamescope gives a game on the Deck) and Wayland, the
    # pieces a Wayland window needs, the desktop portal for file dialogs, and
    # the offscreen platform the checks use.
    for p in platforms/libqxcb.so platforms/libqwayland.so platforms/libqoffscreen.so \
             wayland-shell-integration/libxdg-shell.so platformthemes/libqxdgdesktopportal.so; do
        [ -f "/qt/plugins/$p" ] || { echo "the Qt kit has no plugins/$p" >&2; exit 1; }
        mkdir -p "$A/usr/plugins/$(dirname "$p")"
        cp "/qt/plugins/$p" "$A/usr/plugins/$p"
    done
    for d in wayland-decoration-client wayland-graphics-integration-client; do
        [ -d "/qt/plugins/$d" ] || continue
        mkdir -p "$A/usr/plugins/$d"
        for f in "/qt/plugins/$d"/*.so; do
            # the shared-memory buffers and the client-side frame; not the EGL ones
            case "$(basename "$f")" in *egl*|*dmabuf*|*drm*|*vulkan*) continue ;; esac
            cp "$f" "$A/usr/plugins/$d/"
        done
    done
    closure "$A/usr/bin/takedawn-setup" $(find "$A/usr/plugins" -name '*.so')
    echo "setup window: Qt $(cat /stage/qt-version), $(find "$A/usr/plugins" -name '*.so' | wc -l) plugins"
fi
if [ -s /out/unknown.txt ]; then
    echo "libraries on neither list (tools/release/linux/in-container.sh):" >&2
    sort -u /out/unknown.txt >&2
    exit 1
fi

cp /stage/AppRun "$A/AppRun"; chmod 755 "$A/AppRun"
cp /stage/takedawn.desktop "$A/takedawn.desktop"
cp /stage/takedawn.desktop "$A/usr/share/applications/takedawn.desktop"
cp /stage/takedawn-256.png "$A/takedawn.png"
cp /stage/takedawn-256.png "$A/.DirIcon"
cp /stage/takedawn-256.png "$A/usr/share/icons/hicolor/256x256/apps/takedawn.png"
cp /stage/licenses/* "$A/usr/share/licenses/takedawn/"
cp /stage/README.txt "$A/README.txt"
chmod -R u+rwX,go+rX "$A"
echo "glibc needed: $(objdump -T "$B/bin/takedawn" | grep -o 'GLIBC_[0-9.]*' | sort -V | tail -1)"
echo "carried: $(cut -d' ' -f1 /out/carried.txt | tr '\n' ' ')"
