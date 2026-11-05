#!/usr/bin/env python3
# td5_codekeys.py RUNDIR CODE: type CODE into the code entry of the copy in
# RUNDIR, one press at a time, reading the entry's own log lines after each:
# a press lost in a stall of a loaded machine is simply made again, a list
# cursor that ran on is brought back. Ends with the cursor on the JOIN line.
import re, sys, time

A = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"
rd, code = sys.argv[1], sys.argv[2]
log = rd + "/log.txt"

def state():
    text = open(log, "rb").read().decode("latin1")
    i = text.rfind("the code entry is up (")
    m = re.match(r"the code entry is up \(([^)]*)\)", text[i:])
    typed, row, col = m.group(1), 1, 0
    if "code entry again" in text[max(0, i - 200):i]:
        row = 0
    for line in text[i:].split("\n"):
        m = re.search(r"code entry: cursor on line (\d+)", line)
        if m:
            row = int(m.group(1))
        m = re.search(r"code entry: (\w+) on line (\d+) -> (\S*)", line)
        if m:
            row, typed = int(m.group(2)), m.group(3)
            if m.group(1) == "right":
                col = (col + 1) % 8
            elif m.group(1) == "left":
                col = (col + 7) % 8
    return typed, row, col

def press(key, ms=220):
    open(rd + "/pad.txt", "a").write("%s:%d\n" % (key, ms))
    time.sleep(0.8)

t0 = time.time()
while time.time() - t0 < 110:
    typed, row, col = state()
    if typed == code:
        if row == 0:
            print("typed", code)
            sys.exit(0)
        press("up" if row < 3 else "down")      # the list wraps at neither end for free
        continue
    if not code.startswith(typed):
        press("x")
        continue
    i = A.index(code[len(typed)])
    r, c = 1 + i // 8, i % 8
    if row != r:
        press("down" if row < r else "up")
    elif col != c:
        press("right" if (c - col) % 8 <= 4 else "left")
    else:
        press("a")
sys.exit("not typed in time: " + str(state()))
