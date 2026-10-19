#!/usr/bin/env bash
# build-windows.sh: Takedawn for Windows x64, as one setup exe.
#
#   tools/release/build-windows.sh [--no-setup] [--no-setup-window]
#
#   dist/<version>/Takedawn-<version>-windows-x64-setup.exe
#   build/release/windows/Takedawn/           the program folder it installs
#
# The program is cross-built here (macOS or Linux) with llvm-mingw, the way
# every Windows build of this port has been made (tools/win/fetch-deps.sh
# holds the pins: llvm-mingw, SDL2, the Vulkan headers, glslang); only the
# setup compiler needs Windows. The program folder:
#
#   Takedawn.exe          the game; a window program with the icon and a
#                         version resource. Takedawn.exe --install <image|folder>
#   SDL2.dll              the pinned SDL2 (Zlib licence)
#   licenses\             the notices, licence texts and SOURCE.txt
#   README.txt
#   takedawn-setup.exe    the setup window (Qt), which the game starts when
#   Qt6*.dll, libc++.dll, its data is not installed; cross-built here against
#   platforms\, styles\   the pinned qt.io kit (--no-setup-window leaves it out)
#
# glslang, libc++ and winpthreads are linked statically, the UCRT is part of
# Windows 10 and later, vulkan-1.dll is the graphics driver's; the audit below
# fails the build when the exe imports anything else.
#
# The setup exe (tools/release/windows/takedawn.iss: per user, no
# administrator rights, no zip) is compiled with Inno Setup on the Windows
# machine named by TAKEDAWN_WIN_HOST (ssh, PowerShell) in the one folder
# TAKEDAWN_WIN_DIR; tools/release/windows/remote.ps1 says what runs there
# (--version, --install --check with TAKEDAWN_WIN_DISC, the compile, and an
# install of a registry-free test variant into that folder to check the
# payload). Without a host, or with --no-setup, the program folder and the
# .iss are the result and the script says so.
#
# Nothing is code-signed (decided).
source "$(dirname "$0")/common.sh"
source "$REPO/tools/release/pins.env"
SETUP=1; WITH_SETUP="${TAKEDAWN_SETUP:-1}"
for a in "$@"; do case "$a" in --no-setup) SETUP=0 ;; --no-setup-window) WITH_SETUP=0 ;; *) die "unknown option $a" ;; esac; done

assert_gen
B="$WORK/windows"
D="${BO3_WINDEPS:-$HOME/.cache/bo3-windeps}"
t0=$(date +%s)
[ -x "$D/llvm-mingw/bin/x86_64-w64-mingw32-clang" ] && [ -f "$D/glslang/lib/libglslang.a" ] \
    || BO3_WINDEPS="$D" "$REPO/tools/win/fetch-deps.sh"

mkdir -p "$B/icon"
pngs=()
for s in 16 24 32 48 64 128 256; do
    render_icon "$REPO/assets/icon/takedawn.svg" "$s" "$B/icon/$s.png"; pngs+=("$B/icon/$s.png")
done
"${TAKEDAWN_PYTHON:-python3}" "$REPO/tools/release/make_ico.py" "$B/takedawn.ico" "${pngs[@]}"

say "configuring (x86_64-w64-mingw32, SDL2 + Vulkan)"
cmake -S "$REPO" -B "$B/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBO3_RELEASE=ON -DBO3_REGION="$BO3_REGION" \
    -DCMAKE_TOOLCHAIN_FILE="$REPO/cmake/llvm-mingw-x86_64.cmake" -DLLVM_MINGW_ROOT="$D/llvm-mingw" \
    -DXBOXRECOMP_DIR="$TOOLKIT" -DBO3_BIN_DIR="$B/bin" -DBO3_WIN_ICON="$B/takedawn.ico" \
    -DCMAKE_PREFIX_PATH="$D/SDL2/x86_64-w64-mingw32;$D/glslang" \
    -DVulkan_INCLUDE_DIR="$D/vulkan/include" -DVulkan_LIBRARY="$D/vulkan/lib/libvulkan-1.dll.a" \
    > "$B/configure.log" 2>&1 || { tail -20 "$B/configure.log" >&2; die "configure failed ($B/configure.log)"; }
say "building (-j$JOBS, nice $NICE)"
tb=$(date +%s)
nice -n "$NICE" cmake --build "$B/build" --target burnout3 -j"$JOBS" > "$B/build.log" 2>&1 \
    || { tail -30 "$B/build.log" >&2; die "build failed ($B/build.log)"; }
say "build took $(( $(date +%s) - tb )) s"

# ── the program folder ───────────────────────────────────────────────
PKG="$B/Takedawn"
rm -rf "$PKG"; mkdir -p "$PKG"
cp "$B/bin/Takedawn.exe" "$PKG/"
cp "$D/SDL2/x86_64-w64-mingw32/bin/SDL2.dll" "$PKG/"
copy_licenses "$PKG/licenses"
sed -e "s/@VERSION@/$VERSION/g" -e "s/@DISC@/$DISC_NAME/g" "$REPO/tools/release/windows/README.txt" | sed 's/$/\r/' > "$PKG/README.txt"
HAVE_SETUP=no
if [ "$WITH_SETUP" = 1 ]; then
    # The setup window, cross-built against the pinned qt.io kit for llvm-mingw
    # (the host kit of the same version supplies moc and rcc), with the Qt
    # DLLs, the platform plugin and the style beside it: what windeployqt
    # would copy, worked out from the import tables.
    QTW="$(qt_kit windows desktop win64_llvm_mingw)"
    case "$(uname -s)" in Darwin) QTH="$(qt_kit mac desktop clang_64)" ;; *) QTH="$(qt_kit linux desktop linux_gcc_64)" ;; esac
    say "building the setup window (Qt $QT_VERSION for Windows)"
    icon_py=(); [ -n "${TAKEDAWN_PYTHON:-}" ] && icon_py=(-DTAKEDAWN_ICON_PYTHON="$TAKEDAWN_PYTHON")
    LLVM_MINGW="$D/llvm-mingw" cmake -S "$REPO/tools/install/gui" -B "$B/setup" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$REPO/tools/install/gui/packaging/windows-toolchain.cmake" \
        -DCMAKE_PREFIX_PATH="$QTW" -DQT_HOST_PATH="$QTH" -DTAKEDAWN_ICO="$B/takedawn.ico" -DTAKEDAWN_REGION="$BO3_REGION" \
        ${icon_py[@]+"${icon_py[@]}"} > "$B/setup-configure.log" 2>&1 \
        || { tail -20 "$B/setup-configure.log" >&2; die "setup window: configure failed"; }
    LLVM_MINGW="$D/llvm-mingw" nice -n "$NICE" cmake --build "$B/setup" --target takedawn-setup -j"$JOBS" \
        > "$B/setup-build.log" 2>&1 || { tail -30 "$B/setup-build.log" >&2; die "setup window: build failed ($B/setup-build.log)"; }
    mkdir -p "$PKG/platforms" "$PKG/styles"
    cp "$B/setup/takedawn-setup.exe" "$PKG/"
    cp "$QTW/plugins/platforms/qwindows.dll" "$PKG/platforms/"
    cp "$QTW/plugins/styles/qmodernwindowsstyle.dll" "$PKG/styles/" 2>/dev/null || true
    "${TAKEDAWN_PYTHON:-python3}" - "$PKG" "$QTW/bin" "$D/llvm-mingw/x86_64-w64-mingw32/bin" "$D/llvm-mingw/bin/llvm-objdump" <<'PY' || die "setup window: could not gather its DLLs"
import os, shutil, subprocess, sys
pkg, qtbin, rtbin, objdump = sys.argv[1:5]
def deps(path):
    out = subprocess.run([objdump, "-p", path], capture_output=True, text=True).stdout
    return [l.split(":", 1)[1].strip() for l in out.splitlines() if "DLL Name:" in l]
seen, todo = set(), [os.path.join(pkg, "takedawn-setup.exe")]
for sub in ("platforms", "styles"):
    for dp, _, fn in os.walk(os.path.join(pkg, sub)):
        todo += [os.path.join(dp, f) for f in fn]
while todo:
    cur = todo.pop()
    for d in deps(cur):
        key = d.lower()
        if key in seen:
            continue
        seen.add(key)
        for folder in (qtbin, rtbin):
            src = os.path.join(folder, d)
            if os.path.isfile(src):
                if not os.path.isfile(os.path.join(pkg, d)):
                    shutil.copy2(src, pkg)
                todo.append(os.path.join(pkg, d))
                break
print("==> setup window DLLs: " + ", ".join(sorted(f for f in os.listdir(pkg) if f.endswith(".dll"))))
PY
    rm -rf "$PKG/licenses"; copy_licenses "$PKG/licenses" qt
    HAVE_SETUP=yes
else
    say "the folder is built without the setup window (--no-setup-window)"
fi

# What the exe imports: SDL2.dll, the driver's Vulkan loader and Windows.
pe="$("$D/llvm-mingw/bin/llvm-objdump" -p "$PKG/Takedawn.exe")"
imports="$(printf '%s\n' "$pe" | sed -n 's/^ *DLL Name: //p' | tr 'A-Z' 'a-z' | sort -u)"
bad="$(printf '%s\n' "$imports" | grep -v -E '^(sdl2|vulkan-1|kernel32|user32|gdi32|dbghelp|advapi32|shell32|ole32|oleaut32|winmm|ws2_32|bcrypt|ntdll|dxgi|version|imm32|setupapi|shlwapi|psapi|winhttp|api-ms-win-crt-[a-z0-9-]+)\.dll$' || true)"
[ -z "$bad" ] || die "Takedawn.exe imports DLLs the package does not carry: $bad"
case "$pe" in *"(Windows GUI)"*) ;; *) die "Takedawn.exe is not a window program" ;; esac
say "program folder: $(du -sh "$PKG" | cut -f1), Takedawn.exe $(( $(stat -c %s "$PKG/Takedawn.exe" 2>/dev/null || stat -f %z "$PKG/Takedawn.exe") / 1024 / 1024 )) MB (setup window: $HAVE_SETUP)"

HOST="${TAKEDAWN_WIN_HOST:-}"
if [ "$SETUP" = 0 ] || [ -z "$HOST" ]; then
    say "NOT DONE: the setup exe. Compile tools/release/windows/takedawn.iss with Inno Setup $INNOSETUP_VERSION on Windows:"
    say "  ISCC.exe /DPackage=<copy of $PKG> /DVersion=$VERSION /DIcon=<copy of $B/takedawn.ico>$([ "$BO3_REGION" = PAL ] && printf ' /DRegion=PAL') takedawn.iss"
    say "  (set TAKEDAWN_WIN_HOST and TAKEDAWN_WIN_DIR to have this script do it over ssh)"
    exit 0
fi

# ── on the Windows machine ───────────────────────────────────────────
WDIR="${TAKEDAWN_WIN_DIR:?TAKEDAWN_WIN_DIR: the folder on the Windows machine}"
WFWD="$(printf '%s' "$WDIR" | tr '\\' '/')"
say "copying the program folder to the Windows machine"
ssh "$HOST" "New-Item -ItemType Directory -Force '$WDIR' | Out-Null; foreach (\$p in 'Takedawn','out','itest') { \$q = Join-Path '$WDIR' \$p; if (Test-Path \$q) { Remove-Item -Recurse -Force \$q } }" \
    || die "cannot reach the Windows machine"
scp -q -r "$PKG" "$B/takedawn.ico" "$REPO/tools/release/windows/takedawn.iss" \
    "$REPO/tools/release/windows/remote.ps1" "$HOST:$WFWD/" || die "copy to the Windows machine failed"
disc_arg=""
[ -n "${TAKEDAWN_WIN_DISC:-}" ] && disc_arg="-Disc '$TAKEDAWN_WIN_DISC'"
say "running on Windows: --version, the setup compile, the payload check"
ssh "$HOST" "powershell -NoProfile -ExecutionPolicy Bypass -File '$WDIR\\remote.ps1' -Dir '$WDIR' -Version $VERSION -Region $BO3_REGION $disc_arg" \
    > "$B/remote.log" 2>&1 || { tail -15 "$B/remote.log" >&2; die "the Windows step failed ($B/remote.log)"; }
grep -a "^STEP\|^RESULT" "$B/remote.log" | tr -d '\r' | sed 's/^/    /'
mkdir -p "$DIST"
EXE="$DIST/Takedawn-$VERSION$PKG_TAG-windows-x64-setup.exe"
scp -q "$HOST:$WFWD/out/Takedawn-$VERSION$PKG_TAG-windows-x64-setup.exe" "$EXE" || die "could not fetch the setup exe"
scp -q "$HOST:$WFWD/version.txt" "$B/version.txt"
scp -q "$HOST:$WFWD/version-installed.txt" "$B/version-installed.txt"
[ -n "$disc_arg" ] && scp -q "$HOST:$WFWD/check.txt" "$B/check.txt"
want="$(grep -a '^RESULT OK' "$B/remote.log" | tr -d '\r' | awk '{print $3}')"
[ "$(sha256_of "$EXE")" = "$want" ] || die "the fetched setup exe is not the one compiled"
check_version "$B/version.txt" "Takedawn.exe on Windows"
check_version "$B/version-installed.txt" "Takedawn.exe from the test install"
if [ -n "$disc_arg" ]; then
    grep -aq "^RESULT OK" "$B/check.txt" || die "--install --check on Windows gave no RESULT OK"
    say "Windows --install --check: $(grep -a '^STEP disc OK' "$B/check.txt" | tr -d '\r' | cut -c1-100)"
fi
cp "$B/version.txt" "$DIST/version-windows.txt"
say "$(basename "$EXE"): $(( $(stat -f %z "$EXE" 2>/dev/null || stat -c %s "$EXE") / 1024 / 1024 )) MB, sha256 $(sha256_of "$EXE")"
say "Windows done in $(( $(date +%s) - t0 )) s (setup window: $HAVE_SETUP)"
