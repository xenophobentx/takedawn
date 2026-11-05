#!/usr/bin/env python3
# td5_sheet.py OUT.png every FRAME... : a contact sheet of every Nth frame, to look through a burst.
import sys, math
from PIL import Image
out, n = sys.argv[1], int(sys.argv[2])
fs = sys.argv[3:][::n]
w, h = 320, 240
cols = 5
rows = math.ceil(len(fs) / cols)
sheet = Image.new("RGB", (cols * w, rows * h))
for i, f in enumerate(fs):
    sheet.paste(Image.open(f).convert("RGB").resize((w, h)), ((i % cols) * w, (i // cols) * h))
sheet.save(out)
print(len(fs), "frames", sheet.size)
