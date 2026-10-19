#!/usr/bin/env bash
# gen.sh: make the one generated-code tree a release is built from.
#
#   tools/release/gen.sh [--reuse]
#
# Runs the repository's regen.sh (the whole pipeline, with the guest patches
# of config/patches.json and the spin-wait list) on this machine, checks that
# every listed patch site went in, and writes src/game/recomp/gen/regen.stamp:
#
#   key <regen key>          cmake/release_info.cmake's hash of the inputs
#   tree <sha256>            the generated files (common.sh: gen_tree_hash)
#   files <n>
#   patches <in>/<listed>    from the recompiler's own report
#   spin_waits <n>           RECOMP_SPIN_WAIT sites in the generated C
#
# Every platform then compiles this tree and no other: the build scripts copy
# it (rsync) and a release build refuses a tree whose stamp, files or key do
# not agree (cmake/release_info.cmake, BO3_RELEASE).
#
# Note: tools/install/regen.py (the old user-side installer's regen) passes
# no --patches and must not be used for a release; this script uses regen.sh
# and fails when the patches are not in.
#
# --reuse keeps an existing tree when its stamp is right for this checkout
# (same key, same files); otherwise the regen runs (about four minutes).
#
# Needs: the user's default.xbe at "Burnout 3 Takedown/default.xbe" in this
# checkout (a link is fine), Python 3 with capstone (.venv here, or python3).
source "$(dirname "$0")/common.sh"

if [ "${1:-}" = "--reuse" ] && [ -f "$GEN/regen.stamp" ] \
   && [ "$(stamp_field key)" = "$(regen_key)" ] && [ "$(stamp_field tree)" = "$(gen_tree_hash)" ]; then
    say "generated code is current (key $(stamp_field key), patches $(stamp_field patches))"
    exit 0
fi

XBE="$REPO/Burnout 3 Takedown/default.xbe"
[ -f "$XBE" ] || die "missing $XBE"
want_xbe="$("${TAKEDAWN_PYTHON:-python3}" -c 'import json,sys; print(next(b["xbe"]["sha256"] for b in json.load(open(sys.argv[1]))["builds"] if b["id"] == sys.argv[2]))' "$REPO/tools/install/supported.json" "$BUILD_ID")"
[ "$(sha256_of "$XBE")" = "$want_xbe" ] || die "default.xbe is not the supported build ($want_xbe)"
[ -f "$CFG/patches.json" ] || die "$CFG/patches.json is missing"

# regen.sh takes its Python from <repo>/.venv; a worktree may borrow one.
if [ ! -x "$REPO/.venv/bin/python3" ] && [ -n "${TAKEDAWN_REGEN_VENV:-}" ]; then
    ln -s "$TAKEDAWN_REGEN_VENV" "$REPO/.venv"
fi

rm -f "$GEN/regen.stamp"
mkdir -p "$REPO/build/xr"
mark="$REPO/build/xr/.gen-start"; : > "$mark"
say "regen (regen.sh --disasm, patches and spin waits on)"
start=$(date +%s)
# The defaults, said out loud: an inherited PATCHES= or SPIN_WAITS= would
# generate without them.
( cd "$REPO" && env -u TRACE_FUNCS PATCHES="$CFG/patches.json" \
      SPIN_WAITS="$CFG/spin_waits.json" nice -n "$NICE" bash ./regen.sh --disasm )
say "regen took $(( $(date +%s) - start )) s"

report() {  # <report file> <field>: "<field>/<sites listed>"
    "${TAKEDAWN_PYTHON:-python3}" - "$1" "$2" <<'EOF'
import json, sys
doc = json.load(open(sys.argv[1]))
print("%d/%d" % (doc[sys.argv[2]], len(doc["sites"])))
EOF
}
R="$REPO/build/xr/recomp"
[ "$R/patches.json" -nt "$mark" ] || die "the recompiler wrote no patch report"
patches="$(report "$R/patches.json" patched_sites)"
case "$patches" in 0/*) die "no guest patch went in ($patches)" ;; esac
[ "${patches%/*}" = "${patches#*/}" ] || die "guest patches missing: $patches"
grep -q "RECOMP_PATCH_" "$GEN"/recomp_0*.c || die "the generated C has no RECOMP_PATCH_ call"
spins="$(grep -ac RECOMP_SPIN_WAIT "$GEN"/recomp_0*.c | awk -F: '{n+=$2} END {print n+0}')"
[ "$spins" -gt 0 ] || die "the generated C has no RECOMP_SPIN_WAIT"

{
    echo "key $(regen_key)"
    echo "tree $(gen_tree_hash)"
    echo "files $(ls "$GEN" | grep -c '^recomp_')"
    echo "patches $patches"
    echo "spin_waits $spins"
} > "$GEN/regen.stamp"
say "stamped:"; sed 's/^/    /' "$GEN/regen.stamp"
