#!/usr/bin/env bash
# The first race in the headset, unattended: the TakedawnVR Steam title
# (deploy.sh first) started through Steam's pipe and driven by
# tools/headless/to_first_race.sh through the live pad file, as flat-race.sh
# drives the headless build. The scene's RECOMP_* settings reach the game
# through the launcher's one-shot vr-diag. Nobody should be wearing the
# headset.
#
#   FRAME_HOST=... tools/frame/xr-race.sh <local out dir> [KEY=VALUE ...]
#
# KEY=VALUE: RECOMP_* for the game (RECOMP_XR_DUMP=dumps/xr- writes pictures
# into the data folder's dumps/, copied back; RECOMP_XR_FAKE_VIEWS=1 draws the
# eyes for fixed views). RACE_SECS (default 40) from the environment. The
# game's own log is <out>/log.txt, its stdout <out>/run-stdout.txt. On the
# Frame it uses ~/takedawn-frame (scripts, run folder, a scratch profile and
# shader cache), never the title's own saves.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
source "$repo/tools/frame/common.sh"
host="$frame_host"
out="${1:?usage: xr-race.sh <out dir> [KEY=VALUE ...]}"; shift
r=takedawn-frame

if frame_busy; then exit 3; fi

ssh "${ssh_opts[@]}" "$host" "mkdir -p $r/headless/tools/headless $r/runs $r/cache $r/xr-game $data
	rm -rf ~/$data/dumps; mkdir -p ~/$data/dumps"
rsync -a --delete -e "ssh ${ssh_opts[*]}" "$repo/tools/headless/" "$host:$r/headless/tools/headless/"

gameid="$(frame_gameid)"
echo "$title is gameid $gameid"

# LAUNCH runs with the scene's RECOMP_* exported: they become the vr-diag
# (not RECOMP_HEADLESS: the headset needs the window's Vulkan path), Steam
# starts the title, and LAUNCH lasts as long as the game (the scene's pid).
pat="$game_pat"
launch="env | grep -E '^RECOMP_' | grep -v '^RECOMP_HEADLESS=' > \$HOME/$data/vr-diag; \
printf 'steam://rungameid/%s\\n' $gameid > \$HOME/.steam/steam.pipe; \
for i in \$(seq 90); do pgrep -f '$pat' >/dev/null && break; sleep 1; done; \
while pgrep -f '$pat' >/dev/null; do sleep 1; done"
stop="pkill -TERM -f '$pat'; sleep 5; pkill -KILL -f '$pat' || true"

env_args="RECOMP_PB_BACKEND=vulkan RECOMP_RENDER_SHADER_CACHE=\$HOME/$r/cache-xr RECOMP_SAVE_DIR=\$HOME/$r/xr-game/saves $*"
ssh "${ssh_opts[@]}" "$host" "cd $r && rm -rf runs/xr && \
	REPO=\$HOME/$r/headless GAME_DIR=\$HOME/$r/xr-game SAVES=empty \
	LAUNCH=$(printf '%q' "$launch") STOP=$(printf '%q' "$stop") \
	RACE_SECS=\${RACE_SECS:-${RACE_SECS:-40}} STOP_AT_END=1 SCENE_TIMEOUT=600 \
	bash headless/tools/headless/to_first_race.sh \$HOME/$r/runs/xr $env_args" || echo "scene ended with an error"
ssh "${ssh_opts[@]}" "$host" "pgrep -f '$pat' >/dev/null && { pkill -KILL -f '$pat'; echo 'killed the game'; }; rm -f ~/$data/vr-diag" || true
mkdir -p "$out"
rsync -a -e "ssh ${ssh_opts[*]}" "$host:$r/runs/xr/" "$out/" || true
scp -q "${ssh_opts[@]}" "$host:devkit-game/$title/run-stdout.txt" "$out/run-stdout.txt" || true
rsync -a -e "ssh ${ssh_opts[*]}" "$host:$data/dumps/" "$out/dumps/" 2>/dev/null || true
grep -E 'driving|driven|FAIL|start ' "$out/scene.txt" || true
grep -E '^\[vr-perf\]|stereo:' "$out/run-stdout.txt" "$out/log.txt" 2>/dev/null | tail -6 || true
