# Steam Frame build (VR, OpenXR, Linux ARM64)

State on 2026-10-21, on one Steam Frame (Snapdragon, Adreno 750, Mesa's
Turnip driver in SteamOS, SteamVR's OpenXR runtime): the game builds as a
native Linux ARM64 program, runs from the Frame's Steam library as a devkit
title, and draws every race picture again for each eye. The headset runs at
120 Hz with no missed refreshes over a race; the game keeps its own 60
(its crash cameras still run at 30, as on the console). The eyes are drawn at
SteamVR's recommended size (2160x2160 at the 100% resolution setting) and
the whole frame, both eyes included, costs about 9.5 ms of GPU time.

The toolkit side (OpenXR, the stereo replay, the headset loop) is in the
toolkit (xenophobentx/xboxrecomp, `takedawn` branch): `src/d3d/nv2a_vk_xr.c`,
the "stereo" part of `src/d3d/nv2a_pb_vulkan.c`, `D3D8_GLSL_STEREO` in
`src/d3d/d3d8_vsh_glsl.c`, CMake option `XBOXRECOMP_VK_XR` (Linux only, off by
default). This repository has the build and test scripts in `tools/frame/`
and one game hook, `xbox_vr_scene()` in `src/game/controls.c`.

## Build and install

On a Mac or a Linux PC with Docker (Colima on a Mac: `colima start --memory 4
--cpu 4`). The toolkit sits beside this checkout as `../xboxrecomp`
(`XBOXRECOMP_NAME` for another folder name).

    SPLIT=250 ./regen.sh                  # the generated C, in files small enough for a 4 GB VM
    tools/frame/build.sh all              # Docker image (once), build, package
    export FRAME_HOST=steamos@<frame>.local
    tools/frame/deploy.sh --game-dir "Burnout 3 Takedown"   # first time: the package, your game files, the Steam title
    tools/frame/deploy.sh                 # later: the package only

The image is Ubuntu 22.04 (glibc 2.35; the program needs 2.34, the Frame's
Steam Linux Runtime 4 has 2.41) with clang 15, SDL3 and sdl2-compat, the
Vulkan headers, glslang, SPIRV-Tools and the Khronos OpenXR loader. The
package (`build-frame/package/Takedawn`) carries the libraries the runtime
lacks in `lib/`. The Frame's CPU has the ARMv8.2 crypto extensions, which the
installer's SHA-256 path needs; the build turns them on for the whole program.

`deploy.sh` copies the package to `~/devkit-game/TakedawnVR`, the game files
to `~/.local/share/TakedawnVR/Burnout 3 Takedown`, registers "Devkit Game:
TakedawnVR" with Steam (the Frame needs Developer Mode) and asks SteamVR for
120 Hz for it (SteamVR offers the title 72 Hz otherwise). Start it from the
Steam library: a program started over SSH gets no controllers.

## In the headset

Races are immersive: the game's camera is where your head was when you last
recentred, and you look around from there. Hold View for a second to
recentre. The HUD is on a panel 1.75 m ahead that follows your head's turn
and tilt but not its roll. Menus, the garage, films and loading screens show
on a screen in front of you, in 3D where the game draws 3D.

The Frame's controllers are the first pad: sticks, triggers, A B X Y on the
right controller, the D-pad on the left, Menu is Start, View is Back, the
bumpers are Black and White. Rumble goes to both controllers.

## Settings

The launcher (`tools/frame/LAUNCH-STEAM-FRAME.sh`) sets the defaults below.
For one run, write `KEY=VALUE` lines to `~/.local/share/TakedawnVR/vr-diag`
on the Frame (read and deleted at the next start); to keep a change, edit the
launcher in `~/devkit-game/TakedawnVR`.

| Variable | Default | What |
|---|---|---|
| `RECOMP_XR` | 1 | 0: no headset (the flat window) |
| `RECOMP_XR_STEREO` | 1 | 0: the cinema screen for everything |
| `RECOMP_XR_EYE_SCALE` | 1 | the eyes' size against SteamVR's recommended one. 1.25 cost 12 ms of GPU and the headset dropped to 40-60 Hz |
| `RECOMP_XR_FXAA` | 1 | FXAA on each eye (the window presenter's) |
| `RECOMP_XR_MSAA` | 1 | 2: the eyes' scene multisampled. 2 cost 2.5-4 ms of GPU at 2160x2160 and missed refreshes |
| `RECOMP_XR_HUD_DISTANCE`, `_ANGLE` | 1.75 m, 40 degrees | the HUD panel |
| `RECOMP_XR_HUD_CROP_Y` | 16,464 | the rows of the HUD panel (of 480): the race's letterbox bars left out |
| `RECOMP_XR_SCREEN_DISTANCE`, `_WIDTH` | 2.2 m, 2.6 m | the cinema screen |
| `RECOMP_XR_CINEMA_DEPTH` | 6 | game units the cinema's 3D converges at |
| `RECOMP_XR_WORLD_SCALE` | 1 | game units per metre |
| `RECOMP_GFX_MOTION_BLUR` | 0 | the radial speed blur, off in the headset |
| `RECOMP_GFX_ANISO` | 8 | anisotropic filtering |

## How it works

OpenXR comes up before Vulkan: the runtime makes the Vulkan instance and
device (`XR_KHR_vulkan_enable2`), and the session shares the backend's one
queue under its lock. The backend hands each finished picture to the headset
the way it hands it to the window (three slots), and a headset thread runs
the OpenXR frame loop.

For stereo the backend keeps each frame's passes, clears and draws as it
records them (pipelines, dynamic state, descriptor sets, ring offsets, all
valid until the frame's slot is reused). At the flip it draws the presented
picture's passes again for each eye, into eye images of their own, after the
frame's own draws, which stay as they are for everything the game reads back.
The picture's targets are found by what samples what (the scene, bloom and
blur levels; not the reflection cube faces). Each draw gets what it needs:

- 3D draws (a vertex program with a perspective projection): the program
  keeps its clip position before its own viewport conversion and the eye's
  matrix takes it to the eye (P_eye * V_eye * inverse(P_game));
- billboards, glows, particles and skid marks, which the game projects on
  the CPU and draws as screen positions: rebuilt in 3D from their depth
  through the game camera's projection;
- the composite and the effects that read the picture: as drawn; the race's
  composite into its letterbox rows (16 to 464) is stretched back over the
  whole eye;
- the HUD (the 2D after the composite): drawn once over black and once over
  white, solved for alpha, and shown on its own panel;
- the letterbox bars and lines: left out.

A frame the game marks as its front end (`xbox_vr_scene()`: not the game
module) is drawn for two fixed eyes framed as the flat picture and shown on
the cinema screen instead.

On the Frame the toolkit also pins threads by core speed: the game's threads
on a big core, the GPU command thread on the prime core. Before, the game's
threads sat on a little core and 16-28% of a race's flips were late; now 0-6
in 600.

## Measured

Unattended runs on the Frame (`tools/frame/xr-race.sh`: the first race of a
new profile, driven by `tools/headless/to_first_race.sh`), 2026-10-20 and
2026-10-21:

| What | Result |
|---|---|
| Flat, over SSH, before and after the core change | 57.9 and 56.3 flips/s in the race (the difference is the game's 30 fps crash cameras); late flips 16-28% per 600 before, 0-6 after |
| Headset loop | 120 Hz, 0 missed refreshes over a race (eyes 2160x2160, no picture dumps) |
| Stereo | every race picture drawn for both eyes at the game's rate |
| GPU time a frame | 4.8 ms at 1728x1728 eyes, 9.5 ms at 2160x2160, 12 ms at 2700x2700 (too much), 12 ms with 2x MSAA at 2160 |
| Geometry | with the eye matrix set to identity the eyes equal the flat picture (mean difference 0.00); with real views, parallax and field of view as computed |
| Audio | 0 underruns in a stereo race at 120 Hz (`RECOMP_TRACE=audio_host`). Not listened to |

## Known issues

- The game culls what is outside its own view. A 60 degree head turn still
  showed the track and trees, but cars outside the game's view were not
  checked.
- New poses are drawn at the game's 60 frames a second; SteamVR fills the
  120 Hz in between.
- Draws that sample the target they draw into have no stereo replay yet; a
  frame with one shows flat. None were seen in a race.

## Testing

Every script takes `FRAME_HOST`. Nobody should be wearing the headset.

| Script | What |
|---|---|
| `tools/frame/flat-race.sh <out>` | the first race, flat and headless over SSH; prints the flip rate |
| `tools/frame/xr-race.sh <out> [KEY=VALUE ...]` | the first race in the headset through the Steam title, driven by the scene script |
| `tools/frame/unattended.sh <seconds> <out> [KEY=VALUE ...]` | one Steam launch for a while |

Useful for those runs: `RECOMP_XR_DUMP=dumps/xr-` (the eyes, the flat
picture and the HUD as image files every 5 seconds; writing them costs
refreshes, so measure timing without), `RECOMP_XR_FAKE_VIEWS=1` (fixed eyes,
with `RECOMP_XR_FAKE_IPD` and `RECOMP_XR_FAKE_YAW`), `RECOMP_XR_STEREO_TEST=1`
(identity eye matrix: the eyes must equal the flat picture),
`RECOMP_XR_STEREO_LOG=n|-1|-2` (a frame's draws as classified),
`RECOMP_TRACE=vk_frame=<file>` (GPU time per frame) and
`RECOMP_TRACE=pacing`.
