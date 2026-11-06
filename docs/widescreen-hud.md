# The widescreen HUD fix (`display.hud_fix`, BO-89)

In 16:9 the title draws its 3D for a 16:9 picture, but lays its 2D HUD out in
640x480 and lets the anamorphic picture stretch it 4/3 wide. `display.hud_fix`
(`RECOMP_GFX_HUD_FIX=1`, overlay: Display > Widescreen HUD, default off) changes
only the title's own layout values, through the opt-in guest patches of
`config/patches.json` (toolkit: `docs/pipeline/04-lifting.md`, Guest patches).
Every site has a note there; this page is the map and the list of what was
never seen on screen.

The arithmetic follows the PS2 widescreen fix (ThirteenAG/WidescreenFixesPack,
`source/Burnout3.PCSX2F.WidescreenFix`, MIT); the Xbox sites were found in this
build, with Nahelam's PS2 symbol list as the guide to the names.

## How it works

- `s = (4/3)/aspect` (0.75 at 16:9), `W = 480*aspect`.
- The title's 2D engine: a scene (`CGt2dScene`) has a root frame; a frame has a
  local position, scale and colour and the world values its parent gives it
  (`sub_001C1670`); an object is drawn at frame origin + frame scale x its
  position (`sub_001C6500`). Three kinds of scene exist: the two HUD scenes
  (`CB3HUDManager` at `0x54F900`, scenes `0x54F914` / `0x54F984`), the
  front-end scene `0x557A20` (menus, loading screen, race intro, pause menu,
  results) and the EA Trax scene `0x45BAD0`.
- The HUD scenes' root is squeezed by `s` about x = 320 (`sub_001C17A0`,
  guarded by scene address). Every HUD object and text keeps its shape.
- The HUD layout rectangle (`sub_00053700` and its two inlined copies), from
  which `CB3HUD::GetAlignPos` (`sub_00053BE0`) places every component, is moved
  out so that each side is as far from the *visible* edge as in 4:3. With the
  root squeezed, a rectangle edge at `L` lands on screen at `320 + s*(L - 320)`;
  it has to land at `vis_x0 + s*(30 - ref_x0)`, so
  `L = 30 + (vis_x0 - 320)/s + 320 - ref_x0` (and the same with 610, `vis_x1`,
  `ref_x1` at the right). `vis_x0..vis_x1` are the frame columns the window
  shows of the picture as presented, `ref_x0..ref_x1` those a 4:3 picture
  shows in the same window (`nv2a_present_visible_x`, `_at`: the presenter's
  own layout). The overscan crop shows 21..619 either way; the overscan "Fill
  height" (BO-235) shows the whole 0..640, like no overscan, and stretches the
  picture 480/448 taller (the HUD's x layout does not see that); the 16:10 fill on a
  1280x800 panel shows 51..589 of the 16:9 picture and still 21..619 of a 4:3
  one, so the rectangle is -69.67..709.67 with the crop and -29.67..669.67 with
  that fill. The window's size is known from its creation; a headless run
  uses the overscan alone, or `BO3_HUD_OUTPUT=<w>x<h>`.
- What sits on the root with absolute, full-width coordinates gets the whole
  frame's width and left edge: the letterbox bars and lines, the caption
  strip's background, the two-player viewport frames.
- Outside the scenes: the start countdown and GO (`sub_0001A1E0`) and the
  rival marker (`sub_0018F060`) are squeezed about their own middle / the
  car's projected point. Their positions are not touched.
- The race's centre messages ("YOU ARE 6 SECONDS BEHIND", laps, distance:
  the slots `sub_00057FA0` sets up for the component `sub_00057040` updates)
  slide: `sub_000583B0` moves the message's frame by an offset from the
  screen's middle, +320 to 0 in the first 0.2 s (in from the right), 0 for
  4.5 s, +50 in 0.1 s and to -320 in the last 0.2 s (out to the left). An
  offset of 320 is the frame's edge, 21 columns past the visible picture;
  under the squeezed root it would be 3/4 of the way there. The stored offset
  is scaled so that +-320 is again as far past the visible edge as in 4:3
  (the rectangle's arithmetic with 640 for 610; 1.31 at 16:9 with the crop).
  The title itself shows the message cut by the right edge while it comes in
  and by the left while it goes (4:3: 6 frames, up to 48% of the text past
  the edge); that is its slide.
- The EA Trax pop-up is squeezed about the visible left edge in a frame a
  stage drew (beside the HUD), and about x = 320 in the front end, with the
  menu it sits on (`stage_frame`).
- The front-end scene (BO-222): its root is squeezed by `(4/3)/base_aspect`
  about x = 320 (`base_aspect` is the title's own picture), so the menus, the
  loading screens, the race intro, the pause menu and the results keep their
  4:3 shape in the middle. Over a wide frame (`display.ultrawide`) this
  composes with `fe_squeeze`, so the content is 4:3 there too.
- Its decoration is drawn where the title puts it, which is the whole
  picture. In the object draw (`sub_001C6500`, store patches of the drawn x
  and width, host values `fe_dx` / `fe_dw`), an object of the front-end scene
  whose box, taken back to the title's own columns, reaches from the left edge
  to the right one is drawn at those columns: the borders, the checkered
  strips, the header, the bottom panel, the menu video, the loading picture,
  the LOADING strip and its lines, full-screen fades. An object at least 400
  wide that reaches one edge gets that edge there and keeps the other where
  the squeeze puts it (the loading screen's tip bars, 140..640). The one box
  across the frame that is content, the selected line's brackets
  (`sub_000FB900`), is left to the squeeze. The PS2 fix patches each element's
  width and x; here it is one rule by geometry.
- The front end's 3D: the garage is drawn by the one-player camera
  (`sub_0002F380`), which has the title's 16:9, and is not touched. The
  Crash Nav globe is drawn under the front end's 2D camera (owner
  `0x4D6170` + `0x310`, made once by `sub_0002ECC0` with its constructor's
  4:3 view window), so in 16:9 the title draws the 4:3 view a third wider
  than it is; it follows `display.pictures` (below). That camera is not the
  globe's alone: `sub_0003D9E0` with mode 0 begins every frame's 2D pass
  through it, in races, loading screens and menus, so its window is not
  changed.
- Crash Nav's Select Location map (`display.crashnav_map`, Game page row
  "Crash Nav map", shown while Widescreen HUD is on; BO-222). The map is six
  tiles (callback `0x1C1A60`) on a frame of its own with the route line
  (`0x1C0790`), the crosshair (`0x1C1930`), the shields (`0xD77A0`) and their
  NEW labels (`0x5AC20`); the info box beside the chosen place (`0xF9CE0`,
  `0x1C51A0`, `0x1C1A60`, `0x1C5330`) is on the root frame. The screen is
  told by its route object, and its parts never take the decoration rule
  above (a route box 400 columns from the map's left edge took it, and the
  route was drawn from the screen's edge over the frame: the Europe routes
  Alpine Expressway and Continental Run). `stretched` (default): the map,
  route and crosshair at the title's own columns, across the picture like
  the frame around them; the shields and labels at their places there but at
  their 4:3 shape (a label goes with its nearest shield), the info box moved
  with the chosen place (the shield drawn twice). `native`: the squeeze
  alone, the map 4:3 with the menu. In both, what the title keeps past the
  screen's edges on this screen (the location list's NEW labels at column
  740) stays there, out of the columns the squeeze opens. The zoom that
  leads to the screen (globe to region, region to city) is drawn on four
  pictures of the root frame (`0x1C1A60`, 640x416 from the top left) on
  exactly the tiles' box, with the route object already there: a picture
  with the tiles' drawn box takes the map's shape, so with `native` the
  zoom is 4:3 too and does not jump to 4:3 as it lands (BO-314,
  `tools/headless/crashnav_zoom_dump_scene.sh`). Live; nothing
  changes in 4:3 or with Widescreen HUD off. Headless check:
  Crash Nav > USA and Europe, each place in turn, at 16:9 (both options,
  as shown at 1920x1080 and on a 1280x800 panel) and 4:3.
- `display.pictures` (`RECOMP_DISPLAY_PICTURES`, Game page row "4:3
  pictures", only with Widescreen HUD on; BO-315): the front end's 4:3
  full-screen pictures, which the decoration rule draws across the 16:9
  picture: the loading screen's picture (`0x1C1A60`, 640x384), the films
  behind the menus (`0x1C59A0`, 640x352: main menu, Junction Selection,
  Crash Training) and Crash Nav's zoom and map. `blur` (default, the
  my choice of 2026-10-30; an unset variable reads as blur) keeps them
  4:3 with a blurred, darkened copy of the same picture across the screen
  under them; `black` keeps them 4:3 over a black copy; `stretch` draws them
  across the picture as before (a saved `stretch` stays). The fills are drawn by the picture's own callback (wrapped in
  `src/game/recomp/recomp_manual.c`): 16 taps on an 18-column disc at
  1/(k+1) opacity each, at 45% brightness, then the picture at 4:3. At
  `blur` and `black` the Crash Nav map and its zoom are 4:3 whatever
  `display.crashnav_map` says, and that row is hidden. The race and crash
  results and the garage are 3D and keep their shape anyway. The Crash Nav
  globe (BO-385, GitHub #124) is 3D too, but its camera keeps a 4:3 view
  window (above): at `blur` and `black`, for the globe's draw only
  (`sub_000F31E0`, wrapped in `recomp_manual.c`), the clip x of the
  camera's matrices for that frame is divided by the picture's shape over
  4:3 (4/3 at 16:9, the `globe_k` of `src/game/main.c`): column 0 of the
  projection (`0x4D66B0`) and of the view-projection (`0x4D66F0`), and row
  0 of the transposed copy the globe loads as vertex shader constants
  (`0x4D6730`), put back after the draw. The globe then has the size, shape
  and place it has in 4:3, beside the squeezed menu, and the sides show
  more of it over the menu film's fill; at `stretch` it is the title's own.
  It is read every frame, so a change shows at once. Headless check:
  `tools/headless/globe_scene.sh` (globe, zoom, Select Location, loading
  screen). Live; nothing changes in 4:3.
- Latch: the rectangle sites sample the setting when a stage is set up; every
  other HUD site follows that sample. A change shows whole from the next race
  (or a Restart). The front-end sites follow the setting as it is. A
  `RECOMP_GFX_HUD_FIX` in the environment locks the setting.
- In 4:3 every expression gives the title's own value.

## Verified on screen (Metal, headless frame dumps cut to the visible picture)

Race (POS, LAP, speedometer, boost bar, score, the boost messages and the
awards in the middle, the sliding centre message at 16:9 and 21:9, crash
letterbox (plain bars, and the textured bars of `sub_00050A70`'s mode 3:
853 wide, columns 0..640) and caption strip, "Impact Time"), Road Rage (takedowns counter), Burning Lap
(timer panels), Crash mode (Crashbreaker bar), countdown and GO, rival labels
and triangle, EA Trax pop-up, two players one above the other, the live toggle
with Restart, 4:3 with the fix on (scene dumps equal to fix off).

Front end (BO-222; Vulkan on Linux, headless frame dumps, 16:9 and 21:9,
`tools/headless/fe_scene.sh`): main menu with the EA Trax pop-up, Crash Nav,
Select Mode, Select Options, Select Car, Junction Selection, the loading
screens (tip bars to the edge), Crash Training, the race intro banner, the
pause menu, the crash captions, Crash Results (and its Replay button), the
race finish and RACE RESULTS. At 21:9 the menus and loading screens are 16:9
frames (pillarboxed) and look as at 16:9; over the wide frames the header and
the bottom panel span the 21:9 picture. The countdown and GO are the HUD's
(above). Not reached: a replay screen with its own bars (the PS2 fix lists
top and bottom replay bars; Crash Results' Replay keeps the results page up),
and Get Ready, which none of these routes showed.

## Known unverified sites

Patched by reading the code, never seen in a run. If a report matches one of
these, start here.

| Site(s) | What | Why not seen |
|---|---|---|
| `0x00053770`, `0x0005379F` (`sub_00053700`); `0x00052B7C`, `0x00052B86`, `0x00052B9F` (`sub_00052AB0`) | Two players side by side: layout rectangles and viewport frames | The layout is chosen by `MEM8(0x4D617F)`, which no menu reached sets; the PS2 fix does not patch this layout either |
| `0x0018F83D`, `0x0018F844` (`sub_0018F060`) | The rival marker's first text, drawn instead of the triangle | Races draw the triangle and the position label |

## Known unpatched elements

| Where | What | State |
|---|---|---|
| The HUD's entrance at the start of a stage (`0xF6DC0`, `0xF7C20`, `0x4AE40` objects: POS, LAP, speedometer and boost bar come in from outside the picture over about 15 frames) | Offsets from the element's place, squeezed with the root: the elements start 3/4 as far out in frame columns, the same distance on the screen as in 4:3 | Seen with the extent monitor in every mode, cut for the same frames as in 4:3; left alone |
| `sub_0018F060` | The marker's second text and its small box (drawn when the caller's flag argument is set) and two bar quads | Not seen; left as the title draws them |
| `sub_00051230` | A paragraph's wrap width, 610 - 30 | A width for centred text, left alone |

## The cut-off check (extent monitor)

`BO3_HUD_MON=<file>` (`src/game/main.c`, every platform): at every flip, on
the guest's thread, the four scenes are walked as the title's render walks
them and a line is written for every drawn box that an edge of the visible
picture cuts (callback, frame, object, columns, rows, size, alpha, side).
`BO3_HUD_MON_TRACE=<callbacks|all>` follows objects whether cut or not (the
path of a slide), `BO3_HUD_MON_SHOTS=<prefix>` writes the frames of the flips
the lines are about (`BO3_HUD_MON_SHOTS_CB=<callbacks>`: only the cut boxes of
those callbacks; a shot stops its frame for 11-30 ms, so a played session
names the few it is after, as `tools/win/play-trace.cmd` does). The comment at
`hud_mon_flip` has the format and what the
monitor cannot see (glyphs outside their box, the 2D drawn outside the scenes,
a scene kept in memory while it is not rendered).

    tools/headless/hud_extents.sh runs/x            # race roadrage burninglap crash, 4:3 and 16:9 + fix
    tools/headless/hud_extents.py diff <4:3 hudmon> <16:9 hudmon>
    tools/headless/hud_extents.py trace <hudmon> <callback> [from] [to]
    tools/headless/hud_extents.py same <4:3 hudmon> <4:3 fix-on hudmon>   # both traced with TRACE=all
    tools/headless/hud_extents.py present <out> 169 <frames...>

`hud_extents.sh` runs `hud_scene.sh` (a scripted drive per mode: weaving,
braking into slides, boost) once in 4:3 and once in 16:9 with the fix, one
headless game at a time, and prints the difference: a kind of box (scene,
callback, side, height) cut only in 16:9 is the fix's defect and needs an
explanation; one cut in both, as deep and as long, is the title's own use of
the overscan (the POS / LAP / takedowns plates run 21 columns past the visible
edge in 4:3 and the same distance with the fix; the letterbox, the caption
strip and the front end span the frame). Frames are judged cut to the visible
picture (`present`), never as the 640-column dump.

Callbacks seen (named by where they sit, unless a note in
`config/patches.json` says more): `0x48430` the plates behind POS / LAP / the
takedowns counter, `0x4AE40` the boost bar, `0x4B4D0` the boost message (AIR, ONCOMING,
DRIFT, NEAR MISS, TAILGATING), `0xF7C20` text made by `sub_000F68E0` (speed
digits, the sliding centre message on frame `0x463270`), `0x5AC20` text made
by `sub_0005AA60` (the awards in the middle: "FENDER BENDER! 25"), `0x54D90`
their icons and the crash banner's, `0xF6DC0` the digits of POS and LAP,
`0x4E450` the caption strip's text, `0xFA840` its background, `0xB53F0` the
letterbox lines, `0x1C1930` its bars, `0x2DF00` the EA Trax pop-up.

## Looking at a layout

`BO3_HUD_DUMP=<file>` (macOS/Linux, `src/game/main.c`): `touch <file>.req`
appends a dump of all four scenes to `<file>`: the frame tree and, per layer,
every object with its render callback, frame, position, size and the screen
columns it lands on. The callback address names the code (`config/patches.json`
notes); an object spanning 0..640 on a frame at 0,0 is a full-width element
the squeeze would shorten. `echo 1 > <file>.set` switches `display.hud_fix`
through the settings API, to test the latch.
