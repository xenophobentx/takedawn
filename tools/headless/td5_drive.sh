#!/usr/bin/env bash
# td5_drive.sh <rundir> [KEY=VALUE...]
#
# Takedawn Online step 5 (code entry, lobby code, player counts): one copy,
# started and taken to the main menu, then driven by the lines of
# <rundir>/cmds.txt as they appear (one per line):
#
#   <press>                 a scene_lib press (a:300, down:200, x:200 ...), then 30 flips
#   shot <name>             a frame now, into <rundir>/<name>.bmp
#   burst <name> <step> <n> frames every <step> flips (td3_lib.sh burst)
#   wait <flips>            flips to let go by
#   log <regex>             until the game's log has it
#   online                  A on the main menu's TAKEDAWN ONLINE (and the sign-in) until the page is up
#   collect                 the bursts' frames into their folders
#
# Each line done is echoed to <rundir>/done.txt. The copy stays until
# <rundir>/stop appears.
#
#   NAME=GuestB  OFFSET=30900  NET_ID=10.77.0.2  MODE=lan|internet
#   SERVER=127.0.0.1:port (internet: the lobby to use)
set -u
source "$(dirname "$0")/scene_lib.sh"
source "$(dirname "$0")/td3_lib.sh"
RP="$(cd "$(dirname "$0")/../.." && pwd)"
rd="$1"; shift
HOME_DIR="${HOME_DIR:-$RP/build/td5/home-$(basename "$rd")}"
mkdir -p "$HOME_DIR"
sv="$RP/build/td5/saves-$(basename "$rd")"
[ -d "$sv" ] || cp -cR "$RP/build/td3/saves-ref" "$sv"
srv=()
[ -n "${SERVER:-}" ] && srv=(RECOMP_ONLINE_SERVER="$SERVER")
CAPTURE=1 SAVES=none scene_start "$rd" HOME="$HOME_DIR" RECOMP_SETTINGS=none TAKEDAWN_QUIT_MENU=1 \
    RECOMP_SAVE_DIR="$sv" \
    TAKEDAWN_ONLINE="${MODE:-lan}" TAKEDAWN_PORT_OFFSET="${OFFSET:-30900}" TAKEDAWN_NET_ID="${NET_ID:-10.77.0.2}" \
    TAKEDAWN_NET_FORCE_PUBLIC=1 TAKEDAWN_GAMERTAG="${NAME:-GuestB}" ${srv[@]+"${srv[@]}"} "$@"
# a line that fails ends the run with its copy: an orphan stays signed in and
# counts as a player in every number the run reads
trap 'kill "$(cat "$D/pid" 2>/dev/null)" 2>/dev/null' EXIT
to_main_menu
: > "$D/ready"
: > "$D/done.txt"
[ -f "$D/cmds.txt" ] || : > "$D/cmds.txt"
n=0
while [ ! -f "$D/stop" ]; do
    alive
    m=$(wc -l < "$D/cmds.txt")
    while [ "$n" -lt "$m" ]; do
        n=$((n + 1))
        line=$(sed -n "${n}p" "$D/cmds.txt")
        set -- $line
        case "${1:-}" in
        shot)                       # one frame, through the bursts' list (the feeder keeps dumpat.txt)
            f=$(( $(flip) + 40 ))
            echo "shot-$2 $f 1 1" >> "$D/bursts.txt"
            mkdir -p "$D/burst-shot-$2"
            [ -z "${FEEDER:-}" ] && echo "$f-$f" >> "$D/dumpat.txt"
            wait_flip $(( f + 2 ))
            sleep 0.5
            src=$(printf '%s/fb_flip_%05u.bmp' "$D" "$f")
            [ -f "$src" ] && mv "$src" "$D/$2.bmp"
            rmdir "$D/burst-shot-$2" 2>/dev/null
            ;;
        burst) burst "$2" "${3:-6}" "${4:-60}"; wait_flip "$BURST_F0" ;;
        wait) wait_flips "$2" ;;
        log) wait_game "$2" "${3:-120}" ;;
        online)                     # from the main menu's TAKEDAWN ONLINE line: A until the page is up
            k0=$(grep -ac 'online: four lines listed' "$D/log.txt")
            t0=$(date +%s)
            until [ "$(grep -ac 'online: four lines listed' "$D/log.txt")" -gt "$k0" ]; do
                alive
                [ $(( $(date +%s) - t0 )) -gt "$SCENE_TIMEOUT" ] && die 'online page not shown'
                press a:400 0
                # one A at a time: a second one on the page would be its QUICK PLAY
                t1=$(date +%s)
                until [ "$(grep -ac 'online: four lines listed' "$D/log.txt")" -gt "$k0" ] || [ $(( $(date +%s) - t1 )) -ge 7 ]; do
                    alive
                    sleep 0.2
                done
            done
            wait_flips 60 ;;
        collect) collect_bursts ;;
        "") ;;
        *) press "$1" "${2:-30}" ;;
        esac
        echo "$line" >> "$D/done.txt"
    done
    sleep 0.3
done
collect_bursts
STOP_AT_END=1 scene_end "td5 drive done"
