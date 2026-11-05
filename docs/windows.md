# Windows build (SDL2 + Vulkan)

The Windows build uses the same stack as Linux: SDL2 window, audio (WASAPI via
SDL) and pads, the Vulkan backend (`nv2a_pb_vulkan.c`, `fb_present_vulkan.c`,
glslang) and the Dear ImGui overlay. The kernel and platform layers stay the
native Win32 ones (VirtualAlloc placeholders, vectored-exception MMIO traps,
waitable timers, `%APPDATA%\Takedawn` saves). The older GDI + Direct3D 11 +
XAudio2 + XInput path stays behind `-DBO3_WIN_VULKAN=OFF` (MSVC only). The
Vulkan build needs clang (llvm-mingw): pthread and the GNU-style headers are
used by the shared SDL code, so MSVC is not supported for it.

## Cross-build from macOS or Linux

    tools/win/fetch-deps.sh     # once: llvm-mingw, SDL2, Vulkan headers + import lib, glslang
    tools/win/build-mingw.sh    # bin/burnout3.exe + bin/SDL2.dll

The toolkit checkout is `../xboxrecomp-win` or `../xboxrecomp` (or set
`XBOXRECOMP_DIR`); the generated C is in `src/game/recomp/gen` as usual.
Dependencies go to `~/.cache/bo3-windeps` (`BO3_WINDEPS`). The same llvm-mingw
(Windows x86-64 zip) can build natively on Windows with the same CMake
options (`-DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw-x86_64.cmake`).

## Native build on Windows (Git Bash)

The same three scripts run from Git Bash on Windows: `fetch-deps.sh` takes the
llvm-mingw Windows zip there, and both scripts hand CMake `C:/...` paths
(`cygpath -m`). `regen.sh` calls Python through the `py -3` launcher, so install
capstone for that Python once (`py -3 -m pip install --user capstone`). The
toolkit checkout is found as before (`XBOXRECOMP_DIR`, or a sibling `xboxrecomp`).

    ./regen.sh --disasm
    tools/win/fetch-deps.sh
    tools/win/build-mingw.sh

## Portable mode (BO-263)

A file `portable.txt`, or a folder `data`, beside `Takedawn.exe` turns it on
(`recomp_portable_find`, the toolkit's `recomp_exe_dir.h`; the program decides
once, at the start, from its own folder). Linux has the same switch beside
the `.AppImage` file (`$APPIMAGE`; docs/running.md); macOS and Android ignore it. The
per-user folders of `recomp_user_dir` then all move into `data` next to the
executable and the program's name is not used:

| Normally | Portable |
|---|---|
| `%APPDATA%\Takedawn` (saves, `settings.toml`, logs, reports, crash) | `data\Saves` |
| `%LOCALAPPDATA%\Takedawn` (`Game`, `data-folder.txt`) | `data` (`data\Game`) |
| `%LOCALAPPDATA%\Takedawn\cache` (shaders) | `data\cache` |

`RECOMP_SAVE_DIR`, `--data`, `TAKEDAWN_DATA` and the folder chosen in the setup
window still win over it. Nothing is moved from the usual folders, and the
earlier-name move (`Burnout3` to `Takedawn`) is not done. The log has a
`[SAVE] portable mode` line. The path logic is checked on every host by
`tests/portable_dir` in the toolkit.

## Packaging

Ship: `burnout3.exe`, `SDL2.dll`, the user's own `Burnout 3 Takedown\` folder
(next to the exe; the exe runs from that working directory). Nothing else:
glslang and libc++/winpthreads are linked statically, the UCRT is part of
Windows 10+, `vulkan-1.dll` is the GPU driver's. The exe does not import it:
the game opens it at run time (BO-231), so a PC without one, or with one older
than Vulkan 1.3, gets a message that says to update the graphics driver, not a
loader error before the game starts. The import library the build is given is
only there for CMake's `find_package(Vulkan)`; nothing links it.

## Test checklist (AMD Windows PC)

1. Copy the folder with `burnout3.exe`, `SDL2.dll` and `Burnout 3 Takedown\`.
2. Run from a terminal in that folder: `burnout3.exe` (log on the console; or
   `set RECOMP_STDIO_LOG=run.log`). `--headless` runs without a window.
3. Expect: `[vulkan] device up: <your GPU>`, `[PRESENT] display 0 mode ...
   at N Hz`, `[PRESENT] Vulkan swapchain`, `[SDL2] audio initialized: driver
   wasapi`, then the intro, the menu.
4. Check: the window is fully on the screen (title bar visible), picture,
   sound, pad, rumble, the overlay (Back+Start or F1), saves in
   `%APPDATA%\Takedawn`, Alt+F4 (the process must end).
5. The first run compiles shaders (hitches), caches are in
   `%LOCALAPPDATA%\Takedawn\cache`. Crash reports: `%APPDATA%\Takedawn\crash`.
6. A bug report ([running.md](running.md#reporting-a-bug)): the overlay's
   System page, "Create bug report", and `burnout3.exe --bug-report` from a
   terminal; both write `%APPDATA%\Takedawn\reports\takedawn-report-*.zip`.
   Open it: `system.txt` names Windows, the CPU and the GPU with its driver,
   and no file in it has your user name or the PC's name.

## Things to know

Text is UTF-8, paths included. The executable carries a manifest
(`tools/release/windows/takedawn.exe.manifest`, embedded by every Windows
build) that makes the process's ANSI code page UTF-8 (Windows 10 1903 and
later). So the C library's file calls, `getenv` and the Win32 "A" functions
all take and give UTF-8, and a folder opens whatever script its name is in,
wherever it came from: an argument (`--data`, `--install <image or folder>`,
`--dest`, `--music-dir`, `--cache-dir`, `--record`, `--bug-report --out`,
the xbe), an environment variable (`TAKEDAWN_DATA`, `RECOMP_SAVE_DIR`,
`RECOMP_SETTINGS`, `RECOMP_AUDIO_MUSIC_DIR`, the shader cache's), `%APPDATA%`
of an account with accented letters in its name, or settings.toml. The
arguments are taken from the wide command line and converted once, first
thing in `main` (the toolkit's `recomp_win_utf8_args`); the log's
`[BOOT] text is UTF-8` line says it took effect, and a console the game is
started from is switched to UTF-8 for the log.

Before this the narrow arguments and variables were in the PC's regional
code page while the toolkit's path layer, the settings file and the setup
window (which reads the game's output as UTF-8) all took them for UTF-8: a
name with letters of the PC's own language worked by luck in some places and
not in others, and letters outside it (Cyrillic on a Hungarian system) never
did.

On a Windows older than 1903 the manifest is ignored: arguments stay in the
regional code page and the log says so. The title does not see any of this:
what the guest reads as "ANSI" goes through a fixed Windows-1252
(`XBOX_GUEST_CP` in the toolkit's `kernel.h`), no longer the PC's own code
page.

The setup window is Qt, wide inside: it starts the game with a wide command
line (`QProcess`), and reads paths from variables with
`qEnvironmentVariable`.

- Defaults are the Linux ones. `main.c` sets `RECOMP_VBLANK_SYNC=display`,
  `RECOMP_PRESENT_PACING=sleep` and `RECOMP_PRESENT_SPIN_LEAD=8` for this
  build too; the swapchain is FIFO with the driver's minimal two images
  (`[PRESENT] Vulkan swapchain: ... 2 images, ... FIFO (vsync)`). The kernel's
  timer thread raises the timer resolution (`timeBeginPeriod(1)`) and sleeps
  on high-resolution waitable timers.
- The display lock needs the refresh times. AMD's Windows driver offers no
  `VK_KHR_present_wait` (the log says so), so the Vulkan presenter follows the
  display with a thread on `IDXGIOutput::WaitForVBlank` of the window's
  monitor, as the D3D11 presenter does: `[PRESENT] vblank sync: ... (DXGI
  WaitForVBlank ...)`. At 120 Hz the guest vblank takes every second refresh
  (`display 8.33 ms` in the `[PACING]` lines).
- Guest threads run on every core. The toolkit pins a title's threads to
  one host core, as on the console (another title races without it), but
  Burnout 3 opts out on Windows (`xbox_GuestCpusDefaultUnpinned`, BO-282.10):
  pinned, its thread waited for that idle core to wake after each hand-off
  with the render thread, and the drifting corners at 120 Hz missed about
  twice as many pictures (Ryzen 7 5800X, 3 x 3 runs: frames over 9 ms
  560/609/550 pinned, 316/321/295 unpinned; late flips 56/72/47 against
  5/2/2). Its threads are safe unpinned: the Mac has always run them so, and
  a lockstep race gives the same step hashes either way.
  `RECOMP_DEBUG=guest_cpus=<n>` pins them to core n again (the old default was
  the first even-numbered logical processor from 2 on, `[KERNEL] guest
  threads on one host core (cpu 2)`: core 0 takes the device interrupts, and
  pinned there 1.6 % of the 60 Hz race frames were late).
- Frame rate follows the display mode. With vsync on the game flips at most
  twice per display refresh, so a screen in a 24 Hz mode gives `48 flips/s, 24
  presents/s` (the "45 fps" seen in an early title bar was a test PC's 4K output
  in a 24 Hz mode). Use 60 Hz or more; the log line `[PRESENT] display 0 mode
  WxH at N Hz` names what SDL sees, and `RECOMP_PRESENT_STATS=1` prints
  `flips/s` and `presents/s` every 5 s. With `RECOMP_PRESENT_VSYNC=0`
  (immediate present) the display no longer holds the game back.
- DPI. The process is per-monitor (v2) DPI aware (SDL2's
  `SDL_HINT_WINDOWS_DPI_AWARENESS`, the level its documentation prefers), so
  Windows no longer stretches the window: SDL sizes are pixels and the
  swapchain is the real size. The start size is 2x the 640x480 frame times the
  desktop scale (1920x1440 at 150 %), so the window looks as big as before.
  `SDL_WINDOWS_DPI_AWARENESS=unaware` in the environment restores the old
  behaviour. The overlay scales with the window height.
- Window placement. If the window with its frame does not fit the display's
  usable area (a smaller screen, a streamed 720p/800p desktop,
  `RECOMP_WINDOW_SCALE=3` or 4) it is shrunk to fit (aspect kept) and centred;
  the log says `window did not fit the usable area`. Before that, Windows
  put the title bar above the top edge.
- Exit. Closing the window ends the process (the exit-time log flush used
  to block forever on Windows).
- Pads. Pads come from SDL's game controller API (XInput, DirectInput,
  HID); a pad gets its port at its first button press. The keyboard is a pad on port 0
  with a fixed map ([controls.md](controls.md#the-keyboard)); F1 opens the overlay. Without a pad, `RECOMP_PAD_LIVE` and
  `RECOMP_PAD_SCRIPT` drive the title ([running.md](running.md)).
- Rare long stalls. One frame of 0.3-5 s per 5-10 minute run was seen on
  the first day; the stall hunt put those down to other work on the test PC.
  What is left on a quiet PC: in about 35 minutes of Balanced racing, one
  stall of 50-66 ms in which nothing in the process ran, the time-critical
  thread on `WaitForVBlank` included (six refreshes with no vertical blank
  reported, process CPU time 0): outside the game, cause not found.
- Maximum (the title's own busy-wait) is not for Windows. The overlay's
  Power mode row does not offer it (BO-75; a stored `stock` is read as
  Balanced; `RECOMP_PRESENT_PACING=spin` still selects it for diagnosis).
  The title's
  threads share one host core. With `present.pacing = spin` the main thread
  never sleeps, and Windows hands the core over only at the end of a 31 ms
  quantum, not by the console's priorities. Any other title thread (the file
  thread above all) then waits one or two quanta for the core, with whatever
  it holds: 34-52 racing frames over 20 ms in 5 minutes (23-40 over 33 ms)
  against none in Balanced, and 61 in the menus and the load. Measured with a
  timing build: the file thread sat 31.9-66 ms (steps of 31.25) inside one
  log line's lock while the executor's own line waited up to 60 ms for it;
  with `RECOMP_GUEST_CPUS=all` the waits go (see the table). Balanced, the
  default here, sleeps in the title's waits, so the core is free for whoever
  is made ready. A traced run made it worse than a played one: every line a
  thread logs takes the same lock.
- Recording a session. `tools/win/play-trace.cmd`, copied beside the exe,
  starts the game as a double-click does and writes `traces\log-*.txt`,
  `ft-*.csv` (one line per frame), `vp-*.csv` (every present and display
  refresh) and the HUD monitor's files (BO-89). It records no per-frame log
  lines, for the reason above, and not `RECOMP_TRACE=pb_slow` (which names
  the pushbuffer method a frame stood in: add it to hunt a stall; it costs
  0.8 ms of the executor's frame). `tools/win/pacing_stats.py log ft [vp]` reads
  them: per-state percentiles and every racing frame over 20 ms with where
  its time went.
- Reading a log. Two threads' stderr lines can run together on Windows
  (`...fence 0x00008779[PACING] present 8097 ...`). A reader that takes
  `[PACING]` lines only at a line's start loses those presents and counts a
  33 ms interval for each: the first Balanced baseline's "4-7 racing frames
  over 20 ms" were 9-15 such lines a run, plus the frames where the title
  changes between 60 and 30 Hz.

## Measured (Ryzen 7 5800X, RX 9060 XT, Windows 11)

Settings: the defaults above (vsync on, FIFO, `display` lock, sleep + 8 ms
spin lead), `render.scale` 2, SMAA + CAS + 16x aniso (the reference
`settings.toml`), 3840x2160 desktop at 150 % scale, window 1920x1440.
The run: Single Event race driven by `tools/headless/to_race.sh`, accelerator
held, 20 minutes; frame times from `RECOMP_TRACE=pacing=all,frametime=<file>`,
split by the title's own interval (`tools/win/pacing_stats.py`).

| 60 Hz display, 20-minute race | p50 | p95 | p99 | p99.9 | max | missed (> 1.5 periods) |
|---|---|---|---|---|---|---|
| racing (iv 1): guest flips, 71 977 | 16.68 | 17.02 | 17.14 | 17.81 | 82.5 (the race start) | 2 (0.003 %) |
| racing: host present-to-present, 71 973 | 16.68 | 17.04 | 17.21 | 17.84 | 88.6 | 6 (0.01 %) |
| crash cameras / replays (iv 2): guest, 1 028 | 33.33 | 33.73 | 33.99 | 34.24 | 34.6 | 0 |
| crash cameras: host present, 1 027 | 33.33 | 33.77 | 34.05 | 34.64 | 66.7 | 1 |
| boot, menus, loading: guest, 7 025 | 16.66 | 17.06 | 17.39 | 17.75 | 66.7 | 4 |

1 192 whole seconds of racing: every one at 60 flips/s but one (45). The
in-game frame-time panel (Display > Performance overlay) read `60.0 fps, p99 17.2 ms,
max 18.5 ms` in lap 2. For comparison the Deck's 19-minute session (BO-83): 0.60 %
of the 60 Hz frames over 20 ms, 16 over 33 ms, p99 18.7 ms; here 2 over 20 ms.
At 120 Hz (a 5-minute race): racing p50 16.67, p95 17.05, p99 17.45, 2 missed in
20 572; crash cameras p99 34.02, none missed; every full second 59-60 flips.
Before the pinning change (cpu 0, 60 Hz): racing p95 17.10, p99 31.6 ms, 1.63 %
missed; crash cameras steady (33.34 / 33.76).

### 4K fullscreen at 120 Hz, the power modes (BO-75)

3840x2160 fullscreen at 120 Hz, `render.scale` 2, SMAA, sharpen 0.55, 16x
aniso, vsync; the scripted Single Event race, 5 minutes of it per run, a warm
shader cache, the title's threads on cpu 2 unless said. Host present to
present (`RECOMP_TRACE=vk_present`), racing = the title's interval 1 on the
flip and both neighbours; the race scene's first frame is apart (see below).

| run | racing p50 | p95 | p99 | max | over 20 ms | over 33.5 ms | crash cameras p50 / p99 / missed |
|---|---|---|---|---|---|---|---|
| Balanced, run 1 (16 507 frames) | 16.68 | 17.05 | 17.19 | 18.5 | 0 | 0 | 33.34 / 34.02 / 0 of 1 131 |
| Balanced, run 2 (16 696) | 16.68 | 17.08 | 17.37 | 18.3 | 0 | 0 | 33.32 / 34.16 / 0 of 983 |
| Maximum (spin), run 1 (16 173) | 16.71 | 17.34 | 17.61 | 77.2 | 52 | 40 | 33.32 / 41.55 / 8 of 1 340 |
| Maximum, run 2 (17 236) | 16.68 | 17.46 | 17.80 | 80.1 | 34 | 23 | 33.34 / 34.46 / 3 of 774 |
| Maximum, `RECOMP_GUEST_CPUS=all` (16 494) | 16.60 | 17.47 | 17.60 | 29.0 | 2 | 0 | 33.36 / 34.22 / 0 of 1 102 |
| Balanced, `play-trace.cmd`'s traces and the HUD monitor (16 442) | 16.62 | 17.08 | 17.20 | 21.9 | 1 | 0 | 33.32 / 33.83 / 0 of 1 160 |
| Maximum, the same traces (16 940) | 16.70 | 17.43 | 17.66 | 51.8 | 19 | 8 | 33.34 / 34.41 / 0 of 923 |

The first five rows were traced with a log line per flip and per present
(`pacing=all`), the last two without (`pacing`); the scene driver's own
flip-mark line is in all of them. The one frame over 20 ms in the launcher's
Balanced row follows a HUD monitor frame dump (9-10 ms each, 12 in the run).

The Maximum runs' long frames are the executor or the title waiting a
scheduler quantum or two for a title thread that is off the shared core (the
"Maximum is not for Windows" note above); with the threads on every core the
guest's own flips have none over 20 ms (the two presents over 20 ms came late
after a frame that was on time).

What is left in Balanced, each with its cause:

- The race scene's first frame, 67-77 ms (every run, every mode): the cut
  from the loading screen to the intro camera uploads the scene's 87-88
  textures (3.7 M texels) in one frame: 36.6 ms of CPU decode, 36.5 ms of
  image creation (0.41 ms for each image with its own `vkAllocateMemory` on
  this driver), 2.7 ms of copies (`[vulkan] textures:` under
  `RECOMP_TRACE=pcache`). The frame before it is the loading screen. In the
  race itself 12 frames have 2.3-9.5 ms of uploads and none of them is late.
- A shader or pipeline compiled at first use (`[vulkan] pcache: compiled
  ... on the ack thread`, 15-60 ms): only while the cache does not know the
  scene yet; the two measured runs had none, the warm-up run before them 15
  (4 racing frames of 24-33 ms).
- A present one refresh late after a frame that was on time (25 ms, then
  8 ms): one in seven Balanced runs, none in these two.
- The stall outside the game of the note above: one in about 35 minutes of Balanced racing.

Latency, as far as it can be measured without a camera (Balanced; in Maximum
the present comes 0.45 ms later after the refresh, the rest is the same): 2 frames in flight in the backend, a FIFO swapchain
of 2 images whose acquire never waited (longest 0.23 ms of 16 422, so no
frame was ever queued behind another); from the vblank release to the frame
with the presenter 0.12 ms, to `vkQueuePresent` returned 0.32 ms more (p99
0.5 ms each); the present lands 0.75 ms after a display refresh (p99 1.2) and
7.58 ms before the next one (p95 7.87), the earliest the frame can be scanned
out at 120 Hz. At 60 Hz that last step is a whole refresh, about 15.9 ms. No
`VK_KHR_present_wait` on this driver, so when a frame is really shown is not
known.

## Testing a PC over SSH

The OpenSSH session on Windows is not the desktop: windows opened from it land
on a hidden 1024x768 display. To see the game on the logged-in user's desktop,
start it through a scheduled task that runs in that user's session (no
installer, nothing system-wide; delete the task afterwards):

    $a = New-ScheduledTaskAction -Execute powershell.exe -Argument '-NoProfile -File C:\path\run.ps1'
    $p = New-ScheduledTaskPrincipal -UserId <user> -LogonType Interactive -RunLevel Limited
    Register-ScheduledTask -TaskName bo3-run -Action $a -Principal $p
    Start-ScheduledTask -TaskName bo3-run
    ...
    Unregister-ScheduledTask -TaskName bo3-run -Confirm:$false

`run.ps1` sets the `RECOMP_*` variables and starts `burnout3.exe`; the same
way a script can take a screenshot of the desktop (`CopyFromScreen`) or press
a key (`keybd_event`; pass a scan code too, SDL derives the key from it: F1 is
vk 0x70, scan 0x3B). `RECOMP_WINDOW_SHOT=<flip>,...` with
`RECOMP_WINDOW_SHOT_PREFIX=` writes the presented frame to a BMP.

The reference scenes (`tools/headless/to_race.sh` and the others) run under
Git Bash on Windows too, from the game folder:

    SAVES=<reference saves> LAUNCH='exec ./burnout3.exe' RECOMP_PB_BACKEND=vulkan \
    TRACK_RE='streamed[.]dat' RACE_SECS=1080 \
    RECOMP_TRACE=pacing=all,frametime=C:/path/ft.csv \
      tools/headless/to_race.sh runs/r1 RECOMP_HEADLESS=0
    python3 tools/win/pacing_stats.py runs/r1/log.txt C:/path/ft.csv

`RECOMP_HEADLESS=0` gives a window; `TRACK_RE` accepts whatever track the
random pick gave (the script waits for Silver Lake otherwise). The scene
leaves the game running (STOP_AT_END=1 would stop it as soon as the accelerator
press is queued). In a bash-started run the game is a child of `bash.exe`; stop
it by its pid. A display mode can be changed for a test from the same kind of
task with `ChangeDisplaySettingsEx` (flags 0 is a dynamic change).
