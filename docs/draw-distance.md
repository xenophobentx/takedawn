# Draw distance and pop-in: what the title decides where (BO-112)

Study of the NTSC-U Xbox build, and the one switch that came out of it:
`graphics.vehicle_detail`. The scenery's draw distance is in
the track files, not in the executable, and cannot be raised by a patch; the
camera's far plane is 10 km and is not what limits anything; the distances
at which vehicles change to simpler models are five floats in the
executable, and those are now switchable.

## Where each thing is decided

Addresses are of this build; PS2 names from Nahelam's symbol list where one
fits. `view` is the per-player view object (`0x7397C8 + player * 0x1CC`).

| What pops in | Decided by | Where | Patchable |
|---|---|---|---|
| Track, buildings, mid scenery, props | A list per track unit in the track data: for the unit the camera is in, 17 entries (stride 0xA9 from +0x12A) naming the units to draw by their offset from it, and for each a chain of mesh indices per material (+0x12D, next index at mesh +0x8C). Units within 4 of the camera's use the fully streamed unit (ring `view+0x154`, `view+8` long), further ones the low-detail stream (ring `view+0x17C`, `view+0xC` long). Then a frustum test per unit and mesh | `sub_0019D100` (fills the 17 slots, PS2 `CB3StreamedTrack::UpdateStreamPVS`), `sub_001AD6B0`, `sub_001AD510`, `sub_001AD7A0` (draw) | No: precomputed visibility in `streamed.dat`, and the streaming window the 64 MB console could hold. The "4" at `0x19D244` is in the executable, but the units beyond it are not in memory at full detail |
| Far scenery, backdrop (render flag 1, `0x4D61C5`) | The same unit record: up to 8 group indices at +0x74 into the static track's groups, and a backdrop pair at +0x7E / +0x7F | `sub_001AD7A0` at `0x1AD970`, `sub_001AD050`, `sub_001AD120` | No: data. The flag only switches the layer off |
| Props changing model (pine trees, signs, barriers) | Each prop type's 0x70-byte record in the static track (`MEM32(0x7397B4)` + 0x38, count + 0x34): a type with two models (+0x62 bit 0) draws its detailed one (+0x5C) within its switch distance (+0x64) and its simple one (+0x5E) beyond, both up to its draw distance (+0x68); `sub_0003A740` puts the squares in the draw context `0x549E50` (+0xD0, +0xD4), `sub_0003A840` compares per instance | `sub_001ADD60` at `0x1ADE86` (detailed pass) and `0x1AE00F` (simple pass) | **Yes** (`graphics.scenery_detail`): both models are in the static track, resident all race |
| Camera far plane, depth range | near 0.5, far 10000 for the three cameras (`RwCameraSetNearClipPlane` / `SetFarClipPlane`: `sub_001D92A0`, `sub_001D9360`) | `sub_0002ECC0` at `0x2EDAE`, `0x2EDCC` | Not the limit: nothing is drawn near 10 km. The car's reflection cube uses 0.1 to 200 m (`sub_0003FA20` at `0x3FC06`) |
| Rival cars changing model | Distance from the camera against three floats of the graphics manager, times the camera's factor (+0x90, 1.0): within 15 m level 3 (full), to 30 m level 2, to 60 m level 1, beyond level 0 | `sub_0018DE00` (`CB3RaceCar::Draw`); values `0x4D61D8`, `0x4D61DC`, `0x4D61E0`, set by `sub_00017450` at `0x176BB` (`CB3GraphicsManagerBase::SetDefaultLODSwitchDistances`) | **Yes** |
| Traffic changing model | The same way: within 20 m level 2, to 80 m level 1, beyond level 0 | `sub_001A4710` (`CB3TrafficSystem::Draw`); values `0x4D61E4`, `0x4D61E8` | **Yes** |
| Traffic appearing | Drawn only within 275 m of the camera (`75625` = 275 squared, `0x3A5E44` copied to `0x60E944`), only when its track unit is one of the 17 drawn, at most 32 vehicles a view (8 of them crashing ones), nearest first | `sub_001A4710` at `0x1A4914`, `0x1A4DE9` | Left alone: the traffic's update (`sub_001A6B40`) uses the same 275 m to decide which vehicles are live, and where traffic is made is simulation (valves and triggers in the track data) |
| Vehicle shadows (flag 11) | Drawn for every race car and traffic vehicle that is drawn; no distance of their own | `sub_0019A580` | Nothing to scale |
| Texture detail | The host renderer (`graphics.aniso`, `graphics.lod_bias`) | toolkit | Already settings |

The seventeen render flags of the PS2 research (forum.mattkc.com, viewtopic
p=1170) are the bytes at `0x4D61C4` here, as that post says: 0 position
markers, 1 backplate / far scenery, 2 wrong-way walls, 3 track, 4 track
reflections, 5 mid scenery and props, 6 their reflections, 7 near scenery,
9 traffic, 10 vehicles, 11 vehicle shadows, 12 bloom, 13 fog, 14 motion
blur, 15 HUD, 16 particles. They are on/off switches of layers; none is a
distance. No PS2 patch for draw distance exists (searched: PCSX2 patch
collections, Nahelam's PCSX2-Burnout-3-Takedown-Mods); its `force_lod`
patch hard-codes the same race-car level this setting moves.

## `graphics.vehicle_detail`

Overlay Graphics page, "Vehicle detail distance"; `settings.toml`
`[graphics] vehicle_detail = "original" | "far" | "farthest"`;
`RECOMP_GFX_VEHICLE_DETAIL`. Default `far` on the packaged desktop programs and
the Steam Deck (my decision of 2026-10-12, after I played several races on
the Deck with it), `original` on Android, in a development build and in a
`--headless` run.

`far` multiplies the five distances by 2 (cars 30 / 60 / 120 m, traffic 40 /
160 m), `farthest` by 4 (60 / 120 / 240 m, 80 / 320 m: beyond the 275 m
traffic is drawn to, so traffic never uses its lowest model). Five
`load_f32` guest patches (`config/patches.json`, expression
`loaded * vehicle_lod_scale`) after the loads at `0x18DE5A`, `0x18DE80`,
`0x18DEA3`, `0x1A478F`, `0x1A47A9`; the manager's values are not written.
The title's models, lists and limits are used as they are: all six cars at
the full model is what the start grid already draws. Live, from the next
frame. With the setting at `original` the generated code differs from the
unpatched build by the five gated statements.

It does not change scenery, track, props, traffic appearing or shadows
(props: `graphics.scenery_detail`, below), so "draw distance" would be the
wrong name for it.

## `graphics.scenery_detail` (BO-177)

Overlay Graphics page, "Scenery detail distance"; `settings.toml` `[graphics]
scenery_detail = "original" | "far"`; `RECOMP_GFX_SCENERY_DETAIL`. Default
`far` in release builds and on the Steam Deck (`src/game/main.c`, the
`env_default` block for a window); `original` in a build from source that
is not a release build, and headless.

The pine trees that change shape as the car comes round a bend are track
props with two models, not the track units' streams: at Silver Lake
(C3_V1) the pines are two prop types (26 and 28 of the track's 32) with
model 159 within 250 m and model 144, a flat dark silhouette, beyond it, up
to 1000 m. `far` multiplies every prop type's switch distance by 4 (two
`load_f32` patches, `loaded * scenery_lod_scale`, after the loads at
`0x1ADE86` and `0x1AE00F`, the same distance for the two passes, so each
instance is drawn with exactly one model). At Silver Lake 250 m becomes
1000 m, the pines' own draw distance: they keep the detailed model as long
as they are drawn. Other tracks' switch distances are 50 to 300 m with draw
distances of 100 to 1000 m (the first USA track: palms 70 m of 500 to
1000). Nothing is streamed: the models are in the static track. The draw
distances, the props without two models, the reflections pass
(`sub_001ADA40`) and the track units' full / low-detail choice
(`sub_0019D100`, 4 units, data the streaming window holds) are not
touched. With the setting at `original` the generated code differs from the
unpatched build by the two gated statements. Live, from the next frame.

Measured (Metal, this Mac, 640x480 frame dumps, `tools/headless/scenery_crash.sh`
and `scenery_scene.sh`, `scenery_report.py`): draw calls equal (the same
instances, another model), indices +16 % under the Silver Lake junction's
captions (340514 / 395108), +14.5 % on the run-up, +11.5 % in the crash
camera; on the first USA track +8.5 % paused, +3.1 % on a 60 s drive; on
the city track (C5_V1) +7 % paused, +10.6 % driving. Under the captions
(world standing) 0.21 % of the pixels change, all of them pine trees on the
hillside; original against original again: none.

## Measured (Metal, this Mac, 4:3; `tools/headless/draw_scene.sh`, `draw_report.py`)

Counts per frame, all render passes together (`RECOMP_TRACE=metal_frame`,
which now prints the indices of each pass): counts do not move with the
machine's load. The first rows are the same instant, under the title's
pause menu (the world stands still and is still drawn).

| Where | draws original / far / farthest | indices original / far / farthest |
|---|---|---|
| Rolling start, five rivals 10 to 40 m ahead (paused) | 1167 / 1171 / 1195 | 387294 / 406188 (+4.9 %) / 463665 (+19.7 %) |
| A second later (paused) | 1139 / 1143 / 1167 | 377496 / 396390 / 453867 |
| After a crash into the wall, rivals 100 m+ away (paused) | 1245 / 1255 / 1304 | 367083 / 386013 (+5.2 %) / 447756 (+22 %) |
| 27 changes over a 45 s drive with traffic (in motion, frames 4 to 10 apart) | | mean 340591 / 353465 (+3.8 %) / 374811 (+10 %); largest 413070 / 422622 / 480033 |

So the cost is geometry: up to a fifth more indices in a frame with all
rivals in view, 1 to 5 % more draw calls. Frame time was not measured: the
runs were headless on a machine other agents were loading, and at these
counts the Mac shows nothing. On the Deck and the RP5 the vertex work is
the part to check. Check it on the RP5 before Android gets a default other
than `original` (the Deck's `far` default is my choice after several races
there).

Pictures: `runs/draw2/paused.png` (three pauses and the crash, rows
original / far / farthest, 640x480 frame dumps), `runs/draw3/rivals_3x.png`
(the rivals of one pause from the window, left to right original / far /
farthest: the exhausts, wheels and rear of the cars 20 to 40 m ahead),
`runs/draw3/window/p<n>_<level>.bmp` (the whole window at each level).

- Under the pause menu the picture changes only at the vehicles: 0.07 to
  0.09 % of the window's pixels differ between levels (the rivals' boxes),
  none where no vehicle is in view; original against original again: none.
  HUD, sky, track unchanged.
- At 640x480 the difference is a handful of pixels a car. It is a setting
  for high render scales.

Not checked: frame time and GPU time anywhere; Deck, RP5, Windows, Vulkan;
16:9 and ultrawide (the patch is a distance compare, the projection is not
touched); split screen (the camera's own factor still multiplies); Road
Rage and Crash; the overlay row on screen (its setting is in the toolkit's
`overlay_settings` test); a pile-up with many traffic vehicles at the full
model. The window shots under the pause menu look 640x480-sharp although
the render targets were 1920x1440: not looked into.
