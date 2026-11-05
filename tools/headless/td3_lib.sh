# td3_lib.sh: sourced by the Takedawn Online step-3 scenes after scene_lib.sh.
#
# burst <name> [step] [count]: ask for a frame every <step> flips (default 6,
# about 100 ms) from the next flip on, <count> frames (default 100), into
# <rundir>/burst-<name>/ (fb_flip_NNNNN.bmp). The game dumps the flips named
# in dumpat.txt as it passes them, so the burst starts 30 flips ahead: the
# caller waits for BURST_F0 (wait_flip $BURST_F0) and then presses.
burst() {
    local name=$1 step=${2:-6} n=${3:-100} f0 k
    f0=$(( $(flip) + 30 ))
    BURST_F0=$f0
    mkdir -p "$D/burst-$name"
    echo "$name $f0 $step $n" >> "$D/bursts.txt"
    note "burst $name: $n frames from flip $f0, every $step"
    for (( k = 0; k < 60 && k < n; k++ )); do echo "$(( f0 + k * step ))-$(( f0 + k * step ))"; done > "$D/dumpat.new"
    mv -f "$D/dumpat.new" "$D/dumpat.txt"
    if [ -z "${FEEDER:-}" ]; then
        # The game takes at most 64 listed flips at a time and reads the list
        # again when the file changes: a feeder keeps the next 60 targets in it.
        (
            gpid=$(cat "$D/pid")
            while kill -0 "$gpid" 2>/dev/null; do
                cur=$(tail -c 4096 "$D/events.txt" 2>/dev/null | grep -ao '\[GPU\] flip [0-9]*' | tail -n 1 | awk '{print $3}')
                awk -v cur="${cur:-0}" '{ for (k = 0; k < $4; k++) { t = $2 + k * $3; if (t > cur + 1) print t "-" t } }' \
                    "$D/bursts.txt" | sort -t- -k1,1n | head -n 60 > "$D/dumpat.new"
                [ -s "$D/dumpat.new" ] && mv -f "$D/dumpat.new" "$D/dumpat.txt"
                sleep 0.5
            done
        ) > /dev/null 2>&1 &
        FEEDER=$!
    fi
}
# collect_bursts: move the dumped frames into their burst folders (frames are
# named by flip; each belongs to the burst whose range holds it).
collect_bursts() {
    local name f0 step n k src
    while read -r name f0 step n; do
        for (( k = 0; k < n; k++ )); do
            src=$(printf '%s/fb_flip_%05u.bmp' "$D" $(( f0 + k * step )))
            [ -f "$src" ] && mv "$src" "$D/burst-$name/"
        done
    done < "$D/bursts.txt"
}
wait_game() {   # wait_game <regex> [timeout-s]: until the game's own log has it
    local t=$(date +%s)
    until grep -aqE "$1" "$D/log.txt" 2>/dev/null; do
        alive
        [ $(( $(date +%s) - t )) -gt "${2:-$SCENE_TIMEOUT}" ] && die "no '$1' in the log after ${2:-$SCENE_TIMEOUT}s"
        sleep 0.2
    done
    note "log: $1"
}
# to_online_page: main menu, the TAKEDAWN ONLINE page up, the cursor on QUICK PLAY.
to_online_page() {
    to_main_menu
    wait_flips 90       # a press in the menu's first moments is lost
    press down:200 20; press down:200 20; press down:200 20
    press a:400 90
    local t=$(date +%s)
    until grep -aq 'online: four lines listed' "$D/log.txt" 2>/dev/null; do
        alive
        [ $(( $(date +%s) - t )) -gt "$SCENE_TIMEOUT" ] && die 'online page not shown'
        press a:400 40
    done
    wait_flips 60
    note "on the online page"
}
