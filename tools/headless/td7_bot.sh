#!/usr/bin/env bash
# td7_bot.sh <rundir> <plan0> <plan1> [races] [KEY=VALUE...]
#
# Two copies on this machine, LAN, driven only by the test navigator
# (RECOMP_DEBUG=onlinebot, src/game/onlinebot.c): the scene takes each to the
# main menu, then nothing is pressed from here. Copy 0 is the navigator's host,
# copy 1 its guest; plan0 and plan1 are their TAKEDAWN_ONLINEBOT_PLAN (empty:
# the role's default). KEY=VALUE goes to both copies, ROLE0 / ROLE1 and
# OFFSET0 / OFFSET1 from the environment change those.
#
# Another test's LAN copy on this machine holds the one LAN lobby port (21810),
# so a run next to it needs LOBBY=127.0.0.1:<port>, a lobby of its own
# (build/lobby/takedawn-lobby --port <port> --public 127.0.0.1); the copies are
# then in internet mode against it.
#
# A stay keeps both copies in one lobby after a race, so a game is not counted
# any more: <races> is the number of races. The navigator logs one "race N end"
# per race (N counts all of the copy's races, across lobbies) and is told to stop
# after <races> (TAKEDAWN_ONLINEBOT_RACES). LEAVE_AFTER=K in the environment makes
# each copy leave a lobby after K races in it (a new game follows).
# RUN_TIMEOUT (default 2400 s): a race takes 5 to 7 minutes headless.
# Passes when both copies have "race <races> end" and no "stuck" or "stopped".
# <rundir>/c0, c1: each copy's run folder (log.txt), summary.txt the bot lines.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
RP="$(cd "$HERE/../.." && pwd)"
rd="$1"; p0="$2"; p1="$3"; races="${4:-1}"
shift 3; [ $# -gt 0 ] && shift
# the owner's game: a Takedawn.app in a window (a soak's headless copies run
# from app bundles too, and are not his)
if ps -axo command | grep 'Takedawn.app/Contents/MacOS' | grep -v grep | grep -qv -- '--headless'; then
    echo "the owner's game runs: no run"; exit 2
fi
abs="$RP/$rd"
mkdir -p "$abs"
start_copy() {  # k role name offset id plan
    local k=$1 sv="$RP/build/td7/saves-$(basename "$rd")-$1" home="$RP/build/td7/home-$(basename "$rd")-$1"
    mkdir -p "$home" "$abs/c$k"
    [ -d "$sv" ] || cp -cR "$RP/build/td3/saves-ref" "$sv"
    SAVES=none SCENE_TIMEOUT=600 STOP_AT_END=0 CAPTURE=1 "$HERE/online_scene.sh" "$rd/c$k" \
        HOME="$home" RECOMP_SETTINGS=none TAKEDAWN_QUIT_MENU=1 RECOMP_SAVE_DIR="$sv" \
        ${NET[@]} TAKEDAWN_PORT_OFFSET="$4" TAKEDAWN_NET_ID="$5" TAKEDAWN_NET_FORCE_PUBLIC=1 \
        TAKEDAWN_GAMERTAG="$3" RECOMP_DEBUG="autopilot=1,onlinebot=$2" TAKEDAWN_VOTE_TRACE=1 \
        TAKEDAWN_ONLINEBOT_PLAN="$6" TAKEDAWN_ONLINEBOT_RACES="$races" TAKEDAWN_ONLINEBOT_LEAVE_AFTER="${LEAVE_AFTER:-0}" \
        TAKEDAWN_ONLINEBOT_GUESTS=1 ${EXTRA[@]+"${EXTRA[@]}"} > "$abs/c$k.scene.txt" 2>&1 &
}
EXTRA=("$@")
NET=(TAKEDAWN_ONLINE=lan)
[ -n "${LOBBY:-}" ] && NET=(TAKEDAWN_ONLINE=1 RECOMP_ONLINE_SERVER="$LOBBY" TAKEDAWN_UPNP=0)
stop_all() {
    local k p
    for k in 0 1; do
        p=$(cat "$abs/c$k/pid" 2>/dev/null)
        [ -n "$p" ] && kill "$p" 2>/dev/null
    done
}
trap stop_all EXIT
start_copy 0 "${ROLE0:-host}" HostA "${OFFSET0:-31100}" 10.77.11.1 "$p0"
sleep "${GUEST_DELAY:-20}"
start_copy 1 "${ROLE1:-guest}" GuestB "${OFFSET1:-31110}" 10.77.11.2 "$p1"
t0=$(date +%s); rc=1
while :; do
    sleep 5
    d=0
    for k in 0 1; do
        grep -aq "ONLINEBOT\] race $races end" "$abs/c$k/log.txt" 2>/dev/null && d=$((d + 1))
    done
    [ "$d" = 2 ] && { rc=0; break; }
    grep -aq 'ONLINEBOT\] stopped' "$abs"/c?/log.txt 2>/dev/null && { echo "FAIL: a navigator stopped"; break; }
    for k in 0 1; do
        p=$(cat "$abs/c$k/pid" 2>/dev/null)
        if [ -n "$p" ] && ! kill -0 "$p" 2>/dev/null; then echo "FAIL: copy $k is gone"; break 2; fi
    done
    [ $(( $(date +%s) - t0 )) -gt "${RUN_TIMEOUT:-2400}" ] && { echo "FAIL: timeout"; break; }
done
sleep "${AFTER:-5}"
for k in 0 1; do
    echo "== c$k"
    grep -a 'ONLINEBOT\]' "$abs/c$k/log.txt" | grep -av 'press \|keep-alive' | cut -c1-150
done > "$abs/summary.txt"
s=$(grep -ac 'ONLINEBOT\] stuck' "$abs/c0/log.txt" "$abs/c1/log.txt" | awk -F: '{ n += $NF } END { print n }')
[ "$s" != 0 ] && rc=1
echo "plans '$p0' '$p1': $([ $rc = 0 ] && echo PASS || echo FAIL), stuck lines $s, $(( $(date +%s) - t0 )) s"
exit $rc
