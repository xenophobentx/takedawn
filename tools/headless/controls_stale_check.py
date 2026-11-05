#!/usr/bin/env python3
"""controls_stale_check.py <ctl.txt>: controls_stale_scene.sh's verdict.

For each event: the results byte at its GO, the gate after the GO, and whether
the bottom button reached the title as the accelerator (rt 255 from a255 read),
from the start of its press, which in the crash event is before the GO.
"""
import re
import sys

results = 0
event = None
events = []
gate = None
for line in open(sys.argv[1], errors="replace"):
    w = line.split()
    if not w:
        continue
    if w[0] == "S" and w[2] == "p1_results":
        results = int(w[5], 16)
    elif w[0] == "S" and w[2] == "module_vt" and w[5] == "003A9ACC":
        event = {"results": None, "go": None, "driving": 0, "ps2": None, "first": None}
        events.append(event)
    elif w[0] == "D":
        gate = int(w[3])
        if event and event["go"] and gate:
            event["driving"] = 1
    elif w[0] == "S" and w[2] == "p1_phase" and w[5] == "00000002" and event and not event["go"]:
        event["go"] = int(w[1])
        event["results"] = results
        if gate:
            event["driving"] = 1
    elif w[0] == "P" and event and (event["go"] or gate) and event["first"] is None:
        m = re.search(r"read \w+ a(\d+) .* -> a\d+ .* rt(\d+) ", line)
        if m and int(m.group(1)) > 30:
            event["ps2"] = int(m.group(2)) == 255
            event["first"] = line.strip()

names = ["crash", "road rage", "race"]
bad = len(events) < 3
for name, e in zip(names, events):
    ok = bool(e["driving"] and e["ps2"])
    bad |= not ok
    print(f"{'PASS' if ok else 'FAIL'} {name}: p1_results at the GO {e['results']}, "
          f"gate after the GO {e['driving']}, bottom button is the accelerator {e['ps2']}")
    print(f"     {e['first']}")
sys.exit(1 if bad else 0)
