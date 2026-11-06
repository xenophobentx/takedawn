# Shared by the Steam Frame scripts (sourced, not run).
#
# FRAME_HOST is the Frame's ssh address (user steamos, with your key in its
# ~/.ssh/authorized_keys), for example steamos@<the Frame's name>.local.
# TITLE is the devkit title's name (letters and digits only, default
# TakedawnVR); its data folder on the Frame is ~/.local/share/$TITLE.

frame_host="${FRAME_HOST:-}"
if [[ -z "$frame_host" ]]; then
	echo "Set FRAME_HOST to the Frame's ssh address, e.g. FRAME_HOST=steamos@<frame-name>.local" >&2
	exit 2
fi
title="${TITLE:-TakedawnVR}"
[[ "$title" =~ ^[A-Za-z0-9]+$ ]] || { echo "TITLE must be letters and digits" >&2; exit 2; }
ssh_opts=(-o ConnectTimeout=5 -o BatchMode=yes)
data=".local/share/$title"            # relative to the Frame's home
game_pat="[d]evkit-game/$title/burnout3"   # the running game, by path: its main thread is named sdl-main

frame_ssh() { ssh "${ssh_opts[@]}" "$frame_host" "$@"; }

# Something else in VR (SteamVR runs one app at a time), or this game already?
frame_busy() {
	local busy
	busy="$(frame_ssh 'ps -eo args | grep -E "lepton-steamlaunch|lepton-dev|reaper SteamLaunch" | grep -v grep || true')"
	if [[ -n "$busy" ]]; then echo "another app is running:"; echo "$busy"; return 0; fi
	return 1
}

# The Steam game id of the devkit title (steam://rungameid/<id>).
frame_gameid() {
	frame_ssh python3 - "$title" <<'PY'
import glob, os, re, struct, sys
title = sys.argv[1].encode()
for path in glob.glob(os.path.expanduser('~/.local/share/Steam/userdata/*/config/shortcuts.vdf')):
    data = open(path, 'rb').read()
    for m in re.finditer(rb'\x02appid\x00(.{4})', data, re.S):
        appid = struct.unpack('<I', m.group(1))[0]
        name = re.search(rb'\x01(?:AppName|appname)\x00([^\x00]*)\x00', data[m.end():m.end() + 400])
        if name and (name.group(1) == title or name.group(1) == b'Devkit Game: ' + title):
            print((appid << 32) | 0x02000000)
            sys.exit(0)
sys.exit(1)
PY
}
