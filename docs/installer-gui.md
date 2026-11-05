# Installer window (BO-92)

A Qt 6 window over `tools/install/install.py`. It does no installing itself: it
runs the driver as a subprocess, reads the line protocol from
[`installer.md`](installer.md) and shows it. Everything is under
`tools/install/gui/`. English only.

## Why Qt 6 Widgets in C++

Xerenge's installer (`rexglue/installer`, the model for this one) is a single
C++ `QWidget` on Qt 6 Widgets, with `QProcess` for the tools, `QTabWidget`,
`QProgressBar`, `QFileDialog`, a CMake project, and three small packaging
scripts (`macdeployqt`, `linuxdeploy` with the Qt plugin, Inno Setup). This one
uses the same flavour and the same packaging, for the same reasons:

- **Least to ship.** Widgets needs `QtCore`, `QtGui`, `QtWidgets` and the
  platform plugin; QML adds the Quick stack and a JS engine, PySide6 a Python
  runtime and the Shiboken bindings, hundreds of MB. The driver is Python
  already and runs as its own process, so the window need not be.
- **One code path on three systems.** The same sources build on macOS, on
  Linux (X11, Wayland, the Steam Deck's Desktop Mode) and on Windows, and
  `macdeployqt` / `linuxdeploy` / `windeployqt` are the usual way to pack them.
- **Native look** and system dark mode for free (Qt 6.5+), HiDPI scaling by Qt.
- Proven: it is what Xerenge's players already use.

## Look

Dark by default, on the Fusion style so it is the same on macOS, Linux and Windows:
a near-black asphalt background, light text, a hot orange to yellow accent, red only
for errors and Cancel. One palette and one style sheet (`theme.cpp`); the custom
pieces (`ui_widgets.cpp`) are drawn in code: the header band (a slanted panel, two
faint speed streaks, a thin chevron strip, the icon, the title, and on the right a
readout of the state: *Ready*, *Step 4 of 6*, *Stopped*, *Ready to play*), the
heat-gradient progress bar with slanted ends, and the step marks (ring, turning arc,
tick, cross). The form is a few panels; the disc result is a status card (found,
wrong disc, checking); content stays at most 980 px wide and centred on large
windows. The failing, finished and running states change the header edge, the
progress bar, the status line and the roles of the buttons (Install / Resume / Play
are the accent call to action, Cancel is red). `--look classic` keeps the plain
native look.

Fonts: **system fonts only, nothing bundled**, so there is no font licence to
carry. Titles and headings use the first installed of Bahnschrift, Avenir Next
Condensed, Arial Narrow, Roboto Condensed, Noto Sans Condensed, DejaVu Sans
Condensed, in bold italic; body text is the system sans. No Burnout, EA or
Criterion logo, lettering, font or image is used or imitated, and the title is
plain text. The placeholder icon (a disc and an arrow) is drawn by
`app_icon.cpp`; it is also the installer app's own icon, never the game's.

## Build and run

```
cmake -S tools/install/gui -B tools/install/gui/build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=$(brew --prefix qt)          # macOS; Linux: Qt 6 dev packages
cmake --build tools/install/gui/build
tools/install/gui/build/Burnout3Installer.app/Contents/MacOS/Burnout3Installer \
      --toolkit ../xboxrecomp-apex                       # Linux/Windows: build/burnout3-installer
```

Built inside the source tree it finds `tools/install/install.py` by walking up
from the program, and the driver's `--work` defaults to that tree. Needs Qt 6.4+
(Widgets, Concurrent; Test for `protocol_test`), C++17.

| option | meaning |
|---|---|
| `--iso <path>` | disc image or extracted folder, put in the field (the player presses Install) |
| `--path <dir>` | install into `<dir>` at once, **without a window**, printing progress; exit 0 installed, 1 failed, 2 stopped |
| `--toolkit`, `--work`, `--app-dir`, `--python`, `--driver` | passed to / used for the driver; `--driver` names another `install.py` |
| `--no-pip`, `--speed gentle\|normal\|fast` | the options of the Advanced section |
| `--size WxH` | initial window size |
| `--look classic` | the plain native look instead of the racing one (default); `BO3_GUI_STYLE=classic` too |
| `--preview <dir>` | test mode: three shots (install, progress, finished) |
| `--grab <dir>` | write a PNG of the window at each step (works with `--path`) |
| `--shots <dir>` | test mode, below |
| `--show-grab <png>` | test mode: show the window, save a PNG of it after two seconds, quit (the one on-screen start test of the real platform plugin) |
| `--check-shot <dir>` | test mode: check `--iso` with the real driver and grab the window |
| `--write-icon <png\|ico> [--icon-size n]` | write the installer's own icon and quit |

Environment: `BURNOUT3_DRIVER`, `BURNOUT3_PYTHON`, `BURNOUT3_PAYLOAD` (see
"Where it finds things"); `BO3_GUI_PLATFORM=macos|linux|windows` shows another
system's options (for tests); `BO3_INSTALLER_TEST=1` arms the quiet-failure mode
below. Test, `--grab` and `--path` runs keep their saved settings in a temporary
folder.

## What the window does

One window, after Xerenge's: header, tabs, a progress bar with a status line and
one big button.

- **Install tab.** The disc (a field with *Image...* and *Folder...*, and drag and
  drop), checked as soon as it is chosen with the driver's `--check-disc`
  (found, or a plain-language reason: wrong version, an Xbox 360 disc, not a
  disc image, damaged, not there). On Linux
  *Add to the applications menu* (`--desktop`) and *Add the game to Steam*
  (`--steam`). *Advanced* (folded like Xerenge's *Hacks*): build speed
  (`--nice`/`--jobs`), allow `pip install capstone` (`--no-pip` otherwise), source
  folder (`--work`), toolkit (`--toolkit`), Python (`--python`), app folder
  (`--app-dir`); on Linux build container or host (`--linux-build`), podman
  image store (`--podman-root`) and Steam user (`--steam-user`; the driver picks the
  most recent login by itself and logs which one, the field is only an override);
  on Windows whether to download the portable build tools (`--no-fetch` otherwise)
  and the tools folder (`--tools-dir`). *Install path*
  (`--root`) with the free space. *Build tools*: the driver's `toolchain` step, run
  when the window opens (and on *Check again*) with `--no-pip` against a scratch
  root, so it installs and creates nothing (on Windows also `--no-fetch`); what is
  missing is listed with the command for it and *Copy commands*. What the driver
  fetches itself at the start (capstone with pip, on Windows the portable tools) is
  shown as "will be installed for you" and does not block.
- **Path limits** under the install path: on Windows a warning when the build folder (`<root>\\src\\burnout3`, or the one named under Advanced) would pass about 140 characters (Windows breaks paths over 260 and the build adds about 90); on Linux a refusal for a path with a colon (podman cannot mount it).
- **Graphics preparation (the shader pre-build, BO-93).** One box, ticked by default: *Prepare graphics for
  smooth first races (about 2 minutes, ~10 MB)*, with its cost under it (untick to skip; without it the first
  races can stutter a little; it can be started later from the game's settings, Graphics). The game itself
  asks nothing at its first start. `--no-shaders` starts the window with it unticked (tests); the step is a
  normal row with progress. If it fails the row turns amber with a plain reason, the install is still
  complete (Play is offered) and Update tries again. The choice is not remembered between runs.
- **Sharper menu pictures (BO-311, [upscale-art.md](upscale-art.md)).** A second optional box under
  it: *Sharper menu pictures: upscale the loading screens and menu art with AI*, with how long it takes and
  the disk it uses (the upscaler comes with the package, nothing is downloaded). The game
  says whether it is offered and ticked (`--data-status`: `LOG data upscale on|off|no <quality>`): ticked on
  Apple silicon and with a graphics card of its own, shown but not ticked on a Steam Deck or built-in graphics
  (with the quicker model), not shown on Android or by an older game. It runs after the shader pre-build as its
  own row (`--upscale-art --quality <q>`); Cancel writes its `cancel` file (`LOG upscale work <dir>`). A failure
  or a stop turns the row amber with a plain reason (`messages::ExplainUpscale`), the install is still complete.
  `--upscale` / `--no-upscale` start the window with it ticked or not (tests). The box is only shown while
  no pack is made and the offer was not turned down (`LOG data upscale-pack made|none|declined`). Over an
  install that is already whole (an update) it is offered once, unticked, with its own *Upscale menu
  pictures* button (only that step runs). Seen and not used (Install without it, Play, or closing the
  window) the window runs `--upscale-art --dismiss` and does not show it again; Settings, Graphics keeps it.
- **An installed copy** (`<root>/Game/Burnout 3 Takedown/default.xbe` and
  `install/state.json`) turns *Install* into *Update*: no disc needed, the driver
  skips what is still current.
- **Progress tab.** One row per step of the driver's `PLAN` with its state, its
  `PROGRESS` text and its time, and the log (`LOG`, `WARN`, the commands). The
  bar is the weighted share of the steps (regen and build are most of it); a
  step the driver skips counts as done. Right of the status line: the percentage
  and the elapsed time.
- **Cancel** (the button while running) stops the driver and everything it
  started (its own process group on Unix, `taskkill /T` on Windows). **Resume**
  runs the same command again; the driver skips finished steps and restarts
  an interrupted extraction.
- **Errors** open a message box with what happened in plain words, what to do,
  the failing step and code, and under *Show details* the command, the last 40
  log lines and what the driver printed on stderr. *Copy details* puts that text on
  the clipboard, *Open log folder* shows `<root>/install/logs`. A driver that dies
  without a `RESULT` (a Python traceback) is reported the same way. Codes with their
  own wording are in `messages.cpp`, among them `toolchain-incomplete` (the missing
  commands are listed), `steam-running` ("close Steam first") and
  `steam-user-ambiguous` (plain message: write the user id under Advanced).
- **Finished:** *Play* starts the `.app` (macOS), the `.lnk` in the install directory
  (Windows) or `burnout3.sh` there (Linux); *Show in folder* reveals it.
- The game's icon (`<root>/install/icons/icon-256.png`, made by the driver from the
  player's XBE) replaces the placeholder in the header as soon as it exists. The
  placeholder (a disc and an arrow) is drawn in code; the program ships no image.
- The About tab and a line on the Install tab carry the disclaimer: unofficial,
  not affiliated with EA or Criterion, bring your own disc, nothing from the game
  is downloaded or shared.
- Touch sizes: controls are at least 36 px high; the Install tab scrolls when the
  window is short, so it fits 1280x800 (the Deck's screen) with decorations.

## Options, against Xerenge's installer

Xerenge's installer has the disc image, the install path, a few game settings and
a no-shortcuts switch. This one keeps to that and adds only what our flow needs:

- *Add to the applications menu* (Linux), *Add Start menu and Desktop shortcuts*
  (Windows) and *Add the game to Steam* (Linux): Xerenge's shortcut and menu entry;
  Steam is for the Deck. macOS has none: the driver's `.app` is the shortcut.
- *Advanced* (folded): build speed (the build takes minutes and the machine may be
  in use), allow pip for capstone (a network action the player may refuse), source
  folder, toolkit and Python (the sources are not fetched by the driver yet, so a
  development tree needs them), app folder, and per system: build container or host
  and the podman image store (Linux; the Deck keeps images on the SD card), Steam
  user override, and download of the portable tools with their folder (Windows; a
  network action).
- No game settings (bloom, blur, ...): the port has none to set at install time. No
  option to make a disk image of the built game.

## Where it finds things (`LocateDriver`)

1. `--driver` / `BURNOUT3_DRIVER`: an `install.py`, used as it is.
2. A **payload** (`BURNOUT3_PAYLOAD`, `payload/` beside the program,
   `../share/burnout3-installer/payload` for an AppImage, `../Resources/payload` in
   the macOS app) holding `burnout3/` (a `git archive` of this tree, with
   `tools/install/install.py`) and optionally `xboxrecomp/` (the toolkit), `python/`
   (Windows: embeddable Python with capstone) and `STAMP`. The driver builds in its
   source tree, which cannot be inside a read-only app, so the window first copies
   the payload to `<root>/src` (step *Unpacking the installer's files*, skipped when
   `STAMP` is unchanged) and points `--work`/`--toolkit` at the copy. This is a
   stand-in until the driver fetches its sources itself (open decision 2 in
   `installer.md`).
3. A development build inside the tree: `install.py` found by walking up.

The driver runs with `python3` (`--python`, `BURNOUT3_PYTHON`, the payload's, or
`python3`/`python` from PATH); on macOS `/opt/homebrew/bin` and `/usr/local/bin`
are put first on PATH because an app started from Finder has a bare one. AppImage
library variables are removed from the driver's environment.

## Packaging the installer itself

All three put the same payload next to the program: no game data, no generated
code, no binary built from it (the scripts refuse a payload with an `.xbe` or
`.iso`).

- **macOS** (done): `tools/install/gui/packaging/make-macos-app.sh`
  (after Xerenge's `make-macos-app.sh`: build, icon from `--write-icon` through
  `iconutil`, `macdeployqt`, check that nothing links Homebrew, ad hoc `codesign`,
  zip). Set `XBOXRECOMP_DIR` to put the toolkit in. Needs the sources committed.
- **Linux, Steam Deck:** `packaging/make-appimage-container.sh`. `stage <dir>` (on the Mac) puts the
  window's sources, the payload and the licences in a folder; `build <dir> [podman options]` (on
  the Deck, or any machine with podman or docker and an x86_64 image) runs `inside-container.sh` in an
  `ubuntu:24.04` container: it installs Qt 6.4 and the build tools *there*, downloads linuxdeploy
  and its Qt plugin (build time only), builds the window and writes
  `<dir>/out/Burnout3_Takedown_Installer-x86_64.AppImage` (29 MB: Qt Core, Gui, Widgets, DBus, the
  xcb, Wayland and offscreen platform plugins, the payload and the licences). Nothing is installed
  on the host. Ubuntu 24.04's glibc is older than SteamOS's and Bazzite's. (`make-appimage.sh` is
  the same for a machine that has Qt 6 and linuxdeploy.)
- **Windows: one setup exe, `Burnout3_Takedown_Installer-setup-x86_64.exe` (12.5 MB), is the download.**
  `packaging/make-windows-cross.sh` builds the package folder on a Mac or Linux machine, so no Windows
  machine is loaded: the window is cross-compiled with llvm-mingw (the release the port pins) against
  the prebuilt Qt 6 `win64_llvm_mingw` from aqtinstall, with the same version of Qt's host tools
  (`QT_HOST_PATH`); `windows-toolchain.cmake` is the CMake side. The exe carries the placeholder icon
  (a `.rc` made from `--write-icon`), the Qt DLLs (the closure of `objdump -p` imports, what
  windeployqt would copy), `platforms/qwindows.dll`, the payload and the licences. Qt 6.10.3, because
  the 6.11 Windows packages were not on the server yet. `packaging/make-windows-setup.ps1` then
  compiles `windows-setup.iss` on a Windows machine with Inno Setup's command-line compiler. It
  fetches Inno Setup 6.7.3 by a pinned URL and sha256 and installs it in *portable* mode
  (`/PORTABLE=1 /CURRENTUSER /VERYSILENT`) into `%USERPROFILE%\bo3-tools\innosetup`: checked on a real
  machine, that wrote no registry key, no uninstaller and no Start menu entry (nothing outside the
  folder), and a compile takes 6 seconds. (There is no Inno Setup for macOS; it ran on the test PC.)
  The previous plain zip is no longer a deliverable (`make-windows-cross.sh` still leaves it as a
  by-product).

  What the setup does, after Xerenge's `windows-setup.iss` (per user, no administrator rights, no
  questions): it asks nothing but "Create a desktop icon" (off by default), installs the installer app
  into `%LOCALAPPDATA%\Programs\Burnout 3 Takedown Installer`, adds a Start menu entry, and opens the
  window when it is done. Running it again updates in place. Its uninstaller (Settings > Apps, or
  `unins000.exe`) removes the installer app, and says in a message that the game built with it, its
  data and saves live elsewhere (by default `%LOCALAPPDATA%\Burnout3`) and are not removed. Xerenge's
  wrapper unpacks to a temporary folder and leaves nothing; this one keeps the app so that an update
  or a retry is one click, and removes it cleanly.

  **SmartScreen.** The exe is not signed, so Windows shows "Windows protected your PC / Unknown
  publisher" the first time: *More info*, then *Run anyway*. Signing (a code-signing certificate,
  and reputation for it) is an open decision; nothing here works around the warning.

### What the payload holds

Explicit lists in `packaging/payload.sh`, not the whole `git archive`:

- game repository: `LICENSE README.md CMakeLists.txt cmake/ config/ regen.sh src/
  tools/install/` (the driver, without `gui/` and `tests/`) and `tools/win/`. Not
  `docs/` (it tracks screenshots of the game), `backlog/`, `runs/`, `tools/deck`,
  `headless`, `perf`.
- toolkit (`XBOXRECOMP_DIR`): `LICENSE LICENSES/ NOTICE README.md CMakeLists.txt
  cmake/ include/ src/ templates/ tests/ third_party/` (its CMake adds `tests/*` as subdirectories; the generated code includes `templates/runtime`) and `tools/` only `__init__.py`, `xbe_parser`,
  `disasm`, `func_id`, `abi_analysis`, `recomp` (what regen and the build use). Not
  `docs/`, the other tools.
- `licenses/` next to it: the game repository's and the toolkit's MIT licences, the
  toolkit's `NOTICE` and `LGPL-2.1.txt`, and Qt's LGPL 3 and GPL 3 texts with
  `Qt-NOTICE.txt` (Qt is linked dynamically, where its source is, how to swap it) and
  `Qt-THIRD-PARTY.txt` (the licences of the other projects' code inside Qt, from Qt's own
  source archive).
  The About tab shows them.
- The script refuses a payload with a picture, video, audio, font, disc, `.xbe`,
  archive or `.bin`/`.dat` file, and prints every file over 100 KB (all source or text).
- Qt modules in the macOS app: Core, Gui, Widgets, Concurrent, DBus and the cocoa and
  macOS style plugins, all LGPL 3; the script drops the image-format, input-context
  and icon-engine plugins and whatever library nothing then reaches.
- `make-macos-app.sh` also writes a `.dmg` of the installer (the app and an
  Applications link; no background, no layout script). The zip and dmg are build
  output and are not committed.

## Verified

- **Packaged macOS app** (`make-macos-app.sh`, Qt 6.11.2 from Homebrew): 71 MB app, 28 MB
  zip, 35 MB dmg; payload 11 MB (source only: no file over 100 KB that is not C, C++,
  Objective-C, Python or a text list, and none by media or data extension anywhere in
  the bundle), about 60 MB of Qt Core, Gui, Widgets, Concurrent, DBus, the cocoa platform
  plugin, the macOS style and the libraries they need (ICU alone is 30 MB).
  `codesign --verify --deep --strict` passes, also for the app inside the dmg; `otool -L` of every
  Mach-O names only `/System`, `/usr/lib` and `@rpath`; started with `DYLD_PRINT_LIBRARIES=1`
  every non-system image loaded is inside the bundle; under `sandbox-exec` with `/opt/homebrew`
  and `/usr/local` unreadable it comes up and draws its window (`--show-grab`). The dmg holds the
  app and an `Applications` link.
- **Payload on the real driver:** the packaged app, run unattended (`--iso`, `--path`, a copy of
  a finished root, `--speed gentle`), unpacks its payload to `<root>/src`, regenerates and builds
  the game from that copy and packages it. This caught two directories the build needs
  (`tests/` and `templates/` of the toolkit), now in the list.
- **Against the real driver** (checks): the real ISO is accepted (716 files, 2.48 GB); a text file
  named .iso, the Burnout Revenge ISO (Xbox 360) and a missing path are refused, each with its
  message.
- **Against the fake driver**: a scenario script, since replaced by `tests/window_test.cpp` (ok, missing tool, wrong disc, build error,
  driver crash, resume after the error; 125 more runs of the five scenarios for a teardown race
  that is fixed), `protocol_test` (10), and `--shots` through every page and state for macOS (1280x800
  and 1000x700), Linux and Windows options. The driver's own tests (`tools/install/tests`, 29) pass.

## A silent exit, and what the window does now

A real install once ended with status 0 in the middle of regen and no `RESULT`. Cause: a
Cocoa application treats a *Quit* (the Dock menu, an Apple event, Cmd-Q) as an ordinary
exit, so `app.exec()` returned 0; an unattended run has no window but did have a Dock icon, and
someone quit it. Now:
- test and unattended runs on macOS turn themselves into a UI-element application (no Dock icon, no
  menu bar), and `--path` returns 5 with a log line if the event loop ends without the run having
  ended by itself (the driver is stopped with the window, by its process group);
- closing the window while an install runs asks "An install is running. Stop it and quit?" and, if
  confirmed, stops the driver's process group and waits for it, so the next start resumes;
- a driver that ends without a `RESULT` is a failure in every case, each with its own plain
  message: killed by a signal (`driver-killed`), ended with status 0 but said nothing
  (`driver-no-result`), ended with an error and said nothing (`driver-crashed`). `tests/window_test.cpp`
  checks the crash case against the fake installer (`crash.iso`).

## Verified on Linux and Windows

- **AppImage** (built on the Steam Deck in the container, 29 MB): on the **Deck** (SteamOS, Game Mode
  session up) the window started with the xcb platform on both of gamescope's X servers (`:0`, `:1`)
  and with the Wayland platform on `gamescope-0`, all rendered without mapping a window
  (`--check-shot`, `WA_DontShowOnScreen`), accepted the extracted disc folder (718 files) and found
  the tools. A full unattended install through the AppImage (offscreen platform, `--path`, scratch
  root on the SD card, the SDK image from the existing store, Steam step on a copy of
  `shortcuts.vdf`, desktop entry into a scratch `XDG_DATA_HOME`): toolchain, disc, icon, regen 3:51,
  then build in the container, package, desktop, Steam 2:02, `RESULT OK`; the live `shortcuts.vdf`
  was byte for byte unchanged. On a **Linux desktop**, from the ISO (716 files): the same three
  platforms; the toolchain step installed capstone with pip and pulled the sniper SDK image
  (3.7 GB of store) in 1:36, disc and regen 1:30, build in the container 28 s, package, desktop and
  Steam on a copy of `shortcuts.vdf` 0:29 in all; the live file was unchanged. Both scratch
  folders were removed.
- Not verified: a visible window under gamescope (no window was mapped on either machine, so as not
  to cover the Steam UI), `--linux-build host` (no machine with a host toolchain), the AppImage on
  Wayland-only desktops other than gamescope's.
- **Windows setup exe** (compiled on a Windows test PC, 12.5 MB): see "The setup exe" below.
- Lesson: a scratch podman store must be removed with `podman unshare rm -rf <store>`, not
  `podman system reset`: `system reset` also clears the user's shared `/run/user/<uid>/libpod/tmp`, which
  makes podman mark the user's other (running) containers as stopped.

## The setup exe, verified on a Windows test PC

With no game running and the user's session idle (nothing visible was started): Inno Setup
6.7.3 installed portable under `bo3-tools\innosetup` (registry keys under HKCU and HKLM, the Start
menu, `%LOCALAPPDATA%\Programs`: unchanged), the setup exe compiled in 6 s. A silent run
(`/VERYSILENT /SUPPRESSMSGBOXES /DIR=<scratch>`) installed 574 files (exe, Qt DLLs, `platforms\qwindows.dll`, the
driver in the payload, the licences, `unins000.exe`), made the per-user Start menu entry (and an
uninstall key under HKCU only), exit status 0, no UAC prompt. The installed exe, started hidden
through a scheduled task in `--check-shot` mode, accepted the extracted disc folder (718 files) and
found the tools. The silent uninstaller removed the folder, the shortcut and the key; Start menu and
uninstall keys were then identical to before. Not run: the desktop-icon task (it would put an icon on the
user's desktop), the uninstaller's message box (silent run), and the visible first-run wizard.

## Shaders, verified

- macOS, from the packaged app, unattended, from the ISO into scratch folders with the shader cache
  and the saves in scratch too (`--driver-arg=--shader-cache=...`, `RECOMP_RENDER_SHADER_CACHE`,
  `RECOMP_SAVE_DIR`), no window: the whole install with the graphics preparation on took 8:32, the shaders step
  264 s ("29 vertex, 329 fragment, 71 pipelines; 527055 bytes"), `RESULT OK`. Running `--only shaders`
  again was a skip in under a second. The player's own `~/Library/Caches/burnout3/metal_pipelines.bin`
  was byte-identical in size and time, and `~/Library/Application Support/Burnout3` was not created.
- Failure and cancel paths against a fake game (`tools/install/tests/test_shaders.py`) and the window
  against the fake driver (`shaders-fail`, `shaders-hold`): the row turns amber, the install stays complete.
- Not run: the shader step on Linux or Windows (the game's own pre-build was measured on both by BO-93;
  the installer's step was only run on the Mac), and a failed pre-build of the real game.

## Tests

- `protocol_test` (Qt Test, in the build folder): the protocol parser and the
  wording tables.
- `tests/fake_installer.cpp` stands in for the game's executable and answers the
  window's command lines with the installer's protocol. The disc's file name picks
  the outcome: `<code>.iso` fails the check with that code, `install-<code>.iso`
  fails the install, `hold.iso` waits to be cancelled, `crash.iso` ends without a
  `RESULT`. `window_test` (Qt Test) runs the window against it: every failure
  code, the stop and the resume, and the Play button.
- `--shots <dir>` (with `QT_QPA_PLATFORM=offscreen`, so nothing appears on a screen)
  drives the window through every state against the fake installer named by `--program` (a fresh window,
  valid and refused discs, advanced options, missing tools, a held build that is
  stopped, a resume into a build error with the dialog and its details, a missing
  tool at start, finished, update, about) and saves each with `QWidget::grab()`.
  `BO3_GUI_FORCE_LINUX=1 B3_FAKE_LINUX=1` shows the Linux options on another system.
  Test and unattended modes (any of `--shots`, `--check-shot`, `--show-grab`,
  `--grab`, `--path`, `--write-icon`, or `BO3_INSTALLER_TEST=1`) arm, before Qt
  starts, a message handler and signal handlers that end the process by itself
  (status 3 on a fatal Qt message such as a missing platform plugin, 4 on a crash),
  so the system's crash dialog never appears. Test windows are rendered with
  `WA_DontShowOnScreen`.

## Not done

- Linux and Windows packages are written but not run on those systems; the window
  has not been run on a Steam Deck or on Windows (only rendered with their options
  shown, against the fake driver).
- The payload copy is a stand-in for fetched sources.

