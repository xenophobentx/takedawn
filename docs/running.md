# Running, recording and driving it headless

## The executable

```
bin\burnout3.exe [--headless] [--record out.mp4] [--seconds N] [path\to\default.xbe]
```

Run it from the repo folder. It reads the disc from `Burnout 3 Takedown\` and
keeps the emulated hard disk (profile, cache partitions) in the save folder,
see [Saves, backups and crash reports](#saves-backups-and-crash-reports).
Delete its `UDATA` to go through first boot again.

- Windowed by default. The title bar reads
  `Burnout 3: Takedown | FPS: 14 | draws: 212`. The title comes from the XBE
  certificate; draws are the draw calls in the last frame.
- `--build-shaders` compiles the shaders ahead of time by playing two
  scenes headless; optional, see [shader-prebuild.md](shader-prebuild.md).
- `--bug-report [--out <dir>]` writes a bug report archive and exits,
  without starting the game; see [Reporting a bug](#reporting-a-bug).
- `--headless` opens no window. Use it for scripts, CI, and over RDP, where a
  window would land on whatever screen the remote session is showing.
- `--record out.mp4` writes every presented frame as a BMP into
  `%TEMP%\bo3rec_<pid>\`. When the run ends, after `--seconds N` or on Ctrl+C,
  it encodes them with `ffmpeg` at 30 fps. If ffmpeg is missing or fails, the
  frames stay where they are and the path is printed. The frames go to
  disk because at a few fps streaming gains nothing, and an encoder that
  fails cannot lose the run.

The fps is real frames per wall-clock second. The title steps its simulation a
fixed amount per frame, so at 3 fps a race plays in slow motion but correctly,
and a recording played back at 30 fps looks close to full speed.

## Quitting

The main menu has a QUIT line below Driver Details (BO-301,
`src/game/mainquit.c`). It closes the game the way the window's close button
does: the host's quit request, then `SDL_QUIT`, so the saves in flight finish,
the pads stop and the GPU and the audio device are taken down in order. The
log has `[MENU] main menu: QUIT listed` when the menu was built and
`[MENU] main menu: QUIT chosen` then `[QUIT] asked for` when it was used.
`TAKEDAWN_QUIT_MENU=0` leaves the menu as the title has it, and a headless run
has no window to close and does not list it (`TAKEDAWN_QUIT_MENU=1` lists it
there too, for the scene). Settings > Quit game (on every page)
and Alt+F4 are still there. `tools/headless/mainquit_scene.sh` (WINDOW=1)
drives it.

## Saves, backups and crash reports

The save folder is per user:

| Host | Save folder |
|---|---|
| macOS | `~/Library/Application Support/Takedawn` |
| Linux, Steam Deck | `$XDG_DATA_HOME/takedawn`, else `~/.local/share/takedawn` |
| Windows | `%APPDATA%\Takedawn` |
| Android | `takedawn/` in the app's folder, `Android/data/io.github.xenophobentx.takedawn/files` |

Portable mode (Windows and Linux, BO-263): an empty `portable.txt`, or a
`data` folder, beside `Takedawn.exe` (on Linux beside the `.AppImage` file,
from `$APPIMAGE`; in a build run from a checkout, beside the executable) puts
the saves in `data/Saves`, the game data in `data/Game` and the shader cache
in `data/cache`. Without the marker the folders above are used. The log has a
`[SAVE] portable mode` line. `RECOMP_SAVE_DIR` still wins for the saves.

A folder of the port's earlier name (`Burnout3`, `burnout3`) is moved to the
new name once, at the first start ([installer.md](installer.md)).

`RECOMP_SAVE_DIR=<folder>` uses another one; the headless scripts in
`tools/headless/` set it to the checkout's `saves/`, so a test run never
touches your profile. The first start of a build with the per-user folder
copies an existing `./saves` there once (the partition images are remade, not
copied) and leaves a `MOVED.txt` in the old folder.

What is in it:

- `UDATA/4541005b/<profile>/` the profile (`Profile 1`, `SaveMeta.xbx`) and
  `TDATA/4541005b/` the title's own data, as plain files.
- `Partition*.img` the HDD partitions the title mounts as devices. They are
  sparse: gigabytes on paper, kilobytes on disk.
- `backup/<YYYYMMDD-HHMMSS>/` a copy of `UDATA` and `TDATA` taken at start,
  before the game can write, whenever they changed since the last backup.
  The newest five are kept (`RECOMP_SAVE_BACKUPS=<n>`, `0` none). To go back,
  quit the game and copy a backup's `UDATA` and `TDATA` over the save
  folder's.
- `logs/` the output of this run and of the three before it: `run.log` (the
  first 64 KB: the start, with the device, the display and the settings) and
  `run.tail.log` / `run.tail.old.log` (what came after, at most 768 KB);
  `prev1.*` to `prev3.*` are the earlier runs. Written as the game runs, so
  they are there after a crash or a kill.
- `reports/` the bug report archives, see [Reporting a bug](#reporting-a-bug).
- `crash/<YYYYMMDD-HHMMSS>-<kind>/` a report per crash (`crash`), guest
  bugcheck (`bugcheck`) or hang (`hang`): `report.txt` (host backtrace, guest
  registers, the guest call stack as `tools/stackwalk.py` prints it, and its
  raw `GS` lines), `log.txt` (the last 500 lines of output), `settings.txt`
  (command line, paths, `enhance.toml`, every `RECOMP_*` value) and, on
  Windows, `minidump.dmp`. A hang is no flip for `RECOMP_HANG_WATCHDOG`
  seconds (default 15, 60 before the first frame, `0` off); the game is left
  running.

Moving a profile between machines (Mac and Deck): quit the game on both,
then copy the `UDATA` folder from one save folder to the other, for example
`scp -r ~/Library/Application\ Support/Takedawn/UDATA <user>@<host>:.local/share/takedawn/`.

`RECOMP_DEBUG=crash_test=segv:10` (or `=hang:10`) faults (or hangs) the
game's main thread ten seconds in, to see a report being made.

## Updating

The game never looks for updates by itself and sends nothing at start. To
check, open the settings (Back+Start on a pad, F1, Cmd+, on a Mac), go to the
System page, under the version line, and choose Check for updates.
That makes one anonymous HTTPS request to GitHub's latest-release record for
this port (`https://api.github.com/repos/xenophobentx/takedawn/releases/latest`,
with a `Takedawn/<version>` User-Agent), off the main thread, giving up after
ten seconds. The row then says one of: you have the latest version; version X
is out, with the first lines of its release notes and Open the download
page (it opens the release page in your browser, nothing is downloaded or
installed); no release is published yet; or why it could not check (offline,
timed out). On macOS and Windows the system's own networking does the
request; on Linux the system's `curl` is started. Android has no such row.

## Reporting a bug

The game can pack what a bug report needs into one zip file. Nothing is sent
anywhere. The file is written to your disk, you can open it and read it, and
you attach it to the issue yourself.

How to make one:

- In the game: open the settings (Back+Start on a pad, F1, Cmd+, on a
  Mac), go to the System page and choose Create bug report, or
  Create bug report with a screenshot to add the picture that is behind
  the menu. The line under the menu then says where the file is.
- When the game does not start, or without starting it:
  `burnout3 --bug-report` (`bin\burnout3.exe --bug-report` on Windows) from
  the game's folder. It prints the file's path. `--out <dir>` writes it
  somewhere else.
- Android: the same entry in the in-game settings; and the page the app
  shows when it cannot start the game (no game data, no usable graphics
  driver) has a Create bug report button (Y on a pad), with
  Save a copy to Downloads and Share on the page after it.

Where it goes: `reports/takedawn-report-<date>-<time>.zip` in the
[save folder](#saves-backups-and-crash-reports), beside `settings.toml`:

| Host | Reports folder |
|---|---|
| macOS | `~/Library/Application Support/Takedawn/reports` |
| Linux, Steam Deck | `~/.local/share/takedawn/reports` |
| Windows | `%APPDATA%\Takedawn\reports` |
| Android | `Android/data/io.github.xenophobentx.takedawn/files/takedawn/reports` on the device's storage (USB file transfer or `adb pull`), and `Download/` after "Save a copy to Downloads" |

What is in it (plain text, compressed: 70 to 130 KB in the runs measured,
about 450 KB more with the screenshot):

| File | Contents |
|---|---|
| `README.txt` | what the archive is, when and from where it was made |
| `system.txt` | OS and version, device model, CPU, memory, GPU and driver and graphics API version, display mode and refresh rate, connected controllers (name, vendor:product), the build (toolkit and game commit, build date), the render scale and present mode in effect, whether the game data is there (file names and sizes of the disc's top folder) |
| `settings.toml` | the settings file as it is |
| `settings-effective.txt` | every setting with its value and where it comes from (settings file, environment, default); the `RECOMP_*` and `BO3_*` variables |
| `stats.txt` | the session's frame times (percentiles, late frames), shader and pipeline compile counts, the shader cache's size |
| `logs/current.txt`, `logs/previous-1..3.txt` | the log of this run and of the three before it: each run's first 64 KB and its last 256 KB (128 KB for the earlier runs). From `--bug-report` the newest is `logs/last-run.txt` |
| `logs/xbox_kernel.log` | the kernel layer's own log of the last start |
| `crash/<report>/` | the five newest crash and hang reports: `report.txt`, `log.txt`, `settings.txt` |
| `frame.bmp` | only when asked for: the last frame the game drew, at the game's own 640x480 |

What is never in it:

- save files and profiles, the disc data, the contents of the shader and
  pipeline caches (their sizes and file counts only), Windows minidumps (the
  README says one exists and how big it is);
- environment variables other than the game's own `RECOMP_*` and `BO3_*`;
- your home folder's path, your user name and the computer's name: in every
  text file the home folder is written `~` (and so is any `/home/<name>`,
  `/Users/<name>` or `C:\Users\<name>`), the user name `<user>`, the host
  name `<host>`. A controller's or a display's name stays as the system
  reports it.

On Android the start page's report is made by `ReportActivity`, in a process
of its own (`:report`): it loads `libmain.so` and runs the game's
`--bug-report` path, so the archive is the same one, with the device, its
screen, its pads and the device driver's Vulkan version added to
`README.txt` from the Java side. "Save a copy to Downloads" goes through
MediaStore and needs no permission; the app has no network permission at
all.

For scripts and machines reached over ssh, where there is no menu to press:
`RECOMP_DEBUG=bug_report=30` writes a report 30 seconds after the game
starts (`=30:shot` with the frame), `RECOMP_DEBUG=bug_report=@<file>` writes
one each time that file appears (put `shot` in it for the frame). The log
line `[REPORT] bug_report: done: <path>` names the archive.

## Pads

Two pads are connected by default. Pad 1 is XInput pad 1, pad 2 is XInput pad 2.
The keyboard also drives pad 1 with a fixed map ([controls.md](controls.md#the-keyboard)).
`RECOMP_KEYBOARD=0` turns it off; on Android it is off unless `RECOMP_KEYBOARD=1`.

The driving controls can be the PS2 version's instead of the Xbox one's
(`input.control_scheme = ps2`, overlay: Input > Driving controls,
`RECOMP_INPUT_CONTROL_SCHEME=ps2`): accelerate on the bottom face button,
brake on the left one, boost on the right shoulder. Only while driving; menus
are not changed, and a pad script keeps the Xbox layout. Both tables and how
it works: [controls.md](controls.md).

A pad script is for test runs, and the game says so: `PAD: SCRIPTED INPUT
drives the pads in this run` in the log and, in a window with the overlay, a
red line at the top of the screen. The script and a controller are not mixed:
the first button or trigger held on a controller takes that pad back from the
script for the rest of the run (`PAD: a controller is used on pad N`), and a
headless scripted run does not read the controllers at all. Otherwise a
script holding the accelerator (`rt:600000`) would stay on top of the player's
pad, and the title, which brakes but never reverses while the accelerator is
held, would look as if reverse gear were broken (BO-96).
`RECOMP_DEBUG=pad_script_keep=1` brings the merge back, `pad_script_note=0`
hides the line. Lines already in a `RECOMP_PAD_LIVE` file that was last
written more than 30 s before the game's first pad read are an earlier run's
and are not pressed.

A pad script drives them without hands:

```
set RECOMP_PAD_SCRIPT=20000:start:1200,30000:a:400,90000:rt:60000,90000:p2-rt:60000
```

Each entry is `<ms>:<button>[+<button>...][:<hold ms>]`. Time counts from the
game's first pad read, and the default hold is 150 ms. `RECOMP_PAD_SCRIPT=@file`
reads the entries from a file.

`RECOMP_PAD_LIVE=pad.txt` is the interactive version. Each line appended to the
file, `<button>[+<button>...][:<hold ms>]` with no time, is pressed as soon as it
is read. With frame dumps on, that makes a loop: look at the latest frame,
append a press, repeat.

| Button names | |
|---|---|
| D-pad | `up down left right` |
| Face | `a b x y black white` |
| Triggers | `lt rt` (full pull); `lt=200` holds an analog button (triggers, face, black, white) at that value of 255 |
| Sticks | `lleft lright lup ldown`, `rleft rright rup rdown` (full deflection) |
| Other | `start back lthumb rthumb` |
| Second pad | prefix with `p2-`, e.g. `p2-a`, `p2-rt:60000` |

Menu timing needs care:

- D-pad presses want about 200 ms. Longer ones auto-repeat once the menus run at
  full speed.
- `a` wants about 400 ms, or 1.5 s while a load is in progress.
- The tutorial video before a mode's first event (Race and Road Rage:
  `TrD_xxx.xmv`, Crash: `TrC_xxx.xmv`) has no skip in the original. It
  plays before a mode's first event; once that event has been played it does
  not come again (seen on the owner's profiles). Here A
  or START (a new press, not a button held from the menu) leaves it at any
  time (the film page's "skippable" byte, `src/game/tutorial.c`).
  `tools/headless/skip_tutorial.sh` measures it: a new profile's first
  Race, Road Rage or Crash, the flips from the film's open to the track's
  load (about 7500 played out, 760 with A pressed 600 flips in).

## Widescreen, 480p and internal resolution

The toolkit's enhancements layer is built in (CMake `BO3_ENHANCE`, on by
default). It reads `bin/enhance.toml` beside the executable; copy
`config/enhance.toml.default` there and edit it. Each key also has an
environment variable that wins over the file.

| Key | Variable | What it does |
|---|---|---|
| `display.widescreen` | `RECOMP_DISPLAY_WIDESCREEN=1` | The Xbox dashboard's 16:9 setting. The game reads it at start-up: 16:9 projection, D3D's widescreen mode. `16:10` (BO-271, overlay Display > Aspect ratio 16:10) is the same mode laid out for a 16:10 picture: the camera keeps 16:9's horizontal view and shows more above and below (vert+), the HUD and menus are placed for 1.6, the window shows 16:10. |
| `display.hdtv` | `RECOMP_DISPLAY_HDTV=480p` | The dashboard's HDTV modes. The game asks D3D for 480p (progressive). It has no 720p or 1080i mode. |
| `present.aspect` | `RECOMP_PRESENT_ASPECT` | The window's frame shape. `auto` (default) is 16:9 with widescreen on (16:10 with `display.widescreen` 16:10), else 4:3; also `frame`, `4:3`, `16:9`, `16:10`. |
| `render.scale` | `RECOMP_RENDER_SCALE=1..4` | Internal resolution: 2 is 1280x960, 3 is 1920x1440, 4 is 2560x1920. Metal and D3D11 backends; the CPU rasteriser ignores it. |

### What a first start looks like

The display settings a profile has not set follow the machine (`main.c`, the
`env_default` block; BO-103). They are defaults: a value in `settings.toml`
wins, a `RECOMP_*` variable wins over that, no settings file is written or
changed, and 4:3, a window and 1x are in the overlay's Display and Graphics
pages.

| Machine | Full screen | Aspect | HUD fix | Fit | Filter | Render scale | Anti-aliasing | Sharpening | Menu/HUD upscaling | Texture sharpness | Vehicle detail |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Steam Deck, and anything under gamescope | on | 16:10 on a Deck or a 16:10 screen (BO-271), else 16:9 | on | letterbox (the whole frame; on the Deck's panel a 16:10 picture fills it; `fill16x10` crops a 16:9 one) | linear | 2 | SMAA | 0.30 | 2x | auto | far |
| macOS, Windows, desktop Linux: the packaged program (`-DBO3_RELEASE=ON`) | on (borderless, the desktop's resolution) | 16:10 when the main display's shape is nearer 16:10 than 16:9 (1.45 to 1.69: a 16:10 monitor, a MacBook), else 16:9 | on | letterbox | linear | 3 | SMAA | 0.30 | 2x | auto | far |
| a development build on a desktop | off | 4:3 | off | letterbox | nearest | 1 | off | 0 | off | off | original |
| Android | on | 16:9 | on | letterbox | linear | 1.5 | FXAA | 0.30 | 2x | off | original |
| any `--headless` run | - | 4:3 | off | - | - | 1 | off | 0 | off | off | original |

These values were decided on 2026-10-12; the overlay's "Y default"
and "Reset this page" go back to the same per-platform values. The Deck's
row with 3x, `fill16x10` and no post passes was measured there in Game Mode
with the Balanced power mode, a 90 s race: flips p50 16.67 / p95 16.78 / p99
16.84 ms, 0.17 % over 20 ms, APU 9.2 W (the same race at 1x: p99 16.70 ms,
7.5 W; at 3x with the nearest filter, 120 s: p99 16.70 ms, 8.6 W). The new
row (2x with SMAA, sharpening and the 2D upscaling) has not been measured on
the Deck yet.

How the game decides, from the disassembly: `XGetVideoFlags` (XAPI,
`sub_001CF913`) returns the `XC_VIDEO` word shifted right by 16, and clears
480p/720p/1080i unless the AV pack is HDTV (`HalBootSMCVideoMode` 1). The
display setup (`sub_0003C8A0`) keeps bit 0 as widescreen; `sub_0003D690`
creates a 640x480 device with `D3DPRESENTFLAG_WIDESCREEN` when it is set and
`D3DPRESENTFLAG_PROGRESSIVE` when bit 3 (480p) is, and the camera
(`sub_0002F380`) uses a 16:9 aspect instead of 4:3. The back buffer is
640x480 in every mode, so 16:9 is anamorphic: the console's TV stretched it,
and here the window does (`present.aspect`). There is no 16:9 or 480p entry
in the game's own Options. 480p changes the mode D3D programs
(`AvSetDisplayMode` mode 0x88110F01 instead of 0x080F0D12), not the frame,
so it looks the same on a Mac. Frame dumps stay 640x480 at every setting.

```
RECOMP_PB_BACKEND=metal RECOMP_DISPLAY_WIDESCREEN=1 RECOMP_DISPLAY_HDTV=480p \
RECOMP_RENDER_SCALE=3 ./bin/burnout3
```

## Environment variables

Defaults that `main.c` sets, which you can override:

| Variable | Default | Why |
|---|---|---|
| `RECOMP_PB_EXEC` | `1` | The software GPU. `0` turns it off: nothing is drawn, but boot runs ~50× faster, which helps when following it by file accesses. |
| `RECOMP_FB_WINDOW` | `1` unless `--headless` | The window. |
| `RECOMP_USB` / `RECOMP_USB_PADS` | `1` / `2` | The USB host and pad count (1 to 4). Split-screen modes are greyed out with one pad. |
| `RECOMP_AC97_READY` | `1` | DirectSound refuses to start until the AC'97 codec reports ready, and RenderWare won't start without audio. |
| `RECOMP_APU_DSP_ACK` | `0x825F8810` | The DSP doorbell DirectSound waits on; [boot.md](boot.md) #5. |

For debugging:

| Variable | Does |
|---|---|
| `RECOMP_FB_DUMP=<prefix>` | Writes the framebuffer to `<prefix>00000.bmp`, ... once per GPU report (`RECOMP_PB_REPORT_MS`, e.g. `5000`). |
| `RECOMP_FB_DUMP_FLIPS=1` | Every flip instead: a consecutive sequence. A file path instead of `1` dumps only while that file exists, so a capture can start mid-game. |
| `RECOMP_PB_REPORT_MS=<ms>` | GPU report interval: fps, flips, draws. |
| `RECOMP_RASTER_THREADS=<n>` | Rasteriser worker threads (default: cores − 4; `1` turns threading off). |
| `RECOMP_WATCHDOG_SECS=<s>` | Hang watchdog. If the title makes no progress for that long, dump the guest call stack (`tools/stackwalk.py` in the toolkit reads it). |
| `RECOMP_USB_STATS=1` | Pad reports and control transfers per pad. |
| `RECOMP_FRAME_TRACE=<file>` | Once `<file>` exists, logs every batch of the next frame (surface, texture, blend, depth, combiners, pixels written) to stderr. Delete and recreate the file to trace another frame. `RECOMP_FRAME_TRACE_METHODS=1` adds every method. |
| `BO3_TRAP_NAN=<file>` | Trap the first x87/SSE NaN once `<file>` exists. That is how the camera NaN (lahf) was found. |
| `BO3_HUD_DUMP=<file>` | macOS/Linux. While `<file>.req` exists, append a dump of the title's 2D scenes (frames, layers, objects and where they land) to `<file>`; `<file>.set` with `1`/`0` sets `display.hud_fix` the way the overlay row does. See [widescreen-hud.md](widescreen-hud.md). |
| `BO3_DRIVE_LOG=<file>` | One line per frame of a race: the flip, the speed the HUD shows (m/s) and the pad bytes the title read (port, buttons, analog buttons, LT, RT). `tools/headless/reverse_scene.sh` asserts on it. |
| `BO3_CTL_LOG=<file>` | The driving-controls gate ([controls.md](controls.md)), a line per change: `S` a word of the title's state (module, paused player, event phase, countdown, results), `D` driving or not, `G` a pad's layout, `P` a pad as read and as given to the title. `tools/headless/controls_scene.sh` asserts on it. |
| `BO3_CTL_SCRIPTED=1` | `input.control_scheme` also applies to a pad a script drives (a test's stand-in for a controller). Without it scripted input always has the Xbox layout. |
| `BO3_HUD_OUTPUT=<w>x<h>` | A headless run lays the widescreen HUD out as for a window of that size (e.g. `1280x800` with `RECOMP_PRESENT_FIT=fill16x10`), so frame dumps show the layout a window would get. |
| `RECOMP_DEBUG=guest_cpus=token` | macOS (apexJCL runtime): the title's threads take turns on one guest CPU by a token, since the Mac cannot pin them to one core. Off by default. The `[KERNEL] guest CPU: N preemptions, M requests, longest wait W ms` line every 2 s shows it working; `guest CPU kept N ms in host code` names a host function that sat on it. Needs the lifted code in `__TEXT,__guest`, which the macOS build does (`src/game/CMakeLists.txt`). |
| `RECOMP_DEBUG=autopilot=1` | Test driver (BO-130): the title's own AI drives player 1's car while a race is in play, through the player car's "AI controlled" byte the title uses on the rolling start and after the finish (`src/game/autopilot.c`). Off by default, not in the overlay. Logs `[AUTOPILOT] result place P/N time T s ...` at the finish. `tools/headless/race_drive.sh <rundir>` drives a whole race with it (Mac, and Android through `tools/android/scene-launch.sh`). The AI is slow: it finished Silver Lake (3 laps) 6th of 6. Races only: no Crash mode. |

## A scripted session, start to finish

This is how the screenshots and GIFs in the README were made: headless, a live
pad file, and frame dumps that start once the race is running. In Git Bash:

```
mkdir -p runs/mp && touch runs/mp/pad.txt
RECOMP_PAD_LIVE=runs/mp/pad.txt RECOMP_PB_REPORT_MS=5000 \
RECOMP_FB_DUMP=runs/mp/f RECOMP_FB_DUMP_FLIPS=runs/mp/rec.flag \
  ./bin/burnout3.exe --headless > runs/mp/log.txt 2>&1 &

# The log names every file the title opens, which is how to know where it is.
# Wait for ovid\_FEMain.xmv (the title screen), then:
echo start:1200 >> runs/mp/pad.txt
# ...menu presses, one per screen...
# Once the track has streamed in (Tracks\...\streamed.dat in the log):
printf 'rt:600000\np2-rt:600000\n' >> runs/mp/pad.txt
touch runs/mp/rec.flag          # start dumping every flip
```

Then `ffmpeg -framerate 30 -i runs/mp/f%05d.bmp -pix_fmt yuv420p mp.mp4`.

## Reference scenes: driving a headless run by its log

`tools/headless/` holds one driver per reference scene. Each starts the game
headless, presses buttons through `RECOMP_PAD_LIVE`, and waits on the game's
own log:

| Driver | Scene | Ends at |
|---|---|---|
| `to_intro.sh` | the intro movies, nothing pressed | the attract loop |
| `to_menu.sh` | attract loop, profile screen, main menu | Single Event: Select Mode |
| `to_race.sh` | Single Event > Race, USA, Silver Lake Southbound, first car; holds the accelerator `RACE_SECS` (120) | racing |
| `to_crash.sh` | Single Event > Crash > 1. Trailer Trash, first car; accelerator from the countdown, the impact, the crash cam | Crash Results |
| `to_garage.sh` | main menu > Driver Details, back, Single Event > Race > Select Car | the garage (Select Car) |
| `reverse_scene.sh` | a race; accelerate, brake to a stop on the left trigger and keep it held, both triggers, the trigger half pulled (BO-96) | PASS/FAIL per case: the car reverses |
| `controls_scene.sh` | `SCHEME=ps2\|xbox`, `EVENT=race\|crash`, `FACE=xbox\|nintendo`: the menus, a race with a pause held over the accelerator, Resume, Quit; or a crash event to its results (BO-106). `controls_all.sh <prefix>` runs the default set, the Nintendo face layout with the PS2 scheme among them | PASS/FAIL per check: the PS2 layout only while driving |

```
tools/headless/to_crash.sh runs/c1                       # leaves the game running, pid in runs/c1/pid
STOP_AT_END=1 tools/headless/to_race.sh runs/r1 RECOMP_TRACE=pacing
BIN=build/mac-asan/bin/burnout3 tools/headless/to_garage.sh runs/asan-garage
RECOMP_PB_BACKEND=cpu tools/headless/to_menu.sh runs/m-cpu
```

Extra `KEY=VALUE` arguments go to the game's environment. The rest is in
`scene_lib.sh`'s header: `BIN`, `LAUNCH` (a whole launch command, for
Proton), `GAME_DIR`, `GAME_PATH_PREFIX` (`Z:` under Wine), `SAVES`,
`CAPTURE`, `STOP_AT_END`, `SCENE_TIMEOUT`, `SCENE_FLIPS`.

How the waits work:

- Flips. The drivers run the game with `RECOMP_FLIP_MARK=1`, which
  prints `[GPU] flip N T ms` once a frame (toolkit bo3/scenes; on an older
  build `SCENE_FLIPS=log` uses `RECOMP_FLIP_LOG`, whose lines start the same
  but which also starts the kick-watcher thread, so not for measurements).
  The title steps its simulation and menu animations once a frame, so "wait
  90 flips" is the same moment of the game whether it runs at 60 fps on
  Metal, 15 on the CPU rasteriser or under Proton on the Deck.
- Presses. Each line appended to `pad.txt` is answered by
  `PAD: live "<press>" at t=... ms` when the runtime takes it; the driver
  waits for that line and counts flips from it.
- Files. `[FILE] <path>` names every file the title opens: the scene
  changes (movies, the profile, a car, a track) are anchored on those.
- Retries. Where a screen ignores input while it slides in (the title
  screen, the profile screen), the driver presses again every 120-180 flips
  until the file that screen opens shows up.
- Wall time only bounds a wait (`SCENE_TIMEOUT`, 900 s) so a stuck run ends
  with `FAIL: ...` in `<rundir>/scene.txt`.

Every run starts from a fresh copy of the reference saves
(`<repo>/saves`, or the main checkout's `saves/` from a worktree; `SAVES=` to change): one profile,
Profile 1. The run writes to `saves/` (the cache partitions), and a saves
folder with another profile changes the profile screens.

The driver's own record is `<rundir>/scene.txt` (each step with its flip);
`events.txt` is the part of `log.txt` the waits read (flips, files, presses,
dumps), filtered as the log grows.

### The log lines each scene waits on

All scenes start the same way (`to_main_menu` in `scene_lib.sh`):

| Log line | Means | Then |
|---|---|---|
| `[FILE] D:\tracks\_FEMain.xmv` | the attract loop (title screen) is up | START, again every 180 flips |
| `[FILE] u:\...\SaveMeta.xbx` | START was taken: "Select option: Load Profile" reads the saves | A, again every 150 flips |
| `[FILE] u:\...\Profile 1` | the Load Profile list reads Profile 1's details (nothing loaded yet) | A after 60 flips: loads it ("Load successful"), A after 120: main menu |

Then per scene:

- intro (`to_intro.sh`): `D:\ovid\englis30.xmv` (EA, legal),
  `D:\ovid\cri_rw30.xmv` (Criterion, RenderWare), `D:\ovid\Titles30.xmv`
  (the 57 s intro film), `D:\tracks\_FEMain.xmv` (attract loop).
- menu (`to_menu.sh`): the common start, then down, A: Single Event.
- race (`to_race.sh`): A (Race: Select Options), right (Region: USA),
  down, right (Track: Silver Lake Southbound; both default to Random), A:
  `D:\pveh\COMP\CAR1.bgv` (Select Car), A: loading, then
  `D:\tracks\US\C3_V1\Gamedata.bgd`, `...\Intro.kfs`, and
  `D:\tracks\US\C3_V1\streamed.dat` (the track is in; the loading
  screen, with a random tip, is still up), `D:\tracks\crash1.rws` ~120
  flips later (the race begins: the intro camera, whose cuts differ from run
  to run, then the rolling start by itself). The accelerator goes on at
  crash1.rws + 300 flips, at the GO.
- crash (`to_crash.sh`): down x3 (Crash), A (Junction Selection), down
  (1. Trailer Trash), A: `CAR1.bgv` (Select Car), A:
  `D:\ovid\TrC_ENG.xmv` (Crash Training, ~100 s, plays once a launch while
  profiles do not save), `D:\sound\crash.awd`, `D:\sound\crashmod.awd`,
  `D:\tracks\US\C3_V1\streamed.dat` (the junction; the intro flyby runs),
  `D:\tracks\US\C3_v1\crash3.rws` (flyby over), `D:\tracks\crash2.rws` (the
  captions "Create traffic carnage...", which wait for A). A at crash2 + 300
  flips starts the countdown, the accelerator goes on 60 flips later, and
  `D:\tracks\US\C3_v1\crash1.rws` comes with the impact; the slow-motion
  crash cam ("Impact Time") and Crash Results follow by themselves.
- garage (`to_garage.sh`): down x4, A (Driver Details), B, up x3, A
  (Select Mode), A (Race: Select Options), A: `CAR1.bgv` (Select Car).

### On the Steam Deck (Proton)

The same drivers run there over SSH with the Windows build under umu-run
(`tools/headless/deck_env.sh` sets `LAUNCH`, `GAME_DIR`,
`GAME_PATH_PREFIX=Z:`, `RECOMP_PB_BACKEND=d3d11` and a `STOP` that ends
wineserver; `deck_sync.sh` copies the drivers, an exe and the reference saves
from the Mac):

```
# the Deck is given as the second argument or as DECK_HOST; keep your own in the
# git-ignored tools/headless/local.env (DECK_HOST=deck@<address>, and DECK_SSH="ssh -o
# HostKeyAlias=<name>" if you want a fixed host key name):
tools/headless/deck_sync.sh build/win/bin/burnout3.exe deck@<address>
ssh deck@<address>
cd /run/media/deck/emudeck/Games/burnout3-test/scenes && . tools/headless/deck_env.sh
tools/headless/to_crash.sh runs/c1
```

The native Linux Vulkan build (`vk/run/burnout3`) runs the same way with
`DECK_NATIVE=1 . tools/headless/deck_env.sh`: no Proton, no `Z:`,
`RECOMP_PB_BACKEND=vulkan`. Its backend writes back to guest memory, so
golden shots dump it as on Metal; that column comes first on the Deck.

An exe built before toolkit bo3/scenes has no `RECOMP_FLIP_MARK` and no
`RECOMP_FB_DUMP_AT=@file`: drive it with `SCENE_FLIPS=log`, and it cannot
take golden shots.

## Golden frames

With `CAPTURE=1` the drivers also take their shots: `shot <name> <anchor
flip> <offset>` appends the flip to `<rundir>/dumpat.txt`, which the game
reads through `RECOMP_FB_DUMP_AT=@<file>` (re-read whenever it grows), dumps
that flip as `fb_flip_NNNNN.bmp` (Metal and the CPU rasteriser from guest
memory, D3D11 from its back buffer with `RECOMP_D3D11_DUMP`), and the driver
moves it to `<rundir>/shots/<name>.bmp` (the flips either side, 4 by
default and 12 for the shots over a movie, to `shots/<name>/<k>.bmp`) and
lists it in `shots.tsv`. The compare takes the best flip of that window, so
a movie or a load on its own clock a flip or two off still matches. The
anchor is the flip the log was at when it printed the anchor line (the last
`[GPU] flip` line above it), so the shot is the same frame of the game
whatever the polling delay.

`tools/headless/golden.json` lists the scenes' shots with their anchors,
offsets, masks and limits, the backend columns, and the references' hashes.
The reference PNGs are game frames: they live in
`<repo>/runs/golden/<backend>/` (`GOLDEN_DIR=`), outside
git.

```
python3 tools/headless/golden.py list
python3 tools/headless/golden.py capture crash --backend metal --out runs/g/crash-metal
python3 tools/headless/golden.py determinism runs/g/crash-metal runs/g/crash-metal2
python3 tools/headless/golden.py update runs/g/crash-metal           # make these the references
python3 tools/headless/golden.py check runs/g/crash-metal2           # against its own backend
python3 tools/headless/golden.py check runs/g/crash-metal --against cpu
python3 tools/headless/golden.py xmvref && python3 tools/headless/golden.py xmvcheck runs/g/intro-metal
```

A shot passes when its pixels hash the same as the reference, or when the
mean absolute difference per channel, the share of pixels off by more than
`pixel_tol`, and the worst 64x64 tile's share are within the limits:
`same_backend` (regressions and determinism), `cross_backend` (Metal or
D3D11 against the CPU rasteriser; a deviation that is understood is listed
under the shot's `known` and reported as KNOWN), `video` (a movie frame
against ffmpeg's decode of the same XMV: the best frame within a window
around the shot's time). Masks take out what is random (the EA Trax song
box) or runs on a clock. The method follows apexJCL's BLiNX 2 golden gate
(xboxrecomp-cli `golden.py`, MIT), with log anchors instead of fixed
presents.

What the columns showed (Mac M5, 2026-10-10):

- Determinism. Two Metal runs of each scene give the same frames: the
  intro, garage and attract shots bit-exact, the rest within 0.5 mean
  difference (the menu's scrolling ticker; the window catches a movie frame
  one flip off). Two CPU-rasteriser runs likewise, except the shots over the
  attract movie.
- The intro movies are drawn scaled to 640x448 at y 16 in a 640x464
  frame; ffmpeg's decode placed so (`xmv_place`) matches every intro shot
  within mean difference 0.2-1.5, the frame within 3 of the shot's time.
- CPU rasteriser against Metal: the intro, the race's GO, the crash
  junction's flyby and captions and the garage match within the
  cross-backend limits (mean difference 0.3-2.5). Skipped, with the reason
  in `golden.json`: the shots over the attract movie (it plays on its own
  clock, and at the CPU rasteriser's pace it is elsewhere at the same
  flip), and the crash cam and results (player-driven: at ~5 fps the title
  can take more than one simulation step a frame while the accelerator is
  held by the wall clock, so the car meets the traffic elsewhere).
- The movies keep their own clock. On a loaded Mac (load 10-23 from
  other agents' builds and runs) a run flips slower than 60 for a while and
  the movie runs on: 1 of 30 Metal runs had the attract movie several frames
  on (before the menu shots' window grew to 12), and 2 of 4 intro runs had
  Titles30 up to 6 movie frames on. `check` therefore judges a movie shot
  that misses its reference against the movie itself (`xmvcheck`), which
  all 20 of those intro shots pass. Measure and record references on an
  idle machine.
- A backend that runs a scene slower than 60 fps meets the same two limits;
  the D3D11 column on the Deck will show whether they apply there.

The xemu column is a TODO. BLiNX 2 captured xemu on a Linux PC
(`scripts/xemu_ref.py`: xemu Flatpak with `-qmp` and the GDB stub `-s`, a
uinput pad, memory-state waits over GDB, `screendump` or a desktop
screenshot every 0.5 s, `xemu_crop.py`, `xemu_compare.py` with a shift
search and no threshold). For Burnout 3 that needs xemu somewhere other than
this Mac (the Deck: the AppImage on the SD card, F12 screenshots instead of
`screendump`, which xemu builds without pixman lack), the MCPX boot ROM and a
flash BIOS (on neither machine), and the disc image there; nothing of it is
set up.
