#!/usr/bin/env bash
# td4_stay.sh <rundir> <mode> [KEY=VALUE...]
#
# Takedawn Online step 4, "stay together after a race": the guest copy (LAN,
# offset CLI_OFFSET, default 30880, title address CLI_ID) joins the game the
# hosting copy (td4_host.sh, HOST_RD=<its rundir>) made with QUICK PLAY. Both
# race with RECOMP_DEBUG=autopilot=1 (give it after the mode; the host needs
# it too). Frames every ~6th flip on both copies around each step.
#
#   stay       RACES (default 3) races in a row: after each, A on the
#              results only, on both copies, until both are in the game lobby
#              again (no other press: the chain runs by itself)
#   hostquit   one race; the host's copy ends (a crash or quit) on the results:
#              the guest looks for the game, gives up, is on the online page
#   hostleave  one race, back in the lobby, then the host leaves the lobby
#              (B, Yes): where the guest ends
#   hostyes    one race; the host answers the results' Quit question with Yes:
#              the guest as in hostquit
#   guestquit  one race; the guest's copy ends on the results: the host is
#              back in its lobby alone
#   guestyes   one race; the guest answers the results' Quit question with Yes
#
# HOST_CODED=1: the game is a CREATE GAME with a code, joined with JOIN WITH CODE.
# The game is left running (pid in <rundir>/pid); STOP_AT_END=1 stops it.
set -u
source "$(dirname "$0")/scene_lib.sh"
source "$(dirname "$0")/td3_lib.sh"
source "$(dirname "$0")/td4_lib.sh"
RP="$(cd "$(dirname "$0")/../.." && pwd)"
rd="$1"; mode="$2"; shift 2
HOME_DIR="${HOME_DIR:-$RP/build/td4/home-$(basename "$rd")}"
mkdir -p "$HOME_DIR"
H="${HOST_RD:?HOST_RD}"
fail=0
bad() { note "FAIL: $*"; fail=1; }
sv="$RP/build/td4/saves-$(basename "$rd")"
mkdir -p "$sv"
cp -cRf "$RP/build/td3/saves-ref/" "$sv/"
hcmd() { echo "$*" >> "$H/cmds.txt"; }
hburst() { hcmd "burst $1 $2 $3"; }
CAPTURE=1 SAVES=none scene_start "$rd" HOME="$HOME_DIR" RECOMP_SETTINGS=none TAKEDAWN_QUIT_MENU=1 \
    RECOMP_SAVE_DIR="$sv" \
    TAKEDAWN_ONLINE="${ONLINE_MODE:-lan}" TAKEDAWN_LOBBY="${LOBBY_ADDR:-}" TAKEDAWN_PORT_OFFSET="${CLI_OFFSET:-30880}" TAKEDAWN_NET_ID="${CLI_ID:-10.77.7.2}" \
    TAKEDAWN_NET_FORCE_PUBLIC=1 TAKEDAWN_GAMERTAG="${CLI_TAG:-GuestS}" "$@"
t=$(date +%s)
until [ -f "$H/hostready" ]; do
    alive; sleep 1
    [ $(( $(date +%s) - t )) -gt 600 ] && die "the host is not in its lobby"
done
to_online_page
burst join 6 ${BURST_N:-120}
wait_flip "$BURST_F0"
if [ "${HOST_CODED:-0}" = 1 ]; then         # JOIN WITH CODE, the host's code
    press down:200 20; press down:200 20; press a:300 0
    wait_game 'the code entry is up' 60
    python3 "$(dirname "$0")/td5_codekeys.py" "$D" "$(cat "$H/code")" || die "the code was not typed"
    PADN=$(wc -l < "$D/pad.txt")
    press a:300 0
    wait_game 'QUICKPLAY\] flow state [0-9A-F]+ -> 590' 150
else
    qp_press
fi
wait_flips 90
press a:400 0                               # this car
wait_game '\[NET\] lobby line' 60
wait_flips 150

# race <k>: the host starts it (START; a QUICK PLAY game's own countdown may
# start it first), both drive it (autopilot) to the result
race() {
    local k=$1
    burst race$k 60 80; hburst race$k 60 80
    wait_flip "$BURST_F0"
    # a QUICK PLAY game's countdown starts it by itself after 30 s; START
    # only if it has not (a START in the race would pause it)
    local n0 t
    n0=$(grep -ac '^\[STAY\] the race of' "$H/log.txt")
    t=$(date +%s)
    until [ "$(grep -ac '^\[STAY\] the race of' "$H/log.txt")" -gt "$n0" ]; do
        alive
        if [ $(( $(date +%s) - t )) -gt ${START_WAIT:-50} ]; then
            hcmd start:400
            note "race $k: the host presses START"
            t=$(( $(date +%s) + 600 ))
        fi
        sleep 1
    done
    note "race $k: started"
    wait_count '\[AUTOPILOT\] result' "$k"
    note "race $k: result"
}

# post_race <k> [skip-guest] [skip-host]: frames on both copies, A on the
# results, until each copy is in the game lobby again
post_race() {
    local k=$1 gl hl g h i gdone hdone
    gdone=${2:-0}; hdone=${3:-0}
    gl=$(lobbies_of "$D"); hl=$(lobbies_of "$H")
    burst post$k 6 ${POST_N:-900}; hburst post$k 6 ${POST_N:-900}
    for (( i = 0; i < ${POST_TRIES:-120}; i++ )); do
        alive
        g=$(flow_of "$D"); h=$(flow_of "$H")
        [ "$gdone" = 0 ] && [ "$(lobbies_of "$D")" -gt "$gl" ] && { gdone=1; note "race $k: the guest is in the lobby again"; }
        [ "$hdone" = 0 ] && [ "$(lobbies_of "$H")" -gt "$hl" ] && { hdone=1; note "race $k: the host is in the lobby again"; }
        [ "$gdone" = 1 ] && [ "$hdone" = 1 ] && break
        if [ "$gdone" = 0 ]; then
            [ "$g" = 178 ] && press a:300 0
        fi
        if [ "$hdone" = 0 ]; then
            [ "$h" = 178 ] && hcmd a:300
        fi
        wait_flips ${POST_STEP:-90}
    done
    [ "$gdone" = 1 ] || bad "race $k: the guest is not back in the lobby (flow $g)"
    [ "$hdone" = 1 ] || bad "race $k: the host is not back in the lobby (flow $h)"
    wait_flips 240
}

case "$mode" in
stay)
    for (( r = 1; r <= ${RACES:-3}; r++ )); do
        race $r
        wait_flips 60
        post_race $r
        grep -a '^\[STAY\]' "$D/log.txt" | tail -n 4 | while read -r l; do note "guest: $l"; done
        grep -a '^\[STAY\]' "$H/log.txt" | tail -n 4 | while read -r l; do note "host: $l"; done
        note "race $r: guest RSS $(ps -o rss= -p "$(cat "$D/pid")") KB, host RSS $(ps -o rss= -p "$(cat "$H/pid")") KB"
    done
    ;;
hostquit|hostyes|guestquit|guestyes)
    race 1
    wait_flips 60
    burst post1 6 ${POST_N:-900}; hburst post1 6 ${POST_N:-900}
    wait_flip "$BURST_F0"
    if [ "$mode" = hostquit ] || [ "$mode" = hostyes ]; then
        wait_flips 120
        if [ "$mode" = hostquit ]; then
            hcmd quit
            note "the host's copy ends on the results"
        else
            hcmd b:300; sleep 2; hcmd up:200; sleep 1; hcmd a:300
            note "the host quits on the results (B, Yes)"
        fi
        for (( i = 0; i < 200; i++ )); do
            alive
            [ "$(flow_of "$D")" = 178 ] && press a:300 0
            grep -aqE 'did not open again|the lobby server does not answer' "$D/log.txt" && break
            wait_flips 90
        done
        grep -aqE 'did not open again|the lobby server does not answer' "$D/log.txt" || bad "the guest did not give up"
        note "the guest gave up: the title's message box"
        wait_flips 300
        press a:300 0                       # its Continue
        wait_flips 300
        [ "$(flow_of "$D")" = 5B0 ] || bad "the guest is not on the online page (flow $(flow_of "$D"))"
    elif [ "$mode" = guestquit ]; then
        wait_flips 120
        note "the guest's copy ends on the results"
        collect_bursts
        kill "$(cat "$D/pid")"
        D_DEAD=1
        hl=$(lobbies_of "$H")
        for (( i = 0; i < 200; i++ )); do
            [ "$(flow_of "$H")" = 178 ] && hcmd a:300
            [ "$(lobbies_of "$H")" -gt "$hl" ] && break
            sleep 1.5
        done
        [ "$(lobbies_of "$H")" -gt "$hl" ] && note "the host is in its lobby again" || bad "the host is not back in its lobby"
        sleep 10
    else
        # the results' Quit, Yes (the question starts on NO)
        until [ "$(flow_of "$D")" = 178 ]; do alive; wait_flips 30; done
        wait_flips 200
        press b:300 60; press up:200 30; press a:300 0
        note "the guest quits on the results (B, Yes)"
        hl=$(lobbies_of "$H")
        for (( i = 0; i < 200; i++ )); do
            alive
            [ "$(flow_of "$H")" = 178 ] && hcmd a:300
            [ "$(lobbies_of "$H")" -gt "$hl" ] && break
            wait_flips 90
        done
        wait_flips 900
    fi
    ;;
hostleave)
    race 1
    wait_flips 60
    post_race 1
    burst leave 6 400; hburst leave 6 400
    wait_flip "$BURST_F0"
    hcmd b:300; sleep 2; hcmd up:200; sleep 1; hcmd a:300
    note "the host leaves its lobby (B, Yes)"
    wait_flips 1500
    ;;
esac
[ "${D_DEAD:-0}" = 1 ] || alive
collect_bursts
for b in "$D"/burst-* "$H"/burst-*; do [ -d "$b" ] && frames_to_jpg "$b"; done
grep -a 'unknown screen' "$D/log.txt" && bad "unknown screen in the log"
[ "${STOP_AT_END:-0}" = 1 ] && scene_stop
if [ "$fail" = 0 ]; then note "PASS: td4_stay $mode"; else note "FAIL: td4_stay $mode"; fi
exit $fail
