#!/usr/bin/env python3
"""Draws the button-prompt artwork of assets/glyphs as SVG.

The pictures are this project's own: discs, pills and rings made of circles and
rounded rectangles, and letters from the small stroke alphabet below (lines and
arcs on a 10-unit cap height; no font is used or embedded). Nothing is taken
from the game or from a console maker's artwork: the PlayStation set is a
cross, a circle, a square and a triangle drawn as plain strokes.

One SVG per family and per texture of the game that holds prompts. An SVG has
the texture's own size as its viewBox and a picture in each place the game
keeps one (the places are a table of numbers in LAYOUT; src/game/glyphs.c
lists the same rectangles for the runtime). tools/glyphs/build.py renders
them at 1:1 at build time.

    tools/glyphs/draw.py            writes assets/glyphs/<family>/*.svg
    tools/glyphs/draw.py --check    status 1 if a file on disk differs

Standard library only.
"""
import os
import sys

# ── the stroke alphabet: (advance width, path) on x 0..w, y 0..10 ──────────
LETTERS = {
    "A": (6.0, "M0,10 L3,0 L6,10 M1.2,6.6 L4.8,6.6"),
    "B": (5.6, "M0,0 L0,10 M0,0 L3,0 A2.4,2.4 0 0 1 3,4.8 L0,4.8 M3,4.8 A2.6,2.6 0 0 1 3,10 L0,10"),
    "X": (6.0, "M0,0 L6,10 M6,0 L0,10"),
    "Y": (6.0, "M0,0 L3,5 L6,0 M3,5 L3,10"),
    "L": (4.8, "M0,0 L0,10 L4.8,10"),
    "R": (5.6, "M0,10 L0,0 L3,0 A2.5,2.5 0 0 1 3,5 L0,5 M2.6,5 L5.6,10"),
    "Z": (5.6, "M0,0 L5.6,0 L0,10 L5.6,10"),
    "1": (2.6, "M0,2.2 L2.4,0 L2.4,10"),
    "2": (5.4, "M0,2.6 A2.7,2.6 0 0 1 5.4,2.6 Q5.4,4.6 0,10 L5.4,10"),
    "S": (5.6, "M5.6,1.9 Q5.4,0 2.8,0 Q0,0 0,2.5 Q0,5 2.8,5 Q5.6,5 5.6,7.5 Q5.6,10 2.8,10 Q0.2,10 0,8.1"),
    "T": (6.0, "M0,0 L6,0 M3,0 L3,10"),
    "O": (6.0, "M2.6,0 L3.4,0 Q6,0 6,2.6 L6,7.4 Q6,10 3.4,10 L2.6,10 Q0,10 0,7.4 L0,2.6 Q0,0 2.6,0 Z"),
    "P": (5.6, "M0,10 L0,0 L3,0 A2.6,2.6 0 0 1 3,5.2 L0,5.2"),
    "I": (0.0, "M0,0 L0,10"),
    "N": (6.0, "M0,10 L0,0 L6,10 L6,0"),
    "M": (7.2, "M0,10 L0,0 L3.6,6 L7.2,0 L7.2,10"),
    "E": (4.8, "M4.8,0 L0,0 L0,10 L4.8,10 M0,5 L3.9,5"),
    "U": (6.0, "M0,0 L0,7 Q0,10 3,10 Q6,10 6,7 L6,0"),
    "+": (6.0, "M0,5 L6,5 M3,2 L3,8"),
    # the keyboard set's labels
    "C": (5.6, "M5.4,2.3 Q4.9,0 2.8,0 Q0,0 0,2.8 L0,7.2 Q0,10 2.8,10 Q4.9,10 5.4,7.7"),
    "F": (4.8, "M0,10 L0,0 L4.8,0 M0,4.8 L3.8,4.8"),
    "H": (6.0, "M0,0 L0,10 M6,0 L6,10 M0,5 L6,5"),
    "W": (8.0, "M0,0 L2,10 L4,2.8 L6,10 L8,0"),
    "Q": (6.0, "M2.6,0 L3.4,0 Q6,0 6,2.6 L6,7.4 Q6,10 3.4,10 L2.6,10 Q0,10 0,7.4 L0,2.6 Q0,0 2.6,0 Z M3.6,6.6 L6.4,10.8"),
    # the rest of the alphabet and the digits, for the keys the player gives the buttons
    # (src/game/keycap.c draws them at run time; tools/glyphs/gen_keyfont.py makes its table)
    "D": (5.8, "M0,0 L0,10 L2.6,10 Q5.8,10 5.8,6.6 L5.8,3.4 Q5.8,0 2.6,0 Z"),
    "G": (6.0, "M5.8,2.3 Q5.3,0 3,0 Q0,0 0,3 L0,7 Q0,10 3,10 Q6,10 6,7 L6,5.2 L3.4,5.2"),
    "J": (4.6, "M4.6,0 L4.6,7.2 Q4.6,10 2.4,10 Q0.2,10 0,7.6"),
    "K": (5.6, "M0,0 L0,10 M5.4,0 L0,5.6 M2,3.6 L5.6,10"),
    "V": (6.0, "M0,0 L3,10 L6,0"),
    "0": (5.4, "M2.4,0 L3,0 Q5.4,0 5.4,2.8 L5.4,7.2 Q5.4,10 3,10 L2.4,10 Q0,10 0,7.2 L0,2.8 Q0,0 2.4,0 Z"),
    "3": (5.4, "M0.2,1.8 Q0.6,0 2.7,0 Q5.2,0 5.2,2.6 Q5.2,4.9 2.4,4.9 M2.4,4.9 Q5.4,4.9 5.4,7.4 Q5.4,10 2.7,10 Q0.4,10 0,8.2"),
    "4": (5.8, "M4.4,10 L4.4,0 L0,6.8 L5.8,6.8"),
    "5": (5.4, "M5.0,0 L0.6,0 L0.3,4.4 Q1.2,3.7 2.8,3.7 Q5.4,3.7 5.4,6.9 Q5.4,10 2.7,10 Q0.4,10 0,8.2"),
    "6": (5.4, "M4.8,1.4 Q4.2,0 2.8,0 Q0,0 0,4 L0,7.2 Q0,10 2.7,10 Q5.4,10 5.4,7.2 Q5.4,4.4 2.7,4.4 Q0.6,4.4 0,5.8"),
    "7": (5.4, "M0,0 L5.4,0 L2,10"),
    "8": (5.4, "M2.7,4.9 Q0.2,4.9 0.2,2.5 Q0.2,0 2.7,0 Q5.2,0 5.2,2.5 Q5.2,4.9 2.7,4.9 Q0,4.9 0,7.4 Q0,10 2.7,10 Q5.4,10 5.4,7.4 Q5.4,4.9 2.7,4.9"),
    "9": (5.4, "M0.6,8.6 Q1.2,10 2.6,10 Q5.4,10 5.4,6 L5.4,2.8 Q5.4,0 2.7,0 Q0,0 0,2.8 Q0,5.6 2.7,5.6 Q4.8,5.6 5.4,4.2"),
    ".": (0.0, "M0,9.6 L0.01,9.6"),
    ",": (0.6, "M0.6,8.8 L0.6,9.6 L0,10.8"),
    ";": (0.6, "M0.6,3.6 L0.61,3.6 M0.6,8.8 L0.6,9.6 L0,10.8"),
    "-": (4.0, "M0,5 L4,5"),
    "=": (4.8, "M0,3.4 L4.8,3.4 M0,6.8 L4.8,6.8"),
    "/": (4.8, "M4.8,0 L0,10"),
    "\\": (4.8, "M0,0 L4.8,10"),
    "[": (2.6, "M2.6,-0.5 L0,-0.5 L0,10.5 L2.6,10.5"),
    "]": (2.6, "M0,-0.5 L2.6,-0.5 L2.6,10.5 L0,10.5"),
    "`": (2.0, "M0,0 L2,2.6"),
    "'": (0.6, "M0.3,0 L0.3,3"),
}

# The symbols of SYMBOLS above, and arrows for the arrow keys, as paths round (0, 0) and the stroke
# width: what src/game/keycap.c draws for a key the player gave a button.
SYMBOL_PATHS = {
    "back": (1.9, "M-9,0 L-4,-6 L8.5,-6 L8.5,6 L-4,6 Z M-0.4,-2.4 L4.6,2.4 M4.6,-2.4 L-0.4,2.4"),
    "space": (3.0, "M-9.5,-3 L-9.5,3.5 L9.5,3.5 L9.5,-3"),
    "shift": (2.3, "M0,-8.5 L8.5,0.2 L4.1,0.2 L4.1,7.8 L-4.1,7.8 L-4.1,0.2 L-8.5,0.2 Z"),
    "enter": (2.7, "M10,-7 L10,2.5 L-9,2.5 M-3.6,-3.2 L-9,2.5 L-3.6,8.2"),
    "up": (2.7, "M0,8 L0,-8 M-6,-2.2 L0,-8 L6,-2.2"),
    "down": (2.7, "M0,-8 L0,8 M-6,2.2 L0,8 L6,2.2"),
    "left": (2.7, "M8,0 L-8,0 M-2.2,-6 L-8,0 L-2.2,6"),
    "right": (2.7, "M-8,0 L8,0 M2.2,-6 L8,0 L2.2,6"),
}


def text(s, cx, cy, height, stroke, colour, gap=0.28):
    """s centred on (cx, cy), cap height `height` px, stroke `stroke` px."""
    k = height / 10.0
    widths = [LETTERS[c][0] * k for c in s]
    g = gap * height
    total = sum(widths) + g * (len(s) - 1)
    x = cx - total / 2.0
    out = []
    for c, w in zip(s, widths):
        out.append('<path d="%s" transform="translate(%.3f %.3f) scale(%.4f)" fill="none" stroke="%s" '
                   'stroke-width="%.3f" stroke-linecap="round" stroke-linejoin="round"/>'
                   % (LETTERS[c][1], x, cy - height / 2.0, k, colour, stroke / k))
        x += w + g
    return "".join(out)


# symbols for a key: a path made around (cx, cy)
SYMBOLS = {
    # backspace: a tag pointing left with a cross in it
    "back": lambda cx, cy: (
        '<path d="M%.2f,%.2f L%.2f,%.2f L%.2f,%.2f L%.2f,%.2f L%.2f,%.2f Z M%.2f,%.2f L%.2f,%.2f M%.2f,%.2f L%.2f,%.2f" '
        'fill="none" stroke="%s" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"/>'
        % (cx - 9, cy, cx - 4, cy - 6, cx + 8.5, cy - 6, cx + 8.5, cy + 6, cx - 4, cy + 6,
           cx - 0.4, cy - 2.4, cx + 4.6, cy + 2.4, cx + 4.6, cy - 2.4, cx - 0.4, cy + 2.4, WHITE)),
    # space: the bar's own sign, a wide shallow bracket
    "space": lambda cx, cy: (
        '<path d="M%.2f,%.2f L%.2f,%.2f L%.2f,%.2f L%.2f,%.2f" fill="none" stroke="%s" stroke-width="3" '
        'stroke-linecap="round" stroke-linejoin="round"/>'
        % (cx - 9.5, cy - 3, cx - 9.5, cy + 3.5, cx + 9.5, cy + 3.5, cx + 9.5, cy - 3, WHITE)),
    # shift: an arrow pointing up, outlined
    "shift": lambda cx, cy: (
        '<path d="M%.2f,%.2f L%.2f,%.2f L%.2f,%.2f L%.2f,%.2f L%.2f,%.2f L%.2f,%.2f L%.2f,%.2f Z" fill="none" '
        'stroke="%s" stroke-width="2.3" stroke-linecap="round" stroke-linejoin="round"/>'
        % (cx, cy - 8.5, cx + 8.5, cy + 0.2, cx + 4.1, cy + 0.2, cx + 4.1, cy + 7.8, cx - 4.1, cy + 7.8, cx - 4.1, cy + 0.2,
           cx - 8.5, cy + 0.2, WHITE)),
    # enter: down, then back to the left, with an arrowhead
    "enter": lambda cx, cy: (
        '<path d="M%.2f,%.2f L%.2f,%.2f L%.2f,%.2f M%.2f,%.2f L%.2f,%.2f L%.2f,%.2f" '
        'fill="none" stroke="%s" stroke-width="2.7" stroke-linecap="round" stroke-linejoin="round"/>'
        % (cx + 10, cy - 7, cx + 10, cy + 2.5, cx - 9, cy + 2.5, cx - 3.6, cy - 3.2, cx - 9, cy + 2.5, cx - 3.6, cy + 8.2, WHITE)),
}


# ── the pieces ───────────────────────────────────────────────────────────
INK = "#07080b"          # the outline every piece shares, as the game's discs have one
FACE = ("#3a3e49", "#15171d")       # a dark button face, top and bottom
PILL = ("#e9ebef", "#a9adb8")       # a light shoulder / trigger key
WHITE = "#f4f5f7"
# the original Xbox pad's four buttons: top and bottom of the face, and the letter
XBOX_DISC = {
    "A": ("#7ccf3e", "#2f8a1c", WHITE), "B": ("#f0564a", "#a8161a", WHITE),
    "X": ("#4a9af0", "#1d56b0", WHITE), "Y": ("#f7d843", "#c99a0c", "#2a2203"),
}


def grad(gid, top, bottom, y0, y1):
    return ('<linearGradient id="%s" gradientUnits="userSpaceOnUse" x1="0" y1="%.2f" x2="0" y2="%.2f">'
            '<stop offset="0" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient>'
            % (gid, y0, y1, top, bottom))


class Sheet:
    def __init__(self, w, h):
        self.w, self.h, self.defs, self.body, self.n = w, h, [], [], 0

    def fill(self, top, bottom, y0, y1):
        self.n += 1
        gid = "g%d" % self.n
        self.defs.append(grad(gid, top, bottom, y0, y1))
        return "url(#%s)" % gid

    def disc(self, cx, cy, r=14.0, face=FACE):
        f = self.fill(face[0], face[1], cy - r, cy + r)
        self.body.append('<circle cx="%.2f" cy="%.2f" r="%.2f" fill="%s"/>' % (cx, cy, r, INK))
        self.body.append('<circle cx="%.2f" cy="%.2f" r="%.2f" fill="%s"/>' % (cx, cy, r - 1.6, f))
        # a thin light rim at the top of the face: the button is a dome
        self.body.append('<path d="M%.2f,%.2f A%.2f,%.2f 0 0 1 %.2f,%.2f" fill="none" stroke="#ffffff" '
                         'stroke-opacity="0.22" stroke-width="1"/>'
                         % (cx - (r - 2.6) * 0.87, cy - (r - 2.6) * 0.5, r - 2.6, r - 2.6,
                            cx + (r - 2.6) * 0.87, cy - (r - 2.6) * 0.5))

    def letter_disc(self, cx, cy, s, colour=WHITE):
        self.disc(cx, cy)
        self.body.append(text(s, cx, cy + 0.2, 13.0, 3.3, colour))

    def xbox_disc(self, cx, cy, s):
        """A coloured A B X Y disc in the original Xbox's four colours (this project's own
        drawing): for the sets that show the letter of the other button, because the Nintendo
        face layout moved the game's A to the button an Xbox pad prints B."""
        top, bottom, ink = XBOX_DISC[s]
        self.disc(cx, cy, face=(top, bottom))
        self.body.append(text(s, cx, cy + 0.2, 13.0, 3.3, ink))

    def shape_disc(self, cx, cy, shape, mono=False):
        """mono: the DualSense's symbols, all one light grey; else the coloured ones of the DualShocks."""
        self.disc(cx, cy)
        n0 = len(self.body)
        a = 'fill="none" stroke-width="3.3" stroke-linecap="round" stroke-linejoin="round"'
        if shape == "cross":
            d = 5.6
            self.body.append('<path d="M%.2f,%.2f L%.2f,%.2f M%.2f,%.2f L%.2f,%.2f" stroke="#8fb2ff" %s/>'
                             % (cx - d, cy - d, cx + d, cy + d, cx + d, cy - d, cx - d, cy + d, a))
        elif shape == "circle":
            self.body.append('<circle cx="%.2f" cy="%.2f" r="6.4" stroke="#ff5f5a" %s/>' % (cx, cy, a))
        elif shape == "square":
            d = 5.6
            self.body.append('<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" stroke="#ff8fe0" %s/>'
                             % (cx - d, cy - d, 2 * d, 2 * d, a))
        elif shape == "triangle":
            self.body.append('<path d="M%.2f,%.2f L%.2f,%.2f L%.2f,%.2f Z" stroke="#45e6b0" %s/>'
                             % (cx, cy - 7.0, cx + 7.0, cy + 4.9, cx - 7.0, cy + 4.9, a))
        if mono:
            for i in range(n0, len(self.body)):
                for c in ("#8fb2ff", "#ff5f5a", "#ff8fe0", "#45e6b0"):
                    self.body[i] = self.body[i].replace('stroke="%s"' % c, 'stroke="#e6e8ee"')

    def dots_disc(self, cx, cy, where):
        """A dark button face with four dots in a diamond (top, right, bottom, left): the one
        at `where` ("N", "E", "S", "W") is the button to press, large and white; the others
        are small and grey. No letters: it reads the same on any pad."""
        self.disc(cx, cy)
        d = 6.9
        for name, dx, dy in (("N", 0, -d), ("E", d, 0), ("S", 0, d), ("W", -d, 0)):
            if name == where:
                self.body.append('<circle cx="%.2f" cy="%.2f" r="4.5" fill="%s"/>' % (cx + dx, cy + dy, WHITE))
            else:
                self.body.append('<circle cx="%.2f" cy="%.2f" r="2.2" fill="#8d92a3"/>' % (cx + dx, cy + dy))

    def key(self, x, y, w, h, s, height, stroke, rx=3.5, gap=0.22):
        """A shoulder key: light, with dark letters and the shared outline."""
        f = self.fill(PILL[0], PILL[1], y, y + h)
        self.body.append('<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" rx="%.2f" fill="%s"/>'
                         % (x, y, w, h, rx, INK))
        self.body.append('<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" rx="%.2f" fill="%s"/>'
                         % (x + 1.2, y + 1.2, w - 2.4, h - 2.4, max(rx - 1.2, 0.5), f))
        self.body.append(text(s, x + w / 2.0, y + h / 2.0, height, stroke, "#101218", gap))

    def small_key(self, x, y, w, h, s, height, stroke, gap=0.2):
        """A trigger key, in the 15 pixels the game gives it: dark, a light rim, light letters."""
        f = self.fill(FACE[0], FACE[1], y, y + h)
        self.body.append('<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" rx="2.6" fill="%s" stroke="#c9ccd6" '
                         'stroke-width="1"/>' % (x + 0.5, y + 0.5, w - 1.0, h - 1.0, f))
        self.body.append(text(s, x + w / 2.0, y + h / 2.0, height, stroke, WHITE, gap))

    def cap(self, cx, cy, w, h, what, size=None, stroke=1.5, gap=0.25, rx=6.0):
        """A key of the keyboard, dark like the face buttons: the outline every piece shares, a
        darker lip along the bottom edge (the key's side), a lighter top, and the label. `what`
        is a string of the stroke alphabet, or the name of a symbol ("back", "enter")."""
        x, y = cx - w / 2.0, cy - h / 2.0
        top = self.fill("#454a57", "#1b1d25", y, y + h)
        self.body.append('<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" rx="%.2f" fill="%s"/>' % (x, y, w, h, rx, INK))
        self.body.append('<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" rx="%.2f" fill="#0e1015"/>'
                         % (x + 1.4, y + 1.4, w - 2.8, h - 2.8, max(rx - 1.4, 1.0)))
        self.body.append('<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" rx="%.2f" fill="%s"/>'
                         % (x + 1.4, y + 1.4, w - 2.8, h - 5.0, max(rx - 1.8, 1.0), top))
        self.body.append('<path d="M%.2f,%.2f L%.2f,%.2f" stroke="#ffffff" stroke-opacity="0.22" stroke-width="1" '
                         'stroke-linecap="round"/>' % (x + rx, y + 2.0, x + w - rx, y + 2.0))
        fy = cy - 1.8                      # the middle of the top face
        if what in SYMBOLS:
            self.body.append(SYMBOLS[what](cx, fy))
        else:
            self.body.append(text(what, cx, fy, size, stroke, WHITE, gap))

    def flatcap(self, x, y, w, h, rx, arrow):
        """One of the d-pad's arrow keys: the outline, a light face, a dark arrow. No lip and no
        highlight, so the game can mirror it (it draws up and left from the down and right
        pictures). Light caps with dark arrows: the game tints this picture (the footer's blue), and a
        dark cap would vanish on the footer. `arrow` is "down" or "right"."""
        f = self.fill(PILL[0], PILL[1], y, y + h)
        self.body.append('<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" rx="%.2f" fill="%s"/>' % (x, y, w, h, rx, INK))
        self.body.append('<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" rx="%.2f" fill="%s"/>'
                         % (x + 1.2, y + 1.2, w - 2.4, h - 2.4, max(rx - 1.2, 1.0), f))
        cx, cy = x + w / 2.0, y + h / 2.0
        if arrow == "down":
            d = "M%.2f,%.2f L%.2f,%.2f M%.2f,%.2f L%.2f,%.2f L%.2f,%.2f" % (cx, cy - 3.6, cx, cy + 3.4, cx - 3.0, cy + 0.4, cx, cy + 3.6, cx + 3.0, cy + 0.4)
        else:
            d = "M%.2f,%.2f L%.2f,%.2f M%.2f,%.2f L%.2f,%.2f L%.2f,%.2f" % (cx - 3.4, cy, cx + 3.4, cy, cx + 0.4, cy - 3.0, cx + 3.6, cy, cx + 0.4, cy + 3.0)
        self.body.append('<path d="%s" fill="none" stroke="#101218" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/>' % d)

    def ring(self, cx, cy, r, sign, k=1.0):
        """The small round button in the middle of a pad: three lines (a menu), or a plus.
        k scales the strokes and the sign for a larger ring."""
        self.body.append('<circle cx="%.2f" cy="%.2f" r="%.2f" fill="#101421" stroke="%s" stroke-width="%.2f"/>'
                         % (cx, cy, r - 1.1 * k, WHITE, 2.2 * k))
        a = 'stroke="%s" stroke-width="%.2f" stroke-linecap="round"' % (WHITE, 1.9 * k)
        if sign == "menu":
            for dy in (-3.3 * k, 0.0, 3.3 * k):
                self.body.append('<path d="M%.2f,%.2f L%.2f,%.2f" %s/>'
                                 % (cx - 4.2 * k, cy + dy, cx + 4.2 * k, cy + dy, a))
        else:
            self.body.append('<path d="M%.2f,%.2f L%.2f,%.2f M%.2f,%.2f L%.2f,%.2f" %s/>'
                             % (cx - 4.6 * k, cy, cx + 4.6 * k, cy, cx, cy - 4.6 * k, cx, cy + 4.6 * k, a))

    def word(self, cx, cy, s, height=8.4, stroke=1.9, gap=0.3):
        self.body.append(text(s, cx, cy, height, stroke, WHITE, gap))

    def svg(self, title):
        return ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">\n'
                '<title>%s</title>\n<defs>%s</defs>\n%s\n</svg>\n'
                % (self.w, self.h, self.w, self.h, title, "".join(self.defs), "\n".join(self.body)))


# ── where the game keeps its prompts (texture space, pixels) ───────────────
# "Buttons", 128x64: four discs in the top row (the game's B, A, X, Y in that
# order), then its white and black buttons, the two triggers one above the
# other, the round Start button and the word under it.
LAYOUT = {
    "B": (15, 15), "A": (45, 15), "X": (75, 15), "Y": (105, 15),
    "white": (15, 47), "black": (45, 47),
    "lt": (63, 30, 15, 16.5), "rt": (63, 47, 15, 16.5),
    "start": (103, 41, 10.0), "word": (104, 57),
}

# family -> what stands where. None: the game's own picture stays ("word":
# nothing is drawn there). No set has a word under its Start ring: the game
# draws that block at about half size in its pause menu ("Resume"), where a
# word in the 44 pixels it has comes out 4 pixels tall (OPTIONS, MENU and PLUS
# were tried and were not readable in a 640x480 frame). The ring takes the
# whole block instead, with the sign the button itself carries.
FAMILIES = {
    "playstation": {
        "A": ("shape", "cross"), "B": ("shape", "circle"), "X": ("shape", "square"), "Y": ("shape", "triangle"),
        "white": "L1", "black": "R1", "lt": "L2", "rt": "R2", "start": "menu", "word": None,
    },
    # The DualSense prints the same four symbols without the colours: light grey on
    # the dark buttons.
    "playstation5": {
        "A": ("shape5", "cross"), "B": ("shape5", "circle"), "X": ("shape5", "square"), "Y": ("shape5", "triangle"),
        "white": "L1", "black": "R1", "lt": "L2", "rt": "R2", "start": "menu", "word": None,
    },
    # The game's A is the bottom button, which a Nintendo pad prints B.
    "nintendo_position": {
        "A": ("letter", "B"), "B": ("letter", "A"), "X": ("letter", "Y"), "Y": ("letter", "X"),
        "white": "L", "black": "R", "lt": "ZL", "rt": "ZR", "start": "plus", "word": None,
    },
    # The game's A is the button printed A (the Nintendo face layout).
    "nintendo_label": {
        "A": ("letter", "A"), "B": ("letter", "B"), "X": ("letter", "X"), "Y": ("letter", "Y"),
        "white": "L", "black": "R", "lt": "ZL", "rt": "ZR", "start": "plus", "word": None,
    },
    # Valve's pads (the Steam Deck, the Steam Controllers) print A B X Y where the
    # Xbox pad does, but as light letters on dark buttons, not the Xbox colours.
    "steam": {
        "A": ("letter", "A"), "B": ("letter", "B"), "X": ("letter", "X"), "Y": ("letter", "Y"),
        "white": "L1", "black": "R1", "lt": "L2", "rt": "R2", "start": "menu", "word": None,
    },
    # Any pad, no letters: a diamond of four dots, the one to press filled (draw.py
    # dots_disc). The game's A is the bottom button (S), B the right one (E), X the
    # left one (W), Y the top one (N)...
    "positional": {
        "A": ("dots", "S"), "B": ("dots", "E"), "X": ("dots", "W"), "Y": ("dots", "N"),
        "white": "L1", "black": "R1", "lt": "L2", "rt": "R2", "start": "menu", "word": None,
    },
    # ... or, when the pad is read by letter (a Nintendo pad SDL maps by its printed
    # letters, or the face layout set to nintendo), A is the right button, B the bottom
    # one, X the top one, Y the left one.
    "positional_swapped": {
        "A": ("dots", "E"), "B": ("dots", "S"), "X": ("dots", "N"), "Y": ("dots", "W"),
        "white": "L1", "black": "R1", "lt": "L2", "rt": "R2", "start": "menu", "word": None,
    },
    # The keyboard (the game's keys): Space is A, Backspace B, Left Shift X, Left Ctrl Y; Q and
    # E are the white and black buttons, S and W the triggers (brake, accelerate), Enter is
    # Start. Key caps in the same dark face and outline as the discs, on the same cells.
    "keyboard": {
        "A": ("cap", "space"), "B": ("cap", "back"), "X": ("cap", "shift"),
        "Y": ("cap", "CTRL", 7.6, 1.8, 0.22),
        "white": "Q", "black": "E", "lt": "S", "rt": "W", "start": "enter", "word": None,
    },
    # An Xbox 360 / One / Series pad: the discs are the game's too; it has
    # shoulders where the first pad had its white and black buttons.
    "xbox_modern": {
        "A": None, "B": None, "X": None, "Y": None,
        "white": "LB", "black": "RB", "lt": "LT", "rt": "RT", "start": "menu", "word": None,
    },
}


# ── the face layout swaps the game's A with B and X with Y ────────────────────
# Input > Face buttons = Nintendo makes the game's A the right-hand button, B the bottom
# one, X the top one, Y the left one. A prompt draws the button that does it, as the pad the
# set stands for prints it: for the sets that print something on the four buttons that is the
# set with the pictures of A and B, and of X and Y, exchanged. (Positional and Nintendo have
# their pair already: positional / positional_swapped and nintendo_position / nintendo_label;
# the keyboard has none, its keys are not placed.) The table is in pad_glyphs.h of the toolkit.
SWAP = {"A": "B", "B": "A", "X": "Y", "Y": "X"}


def swapped(fam):
    f = dict(FAMILIES[fam])
    for k, o in SWAP.items():
        f[k] = FAMILIES[fam][o]
    return f


# The original Xbox pad's own discs are the game's own picture, left untouched in the
# unswapped set; in the swapped one they are drawn here, in the same four colours.
XBOX_FACES = {k: ("xbox", k) for k in "ABXY"}
FAMILIES["xbox_swapped"] = {
    **{k: XBOX_FACES[SWAP[k]] for k in "ABXY"},
    "white": None, "black": None, "lt": None, "rt": None, "start": None, "word": None,
}
FAMILIES["xbox_modern_swapped"] = {
    **{k: XBOX_FACES[SWAP[k]] for k in "ABXY"},
    "white": "LB", "black": "RB", "lt": "LT", "rt": "RT", "start": "menu", "word": None,
}
for _fam in ("playstation", "playstation5", "steam"):
    FAMILIES[_fam + "_swapped"] = swapped(_fam)


def face(sheet, cx, cy, what):
    if what is None:
        return
    if what[0] == "shape":
        sheet.shape_disc(cx, cy, what[1])
    elif what[0] == "shape5":
        sheet.shape_disc(cx, cy, what[1], mono=True)
    elif what[0] == "xbox":
        sheet.xbox_disc(cx, cy, what[1])
    elif what[0] == "dots":
        sheet.dots_disc(cx, cy, what[1])
    elif what[0] == "cap":
        sheet.cap(cx, cy + 0.5, 28.0, 26.0, what[1], *what[2:])
    else:
        sheet.letter_disc(cx, cy, what[1])


def buttons(fam):
    f, s = FAMILIES[fam], Sheet(128, 64)
    for k in ("B", "A", "X", "Y"):
        face(s, LAYOUT[k][0], LAYOUT[k][1], f[k])
    for k in ("white", "black"):
        if f[k] is None:
            continue
        cx, cy = LAYOUT[k]
        s.key(cx - 13, cy - 8.5, 26, 17, f[k], 9.6, 2.3, rx=5.5)
    for k in ("lt", "rt"):
        if f[k] is None:
            continue
        x, y, w, h = LAYOUT[k]
        s.small_key(x, y, w, h, f[k], 7.6, 1.45, gap=0.24)
    cx, cy, r = LAYOUT["start"]
    if f["start"] is None:
        pass
    elif f["start"] == "enter":
        s.cap(LAYOUT["word"][0], 46.5, 42.0, 28.0, "enter", rx=7.0)
    elif f["word"] is None:
        s.ring(LAYOUT["word"][0], 46.5, 15.0, f["start"], k=1.5)
    else:
        s.ring(cx, cy, r, f["start"])
        wx, wy = LAYOUT["word"]
        s.word(wx, wy, f["word"], gap=0.3)
    return s.svg("Takedawn button prompts: %s, the game's 128x64 prompt sheet" % fam)


def single(fam, key):
    f = FAMILIES[fam]
    if f[key] is None:
        return None
    s = Sheet(32, 32)
    face(s, 16, 16, f[key])
    return s.svg("Takedawn button prompts: %s, the game's %s prompt (32x32)" % (fam, key))


def dpad(fam):
    """The game's "dpad" texture (32x16): a triangle pointing down in the rectangle (0, 0, 16, 13)
    and one pointing right in (18, 0, 12, 16); the game mirrors them for up and left. The keyboard
    set draws arrow keys in their places."""
    s = Sheet(32, 16)
    s.flatcap(0.5, 0.5, 15.0, 12.0, 3.2, "down")
    s.flatcap(18.5, 0.5, 11.0, 15.0, 3.2, "right")
    return s.svg("Takedawn button prompts: %s, the game's d-pad arrows (32x16)" % fam)


# the families that draw the d-pad as well
DPAD = ("keyboard",)


def files():
    out = {}
    for fam in FAMILIES:
        out["%s/buttons.svg" % fam] = buttons(fam)
        if fam in DPAD:
            out["%s/dpad.svg" % fam] = dpad(fam)
        for key, name in (("A", "a_button"), ("B", "b_button")):
            svg = single(fam, key)
            if svg:
                out["%s/%s.svg" % (fam, name)] = svg
    return out


def main(argv):
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "assets", "glyphs")
    check = len(argv) > 1 and argv[1] == "--check"
    bad = 0
    for rel, svg in sorted(files().items()):
        path = os.path.join(root, rel)
        if check:
            try:
                same = open(path, encoding="utf-8").read() == svg
            except OSError:
                same = False
            if not same:
                print("differs: assets/glyphs/" + rel)
                bad = 1
        else:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(svg)
            print("assets/glyphs/" + rel)
    return bad


if __name__ == "__main__":
    sys.exit(main(sys.argv))
