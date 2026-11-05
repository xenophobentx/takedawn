# Shader pre-build (BO-93)

The GPU backends (Metal on macOS, Vulkan on Linux, the Steam Deck and Windows)
compile a shader or a pipeline the first time a draw needs it, on the render
thread, so a fresh install hitches at the garage, at the first race start and
at the first crash. The pre-build makes the game play those scenes by itself
once, without a window or sound, so everything they need is compiled and
listed before the player gets there.

It is always optional. The setup window has it as a box that is ticked by
default (*Prepare graphics for smooth first races*, a few seconds, ~2 MB;
untick it to skip), and the settings overlay has it as an action (Graphics,
*Build shaders now*). The game never asks about it by itself at its first
start. Without it each shader compiles when it is first drawn, and is
remembered from then on. Where there is no setup window
step (Android, which cannot start a second copy of the game) nothing is
offered and the game compiles as it plays.

Nothing is distributed. The key list holds the title's own data (a vertex
program in it is the program's microcode, byte for byte as in `default.xbe`;
a fragment key is the combiner state the title programmed), so it is made on
the player's machine, from the player's own disc, and stays there.

## What is on disk

The shader cache directory (`render.shader_cache` / `RECOMP_RENDER_SHADER_CACHE`,
default macOS `~/Library/Caches/Takedawn`, Linux `$XDG_CACHE_HOME/takedawn` or
`~/.cache/takedawn`, Windows `%LOCALAPPDATA%\Takedawn\cache`):

| file | what | backend |
|---|---|---|
| `metal_pipelines.bin` | the key list: vertex programs (slots), combiner states, pipeline keys | Metal |
| `vk_pipelines.bin` | the same for Vulkan (its pipeline key also has the vertex layout and topology) | Vulkan |
| `vk_spirv.bin` | SPIR-V by source hash, under the glslang version | Vulkan |
| `vk_pipeline_<vendor>_<device>.bin` | the driver's `VkPipelineCache` | Vulkan |
| `prebuild/state.txt` | which scenes a pre-build finished, `built`, `offer never` | both |

At every start a thread builds the whole key list into the render thread's
tables (the pre-warm) before the title draws; a draw that comes while it is
still running waits for it instead of compiling. On Metal the compiled code
itself is in the system's per-app Metal cache, which the pre-build fills
because it runs the same executable: in an app bundle, start the bundle's
executable (or its launcher), not a copy elsewhere.

The key lists are kept across rebuilds: their version is the key
format's, independent of the toolkit commit. A changed shader generator, a driver update or an OS
update only makes the next start's pre-warm compile again, in the background,
from the same list. Tests and the installer's smoke runs must set
`RECOMP_RENDER_SHADER_CACHE` to a directory of their own (`tools/headless`
does: `build/shader-cache` in the checkout).

## The shipped coverage manifest (BO-208)

The scene pre-build above learns only what its two scenes draw, so the program
also carries a list of what a game needs, made by a sweep of the title on the
developer's machines, and builds all of it before the first race (BO-208).

`graphics/pipelines-vulkan.manifest` and `graphics/pipelines-metal.manifest`
are compiled into the program (`src/game/pcmanifest.c`), so they are the same
on every platform, an APK included. They are key lists in the format of the
files above, under the backend's key-format version plus `; manifest <revision>`.
They hold no title code and no art:

- vertex programs by the 64-bit hash of their slots (record type 16). The
  microcode is found on the player's machine, in the scan of their own
  `default.xbe` (`nv2a_pcache_scan_vsh`), and goes into the local key list;
- combiner states (the register values the title programs) and pipeline keys
  (shader ids, vertex layout, topology, blend, write mask, depth bit).

A program the title assembles at run time (one of 28 in a normal game, used by
a handful of pipelines) is not in the executable; its hash stays unresolved
and the title shows it at its first draw, which then builds the manifest's
pipelines that name it (a rescan).

At every start the backend applies the manifest: what the local key list lacks
is appended, and the pre-warm builds the list on up to four threads
(`RECOMP_PCACHE_THREADS`). The first start, after a new manifest, a driver or a
changed shader generator, is the long one: the title is held at its first
draw while the pre-warm works, up to 180 s (`RECOMP_PCACHE_HOLD_S`), with
*Preparing graphics N/M* on the screen (the overlay line, so Android has it
too). With the caches warm it is lookups, 0.1 to 0.3 s. The installer's
*Prepare graphics* step and the overlay's *Build shaders now* run the same build
(`--build-shaders`, scene `manifest`, seconds instead of minutes). The scene
pre-build is still available as `--build-shaders --scenes race,crash` and is the
default only in a build without a manifest.

### What a setting changes

Only HDR changes the title's own pipelines (half-float targets and the HDR
variant of every combiner). The pre-warm builds the variant in use first, then,
where HDR can be reached at all (the display or the system can show it, or HDR
is on), the other one on a single low-priority thread after the game has
started, so a later switch in the menu waits for nothing. The Windows HDR
detection (BO-207) only has to make `nv2a_host_hdr_potential()` greater than
1. Metal's HDR is the prototype of BO-110 and keeps no HDR keys. Anti-aliasing
(FXAA, SMAA), sharpening, the render scale and the title's own bloom and
motion blur are the presenter's passes or part of the frame; their pipelines
are built at the presenter's first frame, and the title's pipelines have one sample and no
resolution in their keys.

### The sweep

`tools/headless/sweep.sh <outdir> <visit>...` plays each visit (headless, in a
process of its own, with no manifest and nothing seeded from the executable) and
leaves its key list in `<outdir>/<visit>/cache`. `sweep_visits.sh` prints the
standard list: every Single Event track in both directions, Road Rage and Time
Attack on a sample of them, Crash junctions, the garage through every car, the
title's AI racing for 150 game seconds on several tracks, World Tour (every
region, location and event), Multiplayer split screen (five modes) and the menu
pages. A visit is `mode/region/track/cars/drive`
(`shaderbuild.c`, `sweep_build`) or a script file (`script:<file>`, one step a
line: `press`, `pressuntil`, `file`, `race_tail`, `mark`, ...). `BO3_SWEEP_SHOTS=1`
dumps a frame at every mark. Then:

    tools/headless/pcache_keys.py manifest -o graphics/pipelines-vulkan.manifest \
        --rev <name> --base graphics/pipelines-vulkan.manifest <outdir>/*/cache/vk_pipelines.bin

The manifest only grows (`--base` keeps what it had). The key list of a Metal
sweep gives the Metal manifest. One Vulkan manifest serves every Vulkan
platform: the keys do not depend on the driver.

Sweep again when the combiner state (`NV2ACombinerState`) gets a field. The
build guard stops a manifest of another size, and `pcache_keys.py widen` only
adds zero bytes to each state, so a state that has the new field on is still
missing and is built at its first draw. BO-320 did that for `alpha_kill` (0.3):
the standard visits above plus a dozen more autopilot races
(`RECOMP_DEBUG=autopilot=1`, `autorace` and `autoroadrage`), 228 visits on each
backend.

## Cache hygiene (BO-208)

- The driver's pipeline cache is one file per driver build:
  `vk_pipeline_<vendor>_<device>_<uuid>[_t<hash of TU_DEBUG>].bin`, under a
  32-byte header of ours (vendor, device, driver version, length, CRC). A
  truncated, damaged or foreign file is detected, removed and built again,
  where the driver would only have started empty without saying so. The bundled
  Turnip, the system driver and a custom package of one GPU each keep their own
  file, so switching between them throws nothing away. Blobs unused for 90 days
  are removed.
- On Android the key lists, the SPIR-V file and the driver caches are in the
  app's files (`files/graphics`), which the system does not empty; the scaled 2D
  art stays in the cache folder (`ui_scale`, regenerable).
- Invalidation needs no rule: a new manifest revision adds keys, a changed key
  format drops the local list (the manifest then fills it again), a new driver is
  a new file, and a changed shader generator is a changed SPIR-V key.

## HDR variants (BO-110)

With HDR on (Vulkan: graphics.hdr where the display offers HDR10), the
game draws with an HDR variant of each combiner shader and with pipelines
for half-float targets. These are built and kept only where HDR can be
used:

- No HDR display, HDR never switched on (most desktops, all Android):
  nothing HDR is built, listed or cached, so the pre-build's time and the
  cache's size are unaffected by HDR.
- The display can show HDR (the Display page has the HDR row) or HDR
  is on: *Build shaders now* in the overlay builds the HDR variants too
  (the game starts the pre-build with `RECOMP_PCACHE_HDR=1`; each scene
  process's pre-warm builds the HDR variant of every key in the list beside
  the SDR one, so the SPIR-V cache and the driver's cache hold both). The
  setup window's pre-build runs before any display is known and builds SDR
  only.
- HDR switched on for the first time on a machine that has not built
  them: the switch (made in the paused overlay, applied at the next flip)
  starts the pre-warm thread on the HDR variants of the whole key list,
  derived from the SDR keys (the HDR variant of each combiner state, each
  pipeline on a half-float target with it). The draws wait for its
  pipelines as at a start, at most 3 s, and the window shows *Preparing HDR
  shaders* while it runs; nothing is compiled on the render thread for a key
  the list had. What HDR then meets that the list lacked is recorded as its
  own record type (`PC_FS_HDR`, and pipeline keys with the half-float bit),
  which a build without HDR skips.
- At every start the pre-warm builds the variant the targets are in: SDR
  with HDR off, HDR with it on.

## The command

```
burnout3 --build-shaders [--cache-dir <dir>] [--scenes race,crash] [--force] [--status]
                         [--speed <n>|max]
```

Run it where the game runs: the working directory holds `Burnout 3 Takedown/`
(on a packaged install start it through the launcher, which also sets
`enhance.toml` and, on Linux, the library path: `burnout3.sh --build-shaders`,
`"Burnout 3 Takedown.app/Contents/MacOS/Burnout 3 Takedown" --build-shaders`;
on Windows `burnout3.exe --build-shaders` with the working directory
`<root>\Game`).

| option | |
|---|---|
| `--cache-dir <dir>` | the cache to fill; default the game's own (as above). Give the installed game's cache, or nothing |
| `--scenes a,b` | only these scenes (`race`, `crash`); `built` is only recorded by a run of all |
| `--force` | play scenes again that an earlier build finished |
| `--status` | print what is built and exit: status 0 all built, 10 not all |
| `--speed <n>` | how fast the scenes may play, in times real time (default 8): `1` plays them at a game's own pace, `max` has no bound. See *How fast* |

It plays, each in a process of its own (`--build-shaders-scene <name>`, an
internal option), on an empty scratch save folder
(`<cache>/prebuild/saves`, removed afterwards; the player's profile is not
opened) with a new profile that is never saved:

| scene | what it goes through |
|---|---|
| `race` | logos, title screen, profile screens, main menu, Single Event > Race > USA > Silver Lake Southbound, the garage (two cars), the load, the rolling start, about 25 s of driving with boost and a crash into the scenery, the pause menu |
| `crash` | the same start, Single Event > Crash > 1. Trailer Trash, the garage, the load, the intro flyby, the captions, the countdown, the impact, the crash camera, Crash Results |

The two training films a new profile is shown (115 s and 100 s, no button
skips them) are answered with the 3 s Criterion logo file; the intro film is
skipped with START. The scene process is headless, muted, rumble off, and
ignores the host's pads and keys.

### How fast

A scene does not wait for the clock. The title draws a frame and then waits
for the vertical blank it asked for, 16.7 or 33.3 ms after the last; with
nobody watching, that wait is skipped: the clock is moved to the blank
instead (the toolkit's time warp, `src/platform/host_time.h`). Every clock the
title can read is moved by the same amount at the same moment, so to the
title each frame still took its 16.7 ms and the game plays as it does at real
time, frame for frame: the same number of flips between two points of a scene
(1051 from the junction's load to its captions in every run, at any speed).

| what | in a scene |
|---|---|
| the vertical blank (its interrupt, the title's handler, the flip that waits for it) | raised as soon as a finished frame waits for it |
| `rdtsc`, the performance counter, `KeTickCount`, system and interrupt time | host clock plus what was skipped |
| kernel timers, `KeDelayExecutionThread` | due on that clock; a sleeping thread wakes when the clock is moved past its time |
| the sound chip's frame clock | on that clock; what it mixes is dropped instead of waiting for a sound device, so films and music keep the title's time |
| the USB bus the pad is on, and the script's button holds | on that clock: a press of 200 ms is 12 frames at any speed |
| the script's own waits | flips and files the title opens, never time (a step that waits 150 s of real time for nothing has lost its way) |
| a wait's time-out on a kernel object | on that clock, looked at every millisecond |
| the host's own threads (file reads, the pre-warm's compiles, the runtime's housekeeping) | real time |

What stays real is work: the title's own code, the draws, reading and
unpacking files, the first compile of each shader. So a scene takes as long
as the machine needs for its 7000 frames.

It is bounded at 8 times real time. With no bound the logos pass at 2500
frames a second and the menus at 500 on a Mac M5, and whatever the host does
beside the title on its own clock is then that many frames late. One scene
of five played with no bound, on a busy machine and before the USB bus was
put on the title's clock, raced without opponents (49 pipelines where the
others have 59 to 67): a press had not reached a menu. At 8 every wait of
the script lasts at least an eighth of what it does at real time. The bound
costs a few seconds on the Mac (a scene alone: race 29 to 33 s against 23 to
25, crash 36 to 38 s against 31 to 34) and nothing on a machine that cannot
play the scenes 8 times faster anyway.

A machine too slow to play the scenes at 60 frames a second in real time
(the Mac held to its efficiency cores, `taskpolicy -b`: 190 s for the two
scenes) gains little and plays them as it would in real time: frames that
take longer than their 16.7 ms are late in either.

No setting, option or environment variable of the game turns this on: only
a scene process of the pre-build does (`--build-shaders-scene`, which needs
the pre-build's own environment to start at all).

Before the first scene the game's executable is read for everything it can ask for,
whether or not a scene shows it, and the keys go into the list: its vertex
programs (28 blobs of the XDK's compiled-shader format, the microcode as it is
uploaded) and its pixel-shader definitions (34 `D3DPIXELSHADERDEF` blocks; each
gives one combiner state per way its texture stages can be bound, 318 in all,
most of which the game never uses). So a material of a track the scenes do not
visit has its two shaders built already, and only its pipeline (the pair plus
blend state) is left for the first draw.

### Output

stdout carries only these lines (the installer's protocol, one step named
`shaders`); the game's own log goes to `<cache>/prebuild/<scene>.log`, which is
deleted when the scene succeeds.

```
PROTOCOL 1
PLAN shaders
STEP shaders BEGIN Building shaders: the game plays 1 scene by itself, without a window or sound (about 1 minute)
LOG shaders cache <dir>
PROGRESS shaders 0.000 race
LOG shaders 28 vertex programs and 34 pixel shader definitions found in the game's executable
PROGRESS shaders 0.036 starting the game
PROGRESS shaders 0.063 title screen
PROGRESS shaders 0.107 main menu
PROGRESS shaders 0.132 garage
PROGRESS shaders 0.201 race: track loaded
PROGRESS shaders 0.235 race: driving
PROGRESS shaders 0.327 race: boost and a crash
PROGRESS shaders 0.448 race: done
LOG shaders race: done; the list holds 29 vertex, 327 fragment, 59 pipelines (70 compiled in this scene)
PROGRESS shaders 0.448 crash
...
PROGRESS shaders 1.000 done
STEP shaders OK 29 vertex, 329 fragment, 68 pipelines; 526887 bytes in <dir>; 57 s
RESULT OK <dir>
```

or `STEP shaders SKIP already built (...)` + `RESULT OK <dir>`, or

```
STEP shaders FAIL <code> <text>
RESULT FAIL shaders <code>
```

| code | exit status | |
|---|---|---|
| `cache-off` | 1 | `render.shader_cache` is `none`, or the directory cannot be made |
| `no-game-data` | 1 | `Burnout 3 Takedown/default.xbe` is not in the working directory |
| `scene-failed` | 1 | a scene lost its way twice (a wait of 150 s of real time ran out) or its process ended; its log is kept |
| `cancelled` | 1 | stopped by the cancel file |

A bad option is exit status 2 with the usage on stderr.

### Stopping and going on

Kill the process, or create `<cache>/prebuild/cancel`: the scene sees the file
within a second and ends, the build prints `cancelled`. A scene process whose
build has gone ends by itself within a second (its next progress line cannot
be written). Whatever was compiled up to then is in the lists, which are
appended a record at a time. A later run skips the scenes that finished and
plays the rest.

## In the game

- Settings overlay, Graphics page: a status line and *Build shaders now*
  (*Stop building shaders* while it runs, *Build shaders again* after). The
  game starts the same command as a child and shows its progress. The game is
  paused while the overlay is open; closing the overlay stops the build, and
  the next one goes on with the scenes that are left. When it ends the backend
  reads the key list again and builds what is new on its pre-warm thread
  ("loading them into the running game", 16 to 33 s on the Mac, where Metal
  compiles again in this process); *built* is shown when that is done, and no
  restart is needed.
- First start: the game opens no menu and asks nothing about shaders; the
  offer is the setup window's ticked box. (`prebuild/state.txt` may hold an
  `offer never` line for a question the game no longer asks; it is ignored.)
- `BO3_SHADERBUILD_TEST=start[:<options>]` does what the action does without a
  window (a headless test of that path).

## What it costs, what is left

Measured 2026-10-11, each from an empty cache (on the Deck an empty
`XDG_CACHE_HOME`, so Mesa's cache too; on the Mac a fresh bundle id, so an
empty system Metal cache; on Windows an empty `--cache-dir`).

| | Steam Deck (Vulkan, RADV) | Mac M5 (Metal) | Windows (Vulkan, RX 9060 XT) |
|---|---|---|---|
| the pre-build, scenes at real time (`--speed 1`) | 261 s | 265 to 273 s | 263 s |
| the pre-build, scenes at up to 8 times real time (the default) | not measured yet | 57 to 65 s (78 s beside another agent's game) | not measured yet |
| on disk | 7.5 MB (SPIR-V 6.8, key list 0.5, driver cache 0.15) + 2 MB in Mesa's own cache | key list 0.5 MB + 3.7 MB in the app's Metal cache | 7.3 MB (SPIR-V 6.8, key list 0.5) |
| key list | 29 vertex, 329 fragment (34 drawn, the rest from the executable), about 70 pipelines | the same | the same |
| start-up pre-warm of that list | 45 ms, done 0.6 s after launch (windowed) | 36 to 77 ms, done 0.2 s after launch | not measured |

A new profile's first race (`tools/headless/to_first_race.sh`: Silver Lake, the
real training film, 40 s of driving), in a window with vsync, host
present-to-present (`tools/perf/present_stats.py`). The Deck on its Game Mode
screen, the Mac on a quiet machine:

| | Deck, no cache | Deck, pre-built | Mac, no cache | Mac, pre-built |
|---|---|---|---|---|
| render-thread compiles | 126, 1.19 s | 1 pipeline, 18 ms | 120, 2.51 s | 4 pipelines, 42 ms |
| car select, 200 frames: p99 / max | 26.6 / 365 ms (then 243 ms) | 24.5 / 47 ms | 16.9 / 370 ms | 16.9 / 17.0 ms |
| race, intro to end of driving: p50 / p95 / p99 / max | 16.7 / 33.4 / 33.7 / 552 ms | 16.7 / 33.4 / 33.6 / 193 ms | 16.7 / 33.4 / 33.6 / 711 ms | 16.7 / 33.4 / 33.7 / 107 ms |
| race: frames over 20 ms / over 33 ms | 434 / 330 | 408 / 322 | 412 / 401 | 383 / 339 |
| race: frames over 100 ms | 2 (552, 125) | 1 (193) | not counted | not counted |

The over-33 ms counts hardly move because most of them are the title's own
30 fps passages (the intro camera) and the hitch at the race start that is
there without any compile (193 ms on the Deck, 107 ms on the Mac); what the
pre-build takes away is the long stalls: 365 and 243 ms at car select, 552 ms
at the race start and 125 ms in the race on the Deck.

Other checks after a pre-build, headless, render-thread compiles:

| | Steam Deck | Mac M5 | Windows |
|---|---|---|---|
| reference race on a track the pre-build does not visit, 40 s | 2 pipelines, 26 and 22 ms | 2 pipelines, 72 and 23 ms | |
| reference crash event to Crash Results | 0 | 0 | |
| the pre-build's race scene played again | | | 2 pipelines |
| a new profile's first race (`to_first_race.sh`, 40 s), cache from a real-time build | | 1 pipeline, 0.1 ms | |
| the same, cache from a fast build | | 1 pipeline, 0.1 ms | |

A key list whose compiled shaders the system no longer has (after an OS or
driver update): the first start builds the list in the background, and the
render thread waits for the pipelines it needs first, at most 3 s in all,
before the title's first pictures; the window shows "Preparing shaders"
meanwhile. On the Mac that wait was 0.4 s on a quiet machine (2.4 s on a
loaded one); nothing is compiled on the render thread, and no such wait can
come later (it ends with the start-up pre-warm's pipelines or 3 s after the
backend came up, and a re-read after a pre-build is never waited for).

What can still compile at first use after a pre-build:

- pipelines the scenes did not meet: a few in a race from things the short
  scene did not see (1 to 4 in the runs above), and those of materials on
  tracks a new profile cannot reach (two in 40 s on another US track). Their
  shaders are built (from the executable); a pipeline, the pair with its
  blend state, is not known until the title draws with it. 18 to 28 ms each
  on the Deck, up to 72 ms on the Mac;
- anything in modes not played (Road Rage, Time Attack, split screen, replays,
  the end of a race);
- combiner states the title sets through the fixed-function texture stages
  and that no scene showed (they are not in a shader definition): one in the
  list of a longer play session.

Each compiles once and is in the list from then on.

## Where a scene's time goes

The race scene on the Mac M5, per part, from `RECOMP_TRACE=frametime` (the
crash scene is alike). At real time the renderer's thread spends 86% of the
scene waiting for the vertical blank; the title's own thread is busy for
about 20 s of the 120, the draws for 14.

| part | flips | real time: s | of which waiting for the blank | now: s | frames a second now |
|---|---|---|---|---|---|
| start, the two logo films | 1050 | 17.6 | 17.2 | 2.7 | 395 |
| intro film (skipped), title film | 830 to 930 | 13.9 | 13.5 | 2.5 | 380 |
| profile screens to the main menu | 1180 | 19.8 | 18.9 | 3.0 | 400 |
| menus to the garage | 400 | 6.7 | 6.4 | 1.1 | 355 |
| garage, the training film, the load | 1120 to 1190 | 18.7 | 16.0 | 4.2 | 280 |
| the load's end, the intro camera | 420 | 7.1 | 5.5 | 2.2 | 195 |
| driving | 910 | 15.2 | 9.6 | 6.6 | 140 |
| boost, the crash, the pause menu | 970 | 20.2 | 15.0 | 5.8 | 165 |
| the scene | 6950 to 7130 | 119.8 | 102.5 | 28.3 | 250 |

The loads are counted in flips like everything else: the title reads and
unpacks a piece a frame while it draws its loading screen, so a load takes
the same number of frames at any speed (695 flips from the garage's last
press to the track's first file in a real-time run and in a fast one) and a
sixth of the time. Up to the garage the bound of 8 is what holds the scene
(2.1 ms a frame); from the load on it is the work: the title's thread and
the draws.

Processor time for the two scenes: 297 s at real time (1.1 cores for 265 s:
the title's thread spins while it waits for the renderer), 87 to 128 s now
(1.5 cores for a minute).

## The same keys

The lists two builds leave are compared with `tools/headless/pcache_keys.py`
(the keys' kinds and their 64-bit checks; the keys themselves are the
title's data and are not printed). Two runs of a scene do not leave the same
list at any speed: the race is not the same race twice (traffic, where the
car ends up), and its list has had 59 to 67 pipelines in nine real-time
runs and 56 to 67 in fast ones. What is compared is the list after both
scenes, on the Mac M5 (Metal):

| | builds | list after both scenes |
|---|---|---|
| real time | 4 | 29 vertex, 329 fragment, 71 pipelines in three; 70 in one (one pipeline the others have) |
| fast (8), as merged | 5 (one with no bound) | the same 71 in all five |
| fast, earlier states of this work | 8 | the 71 in four, 72 in one (one pipeline no real-time build has), 68 in three |
| fast, held to the efficiency cores | 3 | 71, 70, 69 |

Every vertex and fragment key is in every list. Of the three lists of 68,
one is the race without opponents (see *How fast*: three pipelines of the
other cars are missing). The other two, and the 69, lack the same two
pipelines (checks `b95146d549405261` and `d5d58a90a5c4b369`), which belong
to something a race shows either at its start or only when the car hits
something later (their place in a list says which; both happen at real time
too): in those three builds it never came. No real-time run of the nine
race scenes lacked them, so a fast build may be a little likelier to. They
are then compiled once, the first time a race shows them.

## How it is made

- The scenes are tables in `src/game/shaderbuild.c`, played by the toolkit's
  in-process driver (`xbox_autoplay.h`): waits on the files the title opens
  and on flips, presses through the `RECOMP_PAD_LIVE` file, as
  `tools/headless` does from outside. A file is named by the part of its
  path that says what happened, not by the one file a run happened to open:
  the crash scene's impact is "a crash sound bank of the junction, opened
  after the accelerator went down" (`c3_v1\crash`: `crash1.rws` on macOS and
  Linux, `crash3.rws` was seen on Windows, where the scene then waited 150 s
  for the other and failed), and it goes on after 1500 flips if none is
  opened. The driver's test is the toolkit's `tests/autoplay`.
- The scenes' pace is the toolkit's time warp (`src/platform/host_time.h`,
  `xbox_TimeWarpEnable`), switched on by a scene process and by nothing
  else. Dolphin and other emulators run unthrottled too. Here the clock is
  never scaled: it is moved a frame at a time to where the title waits for
  it. No frame is ever late to the title, so it never takes two steps in one.
- Borrowed: Dolphin's per-game pipeline UID cache compiled at start-up (the
  key list and its pre-warm, since BO-58) and its choice of UIDs over driver
  binaries on Metal; Xerenge's install-time step as the place to do it, with
  the installer's progress protocol; UnleashedRecomp's idea of compiling a
  known pipeline list while the logos play. No code from any of them.
- Not done: Dolphin's ubershaders (one generic shader while the real one
  compiles in the background) would remove the remaining first-use hitches at
  the cost of a second, slower shader path in each backend.
