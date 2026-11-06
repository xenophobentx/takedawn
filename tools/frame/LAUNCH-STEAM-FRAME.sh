#!/usr/bin/env bash
# The Steam Frame title TakedawnVR runs this. Steam starts it, which is also
# what gives the game its controllers; a binary started over SSH sees none.
# The log is run-stdout.txt beside this script.
#
# The game finds its disc files in "Burnout 3 Takedown/" in its working
# directory, the data folder $XDG_DATA_HOME/TakedawnVR (default
# ~/.local/share/TakedawnVR; tools/frame/deploy.sh --game-dir copies them
# there). Saves, settings and logs are Takedawn's own: $XDG_DATA_HOME/takedawn.
#
# One-shot diagnostics for an unattended run: lines KEY=VALUE in
# <data>/vr-diag are exported (RECOMP_*, BO3_*, TAKEDAWN_*, MESA_*, TU_*,
# VK_*) or, for ARGS=..., passed to the game; the file is deleted on read.
set -euo pipefail

here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
data="${XDG_DATA_HOME:-$HOME/.local/share}/TakedawnVR"
mkdir -p "$data"

# Steam's overlay has no use in the headset (SteamVR has its own).
if [[ -n "${LD_PRELOAD:-}" ]]; then
	LD_PRELOAD="$(printf '%s' "$LD_PRELOAD" | tr ': ' '\n\n' | { grep -v gameoverlayrenderer || true; } | paste -sd: -)"
	export LD_PRELOAD
fi
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}"
export DISPLAY="${DISPLAY:-:0}"
export RECOMP_PB_BACKEND="${RECOMP_PB_BACKEND:-vulkan}"
# The headset: OpenXR on the Vulkan device (RECOMP_XR=0 in vr-diag for flat).
export RECOMP_XR="${RECOMP_XR:-1}"
# Burnout 3's letterbox bars (16 rows of 480 at the top and bottom) stay off
# the HUD panel.
export RECOMP_XR_HUD_CROP_Y="${RECOMP_XR_HUD_CROP_Y:-16,464}"
# No radial speed blur in the headset (comfort); RECOMP_GFX_MOTION_BLUR=1 in
# vr-diag brings it back.
export RECOMP_GFX_MOTION_BLUR="${RECOMP_GFX_MOTION_BLUR:-0}"
# Sharper roads at the headset's grazing angles; the eyes at the runtime's
# recommended size times RECOMP_XR_EYE_SCALE (SteamVR's resolution setting
# sets that size: 2160 at 100% on the Frame).
export RECOMP_GFX_ANISO="${RECOMP_GFX_ANISO:-8}"
export RECOMP_XR_EYE_SCALE="${RECOMP_XR_EYE_SCALE:-1}"

diag="$data/vr-diag"
diag_lines=()
extra_args=()
if [[ -f "$diag" ]]; then
	while IFS= read -r line || [[ -n "$line" ]]; do
		case "$line" in
			RECOMP_*=* | BO3_*=* | TAKEDAWN_*=* | MESA_*=* | TU_*=* | VK_*=*) export "${line?}"; diag_lines+=("$line") ;;
			ARGS=*) read -r -a extra_args <<< "${line#ARGS=}"; diag_lines+=("$line") ;;
		esac
	done < "$diag"
	rm -f "$diag"
fi

log="$here/run-stdout.txt"
{
	echo "[launch] $(date -Is) $here/burnout3 $*"
	echo "[launch] data dir $data"
	[[ -f "$data/Burnout 3 Takedown/default.xbe" ]] || echo "[launch] no game files in $data/Burnout 3 Takedown"
	if ((${#diag_lines[@]})); then echo "[diag] ${diag_lines[*]}"; else echo "[diag] none"; fi
} > "$log"

cd "$data"
exec "$here/burnout3" --no-setup "$@" ${extra_args[@]+"${extra_args[@]}"} >> "$log" 2>&1
