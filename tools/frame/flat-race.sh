#!/usr/bin/env bash
# A flat (no VR) race on the Steam Frame, headless over SSH, for a
# performance baseline: copies the package and tools/headless to the Frame and
# runs tools/headless/to_first_race.sh there (no saves, the first race of a new
# profile, then RACE_SECS of driving), with the Vulkan backend. Prints the
# flip rate while driving. Nothing else should be running on the Frame.
#
#   FRAME_HOST=... tools/frame/flat-race.sh <local out dir> [KEY=VALUE ...]
#
# On the Frame: ~/takedawn-frame/{pkg,headless,data,runs,cache}; data/ links
# to the disc files deploy.sh --game-dir put in the title's data folder.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
source "$repo/tools/frame/common.sh"
host="$frame_host"
out="${1:?usage: flat-race.sh <out dir> [KEY=VALUE ...]}"; shift
r=takedawn-frame

ssh "${ssh_opts[@]}" "$host" "mkdir -p $r/pkg $r/headless/tools/headless $r/data/saves $r/runs $r/cache
	[ -e '$r/data/Burnout 3 Takedown' ] || ln -s \"\$HOME/$data/Burnout 3 Takedown\" '$r/data/Burnout 3 Takedown'"
[ "${NO_DEPLOY:-0}" = 1 ] || rsync -a --delete -e "ssh ${ssh_opts[*]}" "$repo/build-frame/package/Takedawn/" "$host:$r/pkg/"
rsync -a --delete -e "ssh ${ssh_opts[*]}" "$repo/tools/headless/" "$host:$r/headless/tools/headless/"

env_args="RECOMP_PB_BACKEND=vulkan RECOMP_RENDER_SHADER_CACHE=\$HOME/$r/cache $*"
ssh "${ssh_opts[@]}" "$host" "cd $r && rm -rf runs/flat && \
	REPO=\$HOME/$r/headless GAME_DIR=\$HOME/$r/data BIN=\$HOME/$r/pkg/burnout3 \
	RACE_SECS=\${RACE_SECS:-80} STOP_AT_END=1 SCENE_TIMEOUT=600 \
	bash headless/tools/headless/to_first_race.sh \$HOME/$r/runs/flat $env_args" || echo "scene ended with an error"
mkdir -p "$out"
rsync -a -e "ssh ${ssh_opts[*]}" "$host:$r/runs/flat/" "$out/" --exclude 'fb_*' || true
grep -E 'driving|driven|FAIL|start ' "$out/scene.txt" || true
python3 - "$out/scene.txt" <<'PY'
import re, sys, datetime
t = {}
for line in open(sys.argv[1]):
    m = re.match(r'(\d+):(\d+):(\d+) (driving|driven) \(flip (\d+)\)', line)
    if m:
        h, mi, s, what, f = m.groups()
        t[what] = (int(h) * 3600 + int(mi) * 60 + int(s), int(f))
if 'driving' in t and 'driven' in t:
    (a, fa), (b, fb) = t['driving'], t['driven']
    print(f"race: {fb - fa} flips in {b - a} s = {(fb - fa) / max(1, b - a):.1f} flips/s")
PY
