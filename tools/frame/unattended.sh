#!/usr/bin/env bash
# One unattended run of the TakedawnVR Steam title on the Frame: writes a
# one-shot vr-diag file, starts the title through Steam's pipe (so it gets
# its controllers and the per-app refresh rate), waits, stops it, and copies
# the log and any picture dumps back.
#
#   FRAME_HOST=... tools/frame/unattended.sh <seconds> <out-dir> [KEY=VALUE ...]
#
# KEY=VALUE lines go to the launcher's vr-diag (RECOMP_*, BO3_*, TAKEDAWN_*,
# MESA_*, TU_*, VK_*, or ARGS=...). Pictures: RECOMP_XR_DUMP=dumps/xr-
# (relative to the data folder). Nobody should be wearing the headset.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
source "$root/tools/frame/common.sh"
seconds="${1:?usage: unattended.sh <seconds> <out-dir> [KEY=VALUE ...]}"
out="${2:?usage: unattended.sh <seconds> <out-dir> [KEY=VALUE ...]}"
shift 2

if frame_busy; then exit 3; fi
if frame_ssh "pgrep -f '$game_pat' >/dev/null"; then
	echo "$title is already running"; exit 3
fi

diag=""
for kv in "$@"; do diag+="$kv"$'\n'; done
printf '%s' "$diag" | frame_ssh "rm -rf ~/$data/dumps; mkdir -p ~/$data/dumps; cat > ~/$data/vr-diag"

gameid="$(frame_gameid)"
echo "launching $title (gameid $gameid) for ${seconds}s"
frame_ssh "printf 'steam://rungameid/%s\n' $gameid > ~/.steam/steam.pipe"
sleep "$seconds"

frame_ssh "pid=\$(pgrep -f '$game_pat' | head -1); if [ -n \"\$pid\" ]; then kill -TERM \$pid; sleep 5; if kill -0 \$pid 2>/dev/null; then kill -9 \$pid; echo killed; else echo 'stopped by TERM'; fi; else echo 'exited on its own'; fi"
if frame_ssh "test -f ~/$data/vr-diag"; then
	frame_ssh "rm -f ~/$data/vr-diag"
	echo "ERROR: the run never started (vr-diag not read). Check Steam's console_log.txt for a prompt waiting in the headset." >&2
	exit 4
fi
mkdir -p "$out"
scp -q "${ssh_opts[@]}" "$frame_host:devkit-game/$title/run-stdout.txt" "$out/run-stdout.txt" || true
scp -q -r "${ssh_opts[@]}" "$frame_host:$data/dumps" "$out/" 2>/dev/null || true
grep -E '^\[diag\]' "$out/run-stdout.txt" || echo "WARNING: no diag echo in the log"
