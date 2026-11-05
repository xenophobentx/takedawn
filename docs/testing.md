# Testing

How the port is tested, and the tools that make it quick. Most of it needs no
hands: the game can drive itself, a script can press the menus, and the
settings menu can be drawn without a window. You need your own disc and a
build ([building.md](building.md)); [running.md](running.md) has the details
of the variables used here.

Two repositories are involved. This one holds the game's scripts and a few
tests. The toolkit (`XBOXRECOMP_DIR`, the runtime and the recompiler) holds
most of the unit tests. Paths below say which.

## The golden rule: a scratch profile

A test must never touch your real saves, settings or shader cache. Every
script in `tools/headless/` already sets `RECOMP_SAVE_DIR` to the checkout's
`saves/`. When you start the game by hand for a test, do the same.

| Platform | What to point away from your profile |
|---|---|
| any | `RECOMP_SAVE_DIR=<scratch>` moves the save root (profile, partitions, logs, backups, and the settings file that lives there). `RECOMP_SETTINGS=none` reads and writes no settings file (a path uses that file). `RECOMP_RENDER_SHADER_CACHE=<dir>` moves the shader cache |
| macOS | `HOME=<scratch>` covers `~/Library/Application Support/Takedawn` and `~/Library/Caches/Takedawn` |
| Linux, Steam Deck | `HOME`, `XDG_DATA_HOME`, `XDG_CONFIG_HOME`, `XDG_CACHE_HOME` in a scratch folder |
| Windows | `APPDATA` and `LOCALAPPDATA` in a scratch folder |
| Android | `tools/android/scene-launch.sh` gives the app a save root and a settings file of its own, see [Android](#android) |

A run on a scratch profile looks like this:

```sh
S=$(mktemp -d)
HOME="$S" RECOMP_SAVE_DIR="$S/saves" RECOMP_SETTINGS=none ./bin/burnout3
```

The scene scripts need a starting save. `SAVES=<dir>` names a folder with
`UDATA/` and `TDATA/` holding one profile, `Profile 1`; the script copies it
into the run before every start, so the run never changes your copy. The
default is a folder in the maintainer's checkout, so set `SAVES` yourself. To
make one, play once with `RECOMP_SAVE_DIR` on a scratch folder, make a
profile, finish an event (the game autosaves after each one) and keep that
folder. `SAVES=empty` starts with no profile at all, which is what
`to_first_race.sh` uses.

Only one game at a time per save folder: a second copy is refused.

## Unit tests

Pure code is tested without a game: no disc, no window, no device.

In this repository the tests are built from your game build folder, next to
the game:

```sh
cmake --build build/mac --target bo3_controls_test bo3_mixer_test bo3_gp_test bo3_fatal_test
ctest --test-dir build/mac/tests/mixer          # one folder per test: controls, mixer, gp, fatal
python3 -m unittest discover -s tools/install/tests    # the installer driver
```

| Test | Covers |
|---|---|
| `tests/controls` | the PS2 driving layout and its hand-over to the menus, with the toolkit's host pad map |
| `tests/mixer` | which sound goes to which mixer bus |
| `tests/gp` | the GP effects image as the APU model runs it |
| `tests/fatal` | when a start-up failure may show a message box (never in an unattended run) |
| `tools/install/tests` | disc images, the supported-build check, install locations, packaging helpers, the shader step |

Most of the toolkit's tests are standalone projects and build on their own:

```sh
cd "$XBOXRECOMP_DIR"
cmake -S tests/kernel_saveguard -B build/kernel_saveguard
cmake --build build/kernel_saveguard && ctest --test-dir build/kernel_saveguard
```

A few are only built from a parent project (the game's build folder includes
the toolkit): `input_map`, `pad_script`, `input_rumble` and the
`input_keyboard` pair. Build their targets there and run `ctest --test-dir`
on the folder under `build/mac/xboxrecomp/src/`.

| Area | Tests |
|---|---|
| Saves | `kernel_saveguard` (a process ended at each step of a save leaves the old save or the new one, whole), `save_io` (export and import), `kernel_fatx_overwrite`, `kernel_directory` |
| Overlay and settings | `overlay_settings` (settings.toml and its precedence), `overlay_update`, `enhance_cfg`, `render_scale` |
| Input | `input_map` (host pad to what the title reads, deadzones, connection cache), `input_rumble`, `input_keyboard`, `pad_script` (a controller takes the pad back from a script), `pad_glyphs` |
| Bug report | `bug_report` (the archive opens, and a planted home path, user name and host name are in none of its files) |
| Sound | `soundtrack`, `audio_mixer`, the `apu_*` tests, `wma_decoder`, `adpcm_decode` |
| Presentation | `present_cadence`, `present_layout`, `vblank_schedule`, `window_title` |
| Graphics | `nv2a_*`, `d3d8_*_split`, `d3d8_smoke`, `nv2a_backend_smoke` |
| Kernel and CPU | `kernel_*`, `x87_trig`, `mmx_*`, `fp_precision`, `memory_layout_posix` |

`ls tests/` in the toolkit is the full list; the first comment of each
`CMakeLists.txt` says what the test checks and how to run it.

## The built-in AI driver

`RECOMP_DEBUG=autopilot=1` lets the title's own AI drive player 1 while a
race is in play. No input is needed, and the idle pause that would stop an
untouched race on the third lap is held off. Races only: Crash mode is not
driven. The AI is slow (it finished Silver Lake, 3 laps, 6th of 6), which is
fine for a soak. At the finish the log has
`[AUTOPILOT] result place P/N time T s ...`.

`tools/headless/race_drive.sh <rundir>` runs a whole race with it: main menu,
Single Event, Race, USA, Silver Lake Southbound, the first car, the race, the
results. It writes `<rundir>/result.txt`, one line per race, with the place
and the race time.

```sh
SAVES=~/bo3-ref-saves tools/headless/race_drive.sh runs/ap1
RACES=20 RACE_STOP=runs/ap1/stop SAVES=~/bo3-ref-saves tools/headless/race_drive.sh runs/soak
touch runs/soak/stop        # ends the soak after the race in progress
```

| Variable | Does |
|---|---|
| `RACES=n` | races the same race `n` times: Continue on the results, Retry, again. A soak for leaks and crashes |
| `RACE_STOP=<file>` | with `RACES`, stops after the race in which that file appears |
| `RACE_LIMIT=s` | the longest one race may take, GO to results (default 900; 3 laps took 395 s) |
| `CAPTURE=1` | takes the `race_results` shots |
| `STOP_AT_END=1` | stops the game when the script is done |

Anything else `scene_lib.sh` reads works too (below). It also runs on
Android through `tools/android/scene-launch.sh`.

## Unlock everything for a test

Cars and events are locked behind progress. To test a late car without
earning it:

- `RECOMP_DEBUG=cheats` shows the cheat rows on the overlay's System page
  (players never see them). Unlock everything asks twice. From the first
  cheat on, the title's saves go to a copy, `<save root>/cheat-session`. When
  the last cheat goes off, the page asks whether to keep that progress (the
  real save goes into the save history first) or throw it away.
- `BO3_UNLOCK_ALL=1` switches the same unlock on from the start, for a
  scripted run. It has no row and does not use the side copy: whatever the
  title saves in that run goes to the save root the run uses. Give such a run
  a scratch save root, never your own.

`tools/headless/unlock_all_scene.sh <rundir>` checks the first path: the real
save stays byte for byte as it was while the copy changes, and `KEEP=1`
checks the keep branch.

## Headless scenes and reference shots

`tools/headless/` has one driver per scene. Each starts the game headless,
presses buttons through `RECOMP_PAD_LIVE`, and waits on the game's own log
(flip counts, files the title opens), never on the clock, so a scene waits
the same number of frames on a fast Mac and a slow CPU rasteriser. A failed
wait ends with `FAIL: ...` in `<rundir>/scene.txt`.

| Script | Scene |
|---|---|
| `to_intro.sh`, `to_menu.sh` | the intro movies; the attract loop to the main menu |
| `to_race.sh`, `to_race_busy.sh` | Single Event, Race, Silver Lake Southbound, then driving |
| `to_first_race.sh` | what a new player gets: no saves, New Profile, the training film, a race |
| `to_crash.sh`, `to_garage.sh` | Crash mode to its results; the car select |
| `race_drive.sh` | an AI-driven race or soak, see above |
| `controls_scene.sh`, `controls_all.sh`, `controls_split_scene.sh`, `reverse_scene.sh` | PASS/FAIL checks: the PS2 layout only while driving (also for both players in split screen), the car reverses on the left trigger |
| `split_scene.sh`, `crash60_scene.sh`, `draw_scene.sh`, `fx_scene.sh`, `hud_scene.sh` | two-player split screen, the 60 fps crash camera, vehicle detail, bloom and motion blur switches, the widescreen HUD |
| `menu_entry_scene.sh`, `pause_entry_scene.sh`, `unlock_all_scene.sh` | the Takedawn settings entry in the title's menus; the cheats |
| `custom_music_scene.sh`, `custom_level_scene.sh` | a custom soundtrack, and its loudness from a WAV of the output |

```sh
tools/headless/to_menu.sh runs/m1                        # leaves the game running, pid in runs/m1/pid
STOP_AT_END=1 tools/headless/to_race.sh runs/r1 RECOMP_TRACE=pacing
CAPTURE=1 STOP_AT_END=1 SAVES=~/bo3-ref-saves tools/headless/to_garage.sh runs/g1
RECOMP_PB_BACKEND=cpu tools/headless/to_menu.sh runs/m-cpu
WINDOW=1 tools/headless/to_race.sh runs/r-win            # in a window, for on-screen checks
```

Extra `KEY=VALUE` arguments go to the game's environment. The header of
`tools/headless/scene_lib.sh` lists the rest: `BIN` (another executable, an
ASan build, say), `LAUNCH` (a whole launch command, for Proton or Android),
`SCENE_TIMEOUT`, `SCENE_FLIPS`. Write a new scene by sourcing `scene_lib.sh`
and reading an existing one; `press`, `wait_log` and `shot` are the pieces.

For shots, `CAPTURE=1` dumps frames at chosen flips. In a window,
`RECOMP_WINDOW_SHOT=<flip>[,<flip>...]` with `RECOMP_WINDOW_SHOT_PREFIX=<path>`
writes the presented frame (the overlay included) as a BMP.
`RECOMP_FB_DUMP=<prefix>` with `RECOMP_FB_DUMP_FLIPS=1` dumps every flip at the
game's own 640x480, for a sequence. `tools/headless/golden.py` compares a
scene's shots with reference frames you make yourself (`capture`, `update`,
`check`, `determinism`). The references are game pictures, so they live
outside git (`GOLDEN_DIR`) and are never committed.

The scenes that test one setting leave a table behind and have a
reader for it: `pace_report.py` (pacing per game state), `draw_report.py`,
`fx_report.py`, `hud_extents.py`, `level_report.py`, `voices_report.py`.

The online scenes run several copies on one machine. `tools/headless/td6_vote.sh
<rundir> <scene>` plays the lobby's track vote on LAN with two or three copies
(`two`, `three`, `tie`, `leave`, `hostgone`, `hostkill`, `late`, `cycles`, `long`).
Frames land in each copy's `burst-vote` folder and the `[VOTE]` log lines are
printed at the end. `TAKEDAWN_VOTE_TRACKS=a,b,c` fixes the rows and
`TAKEDAWN_VOTE_TEST=cycles` repeats votes in one lobby without racing.

## The settings menu, off screen

On a Mac the toolkit builds `overlay_shot`: the overlay drawn with Metal into
a texture and written as BMPs, with synthetic key and controller events. No
window opens and no game runs, so it is a quick look at every page after a
change to the menu, and it checks the pause and input routing on the way.

```sh
cmake --build build/mac --target overlay_shot
mkdir -p runs/ov
overlay_shot runs/ov/                                            # every page: page0.bmp, display.bmp, ...
OVERLAY_SHOT_MIXER=runs/ov/preset.toml overlay_shot runs/ov/mixer_   # the Mixer page
OVERLAY_SHOT_SAVEIO=runs/ov/work overlay_shot runs/ov/save_      # export, import, restore
```

The first argument is a file name prefix. The binary lands under the build
folder's `xboxrecomp/src/overlay/`. The tool sets `RECOMP_SETTINGS=none` and
makes a temporary save root of its own. (`OVERLAY_SHOT_UPDATE=<owner/repo>`
tours Check for updates and sends one real request to GitHub.)

For the overlay inside a running game, `RECOMP_OVERLAY_TEST=<file>` takes
lines appended to the file (`key down`, `pad a`, `state <file>`...), which is
how `unlock_all_scene.sh` and the menu-entry scenes press the settings menu.
`RECOMP_TRACE=help` and `RECOMP_DEBUG=help` print every trace and debug
switch.

## Frame times and pacing

Pacing, presentation and input changes need numbers before and after, from
the same scene (CONTRIBUTING.md asks for them).

```sh
RECOMP_TRACE=pacing=all,frametime=runs/x/ft.csv RECOMP_STDIO_LOG=runs/x/log.txt ./bin/burnout3
python3 tools/perf/framestats.py runs/x/ft.csv                 # one run
python3 tools/perf/framestats.py before.csv after.csv          # side by side
python3 tools/win/pacing_stats.py runs/x/log.txt runs/x/ft.csv # per game state
python3 tools/deck/stats.py runs/x/log.txt 30                  # last 30 s, flips/s, misses
```

| Trace | Writes |
|---|---|
| `pacing` | a `[PACING]` summary every 600 flips; `pacing=all` also every flip |
| `frametime=<file>` | a CSV line per flip: where the frame's time went (executor, guest code, waits) |
| `glass=<file>` | a CSV line per presented frame with the time it reached the glass (Metal, in a visible window); `tools/perf/glass.py` |
| `latency=<file>` | input-to-photon times; `tools/perf/latency_run.sh <rundir>` drives a still pause menu for it, `tools/perf/latency.py` reads it |
| `vk_present=<file>` | present times on Vulkan; `tools/perf/present_stats.py` |
| `pcache=<file>` | shader and pipeline compiles per flip; `tools/headless/pcache_stats.py` |

The numbers to read: the flip interval p50, p95 and p99, and the share of frames
over 20 ms. At 60 Hz a steady run has p50 near 16.7 ms and p99 within a
millisecond or two of it (the goals are p95 under 17.5 ms and p99 under
18.5 ms). The title's simulation should step 60.0 times a second whatever the
display does; `steps/s` in `pace_report.py` shows it, and the overlay's
graph can mark frames in which the title ran more than one step
(`advanced.perf_steps`). A window that is
covered reports no on-glass times, so keep a measured run visible
(`RECOMP_DEBUG=window_float=1` keeps it on top).

The overlay shows the same live: the performance overlay on the Display page
(`RECOMP_OVERLAY_PERF=1`) draws the frame-time graph with p50, p99 and the
on-glass line.

On a Steam Deck, `tools/perf/deck_power.sh <outdir> <seconds> <tag>` samples
APU power, clocks and the game's CPU use once a second beside a pacing run.
`tools/deck/` and `tools/headless/deck_env.sh` start the scenes under Proton
on the Deck; `deck_sync.sh` copies the scripts and the executable there.

## Package and release checks

Test the package a player gets. `tools/release/release.sh`
builds the packages ([release.md](release.md)) and smoke-runs them.

| Script | Checks |
|---|---|
| `tools/release/smoke-macos.sh [<dmg>]` | the mounted disk image, with `HOME` in a scratch folder: `--version`, the setup window draws, no data gives exit status 3, `--install` from `TAKEDAWN_DISC`, a headless start to the main menu |
| `tools/release/smoke-linux.sh [<AppImage>]` | the same on a Linux machine over ssh, in a scratch `HOME`; the checks are in `tools/release/linux/smoke-remote.sh` |
| `tools/release/update-test/update-test.sh --old <app> --new <app> --disc <disc> --saves <dir> --work <scratch>` | an update, old to new to old again: the player's saves, settings and shader cache hash the same at every step, a second copy is refused, the old version still boots |
| `tools/release/test-user-dirs.sh`, `test-arg-order.sh` | the old folder names are moved only by a real start or install; `--data` means the same before and after the word it goes with |

A release build logs only errors and notices. The smoke scripts switch the
full log on (`RECOMP_LOG_DIAG=1`) because their checks read it.

One manual run stays necessary, because no script replaces the setup window
used by someone who has never seen it. The install, by hand:

1. Use a clean profile (a scratch `HOME`, or `TAKEDAWN_DATA=<empty folder>`
   and a scratch `RECOMP_SAVE_DIR`) and start the program with no game data.
   The setup window opens.
2. Drop your own disc image or an extracted disc folder on it. The disc card
   says whether it is accepted.
3. Install, stop it half way and resume, then let it finish. Leave "Prepare
   graphics" ticked.
4. Play. Check the first start, a race, quitting from the menu, and a second
   start.

## Android

The scene scripts run on a device through `tools/android/scene-launch.sh`, as
the `LAUNCH` command. The script runs on the PC that has adb, the game runs
on the device, and the device's log is followed back into the run's
`log.txt`.

```sh
ADB="adb -s <serial>" LAUNCH="$PWD/tools/android/scene-launch.sh" \
GAME_DIR=/tmp/bo3-scene SAVES=none \
  tools/headless/to_first_race.sh /tmp/bo3-scene/r1
```

The app gets its own save root and settings file (`ANDROID_SAVE_DIR`,
`ANDROID_SETTINGS`, default `scene-saves` and `scene-settings.toml` in the
app's files folder); the script refuses to run on the player's own.
`ANDROID_SAVES_FROM=<host dir>` pushes a profile to it before every start.
Environment variables for the game go in as arguments after the run folder,
as on the desktop; `RECOMP_TRACE=pacing=all,frametime=<device path>` writes
its CSV on the device, where `adb pull` fetches it.

Adb over Wi-Fi is quicker for the 2.4 GB of game data and for scenes, and
frees the USB port. On the device, switch on Wireless debugging, then:

```sh
adb pair <ip>:<pairing port>        # asks for the pairing code shown on the device
adb connect <ip>:<port>
adb devices                         # use the listed name as <serial>
adb logcat -s takedawn
```

The build and install steps are in [android.md](android.md).

## Bug reports and logging

When a test finds a bug, attach the game's own report.
System > Create bug report writes a zip (with a screenshot if you pick
that row) to `reports/` in the save folder. It holds the system, GPU and
driver, the settings in effect and where each came from, the frame-time
summary, the last logs and crash reports, and it replaces your home folder
and user name with `~` and `<user>`. Without a window, or when the game does
not start, `burnout3 --bug-report` (`takedawn --bug-report` in a release)
writes the same file and prints its path; `RECOMP_DEBUG=bug_report=30` writes
one 30 seconds into an unattended run. What is in it and what never is:
[running.md](running.md#reporting-a-bug).

A release build is quiet. System > Diagnostic logging turns the full
log on, live; do that before reproducing, so the report carries it. A build
from source, and any run with `RECOMP_TRACE`, `RECOMP_DEBUG` or `--headless`,
logs fully already. A crash or hang report is written on its own to
`crash/<date>-<kind>/` (`RECOMP_DEBUG=crash_test=segv:10` makes one to see
it).
