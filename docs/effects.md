# Bloom and motion blur switches (`graphics.bloom`, `graphics.motion_blur`, BO-101)

Both effects are the title's own, drawn by its code through extra render
passes. Two settings set their strength: overlay Graphics page, "Bloom" and
"Motion blur", sliders from 0 to 100% in steps of 10; `settings.toml`
`[graphics] bloom` / `motion_blur` (0 to 1; the old `false` / `true` read as
0 / 1); `RECOMP_GFX_BLOOM`, `RECOMP_GFX_MOTION_BLUR` (0 to 1). Both default to
1, 100%, the console's look, on every platform; 0 is off (BO-212 made them
strengths). Switching an effect off is an opt-in guest patch
(`config/patches.json`; toolkit `docs/pipeline/04-lifting.md`, Guest patches)
that makes the title's own condition for drawing the effect read false, so
the title takes its existing path for not drawing it. No draw call is
removed and no game code is replaced. A change shows from the next frame the
title builds; nothing latches.

## What the effects are

One block per player holds the parameters of both (`0x4D6340`, `0x4D63E0`;
made by `sub_0002EBE0`, which loads the `radialblurmask` texture; the current
one is `MEM32(0x4D6524)`). `sub_00179FD0` fills it every frame (the PS2
build's `CB3BoostEffectParams::GetRadialBlurParams`): +0x50 / +0x54 the
track's bloom Threshold and Intensity (`0x7397C0` / `0x7397C4`, read from
`ValueDB/TrackBloom/<track>.cfg` by `sub_0019AE10`; 0.75 / 1.0 on the tracks
seen), +0x58 a strength, 0.5 at rest and 0.5 + 0.4 x the car's boost amount
while boosting (0.86 seen), +0x00..+0x1C the blur's centre and scales.

Bloom is `sub_0003E520` (PS2 `CB3Bloom::Render`), called from the end of
a view's 3D (`sub_001AE340`; the garage's `sub_001AE6F0`): the frame's bright
part into a 320x240 target, through two 160x120 targets, then added over the
frame. On screen it is a soft glow that comes with the boost: with the car
at rest or driving without boost, switching it off changes nothing that can
be measured in the picture (below); while boosting the whole picture is
visibly hazier and brighter with it. So it is mainly a boost effect. Its three
passes are drawn in every frame of a stage, boost or not.

Motion blur is the radial blur, `sub_00042AD0` (`sub_00041480` draws the
frame into the 320x240 target, `sub_00041EC0` draws it back over the frame,
scaled about the centre through the mask): streaks towards the edges of the
picture that grow with speed (clear from about 90 mph without boost) and
with the boost. The HUD is drawn after it. It is drawn in every frame of a
stage too, at rest with no visible result.

The same byte gates two more draws that no run reached: another blur
(`sub_000461C0` + `sub_000474D0`, by the PS2 names the directional blur,
chosen by `sub_000459C0`), and the radial blur as a 2D object of a front-end
page (`sub_000FAC70`).

## What switches them

| Setting | Site | The title's condition | Patched to |
|---|---|---|---|
| `graphics.bloom` = 0 | `0x001AE5AA` (stage view), `0x001AEA0D` (garage) | `MEM32(0x4D537C) == -1` before the bloom call | reads 0: the no-bloom path |
| `graphics.bloom` = 0 | `0x0003DC3A`, `0x0003DC55` (BO-181) | the bloom Intensity (`[arg+4]`, the block's +0x54) that sets the weight of the bloom's last result in the frame pass | reads 0: weight 0 |
| `graphics.motion_blur` = 0 | `0x00042AD5`, `0x001AE678`, `0x000FACD9` | the graphics manager's byte `MEM8(0x4D61D2)` (manager `0x4D6170` + 0x62) | reads 0: the blur-off path |

`0x4D61D2` is one of seventeen on/off bytes the manager's constructor
(`sub_0002F260`) sets to 1; nothing in the build writes it by address and no
menu or options value reaches it: a development switch left in. `0x4D537C`
is a player index the title keeps at -1 in play; under the pause menu the
title draws no bloom itself. Neither value is written: only the register the
load filled changes, so every other reader sees the real one. There is no
bloom or blur entry in the title's own Options.

The byte loads needed a patch kind, `load_u8` (toolkit
`tools/recomp/patches.py`); the bloom sites are plain `load_u32`. With both
settings on, `regen.sh` output differs from the unpatched build by the five
gated statements only.

## Strength between 0 and 100% (BO-212)

The two effects are mixed over the picture by a number the title already
holds, so a strength below 100% scales that number in the guest. The
backends (Metal, Vulkan) do nothing special. While the setting is anything but
1, the generated code multiplies the value the instruction loaded by the
setting (`bloom_strength` / `blur_strength`, `src/game/main.c`); at 1 the
patch does not run, so 100% is the title's own code path. 0 is the
off path above.

| Effect | Site | The value | Times the strength |
|---|---|---|---|
| bloom | `0x0003DC3A`, `0x0003DC55` | the bloom Intensity (`[arg+4]`); the frame pass adds the bloom's last 160x120 target with the weight `Intensity x 0.5` (clamped at Intensity 2 to 1.0), handed to the combiners as the constant's alpha, so the glow added to the frame is out = 2 x (V + w x bloom) in the original | the weight, linear in the Intensity: 50% adds half the glow |
| motion blur | `0x00041F76` (`fld dword ptr [esi+0x1C]` in `sub_00041EC0`) | the blur block's opacity; x 255 into the top byte of the four vertices' diffuse colour; the draw blends SRC_ALPHA / INV_SRC_ALPHA (sf 0x302 df 0x303) | the alpha: 50% mixes the blurred picture over the frame at half the title's opacity |

The blur's site is an x87 `fld`, which the patcher's `load_f32` takes since
BO-212 (toolkit `tools/recomp/patches.py`). The bloom's blur passes still
run at any strength above 0; only the composite is weaker. The other blur
(`sub_000461C0`, never reached in a run) and the front-end's stay on or off.

## Switching the bloom off while it shows (BO-181)

Skipping the bloom call alone left a glow frozen on the picture. The pass
that puts the 3D frame on the back buffer, `sub_0003DA90`, runs after the
bloom call whether the bloom ran or not (it is the one 640x480 draw of six
indices before the radial blur in `RECOMP_TRACE=metal_frame`). It samples
the bloom's last 160x120 target (0x00ACC000) and adds it with a weight made
from the bloom Intensity: clamped, times a constant, handed to the register
combiners (`sub_0034E9A0`). With the bloom call skipped that target keeps the
last result drawn, and the frame kept adding it while the world moved under
it. The title never shows this itself: under its pause menu it skips the
bloom the same way, but the world stands still there. So the switch also
makes the Intensity read 0 at the two loads in `sub_0003DA90`; memory keeps
the real value. Checked (Metal, headless, 3x, paused after boosting, the
target bound black every 10 flips by a scratch test hook): with the bloom on
the black target changes 0.03 % of the pixels by up to 10/255 (the stale
glow); after `graphics.bloom` went off live, 0 pixels. With the bloom on the
patch returns the loaded value: the picture is the one it was. The patch is
in the guest code, so the Vulkan and D3D11 backends get the same.

## Cost, per frame of a one-player stage (Metal, `RECOMP_TRACE=metal_frame`)

Render passes of a race frame, in order (F 640x480, h 320x240, q 160x120;
the 14 small passes before them are the car's reflection cube and a 64x32
target):

    both on     F F  h q q  F  h  F      22 passes
    bloom off   F F         F  h  F      19
    blur off    F F  h q q  F            20
    both off    F F         F            17 (what the title itself draws under the pause
                                         menu; the two switches were not off together in a run)

- Bloom: 3 passes, one full-target draw each (320x240, 160x120, 160x120),
  plus the blend of the result over the 640x480 frame. No full-screen copy.
- Motion blur: 2 passes. One draw of the 640x480 frame into the 320x240
  target, then the back buffer is opened a second time (loaded, not cleared:
  on a tile-based GPU that is a full-frame load) and the blurred picture is
  drawn over it before the HUD. No texture-to-texture copy on the host.
- Both are paid in every frame of a race, Road Rage or Crash stage, whatever
  the speed; the garage draws the bloom only.

For Android: the numbers to measure are frame GPU time and the slow-frame
share with each off, at the same place. The pass counts above are from
Metal; the Vulkan backend's `RECOMP_TRACE=vk_frame` gives the same count per
flip.

## Effect targets and the render scale (BO-109)

Every render target, small ones included, is sized through
`nv2a_host_size(w, h, render_scale, ...)` with no size threshold, so the
title's effect targets already follow `graphics.render_scale`; the sizes above
are the guest sizes. `RECOMP_TRACE=metal_frame` prints the host size after `>`
(one race frame, same 22 passes at both scales):

| Guest | Render scale 1 | Render scale 4 | Used for |
|---|---|---|---|
| 640x480 | 640x480 | 2560x1920 | the frame (3D, HUD) |
| 320x240 | 320x240 | 1280x960 | bloom first target; radial blur target |
| 160x120 | 160x120 | 640x480 | bloom's two blur levels |
| 128x128 (x6 faces, 64 KB apart, 28..144 draws each) | 128x128 | 512x512 | the car's reflection cube map, redrawn every frame |
| 128x128 (one more, 2 draws) | 128x128 | 512x512 | small pass after the faces (not identified) |
| 64x32 (3 draws, first of the frame) | 64x32 | 256x128 | small pass (not identified) |

The blur and bloom draw whole targets with texture coordinates, so their reach
is the same fraction of the screen at any scale. The reductions (the bloom's
640x480 to 320x240 to 160x120, the radial blur's 640x480 to 320x240) take four
taps placed in the title's own texels, which at a render scale would land on a
few of the host texels of each block, so a single bright host texel could pass
the bloom threshold and show as a glow dot that 1x never draws (BO-178, GitHub
#78). The toolkit handles it: such a draw reads an area-averaged copy of its
source at the title's size instead (`docs/runtime/overlay.md` in the toolkit,
"Reductions of a scaled target"; `RECOMP_DEBUG=rt_reduce=0` gives the old
picture for a comparison). Nothing is done in the game's own code.

Two extras: `graphics.lod_bias` (`RECOMP_GFX_LOD_BIAS`; "auto" by default on the
packaged desktop programs and the Steam Deck since 2026-10-12, off elsewhere)
and `graphics.dither` (`RECOMP_GFX_DITHER`; off everywhere), described in the
toolkit's `docs/runtime/overlay.md`.

## Checked: the strengths (BO-212)

Metal on the Mac and Vulkan (software, lavapipe, the release AppImage built
from the same sources) on a Linux box, headless; the frame of
`to_race.sh` at the rolling start (`race_start`, before any input of ours),
first track of the USA list, one run a configuration; the Trax box masked.
Builds: the heads before the change and with it, so the only difference is
this work.

- 100 %: the frame differs from the build before in about 1000 pixels (0.3 %)
  on Metal, as two runs of the build before differ from each other (931);
  the same on Vulkan (mean squared difference 0.2 to 5 between any two runs
  of either build, 130 and up only where a run lands on another flip). The
  generated code differs in three statements that do not run at 1: the
  title's own path. Not byte for byte frame by frame: the scene is not
  frame-exact from run to run (above). The garage's `garage_car` shot is
  byte-identical in 4 of 4 runs of each build (it draws no visible effect).
- 0 %: the same as the build before with the effect off (Metal 541 pixels
  apart, Vulkan mean squared difference 2.4, both inside that noise).
- 50 %: both effects at 50 % put the frame 53 % (Metal) and 51 % (Vulkan)
  of the way between 0 and 100 % (the frame projected on the line through
  the two); the blur alone 53 % on Metal. The bloom adds nothing
  measurable to the rolling-start frame (the same finding as above), so its
  linearity is the code's (the weight is Intensity x 0.5 x the strength), and
  its steps are seen in the drive.
- A drive (`tools/headless/strength_scene.sh`, `AUTOPILOT=1 CYCLE=240`):
  frames at 100, 50 and 0 % five flips apart, bloom and blur in turn. The
  radial streaks (gradient energy of the frame, 8 cycles) fall 100 > 50 > 0
  with 50 % at 54 % of the way; the bloom's haze around the sun and lamps
  steps with the setting by eye. The world moves a few metres between the
  three frames.

Not checked: the directional blur, 16:9 and ultrawide, render scales above 1,
the overlay row on a real window (the settings are covered by the toolkit's
`overlay_settings` test).

## Checked (Metal, headless, 4:3, render scale 1)

`tools/headless/fx_scene.sh <rundir> race|roadrage|crash` switches one
effect off for six frames and on again at chosen moments of one run
(`BO3_FX_AT`, `src/game/main.c`) and dumps the frames around both changes;
`tools/headless/fx_report.py <rundir>` compares them and lists the passes.

- Car at rest on the grid, three changes of each switch: the frame pair a
  switch goes off between differs by 0.003 (blur) and 0.001 (bloom) of 255
  beyond the scene's own movement when no step of the track's animated
  chevron lights falls on it. Sky, buildings, car and HUD unchanged.
- At 98, 111 and 157 mph: the radial streaks go and come back with the blur
  switch; HUD, EA Trax pop-up, sky and car unchanged to the eye.
- Boosting at 149 mph (strength 0.81): the haze goes with the bloom switch,
  the blur stays; and the other way round.
- Passes: every change in Race (34), Road Rage (44) and Crash (32) took 3
  (bloom) or 2 (blur) passes off wherever the title was drawing the effect,
  and none where it was not (garage and crash camera: no blur; pause menu:
  neither).
- Re-entry: the first frame with an effect again against the next one shows
  no step beyond the scene's movement (within 1.5 of 255 in 19 of 21
  changes of a run at 60 fps; the two others are a camera cut and a frame
  the loaded machine dropped). Both
  effects are built from the frame being drawn, with no history.
- Defaults: the frame at the rolling start (`to_race.sh`, flip of
  `race_start`) differs between this build and the integration build in
  0.32-0.34 % of its pixels; two runs of this build differ in 0.32 %. The
  scene is not frame-exact from run to run, so "identical" is shown by the
  generated code, not by a bit-equal dump.

Not checked: the directional blur and the front-end 2D blur (no run reached
them; patched by reading), a takedown camera, split screen, 16:9 and
ultrawide, render scales above 1, the Vulkan backend, the overlay rows on
screen (no window in these runs; the rows' settings are covered by the
toolkit's `overlay_settings` test), GPU time. A still picture with the blur
visible does not exist (the title hides it under the pause menu), so the
blur's before/after frames are three frames apart, not the same instant.
