#!/usr/bin/env bash
# Build the Android app (arm64-v8a): libmain.so with the NDK, then the APK
# with the Android SDK's own tools, without Gradle (the steps are the ones
# Xerenge's scripts/build-android-apk.sh takes; the JDK only compiles SDL's
# activity and runs d8 and apksigner, so any JDK from 17 on will do).
# Run tools/android/fetch-deps.sh once first. docs/android.md has the rest.
#
#   tools/android/build-apk.sh [extra cmake -D options]
#
# Environment:
#   ANDROID_HOME       the SDK (default ~/android-sdk): build-tools, a platform
#   ANDROID_NDK_HOME   the NDK (default: the newest under $ANDROID_HOME/ndk)
#   ANDROID_API        minSdk, as given to fetch-deps.sh (default 33)
#   BO3_ANDROID_DEPS   the deps prefix (default ~/.cache/bo3-android-deps)
#   XBOXRECOMP_DIR     the toolkit checkout (default ../xboxrecomp-android or
#                      ../xboxrecomp next to the repo)
#   BUILD_DIR          default build-android; JOBS
#   BO3_KEYSTORE       the signing key (default ~/.android/bo3-debug.keystore,
#                      made on first use; the same key must sign every build
#                      or Android refuses the update)
#   BO3_ANDROID_TURNIP=0  leave the Turnip driver out of the APK; =mesa: the
#                      Mesa 26.2.4 build of fetch-deps.sh instead of the
#                      prebuilt A725 build (the default, BO-152)
#   DEBUGGABLE=1       mark the app debuggable (run-as, GPU profilers)
#   STRIP=0            keep libmain.so's symbol table and debug info
#   SIGN=0             stop at the aligned, unsigned APK
#                      (out/takedawn-android-arm64-unsigned.apk): a release is
#                      signed in a step of its own (tools/release/build-android.sh)
#   TAKEDAWN_ANDROID_BUILD  0..999, the last three digits of versionCode
#   BO3_LICENSES_DIR   the release's licence set (tools/release/common.sh
#                      copy_licenses); packed as assets/licenses/ with the
#                      texts of the Android-only parts (tools/android/licenses/,
#                      Mesa's Turnip and libadrenotools). The app's Licences
#                      page (LicensesActivity) shows them.
#   BO3_REQUIRE_LICENSES=1  fail unless that set is there and in the APK
#                      (tools/release/build-android.sh sets it)
# Output: out/takedawn-android-arm64.apk, signed with the development key. An
# APK signed with another key than the installed one is refused as an update:
# a development build and a release do not replace each other.
#
# versionName is VERSION's first line; versionCode is
# major * 10 000 000 + minor * 100 000 + patch * 1 000 + build (0..99), so
# every release is above the one before and a rebuild of one can be told
# apart; a pre-release (x.y.z-beta) is 900 below x.y.z (see below).
#
# The program's code (src/game/recomp/gen, from regen.sh) is compiled into
# libmain.so. The game data is not in the APK: the first start installs it
# from the player's own disc image (SetupActivity).
set -euo pipefail
HERE="$(cd "$(dirname "$0")/../.." && pwd)"
# The disc: BO3_REGION in the environment or -DBO3_REGION=... among the cmake
# options (docs/pal.md). The PAL APK is its own app beside the NTSC-U one:
# package io.github.xenophobentx.takedawn.pal ("Takedawn PAL"), the PAL
# manifest, out/takedawn-pal-android-arm64*.apk. The Java classes keep their
# package; only the application's id is renamed.
REGION="${BO3_REGION:-NTSC-U}"
for a in "$@"; do case "$a" in -DBO3_REGION=*) REGION="${a#-DBO3_REGION=}" ;; esac; done
case "$REGION" in
  NTSC-U) DISC_MANIFEST=ntsc-u-retail; APP_ID=io.github.xenophobentx.takedawn; APP_LABEL=Takedawn; APK_TAG="" ;;
  PAL)    DISC_MANIFEST=pal-retail; APP_ID=io.github.xenophobentx.takedawn.pal; APP_LABEL="Takedawn PAL"; APK_TAG="-pal" ;;
  *) echo "BO3_REGION must be NTSC-U or PAL" >&2; exit 1 ;;
esac
REGION_CMAKE=(-DBO3_REGION="$REGION")
SDK="${ANDROID_HOME:-$HOME/android-sdk}"
NDK="${ANDROID_NDK_HOME:-$(ls -d "$SDK"/ndk/* 2>/dev/null | sort -V | tail -1)}"
API="${ANDROID_API:-33}"
TARGET_API=34
ABI=arm64-v8a
D="${BO3_ANDROID_DEPS:-$HOME/.cache/bo3-android-deps}"
TK="${XBOXRECOMP_DIR:-}"
if [ -z "$TK" ]; then
  for c in "$HERE/../xboxrecomp-android" "$HERE/../xboxrecomp"; do [ -d "$c" ] && TK="$c" && break; done
fi
[ -d "$TK" ] || { echo "toolkit checkout not found; set XBOXRECOMP_DIR" >&2; exit 1; }
[ -f "$NDK/build/cmake/android.toolchain.cmake" ] || { echo "NDK not found (ANDROID_NDK_HOME)" >&2; exit 1; }
[ -f "$D/SDL2/lib/libSDL2.so" ] && [ -f "$D/glslang/lib/libglslang.a" ] || "$HERE/tools/android/fetch-deps.sh"
ls "$HERE"/src/game/recomp/gen/recomp_*.c >/dev/null 2>&1 || { echo "no generated C: run regen.sh first" >&2; exit 1; }
B="${BUILD_DIR:-$HERE/build-android}"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}"
HOST_TAG="$(ls "$NDK/toolchains/llvm/prebuilt" | head -1)"
SYSROOT="$NDK/toolchains/llvm/prebuilt/$HOST_TAG/sysroot"
BT="$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)"
PLATFORM="$(ls -d "$SDK"/platforms/android-* | sort -V | tail -1)"
JAR="$PLATFORM/android.jar"

# A replacement GPU driver (docs/android.md, "The GPU driver"): libadrenotools
# when fetch-deps.sh built it, and a Turnip driver, unless BO3_ANDROID_TURNIP=0:
# the prebuilt A725 build (turnip-a725/, BO-152) by default, the Mesa release
# fetch-deps.sh built (turnip/) with BO3_ANDROID_TURNIP=mesa.
TD="$D/turnip-a725"
[ "${BO3_ANDROID_TURNIP:-1}" = mesa ] && TD="$D/turnip"
ADRENO=()
[ -f "$D/adrenotools/lib/libadrenotools.a" ] && ADRENO=(-DXBOXRECOMP_ANDROID_ADRENOTOOLS="$D/adrenotools")

echo "== native (NDK $(basename "$NDK"), android-$API, $ABI)"
cmake -S "$HERE" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=$ABI -DANDROID_PLATFORM=android-$API -DANDROID_STL=c++_shared \
  -DXBOXRECOMP_DIR="$TK" \
  -DCMAKE_FIND_ROOT_PATH="$D/SDL2;$D/glslang" \
  -DCMAKE_PREFIX_PATH="$D/SDL2;$D/glslang" \
  -DVulkan_INCLUDE_DIR="$SYSROOT/usr/include" \
  -DVulkan_LIBRARY="$SYSROOT/usr/lib/aarch64-linux-android/$API/libvulkan.so" \
  -DBO3_BIN_DIR="$B/lib" \
  ${ADRENO[@]+"${ADRENO[@]}"} \
  "${REGION_CMAKE[@]}" "$@"
cmake --build "$B" --target burnout3 -j"$JOBS"

OUT="$B/apk"
rm -rf "$OUT"
mkdir -p "$OUT/gen" "$OUT/classes" "$OUT/dex" "$OUT/pack/lib/$ABI" "$HERE/out"

echo "== resources and manifest ($(basename "$BT"), $(basename "$PLATFORM"))"
"$BT/aapt2" compile --dir "$HERE/android/res" -o "$OUT/res.zip"
# The app's name for this region, over the one in android/res.
mkdir -p "$OUT/overlay/values"
printf '<?xml version="1.0" encoding="utf-8"?>\n<resources>\n    <string name="app_name">%s</string>\n    <string name="driver_name">%s: graphics driver</string>\n</resources>\n' \
  "$APP_LABEL" "$APP_LABEL" > "$OUT/overlay/values/strings.xml"
"$BT/aapt2" compile --dir "$OUT/overlay" -o "$OUT/overlay.zip"
VNAME="$(head -1 "$HERE/VERSION" | tr -d '[:space:]')"
[[ "$VNAME" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)(-[0-9A-Za-z.]+)?$ ]] || { echo "VERSION: $VNAME is not major.minor.patch[-pre-release]" >&2; exit 1; }
VBUILD="${TAKEDAWN_ANDROID_BUILD:-0}"
[ "$VBUILD" -ge 0 ] && [ "$VBUILD" -le 99 ] || { echo "TAKEDAWN_ANDROID_BUILD: 0..99" >&2; exit 1; }
VCODE=$(( BASH_REMATCH[1] * 10000000 + BASH_REMATCH[2] * 100000 + BASH_REMATCH[3] * 1000 + VBUILD ))
# A pre-release (0.1.2-beta) is numbered after every build of the release
# before it and before every build of its own release: the patch before's
# code + 100 (0.1.2-beta: 101100 + build; 0.1.1 builds are 101000..101099,
# 0.1.2's 102000..102099). A patch-0 pre-release (0.2.0-beta) takes the
# minor before's last patch slot the same way.
if [ -n "${BASH_REMATCH[4]}" ]; then
    VCODE=$(( VCODE - 900 ))
fi
MANIFEST_ID="$(sed -n 's/^# id //p' "$HERE/config/disc/$DISC_MANIFEST.manifest" | head -1)"
[ -n "$MANIFEST_ID" ] || { echo "no id line in config/disc/$DISC_MANIFEST.manifest" >&2; exit 1; }
echo "   $APP_LABEL $VNAME ($APP_ID), versionCode $VCODE, disc manifest $MANIFEST_ID"
mkdir -p "$OUT/gen/io/github/xenophobentx/takedawn"
cat > "$OUT/gen/io/github/xenophobentx/takedawn/BuildInfo.java" <<JAVA
package io.github.xenophobentx.takedawn;

/** Made by tools/android/build-apk.sh. */
final class BuildInfo {
    static final String VERSION = "$VNAME";
    /** The disc manifest the native library was built with (the installer's marker names it). */
    static final String MANIFEST_ID = "$MANIFEST_ID";
}
JAVA

# The licence texts (assets/licenses/): the release set, then what only this
# APK has. A copy of this program carries them with it.
AL="$OUT/assets/licenses"
mkdir -p "$AL"
if [ -n "${BO3_LICENSES_DIR:-}" ] && [ -d "$BO3_LICENSES_DIR" ]; then
  cp "$BO3_LICENSES_DIR"/* "$AL/"
elif [ "${BO3_REQUIRE_LICENSES:-0}" = 1 ]; then
  echo "BO3_LICENSES_DIR ($BO3_LICENSES_DIR) is not a folder: a release APK carries the licences" >&2; exit 1
else
  echo "   NO release licence set (BO3_LICENSES_DIR): a development build; Licences shows the Android parts only"
fi
if [ ${#ADRENO[@]} -gt 0 ]; then
  cp "$HERE/tools/android/licenses/libadrenotools-BSD-2-Clause.txt" \
     "$HERE/tools/android/licenses/linkernsbypass-BSD-2-Clause.txt" "$AL/"
  if [ "${BO3_ANDROID_TURNIP:-1}" != 0 ] && [ -f "$TD/libvulkan_freedreno.so" ]; then
    cp "$HERE"/tools/android/licenses/Mesa-*.txt "$AL/"
    # Which Mesa, from where, with which change: the source of the driver.
    cp "$TD/SOURCE.txt" "$AL/Mesa-Turnip-SOURCE.txt"
    if [ "$TD" = "$D/turnip" ]; then
      for p in "$HERE"/tools/android/patches/*.patch; do
        [ -f "$p" ] && cp "$p" "$AL/Mesa-Turnip-$(basename "$p" .patch)-patch.txt"
      done
    fi
  fi
fi
if [ "${BO3_REQUIRE_LICENSES:-0}" = 1 ]; then
  for f in THIRD_PARTY.txt SOURCE.txt takedawn-LICENSE.txt xboxrecomp-LGPL-2.1.txt xboxrecomp-NOTICE.txt \
           DearImGui-MIT.txt SDL2-Zlib.txt glslang.txt LLVM-Apache-2.0-with-exceptions.txt \
           libadrenotools-BSD-2-Clause.txt Mesa-license.txt Mesa-MIT.txt Mesa-Turnip-SOURCE.txt; do
    [ -s "$AL/$f" ] || { echo "licence file $f is missing from the APK's assets" >&2; exit 1; }
  done
fi
echo "   $(ls "$AL" | wc -l | tr -d ' ') licence files in assets/licenses"

"$BT/aapt2" link -o "$OUT/base.apk" -I "$JAR" --manifest "$HERE/android/AndroidManifest.xml" \
  --rename-manifest-package "$APP_ID" --custom-package io.github.xenophobentx.takedawn -R "$OUT/overlay.zip" --auto-add-overlay \
  --java "$OUT/gen" ${DEBUGGABLE:+--debug-mode} --version-code "$VCODE" --version-name "$VNAME" \
  --min-sdk-version "$API" --target-sdk-version "$TARGET_API" -A "$OUT/assets" "$OUT/res.zip"

echo "== java (SDL's activity, the start and game activities)"
find "$D/SDL2/java" "$HERE/android/java" "$OUT/gen" -name '*.java' > "$OUT/sources.txt"
javac -nowarn -Xlint:none -source 17 -target 17 -encoding UTF-8 \
  -classpath "$JAR" -d "$OUT/classes" @"$OUT/sources.txt"
find "$OUT/classes" -name '*.class' > "$OUT/classes.txt"
[ -s "$OUT/classes.txt" ] || { echo "javac produced nothing" >&2; exit 1; }
"$BT/d8" --release --min-api "$API" --lib "$JAR" --output "$OUT/dex" @"$OUT/classes.txt"

echo "== native libraries"
cp "$B/lib/libmain.so" "$D/SDL2/lib/libSDL2.so" \
   "$SYSROOT/usr/lib/aarch64-linux-android/libc++_shared.so" "$OUT/pack/lib/$ABI/"
if [ ${#ADRENO[@]} -gt 0 ]; then
  # libadrenotools opens its hooks from the app's native library directory.
  cp "$D/adrenotools/lib/libhook_impl.so" "$D/adrenotools/lib/libmain_hook.so" \
     "$D/adrenotools/lib/libfile_redirect_hook.so" "$D/adrenotools/lib/libgsl_alloc_hook.so" \
     "$OUT/pack/lib/$ABI/"
  if [ "${BO3_ANDROID_TURNIP:-1}" != 0 ] && [ -f "$TD/libvulkan_freedreno.so" ]; then
    cp "$TD/libvulkan_freedreno.so" "$OUT/pack/lib/$ABI/"
    echo "   with $(head -1 "$TD/SOURCE.txt")"
  fi
fi
if [ "${STRIP:-1}" != 0 ]; then
  # The dynamic symbol table stays: the crash report names sub_XXXXXXXX
  # from it (dladdr).
  # The Turnip driver goes in as it is (the prebuilt one is checked by its
  # sha256; the Mesa build is stripped already).
  for so in "$OUT/pack/lib/$ABI/"*.so; do
    [ "$(basename "$so")" = libvulkan_freedreno.so ] && continue
    "$NDK/toolchains/llvm/prebuilt/$HOST_TAG/bin/llvm-strip" --strip-unneeded "$so"
  done
fi

echo "== packing"
cp "$OUT/base.apk" "$OUT/unaligned.apk"
cp "$OUT/dex/classes.dex" "$OUT/pack/"
(cd "$OUT/pack" && zip -q -r "$OUT/unaligned.apk" classes.dex lib)
"$BT/zipalign" -f 4 "$OUT/unaligned.apk" "$OUT/aligned.apk"

if [ "${SIGN:-1}" = 0 ]; then
  APK="$HERE/out/takedawn$APK_TAG-android-arm64-unsigned.apk"
  cp "$OUT/aligned.apk" "$APK"
  ls -l "$APK"
  exit 0
fi
KS="${BO3_KEYSTORE:-$HOME/.android/bo3-debug.keystore}"
if [ ! -f "$KS" ]; then
  mkdir -p "$(dirname "$KS")"
  keytool -genkeypair -keystore "$KS" -storepass android -keypass android \
    -alias bo3 -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=burnout3 debug" >/dev/null
fi
APK="$HERE/out/takedawn$APK_TAG-android-arm64.apk"
"$BT/apksigner" sign --ks "$KS" --ks-pass pass:android --key-pass pass:android --out "$APK" "$OUT/aligned.apk"
ls -l "$APK"
