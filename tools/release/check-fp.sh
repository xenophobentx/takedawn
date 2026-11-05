#!/usr/bin/env bash
# check-fp.sh <objdump> <program> [allow-regex]
#
# Fails when the program has a fused multiply-add instruction (BO-282.2):
# the build sets -ffp-contract=off on every target (the top CMakeLists.txt),
# so none may come from the game or the toolkit, with or without LTO and
# PGO. Counts x86 FMA3/FMA4 (vfmadd231sd, vfnmsub..., vfmaddsd) and arm64
# fmadd, fmsub, fnmadd, fnmsub, fmla, fmls, by function; prints the total and
# the worst. No exceptions in our code.
#
# What this proves: our code (the generated chunks and the toolkit as built
# here) does not fuse. It does not prove that the platforms compute alike:
# the host's libm (sin, cos, exp, pow), the C++ runtime and the graphics
# driver stack (MoltenVK, the Vulkan driver) are not in the program and are
# not checked.
#
# allow-regex: functions of a prebuilt library linked in statically (not our
# code), left out of the verdict but listed.
#
#   tools/release/check-fp.sh objdump build/release/macos/Takedawn.app/Contents/MacOS/Takedawn
#   tools/release/check-fp.sh ~/.cache/bo3-windeps/llvm-mingw/bin/llvm-objdump Takedawn.exe
set -euo pipefail
OD="$1"; BIN="$2"; ALLOW="${3:-^$}"
[ -f "$BIN" ] || { echo "check-fp: no program at $BIN" >&2; exit 2; }
out="$("$OD" -d --no-show-raw-insn "$BIN" | awk -v allow="$ALLOW" '
    /^[0-9a-fA-F]+ <.*>:$/ { fn = $2; sub(/^</, "", fn); sub(/>:$/, "", fn); next }
    {
        line = $0; sub(/^[ \t]*[0-9a-fA-F]+:[ \t]*/, "", line)
        split(line, w, /[ \t]+/); m = tolower(w[1])
        if (m ~ /^v?fn?m(add|sub)[a-z0-9]*$/ || m ~ /^fml[as](\.|$)/) {
            if (fn ~ allow) a[fn]++
            else c[fn]++
        }
    }
    END {
        for (k in c) { t += c[k]; printf "fused %d %s\n", c[k], k }
        for (k in a) { ta += a[k]; printf "allowed %d %s\n", a[k], k }
        printf "total %d allowed %d\n", t + 0, ta + 0
    }')"
total="$(printf '%s\n' "$out" | sed -n 's/^total \([0-9]*\) .*/\1/p')"
allowed="$(printf '%s\n' "$out" | sed -n 's/^total [0-9]* allowed \([0-9]*\)/\1/p')"
# (sed -n reads to the end: head would close the pipe on sort, whose SIGPIPE
# fails the script under pipefail once the lists are long, as with ncnn's
# thousands of kernels in the Android library.)
{ printf '%s\n' "$out" | grep '^fused ' || true; } | sort -k2 -rn | sed -n '1,15s/^/  /p'
{ printf '%s\n' "$out" | grep '^allowed ' || true; } | sort -k2 -rn | sed -n '1,5s/^/  /p'
if [ "$total" -gt 0 ]; then
    echo "check-fp: $(basename "$BIN"): $total fused multiply-add instructions ($allowed allowed): contraction is on somewhere" >&2
    exit 1
fi
echo "check-fp: $(basename "$BIN"): no fused multiply-add instructions ($allowed in allowed functions)"
