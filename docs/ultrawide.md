# Ultrawide (`display.ultrawide`, BO-94)

`display.ultrawide` (`RECOMP_DISPLAY_ULTRAWIDE` = `off` | `21:9` | `32:9` |
`window`; overlay: Display > Ultrawide; default off) draws the race's 3D for a
picture wider than 16:9 and leaves the front end as it is. It needs the
title's 16:9 mode (`display.aspect` 16:9); in 4:3 and 16:10 it does nothing.
The camera patches it uses are on whenever the title is in its widescreen
mode: at 16:9 they give the title's own values, at 16:10 (BO-271) the aspect
1.6 with 16:9's horizontal view (vert+; `fov_aspect` is the aspect, at least
16/9). Like the
widescreen HUD fix ([widescreen-hud.md](widescreen-hud.md)) it changes only
the title's own values, through `config/patches.json`.

## What changes

- Camera: the camera set-ups (`sub_0002F380`, one player; `sub_0002F480`,
  two) store a viewport aspect (16/9, `0x3B18EC`, its only use) and a
  field-of-view factor (1.2; 4:3 has 1.0). The patches give the picture's real
  aspect and scale the factor with it, so the vertical view stays 16:9's and
  the picture gains at the sides (hor+). PS2 counterpart: the three aspect
  globals of ThirteenAG's fix (`0x4E0C70`, `0x4E0C7C`, `0x4E0C80` = aspect * 2).
- Which frames are wide: marks (a patch kind that only tells the host the
  title got somewhere) at the stage set-ups, the stage renders and the five
  writers of `NV097_FLIP_STALL`: a frame is wide if a stage drew the world in
  it. `src/game/main.c` keeps the answer per flip number; the presenter
  (`nv2a_present_frame_aspect_fn`) shows a wide frame at the wide aspect and
  every other frame (menus, loading screens, videos, the garage) at 16:9 in the
  middle of the window. The mode switches per frame: the loading screen's last
  frame is 16:9, the race's first is wide (checked with every flip dumped).
- The front-end scene over a wide frame (pause menu, race intro banner,
  crash results) is squeezed about the centre to the size it has in 16:9.
  With the widescreen HUD fix on, its content is 4:3 and its decoration (the
  header, the bottom panel) spans the wide frame
  ([widescreen-hud.md](widescreen-hud.md), BO-222).
- The HUD follows `aspect` (set by the widescreen HUD fix, which should be
  on with this).
- The setting is sampled when a stage is set up: a change shows from the next
  race or Restart.

## States

| State | Shown |
|---|---|
| Menus, car select (3D garage), loading screens, videos | 16:9, bars at the sides |
| Race, Road Rage, Burning Lap, Crash: the 3D world and its HUD | wide |
| Race intro banner, pause menu, crash results (drawn over the world) | wide 3D, the overlay at its 16:9 size in the middle (with the HUD fix: its content 4:3, its decoration across the frame) |
| Crash mode's loading screens between its phases | 16:9 |
| A stage's last frame, drawn under the loading screen | 16:9 |

## Known

- Leaving a stage (Quit, Continue) the title draws one last stage frame with
  the loading screen already up, then flips once without drawing. The
  front-end scene keeps a picture that fills the screen on its layer 0 and
  never uses that layer over the 3D world, so a stage frame whose layer 0 has
  an object is a front-end frame: shown at 16:9, the scene unsqueezed, like
  the loading screens after it (`uw_fe_owns_picture` in `src/game/main.c`,
  a read of the scene the title built). Without this the world showed in the
  side strips for two frames.
- A flip without a stage frame straight after a wide one repeats that frame
  and is shown as it. If some path's first frame after a stage were a real
  front-end frame without a layer-0 picture, it would be shown wide for one
  frame; none was seen (race quit, crash mode's phases).
- Crash mode goes to 16:9 twice for about 135 frames (after the junction's
  intro flyby, and before the results): those are loading screens (black,
  the LOADING strip), not 3D.
- `window` takes the window's shape when a stage is set up, so a resize
  shows from the next race; windows not wider than 16:9 leave the mode off.
- Not exercised: two players (`sub_0002F480` sites), `32:9`, changing the
  row's value in the overlay (the row is shown), a resize between two races
  in a real window, the Vulkan presenter at run time (it is built; the Deck
  and the Bazzite have no wide output), the D3D11 presenter (it has no
  per-frame aspect: every frame at the configured one).
- Resolution: the window shows 598 of the frame's 640 columns over the whole
  width. At `graphics.render_scale` 3 that is 1794 samples for a 2560-wide
  21:9 picture (0.70 a pixel; 16:9 at 1920 has 0.93); scale 4 gives 2392
  (0.93). For 3440x1440 scale 4 is 0.70 a pixel. A horizontal-only scale is
  not built.

`BO3_UW_LOG=1` prints the stage set-ups and every change of a frame's shape
with its flip number.
