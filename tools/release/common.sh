# common.sh: sourced by the scripts in tools/release/.
#
# Where things are, the same for every script:
#
#   REPO        this checkout
#   TOOLKIT     the toolkit checkout: XBOXRECOMP_DIR, else ../xboxrecomp
#   GEN         src/game/recomp/gen, the generated C (never committed)
#   DIST        dist/<version>/ in this checkout (git-ignored): the artefacts
#   WORK        build/release/ in this checkout (git-ignored): build trees
#   DEPS        ~/.cache/takedawn-deps (TAKEDAWN_DEPS): pinned libraries
#
# Machine names and other private settings are never in a committed file:
# they come from the environment or from the git-ignored
# tools/release/local.env (KEY=VALUE lines, see local.env.example); the
# environment wins.
#
#   XBOXRECOMP_DIR        the toolkit checkout
#   TAKEDAWN_WIN_HOST     ssh name of the Windows build machine
#   TAKEDAWN_WIN_DIR      the one folder used on it (a Windows path)
#   TAKEDAWN_LINUX_HOST   ssh name of the Linux build machine (podman)
#   TAKEDAWN_LINUX_DIR    the one folder used on it (default ~/takedawn-build)
#   TAKEDAWN_DISC         a disc image or extracted folder for the smoke runs
#   TESTHOST_LOCK         the shared test-machine lock script (optional)
#   JOBS, NICE            build parallelism (default 4) and niceness (10)

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
_env_file="$REPO/tools/release/local.env"
if [ -f "$_env_file" ]; then
    while IFS= read -r _line || [ -n "$_line" ]; do
        case "$_line" in ''|'#'*) continue ;; esac
        _k="${_line%%=*}"; _v="${_line#*=}"
        # the environment wins over the file
        if [ -z "${!_k+x}" ]; then export "$_k=$_v"; fi
    done < "$_env_file"
fi

TOOLKIT="${XBOXRECOMP_DIR:-$REPO/../xboxrecomp}"
[ -d "$TOOLKIT/tools/recomp" ] || { echo "toolkit checkout not found at $TOOLKIT; set XBOXRECOMP_DIR" >&2; exit 1; }
TOOLKIT="$(cd "$TOOLKIT" && pwd)"
export XBOXRECOMP_DIR="$TOOLKIT"

# The disc the release is for: BO3_REGION=NTSC-U (the default) or PAL
# (docs/pal.md). It picks the build of supported.json, config/ or
# config/pal/, and the manifest; and the packages' identity, so that the two
# install side by side and their files never overwrite each other:
# APP_TITLE (the app's name), PKG_TAG (in every package file name:
# Takedawn-<version><tag>-...), ID_SUFFIX (after the macOS bundle id and the
# Android package id).
BO3_REGION="${BO3_REGION:-NTSC-U}"
case "$BO3_REGION" in
    NTSC-U) BUILD_ID=ntsc-u-retail; CFG="$REPO/config"; DISC_NAME="NTSC-U (North America)"
            APP_TITLE="Takedawn"; PKG_TAG=""; ID_SUFFIX="" ;;
    PAL)    BUILD_ID=pal-retail;    CFG="$REPO/config/pal"; DISC_NAME="PAL (Europe)"
            APP_TITLE="Takedawn PAL"; PKG_TAG="-pal"; ID_SUFFIX=".pal" ;;
    *) echo "BO3_REGION must be NTSC-U or PAL" >&2; exit 1 ;;
esac
export BO3_REGION

GEN="$REPO/src/game/recomp/gen"
WORK="${TAKEDAWN_WORK:-$REPO/build/release$PKG_TAG}"
DEPS="${TAKEDAWN_DEPS:-$HOME/.cache/takedawn-deps}"
JOBS="${JOBS:-4}"
NICE="${NICE:-10}"

VERSION="$(sed -n 1p "$REPO/VERSION" | tr -d '\r ')"
CODENAME="$(sed -n 2p "$REPO/VERSION" | tr -d '\r')"
DIST="${TAKEDAWN_DIST:-$REPO/dist/$VERSION$PKG_TAG}"

say()  { printf '==> %s\n' "$*"; }
die()  { printf '!! %s\n' "$*" >&2; exit 1; }

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
    else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

# The commit of a checkout as the program prints it (the toolkit's
# cmake/build_id.cmake): 10 hex digits, "-dirty" when tracked files have
# changes that are not committed.
commit_of() {
    local c
    c="$(git -C "$1" rev-parse --short=10 HEAD)"
    if [ -n "$(git -C "$1" status --porcelain --untracked-files=no)" ]; then c="$c-dirty"; fi
    printf '%s' "$c"
}

# What a copy of a checkout without git says it was built from: the line for
# its BUILD_COMMIT file (the toolkit's cmake/build_id.cmake reads it), in the
# words a build inside the checkout would use.
build_commit_line() {
    printf '%s (%s)' "$(commit_of "$1")" \
        "$(git -C "$1" rev-parse --abbrev-ref HEAD | sed 's/[^A-Za-z0-9_./-]/_/g')"
}

# The regen key of these two checkouts, from the rule's one home
# (cmake/release_info.cmake, run as a script).
regen_key() {
    local t
    t="$(mktemp -d)"
    cmake -DBO3_ROOT="$REPO" -DXBOXRECOMP_DIR="$TOOLKIT" -DBO3_RELEASE_DIR="$t" -DBO3_REGION="$BO3_REGION" \
          -P "$REPO/cmake/release_info.cmake" >/dev/null 2>&1 \
        || { rm -rf "$t"; die "cmake/release_info.cmake failed"; }
    sed -n 's/^#define BO3_REGEN_KEY "\(.*\)"/\1/p' "$t/bo3_release.h"
    rm -rf "$t"
}
manifest_id() { sed -n 's/^# id \([0-9a-f]*\).*/\1/p' "$REPO/config/disc/$BUILD_ID.manifest"; }

# The hash of the generated files: "<sha256>  <name>" a line, sorted by name,
# hashed. The same rule is in cmake/release_info.cmake (BO3_RELEASE).
gen_tree_hash() {
    local dir="${1:-$GEN}" tmp
    tmp="$(mktemp)"
    ( cd "$dir" && for f in $(ls | grep '^recomp_' | LC_ALL=C sort); do
          printf '%s  %s\n' "$(sha256_of "$f")" "$f"; done ) > "$tmp"
    sha256_of "$tmp"
    rm -f "$tmp"
}
stamp_field() { sed -n "s/^$1 //p" "$GEN/regen.stamp" 2>/dev/null | head -1; }

# The generated code is the stamped tree for this checkout, patches in.
# Every build script starts with this.
assert_gen() {
    [ -f "$GEN/regen.stamp" ] || die "no $GEN/regen.stamp: run tools/release/gen.sh first"
    local key tree patches
    key="$(regen_key)"
    [ "$(stamp_field key)" = "$key" ] \
        || die "generated code has regen key $(stamp_field key), this checkout gives $key: run tools/release/gen.sh"
    tree="$(gen_tree_hash)"
    [ "$(stamp_field tree)" = "$tree" ] || die "generated files changed since they were stamped"
    patches="$(stamp_field patches)"
    case "$patches" in
        0/*|'') die "generated code has no guest patches ($patches)" ;;
    esac
    [ "${patches%/*}" = "${patches#*/}" ] || die "guest patches missing: $patches"
    GEN_KEY="$key"; GEN_TREE="$tree"; GEN_PATCHES="$patches"
}

# One "key value" line of a program's --version output (a file of it).
version_field() { tr -d '\r' < "$1" | sed -n "s/^$2 //p" | head -1; }

# The --version output of a built program says what this checkout says.
# check_version <file with the output> <label>
check_version() {
    local f="$1" label="$2" want got
    for pair in "version $VERSION" "regen $GEN_KEY" \
                "game $(commit_of "$REPO")" "toolkit $(commit_of "$TOOLKIT")"; do
        want="${pair#* }"; got="$(version_field "$f" "${pair%% *}")"
        got="${got%% *}"
        [ "$got" = "$want" ] || die "$label: --version says ${pair%% *} '$got', expected '$want'"
    done
    got="$(version_field "$f" manifest)"; got="${got%% *}"
    [ "$got" = "$(manifest_id)" ] || die "$label: manifest id '$got', expected '$(manifest_id)'"
    got="$(version_field "$f" gen)"
    [ "$got" = "$GEN_TREE patches $GEN_PATCHES" ] \
        || die "$label: generated code '$got', expected '$GEN_TREE patches $GEN_PATCHES'"
    say "$label: version, commits, regen key, manifest id and patches agree"
}

# The icon PNG at a size from assets/icon. Wants resvg (the resvg-py module,
# or the resvg / rsvg-convert programs); TAKEDAWN_PYTHON names a Python that
# has resvg_py.
render_icon() {  # <svg> <size> <out.png>
    local py="${TAKEDAWN_PYTHON:-python3}"
    if "$py" -c 'import resvg_py' 2>/dev/null; then
        "$py" - "$1" "$2" "$3" <<'EOF'
import sys, resvg_py
svg, size, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
png = resvg_py.svg_to_bytes(svg_path=svg, width=size, height=size)
open(out, "wb").write(bytes(png))
EOF
    elif command -v resvg >/dev/null 2>&1; then resvg -w "$2" -h "$2" "$1" "$3"
    elif command -v rsvg-convert >/dev/null 2>&1; then rsvg-convert -w "$2" -h "$2" "$1" -o "$3"
    else die "no SVG renderer: set TAKEDAWN_PYTHON to a Python with resvg_py, or install resvg"
    fi
}

# A qt.io Qt kit in the cache, fetched once with the pinned aqtinstall (a venv
# of its own; nothing system-wide). qt_kit <host> <target> <arch> -> the kit's
# path, e.g. qt_kit mac desktop clang_64 -> $DEPS/qt/<version>/macos.
# TAKEDAWN_QT_<HOST> (TAKEDAWN_QT_MAC, ...) names an existing kit instead.
qt_kit() {
    local host="$1" target="$2" arch="$3" sub over
    over="TAKEDAWN_QT_$(printf '%s' "$host" | tr 'a-z' 'A-Z')"
    if [ -n "${!over:-}" ]; then printf '%s' "${!over}"; return; fi
    local archives="qtbase"
    case "$arch" in
        clang_64) sub=macos ;;
        linux_gcc_64) sub=gcc_64 ;;      # host tools for a cross build only: see linux/build-qt.sh
        win64_llvm_mingw) sub=llvm-mingw_64 ;;
        *) sub="$arch" ;;
    esac
    if [ ! -d "$DEPS/qt/$QT_VERSION/$sub/lib" ]; then
        [ -x "$DEPS/aqt-venv/bin/aqt" ] || {
            python3 -m venv "$DEPS/aqt-venv" >&2
            "$DEPS/aqt-venv/bin/pip" install -q "aqtinstall==$AQT_VERSION" >&2; }
        "$DEPS/aqt-venv/bin/aqt" install-qt "$host" "$target" "$QT_VERSION" "$arch" \
            --archives $archives -O "$DEPS/qt" >&2 || die "could not fetch Qt $QT_VERSION ($arch)"
    fi
    printf '%s' "$DEPS/qt/$QT_VERSION/$sub"
}

# The licences folder every package carries (docs/release.md).
copy_licenses() {  # <dest dir> [qt]: with "qt", Qt's notices too
    local dest="$1"; shift
    mkdir -p "$dest"
    # The names are the ones the setup window's "About and licences" lists
    # (tools/install/gui/installer_window.cpp).
    cp "$REPO/tools/release/licenses/THIRD_PARTY_NOTICES.md" "$dest/THIRD_PARTY.txt"
    cp "$REPO/tools/release/licenses/"*.txt "$dest/"
    cp "$REPO/LICENSE" "$dest/takedawn-LICENSE.txt"
    cp "$TOOLKIT/LICENSE" "$dest/xboxrecomp-LICENSE.txt"
    cp "$TOOLKIT/NOTICE" "$dest/xboxrecomp-NOTICE.txt"
    cp "$TOOLKIT/LICENSES/LGPL-2.1.txt" "$dest/xboxrecomp-LGPL-2.1.txt"
    cp "$TOOLKIT/third_party/imgui/LICENSE.txt" "$dest/DearImGui-MIT.txt"
    cp "$TOOLKIT/third_party/smaa/LICENSE.txt" "$dest/SMAA-MIT.txt"
    # The overlay's second font (a subset of DejaVu Sans Mono): its licence
    # asks for the notices with every copy.
    [ -f "$TOOLKIT/third_party/dejavu/LICENSE.txt" ] && \
        cp "$TOOLKIT/third_party/dejavu/LICENSE.txt" "$dest/DejaVu-fonts-LICENSE.txt"
    # The overlay's text font (a subset of Roboto Condensed): the OFL goes
    # with every copy.
    [ -f "$TOOLKIT/third_party/roboto_condensed/OFL.txt" ] && \
        cp "$TOOLKIT/third_party/roboto_condensed/OFL.txt" "$dest/RobotoCondensed-OFL.txt"
    cp "$TOOLKIT/LICENSES/ADPCM-XQ-BSD-3-Clause.txt" "$dest/ADPCM-XQ-BSD-3-Clause.txt"
    cp "$TOOLKIT/third_party/dr_libs/LICENSE.txt" "$dest/dr_libs-LICENSE.txt"
    cp "$TOOLKIT/third_party/stb_vorbis/LICENSE.txt" "$dest/stb_vorbis-LICENSE.txt"
    if [ "${1:-}" = qt ]; then
        cp "$REPO/tools/install/gui/licenses/"Qt-*.txt "$dest/"
        sed -i.bak "s/@QT_VERSION@/${QT_VERSION:?pins.env}/g" "$dest/Qt-NOTICE.txt" && rm -f "$dest/Qt-NOTICE.txt.bak"
    fi
    # Where the corresponding source of this very build is.
    cat > "$dest/SOURCE.txt" <<EOF
Takedawn $VERSION "$CODENAME"

This program was built from these two source trees:

  game     $(commit_of "$REPO")
  toolkit  $(commit_of "$TOOLKIT")   (xboxrecomp; it holds the LGPL-2.1 files
                                 listed in xboxrecomp-NOTICE.txt: src/apu/apu_core.c,
                                 apu_dsp.c, apu_vp.c and their headers, from xemu)

The complete source of both, at exactly these commits, is published with
this release: see the release page this file came from (the source archives
beside the downloads, and the two public repositories at the tags of this
version). That is the corresponding source the LGPL asks for; the build
scripts are in the game repository under tools/release/.

The game's own code and data are not part of this program's source: the
program is made from the user's disc's executable by the toolkit's
recompiler, and the data is installed from the user's own disc.
EOF
}

# A release APK carries the licence texts: the same set as the other
# packages (copy_licenses) and the texts of the Android-only parts, as
# assets/licenses/ (the app's Licences page shows them). Fails otherwise.
check_apk_licenses() {  # <apk>
    local apk="$1" list f n
    [ -f "$apk" ] || die "no APK at $apk to check"
    list="$(unzip -Z1 "$apk" 2>/dev/null)" || die "cannot list $apk"
    n="$(printf '%s\n' "$list" | grep -c '^assets/licenses/.')" || true
    [ "${n:-0}" -gt 0 ] || die "$(basename "$apk") has no assets/licenses: a release APK ships the licence texts"
    for f in THIRD_PARTY.txt SOURCE.txt takedawn-LICENSE.txt xboxrecomp-LICENSE.txt xboxrecomp-NOTICE.txt \
             xboxrecomp-LGPL-2.1.txt DearImGui-MIT.txt SDL2-Zlib.txt glslang.txt \
             LLVM-Apache-2.0-with-exceptions.txt libadrenotools-BSD-2-Clause.txt \
             linkernsbypass-BSD-2-Clause.txt Mesa-license.txt Mesa-MIT.txt Mesa-Turnip-SOURCE.txt; do
        printf '%s\n' "$list" | grep -qx "assets/licenses/$f" \
            || die "$(basename "$apk"): assets/licenses/$f is missing"
    done
    # not empty either
    for f in THIRD_PARTY.txt SOURCE.txt xboxrecomp-LGPL-2.1.txt Mesa-MIT.txt; do
        [ "$(unzip -p "$apk" "assets/licenses/$f" | wc -c | tr -d ' ')" -gt 200 ] \
            || die "$(basename "$apk"): assets/licenses/$f is empty or too short"
    done
    say "$(basename "$apk"): $n licence files in assets/licenses"
}

# A built program is run only with a scratch profile: it keeps saves,
# settings, game data and the shader cache in per-user folders (HOME and
# XDG_* here; APPDATA and LOCALAPPDATA on Windows, windows/remote.ps1), and a
# start or an install would move or make folders in the real one.
# scratch_home <dir>: makes it and refuses the user's own home.
scratch_home() {
    local d="$1" real
    [ -n "$d" ] || die "a smoke step needs a scratch profile folder"
    mkdir -p "$d" || die "cannot make the scratch profile $d"
    real="$(cd "${HOME:-/}" && pwd -P)"
    [ "$(cd "$d" && pwd -P)" != "$real" ] || die "the scratch profile is the real home ($real): not running the program there"
}
# scratch_run <dir> <program> [args]: the program with that profile and nothing else of the environment.
scratch_run() {
    local d="$1"; shift
    scratch_home "$d"
    env -i HOME="$d" XDG_DATA_HOME="$d/.local/share" XDG_CACHE_HOME="$d/.cache" XDG_CONFIG_HOME="$d/.config" \
        PATH=/usr/bin:/bin TMPDIR="${TMPDIR:-/tmp}" "$@"
}
