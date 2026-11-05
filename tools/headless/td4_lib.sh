# td4_lib.sh: sourced by the Takedawn Online step-4 scenes (td4_host.sh,
# td4_stay.sh) after scene_lib.sh and td3_lib.sh.

# qp_press: A on QUICK PLAY until the title's Quick Match runs (a press that
# lands while the page is still settling is not taken), then wait for the
# car state
qp_press() {
    local n0 k
    n0=$(grep -ac "QUICK PLAY: the title's Quick Match runs" "$D/log.txt")
    for (( k = 0; k < 5; k++ )); do
        press a:200 0
        wait_flips 120
        [ "$(grep -ac "QUICK PLAY: the title's Quick Match runs" "$D/log.txt")" -gt "$n0" ] && break
    done
    wait_game 'QUICKPLAY\] flow state [0-9A-F]+ -> 590' 150
}

# flow_of <rundir>: the flow state the copy's [STAY] log last named
flow_of() { grep -a '^\[STAY\] flow ' "$1/log.txt" | tail -n 1 | sed -n 's/.*-> \([0-9A-F]*\).*/\1/p'; }
# lobbies_of <rundir>: how many times the copy's game lobby came up
lobbies_of() { grep -ac '\[NET\] lobby line' "$1/log.txt"; }
# frames_to_jpg <dir>: the burst's frames as JPEG (a third of the size), BMPs removed
frames_to_jpg() {
    python3 - "$1" <<'PY'
import os, sys
from PIL import Image
d = sys.argv[1]
for f in sorted(os.listdir(d)):
    if f.endswith('.bmp'):
        p = os.path.join(d, f)
        try:
            Image.open(p).convert('RGB').save(p[:-4] + '.jpg', quality=85)
        except OSError:
            pass                        # a frame cut off by the game's end
        os.remove(p)
PY
}
