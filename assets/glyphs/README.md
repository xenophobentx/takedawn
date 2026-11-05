# Button prompts (`input.glyphs`)

Original artwork for this project: the prompts the game shows (Select, Back,
Crashbreaker, Resume...) drawn for pads other than the first Xbox pad.
Nothing here is taken from the game, and nothing is a console maker's
artwork or font: the pictures are circles, rounded rectangles and a small
stroke alphabet of lines and arcs, all written out in `tools/glyphs/draw.py`.
The PlayStation set is a cross, a circle, a square and a triangle as plain
strokes.

| folder | for | what it changes |
|---|---|---|
| `playstation/` | DualShock 3, 4 | the four shapes, L1 R1 L2 R2, a three-line Options ring |
| `playstation5/` | DualSense, DualSense Edge | the same four symbols as the DualSense prints them (light grey, no colours), L1 R1 L2 R2, the Options ring |
| `positional/` | any pad (chosen by hand, never by auto) | no letters: a diamond of four dots, the one to press filled (bottom for the game's A, right for B, left for X, top for Y), L1 R1 L2 R2, a Menu ring |
| `positional_swapped/` | the same, when the pad is read by letter (a Nintendo pad SDL maps by its printed letters, or `input.face_layout = nintendo`) | the filled dot is the right one for the game's A, bottom for B, top for X, left for Y |
| `nintendo_position/` | a pad printed the Nintendo way, read by position | the letter printed on the button to press: B where the game means A, A for B, Y for X, X for Y; L R ZL ZR; a plus ring |
| `nintendo_label/` | the same pad when the game's A is the button printed A (`input.face_layout = nintendo`, or a pad SDL reads by letter) | the same look, the letters unswapped |
| `steam/` | Steam Deck, Steam Controller | A B X Y as light letters on dark buttons, L1 R1 L2 R2, a Menu ring |
| `keyboard/` | the keyboard (a mapped key was the last input) | key caps on the same cells: Space, a backspace tag, Shift, Ctrl (A B X Y); Q and E (white, black); S and W (the triggers: brake, accelerate); an Enter arrow for Start; arrow keys in place of the d-pad triangles (`dpad.svg`, 32x16) |
| `xbox_modern/` | Xbox 360 / One / Series pads | LB RB LT RT and a Menu ring; A B X Y stay the game's own |
| `xbox_swapped/`, `xbox_modern_swapped/`, `playstation_swapped/`, `playstation5_swapped/`, `steam_swapped/` | the same sets when the pad is read by letter: `input.face_layout = nintendo` moves the game's A to the right-hand button, so the prompts show what that button is | the pictures of A and B, and of X and Y, exchanged: B A Y X on the Xbox and Valve pads, circle cross triangle square on the PlayStation ones. The Xbox discs are drawn here in the four Xbox colours (the unswapped Xbox set keeps the game's own) |

The prompt shows the button that actually triggers the action once the face layout is applied. The table
of which set draws what, for each pad and layout, is in the toolkit's `src/input/pad_glyphs.h`;
`tools/glyphs/tests` checks the pictures against it.

Each folder has `buttons.svg` (128x64), `a_button.svg` and `b_button.svg` (32x32). An SVG has the size of the game
texture it goes into and holds a picture at each place where the game keeps a
prompt. The positions are numbers (`LAYOUT` in `draw.py`, the rectangles in
`src/game/glyphs.c`). At run time the pictures are copied over the game's
texture as it is uploaded to the GPU; where a set changes nothing (the A B X Y
discs of the Xbox set) the game's own texels simply stay.

The SVGs are written by `tools/glyphs/draw.py` (`--check` tells whether they
match it) and rendered at 1:1 by `tools/glyphs/build.py` when the game is
built, with the renderer `assets/icon` uses: `resvg`, `rsvg-convert` or the
`resvg_py` module (`-DBO3_GLYPHS_PYTHON=`, or `TAKEDAWN_PYTHON`). No
rendered files are kept here. A developer build without a renderer builds without
the prompts and prints a warning; a release build stops.

    tools/glyphs/build.py assets/glyphs out.c out.h --png preview/   # to look at them

## The heading font's W (`headfont/`)

`headfont/w.svg` (34x38) is not a button prompt: it is the letter W for the
game's heading font, the one the red page headings (SINGLE EVENT, DRIVER
DETAILS, the online pages' heading) are drawn with. The font is in the game's
executable and has only the letters its headings use, none of which has a W,
so TAKEDAWN ONLINE could not be written with it. `src/game/headfont.c` puts
this W into a free corner of the font's atlas and its glyph table at run time
(see `headfont.h`). It is drawn from the W of [Anton](https://github.com/googlefonts/AntonFont)
(SIL Open Font License 1.1, Copyright 2020 The Anton Project Authors; the
licence is `headfont/Anton-OFL.txt`) by `tools/glyphs/headfont.py`, which
scales the outline into the cell and gives it a one-texel stroke so it is as
heavy as the game's letters. Nothing of the game's font is in it. It is
rendered with the prompts (`build.py`) as `bo3_glyph_headfont_w`.

    tools/glyphs/headfont.py Anton-Regular.ttf            # write headfont/w.svg (needs fontTools)
    tools/glyphs/headfont.py Anton-Regular.ttf --check    # status 1 if it differs
