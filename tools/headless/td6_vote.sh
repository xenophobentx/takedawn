#!/usr/bin/env bash
# td6_vote.sh <rundir> <scene>: the track vote in real copies (BO-356).
# Frames: <rundir>/c<k>/burst-*; the [VOTE] lines come out at the end. EXTRA:
# more KEY=VALUE for every copy.
#
#   two      two copies: the vote, one vote each, the winner, the race load
#   three    three copies, moving, changing a vote, nobody else voting
#   tie      two copies vote differently: the host picks among the two
#   leave    three copies: a guest leaves during the vote, it goes on
#   hostgone two copies: the host says Yes to leaving during the vote
#   hostkill two copies: the host's copy dies during the vote
#   late     a third copy joins while the vote runs
#   cycles   two copies, TAKEDAWN_VOTE_TEST=cycles: 20 votes in one lobby
#   long     two copies: the longest names, votes and moves
set -u
source "$(dirname "$0")/td6_lib.sh"
rd="$1"; scene="$2"
n=2
case "$scene" in three|leave|late) n=3 ;; esac
V_ENV="RECOMP_DEBUG=autopilot=1 ${EXTRA:-}"
popups() { local k; for (( k = 0; k < n; k++ )); do wait_vcount "$k" "VOTE. popup open" 1 60 || exit 1; done; }
raced() {   # every copy past the lobby into the race's track files
    local k
    for (( k = 0; k < n; k++ )); do wait_vlog "$k" 'VOTE. lobby left' 150; done
    wait_more 35
}
wait_more() { local t=$(( $(date +%s) + $1 )); while [ "$(date +%s)" -lt "$t" ]; do sleep 1; done; }
v_start "$n" "$rd"
trap v_stop EXIT
case "$scene" in
two)
    v_game 2 || exit 1
    popups
    to 0 'down:200'; to 0 'a:300'; to 1 'down:200'; to 1 'a:300'
    raced
    ;;
three)                              # moves, a changed vote, the host never votes: the full 15 s
    v_game 3 || exit 1
    popups
    to 1 'down:200'; to 1 'a:300'
    to 2 'down:200'; to 2 'down:200'; to 2 'a:300'
    to 2 'wait 120'; to 2 'up:200'; to 2 'a:300'
    raced
    ;;
tie)
    v_game 2 || exit 1
    popups
    to 0 'a:300'; to 1 'down:200'; to 1 'a:300'
    raced
    ;;
leave)                              # B, NO back to the vote, then B and Yes
    v_game 3 || exit 1
    popups
    to 1 'a:300'
    to 2 'b:300 60'; to 2 'a:300 60'
    wait_line 2 'a:300 60'
    to 2 'b:300 60'; to 2 'up:150 20'; to 2 'a:300 200'
    wait_line 2 'a:300 200'
    wait_vlog 0 'VOTE. lobby left' 150; wait_vlog 1 'VOTE. lobby left' 150
    wait_more 35
    ;;
hostgone)                           # the host answers Yes to its leave question
    v_game 2 200 || exit 1
    popups
    to 0 'b:300 60'; to 0 'up:150 20'; to 0 'a:300 200'
    wait_line 0 'a:300 200'
    wait_more 12
    to 1 'a:300 150'                # the title's OK on its error box
    wait_line 1 'a:300 150'
    wait_vcount 1 'online: four lines listed' 2 30
    ;;
hostkill)                           # the host's copy just dies
    v_game 2 400 || exit 1
    popups
    wait_more 3
    kill -9 "$(cat "$VR/c0/pid")"
    wait_more 30
    to 1 'a:300 150'
    wait_line 1 'a:300 150'
    wait_vcount 1 'online: four lines listed' 2 30
    ;;
late)
    v_join 0 || exit 1
    to 2 'wait 150'; to 2 'down:200 30'; to 2 'down:200 30'; to 2 'down:200 31'; to 2 online
    wait_line 2 online
    to 0 'burst vote 6 300'; to 1 'burst vote 6 300'; to 2 'burst vote 6 300'
    wait_line 2 'burst vote 6 300'
    v_join 1 || exit 1
    to 2 'a:100'; to 2 'log QUICKPLAY..flow.state.[0-9A-F]+.->.590 60'; to 2 'wait 60'; to 2 'a:400'
    raced
    ;;
cycles)
    v_game 2 || exit 1
    i=0
    while [ "$i" -lt 20 ]; do
        i=$(( i + 1 ))
        wait_vcount 0 'VOTE. popup open' "$i" 40 || exit 1
        wait_vcount 1 'VOTE. popup open' "$i" 40 || exit 1
        wait_more 2
        to 0 "a:300 1$i"; to 1 "a:300 1$i"
    done
    wait_vcount 0 'VOTE. popup open' 21 40
    ;;
long)
    v_game 2 80 || exit 1
    popups
    to 0 'down:200'; to 0 'a:300'; to 1 'down:200'; to 1 'down:200'; to 1 'a:300'
    wait_more 3; to 0 'down:200 40'; to 0 'down:200 40'; to 0 'up:200 40'; to 0 'up:200 40'
    wait_more 12
    ;;
esac
for (( k = 0; k < n; k++ )); do to "$k" collect; done
sleep 4
grep -a '\[VOTE\]' "$VR"/c*/log.txt | cut -d/ -f"$(echo "$VR" | tr -cd / | wc -c | awk '{print $1+2}')"-
