#!/usr/bin/env bash
# Copies the Steam Frame package to a Frame and registers it as a devkit
# title, which then shows in the Frame's Steam library.
#
#   FRAME_HOST=steamos@<frame>.local tools/frame/deploy.sh [--package DIR]
#       [--game-dir DIR] [--no-register] [--logs] [--dry-run]
#
#   --package DIR   the staged package (default build-frame/package/Takedawn,
#                   made by tools/frame/build.sh package)
#   --game-dir DIR  also copy your own unpacked disc (the folder holding
#                   default.xbe, checked by its sha1) to the title's data
#                   folder, ~/.local/share/TakedawnVR/Burnout 3 Takedown on
#                   the Frame. Once is enough; it stays across deploys.
#   --no-register   only copy files
#   --logs          print the last run's log (run-stdout.txt) and exit
#   --dry-run       print what would run, change nothing
#
# Registering also asks SteamVR for 120 Hz for the title (REFRESH to change).
#
# The title is a native Linux ARM64 title: compat tool
# SteamLinuxRuntime_4-arm64, steam_play=0. Start it from the Steam library: a
# binary started over SSH sees no controllers. Quit the game before
# deploying over it. The Frame needs Developer Mode (devkit-utils in ~).
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
source "$root/tools/frame/common.sh"
remote_dir="devkit-game/$title"
package="$root/build-frame/package/Takedawn"
xbe_sha1="2b25b18c759f72386917b711291cf2fb79da5d5c"
game_dir=""
register=1
logs=0
dry=0

while (($#)); do
	case "$1" in
		--package) package="$2"; shift 2 ;;
		--game-dir) game_dir="$2"; shift 2 ;;
		--no-register) register=0; shift ;;
		--logs) logs=1; shift ;;
		--dry-run) dry=1; shift ;;
		*) echo "unknown option $1" >&2; exit 2 ;;
	esac
done

run() {
	if ((dry)); then printf '+'; printf ' %q' "$@"; printf '\n'; else "$@"; fi
}

if ((logs)); then
	exec ssh "${ssh_opts[@]}" "$frame_host" "cat ~/$remote_dir/run-stdout.txt"
fi

[[ -x "$package/burnout3" && -x "$package/LAUNCH-STEAM-FRAME.sh" ]] || {
	echo "$package is not a package (run tools/frame/build.sh package)" >&2
	exit 1
}
if [[ -n "$game_dir" ]]; then
	[[ -f "$game_dir/default.xbe" ]] || { echo "$game_dir has no default.xbe" >&2; exit 1; }
	have="$(shasum -a 1 "$game_dir/default.xbe" | cut -d' ' -f1)"
	[[ "$have" == "$xbe_sha1" ]] || { echo "default.xbe is not the retail USA one (sha1 $have)" >&2; exit 1; }
fi

if ! ((dry)); then
	frame_ssh true 2>/dev/null || {
		echo "The Frame is not reachable at $frame_host (Wi-Fi, charge, ssh key)." >&2
		exit 1
	}
fi

# macOS rsync is 2.6: no --info. -a keeps the executable bits.
run ssh "${ssh_opts[@]}" "$frame_host" "mkdir -p ~/$remote_dir ~/$data"
run rsync -a --delete --partial --exclude 'run-*.txt' "$package/" "$frame_host:$remote_dir/"
if [[ -n "$game_dir" ]]; then
	# macOS rsync 2.6 hands the remote path to the remote shell as is: escape the spaces
	run rsync -a --partial "$game_dir/" "$frame_host:$data/Burnout\ 3\ Takedown/"
fi

if ((register)); then
	home="$(frame_ssh 'echo $HOME')"
	parms='{"gameid": "'$title'", "directory": "'$home/$remote_dir'", "force_appid": "", "argv": ["LAUNCH-STEAM-FRAME.sh"], "env": {}, "settings": {"steam_play": "0", "compat_tool": "SteamLinuxRuntime_4-arm64"}, "lepton_args": ""}'
	run ssh "${ssh_opts[@]}" "$frame_host" "cd ~/devkit-utils && python3 steam-client-create-shortcut --parms '$parms'"
	echo "Registered $title. Start it from the Frame's Steam library; read the log with: $0 --logs"
	# 120 Hz for this title (SteamVR offers it 72 Hz otherwise): SteamVR's
	# per-app setting, kept in its settings file. vrcmd needs SteamVR running.
	if ! ((dry)) && gameid="$(frame_gameid)"; then
		appid=$((gameid >> 32))
		key="steam.app.$appid.preferredRefreshRate"
		if frame_ssh "/opt/steamvr/bin/linuxarm64/vrcmd --set-settings-float $key ${REFRESH:-120}" >/dev/null 2>&1; then
			echo "Asked SteamVR for ${REFRESH:-120} Hz for $title."
		else
			echo "SteamVR isn't running, so the refresh rate wasn't set. With SteamVR up, run on the Frame:"
			echo "  /opt/steamvr/bin/linuxarm64/vrcmd --set-settings-float $key ${REFRESH:-120}"
		fi
	elif ! ((dry)); then
		echo "Steam hasn't saved the new title yet; run this again to set its refresh rate."
	fi
fi
