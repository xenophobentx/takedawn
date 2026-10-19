#!/usr/bin/env bash
# release.sh: build every artefact of one version from one generated-code
# tree, smoke-run what can be run from here, and write the checksums.
#
#   tools/release/release.sh <version> [--only macos,windows,linux,android]
#                                      [--allow-dirty] [--reuse-gen]
#
#   dist/<version>/
#     Takedawn-<version>-macos-arm64.dmg
#     Takedawn-<version>-windows-x64-setup.exe
#     Takedawn-<version>-linux-x86_64.AppImage
#     (Android: whatever tools/release/build-android.sh puts there)
#     SHA256SUMS        sha256 of every artefact, `shasum -a 256 -c` format
#     BUILDINFO         version, codename, both commits, the regen key, the
#                       generated tree's hash, the patch count, the disc
#                       manifest id, the pins, and what was and was not
#                       verified
#
# It does NOT upload, tag, push or publish anything, and has no step that
# could: putting the files on a release page is a person's decision.
#
# Order: the version argument must be what the VERSION file says; both
# checkouts must be clean (--allow-dirty for a trial run: the artefacts then
# say "-dirty" and BUILDINFO says so); tools/release/gen.sh makes the
# generated code (--reuse-gen keeps a tree whose stamp is right); then
# build-macos.sh, smoke-macos.sh, build-windows.sh, build-linux.sh and, when
# it exists, build-android.sh. Each build script checks its own program's
# --version against this checkout; here the artefacts are checked against
# each other. A platform that fails or cannot run is reported and the rest
# goes on; the exit status is 0 only when every requested platform was built
# and every check that ran passed.
source "$(dirname "$0")/common.sh"
source "$REPO/tools/release/pins.env"

[ $# -ge 1 ] || die "usage: release.sh <version> [--only macos,windows,linux,android] [--allow-dirty] [--reuse-gen]"
WANT_VERSION="$1"; shift
ONLY="macos,windows,linux,android"; ALLOW_DIRTY=0; REUSE=""
while [ $# -gt 0 ]; do
    case "$1" in
        --only) ONLY="$2"; shift ;;
        --allow-dirty) ALLOW_DIRTY=1 ;;
        --reuse-gen) REUSE="--reuse" ;;
        *) die "unknown option $1" ;;
    esac
    shift
done
[ "$WANT_VERSION" = "$VERSION" ] || die "the VERSION file says $VERSION, not $WANT_VERSION"
for r in "$REPO" "$TOOLKIT"; do
    case "$(commit_of "$r")" in
        *-dirty) [ "$ALLOW_DIRTY" = 1 ] || die "$r has uncommitted changes (--allow-dirty for a trial run)" ;;
    esac
done
wants() { case ",$ONLY," in *",$1,"*) return 0 ;; *) return 1 ;; esac; }

T="$REPO/tools/release"
mkdir -p "$DIST" "$WORK"
LOGS="$WORK/logs"; mkdir -p "$LOGS"
DONE=(); FAILED=(); NOTRUN=()
t0=$(date +%s)

say "Takedawn $VERSION \"$CODENAME\": game $(commit_of "$REPO"), toolkit $(commit_of "$TOOLKIT")"
"$T/gen.sh" $REUSE 2>&1 | tee "$LOGS/gen.log"
assert_gen
say "generated code: key $GEN_KEY, tree ${GEN_TREE:0:16}, patches $GEN_PATCHES"

step() {  # <platform> <log name> <command...>: run, keep the log, note the outcome
    local name="$1" log="$LOGS/$2.log" t; shift 2
    t=$(date +%s)
    if "$@" > "$log" 2>&1; then
        grep -a -E "^(==>|OK  |NOT RUN|NOT DONE|    )" "$log" | sed 's/^/  /'
        TIMES+=("$name $(( $(date +%s) - t )) s")
        return 0
    fi
    tail -12 "$log" | sed 's/^/  /' >&2
    return 1
}
TIMES=()

if wants macos; then
    if [ "$(uname -s)" != Darwin ]; then NOTRUN+=("macos: not on a Mac")
    elif step macos build-macos "$T/build-macos.sh"; then
        DONE+=(macos)
        if step macos-smoke smoke-macos "$T/smoke-macos.sh"; then
            while IFS= read -r l; do NOTRUN+=("macos smoke: ${l#NOT RUN }"); done \
                < <(grep -a "^NOT RUN" "$LOGS/smoke-macos.log" || true)
        else FAILED+=("macos smoke run"); fi
    else FAILED+=("macos build"); fi
fi
if wants windows; then
    if step windows build-windows "$T/build-windows.sh"; then
        if [ -f "$DIST/Takedawn-$VERSION$PKG_TAG-windows-x64-setup.exe" ] && ! grep -aq "^==> NOT DONE" "$LOGS/build-windows.log"; then
            DONE+=(windows)
            [ -n "${TAKEDAWN_WIN_DISC:-}" ] || NOTRUN+=("windows: --install --check (TAKEDAWN_WIN_DISC not set)")
            NOTRUN+=("windows: the real setup exe was compiled, not run (a registry-free variant was installed and compared); no game start; the game's hand-over to the setup window (CreateProcess) was not run")
        else
            NOTRUN+=("windows: the setup exe was not compiled (no TAKEDAWN_WIN_HOST); the program folder and the .iss are in $WORK/windows")
        fi
    else FAILED+=("windows build"); fi
fi
if wants linux; then
    if [ -z "${TAKEDAWN_LINUX_HOST:-}" ] && [ "$(uname -s)" != Linux ]; then
        NOTRUN+=("linux: no TAKEDAWN_LINUX_HOST")
    elif step linux build-linux "$T/build-linux.sh"; then
        DONE+=(linux)
        if step linux-smoke smoke-linux "$T/smoke-linux.sh"; then
            while IFS= read -r l; do NOTRUN+=("linux smoke: ${l#NOT RUN }"); done \
                < <(grep -a "^NOT RUN" "$LOGS/smoke-linux.log" || true)
        else FAILED+=("linux smoke run"); fi
        NOTRUN+=("linux: run on the build machine only (headless, RADV): no window on a real screen, no Steam Deck, no Game Mode start, no desktop without the usual libraries")
    else FAILED+=("linux build"); fi
fi
if wants android; then
    if [ -x "$T/build-android.sh" ]; then
        # build-android.sh builds on its machine and signs here with
        # sign-android.sh; the key is read where it lies, never printed or copied.
        if [ -z "${TAKEDAWN_ANDROID_HOST:-}" ]; then
            NOTRUN+=("android: no TAKEDAWN_ANDROID_HOST")
        elif step android build-android "$T/build-android.sh"; then
            DONE+=(android)
            NOTRUN+=("android: built and signed, not installed or started on a device by this script")
        else FAILED+=("android build or signing"); fi
    else
        NOTRUN+=("android: tools/release/build-android.sh is not there yet")
    fi
fi

# What a program says it is, on one line: the --version fields that must be
# the same on every platform (commits without their branch names).
identity_of() {
    local k v out=""
    for k in version codename game toolkit regen manifest gen; do
        v="$(version_field "$1" "$k")"
        if [ "$k" = game ] || [ "$k" = toolkit ] || [ "$k" = manifest ]; then v="${v%% *}"; fi
        out="$out$k=$v;"
    done
    printf '%s' "$out"
}

# ── the artefacts agree with each other ──────────────────────────────
say "cross-checking the artefacts"
ref=""
for p in "${DONE[@]+"${DONE[@]}"}"; do
    f="$DIST/version-$p.txt"
    if [ ! -f "$f" ]; then
        [ "$p" = android ] && NOTRUN+=("android: an APK has no --version to compare; its build checks the generated tree and the regen key itself") \
                           || NOTRUN+=("$p: no --version output to compare (version-$p.txt)")
        continue
    fi
    line="$(identity_of "$f")"
    if [ -z "$ref" ]; then ref="$line"; refp="$p"
    elif [ "$line" != "$ref" ]; then
        FAILED+=("$p and $refp disagree: $line / $ref")
    fi
done
[ -n "$ref" ] && say "all built platforms say: $(printf '%s' "$ref" | tr ';' ' ')"

# ── the APK carries its licence texts ────────────────────────────────
case " ${DONE[*]:-} " in *" android "*)
    apk="$DIST/Takedawn-$VERSION$PKG_TAG-android-arm64.apk"
    if ( check_apk_licenses "$apk" ); then :; else FAILED+=("android: $(basename "$apk") has no licence texts in assets/licenses"); fi ;;
esac

# ── checksums and build information ──────────────────────────────────
( cd "$DIST" && rm -f SHA256SUMS && for f in Takedawn-* takedawn-*; do
      case "$f" in *-unsigned.apk|*.idsig) continue ;; esac   # not for release
      [ -f "$f" ] && printf '%s  %s\n' "$(sha256_of "$f")" "$f"; done > SHA256SUMS.tmp; mv SHA256SUMS.tmp SHA256SUMS )
{
    echo "Takedawn $VERSION \"$CODENAME\""
    echo "built      $(date -u +%Y-%m-%dT%H:%M:%SZ) on $(uname -s) $(uname -m)"
    echo "game       $(commit_of "$REPO") ($(git -C "$REPO" rev-parse --abbrev-ref HEAD))"
    echo "toolkit    $(commit_of "$TOOLKIT") ($(git -C "$TOOLKIT" rev-parse --abbrev-ref HEAD))"
    echo "regen key  $GEN_KEY"
    echo "gen tree   $GEN_TREE ($(stamp_field files) files)"
    echo "patches    $GEN_PATCHES guest patch sites, $(stamp_field spin_waits) spin waits"
    echo "manifest   $(manifest_id) ($BUILD_ID disc)"
    echo "signing    none on macOS (ad hoc seal without identity, not notarised), Windows and Linux; the Android APK with the project's key"
    echo
    echo "pins"
    grep -E '^[A-Z0-9_]+=' "$T/pins.env" | sed 's/^/  /'
    sed -n 's/^\([A-Z0-9_]*_\(VER\|TAG\)\)=\(.*\)/  \1=\3 (tools\/win\/fetch-deps.sh)/p' "$REPO/tools/win/fetch-deps.sh"
    [ -f "$WORK/linux/carried.txt" ] && { echo "  Linux, carried from the sniper image:"; sed 's/^/    /' "$WORK/linux/carried.txt"; }
    echo
    echo "artefacts"
    sed 's/^/  /' "$DIST/SHA256SUMS"
    echo
    echo "built:        ${DONE[*]:-none}"
    echo "failed:       ${FAILED[*]:-none}"
    echo "not verified:"
    for n in "${NOTRUN[@]+"${NOTRUN[@]}"}"; do echo "  - $n"; done
    echo "  - no artefact was started on a clean machine by a person; nothing was uploaded"
    echo
    echo "times"
    for n in "${TIMES[@]+"${TIMES[@]}"}"; do echo "  $n"; done
} > "$DIST/BUILDINFO"

echo
cat "$DIST/BUILDINFO"
echo
say "dist: $DIST ($(( $(date +%s) - t0 )) s). Nothing was uploaded."
if [ ${#FAILED[@]} -gt 0 ]; then die "failed: ${FAILED[*]}"; fi
for p in $(printf '%s' "$ONLY" | tr ',' ' '); do
    case " ${DONE[*]:-} " in *" $p "*) ;; *) [ "$p" = android ] && { [ ! -x "$T/build-android.sh" ] || [ -z "${TAKEDAWN_ANDROID_HOST:-}" ]; } && continue
                                             die "$p was requested and not built" ;; esac
done
