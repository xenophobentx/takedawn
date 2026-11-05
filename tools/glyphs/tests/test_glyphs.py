"""The button-prompt art (tools/glyphs, assets/glyphs): that the SVGs on disk are what
draw.py writes, that every set the runtime knows has its pictures, and that the keyboard
set draws something different in every cell of the game's prompt sheet.

    python3 -m unittest discover tools/glyphs/tests

The rendering tests need an SVG renderer (resvg, rsvg-convert or resvg_py) and are skipped
without one.
"""
import os
import re
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.abspath(os.path.join(TOOLS, "..", ".."))
sys.path.insert(0, TOOLS)

import build  # noqa: E402
import draw  # noqa: E402


class Art(unittest.TestCase):
    def test_svgs_match_draw(self):
        self.assertEqual(draw.main(["draw.py", "--check"]), 0, "assets/glyphs differs from tools/glyphs/draw.py")

    def test_every_family_has_a_sheet_and_the_runtime_lists_it(self):
        src = open(os.path.join(ROOT, "src", "game", "glyphs.c"), encoding="utf-8").read()
        used = set(re.findall(r"bo3_glyph_(\w+?)_buttons", src))
        self.assertEqual(used, set(draw.FAMILIES), "glyphs.c and draw.py FAMILIES disagree")
        for fam in draw.FAMILIES:
            self.assertTrue(os.path.isfile(os.path.join(ROOT, "assets", "glyphs", fam, "buttons.svg")), fam)

    def test_keyboard_set_names_every_key(self):
        k = draw.FAMILIES["keyboard"]
        self.assertEqual([k[c][1] for c in "AXY"], ["space", "shift", "CTRL"])
        self.assertEqual(k["B"], ("cap", "back"))
        self.assertEqual((k["white"], k["black"], k["lt"], k["rt"], k["start"]), ("Q", "E", "S", "W", "enter"))
        for name in ("a_button", "b_button"):
            self.assertTrue(os.path.isfile(os.path.join(ROOT, "assets", "glyphs", "keyboard", name + ".svg")))

    def test_dpad_texture_is_wired(self):
        src = open(os.path.join(ROOT, "src", "game", "glyphs.c"), encoding="utf-8").read()
        for fam in draw.DPAD:
            self.assertIn("bo3_glyph_%s_dpad" % fam, src)
            self.assertTrue(os.path.isfile(os.path.join(ROOT, "assets", "glyphs", fam, "dpad.svg")))
        self.assertIn('"dpad"', src)

    def test_stroke_alphabet_has_the_labels(self):
        for word in ("CTRL", "Q", "E", "S", "W"):
            for ch in word:
                self.assertIn(ch, draw.LETTERS, ch)


class FaceLayout(unittest.TestCase):
    """The prompts follow Input > Face buttons (the toolkit's pad_glyphs.h has the table): the
    title's A B X Y are on the bottom, right, left, top buttons (S E W N) when the pad is read by
    position, on E S N W when it is read by letter. A set's picture of the title's action is what
    its pad prints on the button that triggers it. Each pair below is (read by position, read by
    letter); the second must be the first looked up through that rule."""
    BY_POS = {"A": "S", "B": "E", "X": "W", "Y": "N"}
    BY_LETTER = {"A": "E", "B": "S", "X": "N", "Y": "W"}

    def prints(self, fam):
        """What the unswapped set prints on S E W N (it draws A on S, B on E, X on W, Y on N)."""
        f = draw.FAMILIES[fam]
        return {self.BY_POS[k]: f[k] for k in "ABXY"}

    def assertSwapPair(self, plain, swapped, plain_faces=None):
        on = {self.BY_POS[k]: v for k, v in (plain_faces or draw.FAMILIES[plain]).items() if k in "ABXY"}
        for action in "ABXY":
            self.assertEqual(draw.FAMILIES[swapped][action], on[self.BY_LETTER[action]],
                             "%s: the title's %s is on the %s button" % (swapped, action, self.BY_LETTER[action]))

    def test_sets_that_print_on_the_face_buttons_have_a_swapped_twin(self):
        for fam in ("playstation", "playstation5", "steam", "positional"):
            self.assertSwapPair(fam, fam + "_swapped")
        self.assertSwapPair("xbox_modern", "xbox_modern_swapped", {k: ("xbox", k) for k in "ABXY"})
        self.assertSwapPair("xbox_modern", "xbox_swapped", {k: ("xbox", k) for k in "ABXY"})

    def test_nintendo_is_the_pad_that_prints_the_other_way_round(self):
        # a Nintendo pad prints B on S, A on E, Y on W, X on N: by position the game's A shows B,
        # by letter (the face layout) the game's A is on E and shows A
        pos, lab = draw.FAMILIES["nintendo_position"], draw.FAMILIES["nintendo_label"]
        self.assertEqual({k: pos[k][1] for k in "ABXY"}, {"A": "B", "B": "A", "X": "Y", "Y": "X"})
        self.assertEqual({k: lab[k][1] for k in "ABXY"}, {k: k for k in "ABXY"})
        nin = {"S": "B", "E": "A", "W": "Y", "N": "X"}
        for action in "ABXY":
            self.assertEqual(pos[action][1], nin[self.BY_POS[action]])
            self.assertEqual(lab[action][1], nin[self.BY_LETTER[action]])

    def test_swapped_sets_are_the_same_art_in_the_other_places(self):
        for fam in ("playstation", "playstation5", "steam"):
            for key in ("white", "black", "lt", "rt", "start", "word"):
                self.assertEqual(draw.FAMILIES[fam][key], draw.FAMILIES[fam + "_swapped"][key], fam + " " + key)
        self.assertEqual({draw.FAMILIES["playstation_swapped"][k][1] for k in "ABXY"},
                         {"cross", "circle", "square", "triangle"})
        # the cross is not where the circle is
        self.assertEqual(draw.FAMILIES["playstation_swapped"]["A"], ("shape", "circle"))
        self.assertEqual(draw.FAMILIES["playstation_swapped"]["B"], ("shape", "cross"))
        self.assertEqual(draw.FAMILIES["playstation_swapped"]["X"], ("shape", "triangle"))
        self.assertEqual(draw.FAMILIES["playstation_swapped"]["Y"], ("shape", "square"))
        self.assertEqual(draw.FAMILIES["steam_swapped"]["A"], ("letter", "B"))

    def test_xbox_swapped_draws_only_the_discs(self):
        f = draw.FAMILIES["xbox_swapped"]
        self.assertEqual([f[k] for k in "ABXY"], [("xbox", c) for c in "BAYX"])
        for key in ("white", "black", "lt", "rt", "start", "word"):
            self.assertIsNone(f[key])
        self.assertEqual(len(draw.XBOX_DISC), 4)

    def test_keyboard_has_no_swapped_set(self):
        self.assertNotIn("keyboard_swapped", draw.FAMILIES)

    def test_the_runtime_has_a_slot_for_every_swapped_set(self):
        src = open(os.path.join(ROOT, "src", "game", "glyphs.c"), encoding="utf-8").read()
        for fam in ("xbox_swapped", "xbox_modern_swapped", "playstation_swapped", "playstation5_swapped", "steam_swapped"):
            for name in ("buttons", "a_button", "b_button"):
                self.assertIn("bo3_glyph_%s_%s" % (fam, name), src)


class Ps2Driving(unittest.TestCase):
    """input.control_scheme = ps2: while driving the game's A is the right shoulder and its B the right
    trigger (docs/controls.md), and glyphs.c draws them over A_Button and B_Button."""

    def src(self):
        return open(os.path.join(ROOT, "src", "game", "glyphs.c"), encoding="utf-8").read()

    def test_every_picture_is_drawn_and_wired(self):
        table = re.search(r"#define PS2_PAIRS\(X\)(.*?)\n#define", self.src(), re.S).group(1)
        for name in draw.PS2_DRIVING:
            self.assertTrue(os.path.isfile(os.path.join(ROOT, "assets", "glyphs", "ps2_driving", name + ".svg")), name)
            self.assertRegex(table, r"\bps2_driving_%s\b" % name)

    def test_labels_are_the_sheets_right_shoulder_and_trigger(self):
        # the same words the set's own sheet prints on its right shoulder and trigger
        labels = {v[1]: k for k, v in draw.PS2_DRIVING.items()}
        for fam in ("playstation", "playstation5", "nintendo_position", "nintendo_label", "steam",
                    "positional", "positional_swapped", "xbox_modern", "xbox_modern_swapped"):
            f = draw.FAMILIES[fam]
            self.assertIn(f["black"], labels, fam)
            self.assertIn(f["rt"], labels, fam)
            self.assertEqual(draw.PS2_DRIVING[labels[f["black"]]][0], "shoulder")
            self.assertEqual(draw.PS2_DRIVING[labels[f["rt"]]][0], "trigger")

    def test_each_set_has_its_twin_in_set_order(self):
        # PS2_PAIRS: the 15 sets in set order (set 0 first), each with the labels of its family's right
        # shoulder and trigger (the keyboard keeps its own keys); k_a and k_b take the two columns
        src = self.src()
        want = {"rb": "rt", "r1": "r2", "r": "zr"}
        table = re.search(r"#define PS2_PAIRS\(X\)(.*?)\n#define", src, re.S).group(1)
        pairs = re.findall(r"X\((\w+), (\w+)\)", table)
        self.assertEqual(len(pairs), 15)
        for i, (a, b) in enumerate(pairs):
            if i == 9:                                      # set 9 is the keyboard
                self.assertEqual((a, b), ("keyboard_a_button", "keyboard_b_button"))
                continue
            self.assertEqual(want[a.replace("ps2_driving_", "")], b.replace("ps2_driving_", ""), i)
        self.assertIn("PS2_PAIRS(PS2_A)", re.search(r"k_a\[\] = \{(.*?)\};", src, re.S).group(1))
        self.assertIn("PS2_PAIRS(PS2_B)", re.search(r"k_b\[\] = \{(.*?)\};", src, re.S).group(1))


@unittest.skipUnless(build.renderer(), "no SVG renderer")
class Rendered(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.how = build.renderer()

    def sheet(self, fam, name="buttons"):
        svg = os.path.join(ROOT, "assets", "glyphs", fam, name + ".svg")
        w, h = build.svg_size(svg)
        pw, ph, px = build.png_rgba(build.render_png(self.how, svg, w, h))
        self.assertEqual((pw, ph), (w, h))
        return w, h, px

    @staticmethod
    def cell(px, w, x, y, cw, ch):
        return [px[(y + j) * w + x + i] for j in range(ch) for i in range(cw)]

    def test_keyboard_cells_are_drawn_and_distinct(self):
        w, h, px = self.sheet("keyboard")
        # the cells of the game's "Buttons" sheet (draw.LAYOUT): B A X Y, white, black, triggers, start
        cells = {
            "B": (1, 1, 28, 28), "A": (31, 1, 28, 28), "X": (61, 1, 28, 28), "Y": (91, 1, 28, 28),
            "white": (2, 39, 26, 17), "black": (32, 39, 26, 17), "lt": (63, 30, 15, 16), "rt": (63, 47, 15, 16),
            "start": (84, 33, 40, 28),
        }
        seen = {}
        for name, (x, y, cw, ch) in cells.items():
            c = self.cell(px, w, x, y, cw, ch)
            opaque = sum(1 for p in c if p[3] > 200)
            self.assertGreater(opaque, cw * ch * 0.6, name + " is drawn")
            light = sum(1 for p in c if p[3] > 200 and min(p[:3]) > 200)
            self.assertGreater(light, 8, name + " has a label")
            key = tuple(c)
            self.assertNotIn(key, seen, "%s and %s look alike" % (name, seen.get(key)))
            seen[key] = name

    def test_keyboard_caps_are_not_discs(self):
        # a key cap fills the corners of its cell further than a 28-pixel disc does
        w, h, px = self.sheet("keyboard")
        _, _, mod = self.sheet("steam")
        corner = lambda p: p[5 * w + 34][3]           # inside a cap's top left corner, outside a disc
        self.assertGreater(corner(px), 200)
        self.assertLess(corner(mod), 120)

    def test_dpad_keys_sit_where_the_games_triangles_do(self):
        # the game draws the 32x16 "dpad" from a down triangle in (0, 0, 16, 13) and a right
        # triangle in (18, 0, 12, 16), mirrored for up and left: the caps stay in those boxes
        w, h, px = self.sheet("keyboard", "dpad")
        self.assertEqual((w, h), (32, 16))
        inside = lambda x, y: (x < 16 and y < 13) or (18 <= x < 30)
        for y in range(h):
            for x in range(w):
                if px[y * w + x][3] > 20:
                    self.assertTrue(inside(x, y), "a pixel outside the arrow keys at %d,%d" % (x, y))
        for (x0, y0, x1, y1) in ((1, 1, 15, 12), (19, 1, 29, 15)):
            n = sum(1 for y in range(y0, y1) for x in range(x0, x1) if px[y * w + x][3] > 200)
            self.assertGreater(n, (x1 - x0) * (y1 - y0) * 0.7)
            light = sum(1 for y in range(y0, y1) for x in range(x0, x1)
                        if px[y * w + x][3] > 200 and min(px[y * w + x][:3]) > 200)
            self.assertGreater(light, 6, "the arrow")

    def test_single_discs_are_32_square(self):
        for name in ("a_button", "b_button"):
            w, h, px = self.sheet("keyboard", name)
            self.assertEqual((w, h), (32, 32))
            self.assertGreater(sum(1 for p in px if p[3] > 200), 500)

    def test_label_fits_inside_the_cap(self):
        # the label's light pixels stay off the outer two pixels of the cap
        w, h, px = self.sheet("keyboard")
        for name, (x, y, cw, ch) in {"A": (31, 1, 28, 28), "X": (61, 1, 28, 28), "Y": (91, 1, 28, 28)}.items():
            c = self.cell(px, w, x, y, cw, ch)
            for j in range(ch):
                for i in range(cw):
                    if i < 2 or i >= cw - 2 or j < 2 or j >= ch - 2:
                        p = c[j * cw + i]
                        self.assertFalse(p[3] > 200 and min(p[:3]) > 200, "%s label touches the edge at %d,%d" % (name, i, j))


if __name__ == "__main__":
    unittest.main()
