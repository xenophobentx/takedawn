#!/usr/bin/env bash
# Regenerate the recompiled C from default.xbe with the xboxrecomp toolkit
# (../xboxrecomp), in the one order that works. Same shape as hl2/regen.sh.
#
#   xbe_parser    section layout -> build/xr/analysis.json
#   disasm        ALL code sections, never --text-only: without the XDK
#                 sections (D3D, DSOUND, XNET, XPP...) there are no function
#                 boundaries there and each function is glued to the next.
#   func_id       library-function identification (CRT, XDK).
#   abi_analysis  calling convention / params / return type.
#   recomp        emits the C into src/game/recomp/gen (gitignored).
#
# Nothing here is committed output: build/ and gen/ are regenerable from the
# user's own default.xbe.
#
# Usage: ./regen.sh [--disasm]
#
# BO3_REGION=PAL translates the European (Fr/De/It) disc's default.xbe
# instead: the seed, spin-wait and patch lists of config/pal/
# (tools/region/port_config.py writes them from config/) and
# recomp/recomp_manual_pal.c, from "Burnout 3 Takedown (PAL)/default.xbe"
# (else "Burnout 3 Takedown/default.xbe"), into src/game/recomp/gen.pal (its
# analysis in build/xr-pal). Build it with cmake -DBO3_REGION=PAL.
#
# BO3_REGION=PAL-ENSV translates the Europe/Australia (En/Es/Nl/Sv) disc's:
# config/pal-ensv/ and recomp/recomp_manual_pal_ensv.c, from
# "Burnout 3 Takedown (PAL-ENSV)/default.xbe", into src/game/recomp/gen.pal-ensv
# (its analysis in build/xr-pal-ensv). Build it with cmake -DBO3_REGION=PAL-ENSV.
#
# Each disc has its own tree, so all three stand in one checkout: every
# package carries the three programs (docs/release.md).
#
# BO3_XBE names another default.xbe than the region's usual place.

set -uo pipefail

BO3="$(cd "$(dirname "$0")" && pwd)"
RECOMP="${XBOXRECOMP_DIR:-$BO3/../xboxrecomp}"

# Windows' py launcher. Elsewhere there is none: stand in for it with the
# repo's .venv when there is one, else python3 (the toolkit's macOS setup
# script defines the same shim).
if ! command -v py >/dev/null 2>&1; then
    py() {
        [[ "${1:-}" == "-3" ]] && shift
        if [[ -x "$BO3/.venv/bin/python3" ]]; then
            "$BO3/.venv/bin/python3" "$@"
        else
            python3 "$@"
        fi
    }
fi
XBE="$BO3/Burnout 3 Takedown/default.xbe"
CFG="$BO3/config"
MANUAL="$BO3/src/game/recomp/recomp_manual.c"
GEN="$BO3/src/game/recomp/gen"
OUT="$BO3/build/xr"
case "${BO3_REGION:-NTSC-U}" in
    NTSC-U|ntsc-u|NTSC|ntsc|"") ;;
    PAL|pal)
        [[ -f "$BO3/Burnout 3 Takedown (PAL)/default.xbe" ]] && XBE="$BO3/Burnout 3 Takedown (PAL)/default.xbe"
        CFG="$BO3/config/pal"
        MANUAL="$BO3/src/game/recomp/recomp_manual_pal.c"
        GEN="$BO3/src/game/recomp/gen.pal"
        OUT="$BO3/build/xr-pal" ;;
    PAL-ENSV|pal-ensv)
        XBE="$BO3/Burnout 3 Takedown (PAL-ENSV)/default.xbe"
        CFG="$BO3/config/pal-ensv"
        MANUAL="$BO3/src/game/recomp/recomp_manual_pal_ensv.c"
        GEN="$BO3/src/game/recomp/gen.pal-ensv"
        OUT="$BO3/build/xr-pal-ensv" ;;
    *) echo "BO3_REGION must be NTSC-U, PAL or PAL-ENSV"; exit 1 ;;
esac
XBE="${BO3_XBE:-$XBE}"

if [[ ! -f "$XBE" ]]; then
    echo "missing $XBE -- extract it from your own disc image"
    exit 1
fi
mkdir -p "$OUT"
# recomp runs from the toolkit dir, so a relative trace list must be absolutised.
if [[ -n "${TRACE_FUNCS:-}" && "$TRACE_FUNCS" != /* ]]; then
    TRACE_FUNCS="$BO3/$TRACE_FUNCS"
fi

SEEDS=()
[[ -f "$CFG/seed_functions.json" ]] && \
    SEEDS=(--seed-functions "$CFG/seed_functions.json")
# Busy-wait loops lowered to RECOMP_SPIN_WAIT (only spin unless the runtime's
# present.pacing is sleep); SPIN_WAITS= (empty) generates without them.
SPIN=()
SPIN_WAITS="${SPIN_WAITS-$CFG/spin_waits.json}"
[[ -n "$SPIN_WAITS" && -f "$SPIN_WAITS" ]] && SPIN=(--spin-waits "$SPIN_WAITS")

# Opt-in guest patches (tools.recomp --patches): config/patches.json. Each is
# a call after a listed load that a runtime setting can steer; the build runs
# as shipped until the setting says otherwise. PATCHES= (empty) generates
# without them.
PATCH=()
PATCHES="${PATCHES-$CFG/patches.json}"
[[ -n "$PATCHES" && -f "$PATCHES" ]] && PATCH=(--patches "$PATCHES")

# Guest memory ordering on arm64 (tools.recomp --mem-order): config/
# mem_order.json lists the lock-release stores emitted as store-releases and
# asks for an acquire fence after every spin-wait candidate. No change to the
# x86-64 output. MEM_ORDER= (empty) generates without it.
MEMORD=()
MEM_ORDER="${MEM_ORDER-$CFG/mem_order.json}"
[[ -n "$MEM_ORDER" && -f "$MEM_ORDER" ]] && MEMORD=(--mem-order "$MEM_ORDER")

if [[ "${1:-}" == "--disasm" || ! -f "$OUT/disasm/functions.json" ]]; then
    echo "==> xbe_parser"
    (cd "$RECOMP" && py -3 -m tools.xbe_parser "$XBE" \
        --json "$OUT/analysis.json" --quiet) || exit 1

    echo "==> disasm"
    (cd "$RECOMP" && py -3 -m tools.disasm "$XBE" \
        --analysis-json "$OUT/analysis.json" "${SEEDS[@]}" \
        -o "$OUT/disasm" -v) > "$OUT/disasm.log" 2>&1 || { tail -20 "$OUT/disasm.log"; exit 1; }
    tr '\r' '\n' < "$OUT/disasm.log" | grep -E "Total functions|Reachable|Seeded" || true
fi

echo "==> func_id"
(cd "$RECOMP" && py -3 -m tools.func_id "$XBE" \
    --functions "$OUT/disasm/functions.json" \
    --strings   "$OUT/disasm/strings.json" \
    --xrefs     "$OUT/disasm/xrefs.json" \
    -o "$OUT/func_id" | tail -1) || exit 1

echo "==> abi_analysis"
(cd "$RECOMP" && py -3 -m tools.abi_analysis "$XBE" \
    --functions  "$OUT/disasm/functions.json" \
    --identified "$OUT/func_id/identified_functions.json" \
    --output-dir "$OUT/abi" | tail -1) || exit 1

echo "==> recomp"
(cd "$RECOMP" && py -3 -m tools.recomp "$XBE" --all --split 1000 \
    --disasm-dir  "$OUT/disasm" \
    --func-id-dir "$OUT/func_id" \
    --abi-dir     "$OUT/abi" \
    --exclude-manual "$MANUAL" \
    ${SPIN[@]+"${SPIN[@]}"} \
    ${PATCH[@]+"${PATCH[@]}"} \
    ${MEMORD[@]+"${MEMORD[@]}"} \
    ${TRACE_FUNCS:+--trace-functions "$TRACE_FUNCS"} \
    --gen-dir     "$GEN" \
    -o "$OUT/recomp"
) > "$OUT/recomp.log" 2>&1
st=$?
grep -aE "unresolved|functions \(|Complete" "$OUT/recomp.log" || true
if [ $st -ne 0 ]; then
    echo "!! recomp failed (status $st) -- gen/ is now STALE" >&2
    tail -25 "$OUT/recomp.log" >&2
    exit $st
fi
echo "==> done. Now: cmake --build build-xr --config Release"
