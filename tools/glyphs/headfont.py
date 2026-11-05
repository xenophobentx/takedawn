#!/usr/bin/env python3
"""Writes assets/glyphs/headfont/w.svg: the letter W the game's heading font
lacks, for the TAKEDAWN ONLINE heading (src/game/headfont.c).

    headfont.py <Anton-Regular.ttf> [--check]

The game draws its red page headings (SINGLE EVENT, SETTINGS, XBOX LIVE...)
from a font in its executable whose atlas has only the letters those
headings use: no W. This draws one from the letter W of Anton (SIL Open Font
License 1.1, Copyright 2020 The Anton Project Authors; the licence is
assets/glyphs/headfont/Anton-OFL.txt), from
https://github.com/google/fonts/raw/main/ofl/anton/Anton-Regular.ttf
(SHA-256 below). Nothing of the game's font is used: the cell's size and the
stroke weight are numbers chosen to sit beside its letters, which are white
on clear, upright (the game slants the whole heading), 38 texels tall.

The outline is scaled into the cell and given a stroke of STROKE texels,
which makes the letter heavier the way the game's letters are heavier than
Anton's. The SVG is rendered at its own size when the game is built
(tools/glyphs/build.py, like the button prompts) and copied into the free
corner of the font's atlas at run time. fontTools is needed here only, not to
build the game. --check: status 1 if the file on disk differs.
"""
import hashlib
import os
import sys

ANTON_SHA256 = "a4ba3a92350ebb031da0cb47630ac49eb265082ca1bc0450442f4a83ab947cab"

CELL_W, CELL_H = 34, 38     # the glyph's rectangle in the atlas (headfont.c: HF_W_W, HF_W_H)
STROKE = 1.0                # texels, half on each side of the outline


def svg_for(font_path):
    from fontTools.pens.boundsPen import BoundsPen
    from fontTools.pens.svgPathPen import SVGPathPen
    from fontTools.pens.transformPen import TransformPen
    from fontTools.ttLib import TTFont

    f = TTFont(font_path)
    gs = f.getGlyphSet()
    name = f.getBestCmap()[ord("W")]
    bp = BoundsPen(gs)
    gs[name].draw(bp)
    x0, y0, x1, y1 = bp.bounds
    h = STROKE / 2
    # font units (y up) into the cell (y down), the stroke's half inside the cell on every side
    sx = (CELL_W - 2 * h) / (x1 - x0)
    sy = (CELL_H - 2 * h) / (y1 - y0)
    pen = SVGPathPen(gs, ntos=lambda v: ("%.3f" % v).rstrip("0").rstrip("."))
    gs[name].draw(TransformPen(pen, (sx, 0, 0, -sy, h - x0 * sx, h + y1 * sy)))
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">\n'
            '<!-- Takedawn: the letter W for the game\'s heading font (%dx%d texels), drawn from Anton\'s W\n'
            '     (SIL Open Font License 1.1, Copyright 2020 The Anton Project Authors). Written by\n'
            '     tools/glyphs/headfont.py. -->\n'
            '<path d="%s" fill="#fff" stroke="#fff" stroke-width="%g" stroke-linejoin="miter" stroke-miterlimit="4"/>\n'
            '</svg>\n') % (CELL_W, CELL_H, CELL_W, CELL_H, CELL_W, CELL_H, pen.getCommands(), STROKE)


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    data = open(argv[1], "rb").read()
    if hashlib.sha256(data).hexdigest() != ANTON_SHA256:
        sys.stderr.write("headfont.py: %s is not the Anton-Regular.ttf this was made with\n" % argv[1])
        return 1
    svg = svg_for(argv[1])
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "assets", "glyphs",
                        "headfont", "w.svg")
    if len(argv) > 2 and argv[2] == "--check":
        try:
            same = open(path, encoding="utf-8").read() == svg
        except OSError:
            same = False
        if not same:
            print("differs: assets/glyphs/headfont/w.svg")
        return 0 if same else 1
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(svg)
    print("assets/glyphs/headfont/w.svg")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
