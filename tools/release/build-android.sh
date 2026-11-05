#!/usr/bin/env bash
# build-android.sh: the Android APK of a release (arm64-v8a).
#
#   tools/release/build-android.sh [--no-sign]
#
# The APK is built on a Linux machine that has the Android SDK and NDK
# (TAKEDAWN_ANDROID_HOST, over ssh), from the very files of this checkout:
# the two source trees and the stamped generated code (tools/release/gen.sh)
# are copied there with rsync, tools/android/build-apk.sh makes the unsigned,
# aligned APK, and it comes back here. The build machine never sees the
# signing key: the signing is a step of its own on this machine
# (tools/release/sign-android.sh), where the key lives.
#
# What is asserted: the generated code is the stamped tree for this checkout
# (common.sh assert_gen), the copy on the build machine hashes to the same
# tree, the regen key the build prints is this checkout's, and the APK's
# version name and code are VERSION's.
#
# The licence texts: the Mac/Linux/Windows set (common.sh copy_licenses, with
# SOURCE.txt) is staged here and copied to the build machine, where
# build-apk.sh packs it as assets/licenses/ with the texts of the parts only
# the APK has (tools/android/licenses/). The build fails without them and so
# does this script (common.sh check_apk_licenses) on the unsigned and the
# signed APK.
#
# In the APK: libmain.so, libmain_pal.so and libmain_ensv.so (the three
# discs' programs, src/game/package.h; the app loads the installed disc's),
# SDL2, libc++, libadrenotools' hooks,
# a Mesa Turnip driver: by default the prebuilt A725 build that
# tools/android/fetch-deps.sh takes out of UnleashedRecomp-Android's release
# (BO-152, docs/android.md). No game data, no shader key list.
#
# Environment, or tools/release/local.env (git-ignored); no machine name is
# in a committed file:
#   TAKEDAWN_ANDROID_HOST   ssh name of the build machine
#   TAKEDAWN_ANDROID_DIR    the one folder used on it (default takedawn-build/android)
#   TAKEDAWN_ANDROID_ENV    a file on it to source first: ANDROID_HOME,
#                           ANDROID_NDK_HOME, JAVA_HOME, PATH (cmake, ninja),
#                           BO3_ANDROID_DEPS (optional)
#                           and TAKEDAWN_PYTHON, a Python there with resvg_py
#                           (the button-prompt textures are drawn at build time)
#   TAKEDAWN_ANDROID_BUILD  0..999, the last digits of versionCode (default 0)
# Output: dist/<version>/Takedawn-<version>-android-arm64-unsigned.apk and,
# signed, Takedawn-<version>-android-arm64.apk.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STAMPED=1
if [ -f "$HERE/common.sh" ]; then
    # shellcheck source=/dev/null
    source "$HERE/common.sh"
    assert_gen_all
else
    # Before the release scripts' common part is in this checkout: the same
    # names, and a build that says it is not a release.
    REPO="$(cd "$HERE/../.." && pwd)"
    if [ -f "$HERE/local.env" ]; then set -a; . "$HERE/local.env"; set +a; fi
    TOOLKIT="$(cd "${XBOXRECOMP_DIR:-$REPO/../xboxrecomp}" && pwd)"
    GEN="${TAKEDAWN_GEN:-$REPO/src/game/recomp/gen}"
    VERSION="$(sed -n 1p "$REPO/VERSION" | tr -d '\r ')"
    DIST="${TAKEDAWN_DIST:-$REPO/dist/$VERSION}"
    say() { printf '==> %s\n' "$*"; }
    die() { printf '!! %s\n' "$*" >&2; exit 1; }
    sha256_of() { shasum -a 256 "$1" | cut -d' ' -f1; }
    gen_tree_hash() {
        local t; t="$(mktemp)"
        ( cd "${1:-$GEN}" && for f in $(ls | grep '^recomp_' | LC_ALL=C sort); do
              printf '%s  %s\n' "$(sha256_of "$f")" "$f"; done ) > "$t"
        sha256_of "$t"; rm -f "$t"
    }
    GEN_TREE="$(gen_tree_hash)"; GEN_KEY=""; STAMPED=0
    say "NOT A RELEASE BUILD: no tools/release/common.sh here, the generated code is not checked against a stamp"
fi
[ -n "${TAKEDAWN_ANDROID_HOST:-}" ] || die "set TAKEDAWN_ANDROID_HOST (the ssh name of the Android build machine)"
H="$TAKEDAWN_ANDROID_HOST"
D="${TAKEDAWN_ANDROID_DIR:-takedawn-build/android}"
ENVF="${TAKEDAWN_ANDROID_ENV:-}"
ls "$GEN"/recomp_0*.c >/dev/null 2>&1 || die "no generated code in $GEN"
# Every program's generated code (common.sh PKG_REGIONS: gen, gen.pal,
# gen.pal-ensv), each with its tree and key to compare with the build's.
GENS=""; WANT=""
if [ "$STAMPED" = 1 ]; then
    for r in $PKG_REGIONS; do
        use_region "$r"; assert_gen
        GENS="$GENS $(basename "$GEN")"; WANT="$WANT $(basename "$GEN"):$GEN_TREE:$GEN_KEY"
    done
    use_region "${PKG_REGIONS%% *}"; assert_gen
else
    GENS="gen"; WANT="gen:$GEN_TREE:"
fi

say "copying the sources and the generated code to the build machine"
ssh "$H" "mkdir -p '$D/src/takedawn/src/game/recomp/gen' '$D/src/xboxrecomp' '$D/licenses'"
if [ "$STAMPED" = 1 ]; then
    LICSTAGE="$(mktemp -d)"
    copy_licenses "$LICSTAGE/licenses"
    rsync -a --delete "$LICSTAGE/licenses/" "$H:$D/licenses/"
    rm -rf "$LICSTAGE"
else
    ssh "$H" "rm -rf '$D/licenses'/*"
fi
# Only what git tracks (BO-200: a worktree's saves/, runs/ and scratch are not
# part of a build, and once filled the build machine's disk). Only
# nothing else is copied; the generated code has its own copy below.
git -C "$REPO" ls-files -z | rsync -a --from0 --files-from=- "$REPO/" "$H:$D/src/takedawn/"
git -C "$TOOLKIT" ls-files -z | rsync -a --from0 --files-from=- "$TOOLKIT/" "$H:$D/src/xboxrecomp/"
for g in $GENS; do
    ssh "$H" "mkdir -p '$D/src/takedawn/src/game/recomp/$g'"
    rsync -a --delete "$REPO/src/game/recomp/$g/" "$H:$D/src/takedawn/src/game/recomp/$g/"
done
# The copies have no git: say which commits they are (the toolkit's
# cmake/build_id.cmake reads BUILD_COMMIT), so the program's version line
# names them as on every other platform.
if [ "$STAMPED" = 1 ]; then
    ssh "$H" "printf '%s\n' '$(build_commit_line "$REPO")' > '$D/src/takedawn/BUILD_COMMIT';
              printf '%s\n' '$(build_commit_line "$TOOLKIT")' > '$D/src/xboxrecomp/BUILD_COMMIT'"
fi

say "building the unsigned APK"
LICENV=""
if [ "$STAMPED" = 1 ]; then LICENV='export BO3_LICENSES_DIR="$PWD/../../licenses" BO3_REQUIRE_LICENSES=1 BO3_REQUIRE_ALL_PROGRAMS=1'; fi
# shellcheck disable=SC2087
ssh "$H" bash -s <<REMOTE
set -euo pipefail
${ENVF:+. "$ENVF"}
cd "$D/src/takedawn"
export XBOXRECOMP_DIR="\$PWD/../xboxrecomp" BUILD_DIR="\$PWD/../../build" SIGN=0
export TAKEDAWN_ANDROID_BUILD="${TAKEDAWN_ANDROID_BUILD:-0}"
$LICENV
tools/android/fetch-deps.sh > ../../fetch-deps.log 2>&1 || { tail -20 ../../fetch-deps.log; exit 1; }
tools/android/build-apk.sh -DBO3_RELEASE=ON \${TAKEDAWN_PYTHON:+-DBO3_GLYPHS_PYTHON=\$TAKEDAWN_PYTHON} > ../../build-apk.log 2>&1 || { grep -n "error" ../../build-apk.log | head -20; tail -5 ../../build-apk.log; exit 1; }
grep -a "regen key" ../../build-apk.log || true
grep -a "versionCode\|with Mesa\|licence files" ../../build-apk.log || true
TDIR=turnip-a725; [ "\${BO3_ANDROID_TURNIP:-1}" = mesa ] && TDIR=turnip
cat "\${BO3_ANDROID_DEPS:-\$HOME/.cache/bo3-android-deps}/\$TDIR/SOURCE.txt" | sed 's/^/   turnip: /'
REMOTE
mkdir -p "$DIST"
log="$(mktemp)"
ssh "$H" "cat '$D/build-apk.log'" > "$log"
rkeys="$(sed -n 's/.*regen key \([0-9a-f]*\).*/\1/p' "$log" | sort -u | tr '\n' ' ')"
rm -f "$log"
for w in $WANT; do
    g="${w%%:*}"; rest="${w#*:}"; tree="${rest%%:*}"; key="${rest#*:}"
    rtree="$(ssh "$H" "cd '$D/src/takedawn/src/game/recomp/$g' && t=\$(mktemp) && for f in \$(ls | grep '^recomp_' | LC_ALL=C sort); do printf '%s  %s\n' \"\$(sha256sum \"\$f\" | cut -d' ' -f1)\" \"\$f\"; done > \$t && sha256sum \$t | cut -d' ' -f1; rm -f \$t")"
    [ "$rtree" = "$tree" ] || die "$g: the generated code on the build machine ($rtree) is not this checkout's ($tree)"
    if [ "$STAMPED" = 1 ]; then
        case " $rkeys " in *" $key "*) ;; *) die "$g: no build had this checkout's regen key $key (the builds': $rkeys)" ;; esac
    fi
done
rkey="$rkeys"
U="$DIST/Takedawn-$VERSION-android-arm64-unsigned.apk"
scp -q "$H:$D/src/takedawn/out/takedawn-android-arm64-unsigned.apk" "$U"
for lib in libmain.so libmain_pal.so libmain_ensv.so; do
    [ "$STAMPED" = 0 ] && [ "$lib" != libmain.so ] && continue
    unzip -Z1 "$U" | grep -qx "lib/arm64-v8a/$lib" || die "the APK has no lib/arm64-v8a/$lib"
done
[ "$STAMPED" = 0 ] || check_apk_licenses "$U"
say "unsigned: $U ($(wc -c < "$U" | tr -d ' ') bytes, sha256 $(sha256_of "$U"))"
say "generated code:$(for w in $WANT; do printf ' %s %s' "${w%%:*}" "$(printf '%s' "${w#*:}" | cut -c1-16)"; done)${rkey:+; regen keys $rkey}"
if [ "${1:-}" = "--no-sign" ]; then exit 0; fi
"$HERE/sign-android.sh" "$U" "$DIST/Takedawn-$VERSION-android-arm64.apk"
[ "$STAMPED" = 0 ] || check_apk_licenses "$DIST/Takedawn-$VERSION-android-arm64.apk"
