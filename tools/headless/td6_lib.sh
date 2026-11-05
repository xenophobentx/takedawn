# td6_lib.sh: sourced by the track vote scenes (BO-356): copies driven by
# td5_drive.sh, each with a line file, in LAN, offsets 30920 / 30930 / 30940.
#
#   v_start <n> <rundir>     copies 0..n-1 up to their main menus
#   v_game  <n>              copy 0 makes a game by QUICK PLAY, 1..n-1 find it
#                            and take a car, in that order
#   to <k> <line>            a line for copy k
#   wait_line <k> <line>     until copy k has done it
#   wait_vlog <k> <regex>    until copy k's log has it
#   v_stop                   all copies end, frames in their burst folders
V_NAMES=(HostA GuestB GuestC)
RP="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

to() { echo "$2" >> "$VR/c$1/cmds.txt"; }
wait_line() {
    local t=$(date +%s)
    until grep -qxF "$2" "$VR/c$1/done.txt" 2>/dev/null; do
        [ $(( $(date +%s) - t )) -gt "${3:-300}" ] && { echo "FAIL: copy $1 never did: $2"; return 1; }
        kill -0 "$(cat "$VR/c$1/pid")" 2>/dev/null || { echo "FAIL: copy $1 is gone (waiting for: $2)"; return 1; }
        sleep 0.3
    done
}
wait_vlog() {
    local t=$(date +%s)
    until grep -aqE "$2" "$VR/c$1/log.txt" 2>/dev/null; do
        [ $(( $(date +%s) - t )) -gt "${3:-120}" ] && { echo "FAIL: copy $1 log never had: $2"; return 1; }
        sleep 0.2
    done
}
wait_vcount() {   # wait_vcount <k> <regex> <n> [timeout]
    local t=$(date +%s)
    until [ "$(grep -acE "$2" "$VR/c$1/log.txt" 2>/dev/null)" -ge "$3" ]; do
        [ $(( $(date +%s) - t )) -gt "${4:-120}" ] && { echo "FAIL: copy $1 log had '$2' fewer than $3 times"; return 1; }
        sleep 0.2
    done
}
v_start() {
    VN=$1; VR="$RP/$2"
    mkdir -p "$VR"
    local k
    if pgrep -fl "Takedawn.app/Contents/MacOS" > /dev/null; then echo "the owner's game runs: no run"; exit 2; fi
    for (( k = 0; k < VN; k++ )); do
        mkdir -p "$VR/c$k"
        : > "$VR/c$k/cmds.txt"
        NAME=${V_NAMES[$k]} OFFSET=$(( 30920 + k * 10 )) NET_ID=10.77.9.$(( k + 1 )) MODE=lan \
            HOME_DIR="$RP/build/td6/home-$k" \
            "$HERE/td5_drive.sh" "$2/c$k" TAKEDAWN_VOTE_TRACE=1 ${V_ENV:-} > "$VR/c$k/drive.txt" 2>&1 &
    done
    for (( k = 0; k < VN; k++ )); do
        local t=$(date +%s)
        until [ -f "$VR/c$k/ready" ]; do
            [ $(( $(date +%s) - t )) -gt 240 ] && { echo "FAIL: copy $k no menu"; v_stop; exit 1; }
            sleep 0.5
        done
    done
}
v_join() {  # v_join <k>: copy k to the online page, QUICK PLAY, a car
    local k=$1
    to "$k" 'wait 150'; to "$k" 'down:200 30'; to "$k" 'down:200 30'; to "$k" 'down:200 31'
    to "$k" online
    to "$k" 'a:100'
    to "$k" 'log QUICKPLAY..flow.state.[0-9A-F]+.->.590 180'
    to "$k" 'wait 60'
    to "$k" 'a:400'
    to "$k" 'log NET..lobby.line 90'
    wait_line "$k" 'log NET..lobby.line 90'
}
v_game() {  # every guest at the car select first: the vote starts at the first car taken
    local k
    v_join 0 || return 1
    for (( k = 1; k < $1; k++ )); do
        to "$k" 'wait 150'; to "$k" 'down:200 30'; to "$k" 'down:200 30'; to "$k" 'down:200 31'
        to "$k" online
        to "$k" 'a:100'
        to "$k" 'log QUICKPLAY..flow.state.[0-9A-F]+.->.590 180'
        to "$k" 'wait 60'
        wait_line "$k" 'wait 60' || return 1
    done
    # the frames start here, before the vote: a loaded machine takes its time
    for (( k = 0; k < $1; k++ )); do to "$k" "burst vote 6 ${2:-300}"; done
    for (( k = 0; k < $1; k++ )); do wait_line "$k" "burst vote 6 ${2:-300}" || return 1; done
    for (( k = 1; k < $1; k++ )); do to "$k" 'a:400'; done
    for (( k = 1; k < $1; k++ )); do to "$k" 'log NET..lobby.line 90'; done
    for (( k = 1; k < $1; k++ )); do wait_line "$k" 'log NET..lobby.line 90' || return 1; done
}
v_stop() {
    local k
    for (( k = 0; k < VN; k++ )); do : > "$VR/c$k/stop"; done
    sleep 5
    for (( k = 0; k < VN; k++ )); do kill "$(cat "$VR/c$k/pid" 2>/dev/null)" 2>/dev/null; done
    wait
}
