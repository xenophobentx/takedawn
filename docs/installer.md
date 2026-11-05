# Installer (BO-92)

> Status, 2026-10-12: this describes the source installer (recompile and build
> on the player's machine). Since the 2026-10-11 decision the releases ship the
> prebuilt program with the compiled game code, and the setup only checks the
> disc and installs its data; packaging, setup and updates are in
> [release.md](release.md) (BO-103, BO-100) and the window in
> [installer-gui.md](installer-gui.md).

The port cannot ship the game's data, the recompiled C, or a binary built from
it: all three are the game. So, like Xerenge's installer for Burnout Revenge,
this one runs on the player's machine. It takes the player's own disc image (or
an already extracted folder), recompiles and builds there, and produces an app
whose icon is taken from the player's own `default.xbe`. Nothing it makes is
meant to leave that machine.

This slice is the core and macOS packaging of the source installer. The driver
speaks a line protocol a GUI can sit on (below); the setup window described
later in this page talks to the game's executable, not to this driver. Everything is under
`tools/install/`.

```
python3 tools/install/install.py --source "Burnout 3 - Takedown (USA).iso" \
        --toolkit ../xboxrecomp-apex
```

Result: `~/Applications/Burnout 3 Takedown.app`, the disc's files in
`~/Library/Application Support/Burnout3/Game/`. Add `--nice 19 --jobs 4` to keep
a machine usable while it builds.

## What it does

| step | what | skipped when |
|---|---|---|
| `toolchain` | checks for everything below and says what is missing and how to get it | never (it is quick) |
| `disc` | identifies the image or folder, refuses any `default.xbe` that is not the supported build, extracts or copies the files to `<root>/Game/Burnout 3 Takedown/` | the installed `default.xbe` hashes to the supported build and the stamp says it was installed |
| `icon` | `$$XTIMAGE` of that XBE to PNG, `.icns`, `.ico` | icons exist for this XBE hash |
| `regen` | `regen.sh` (xbe_parser, disasm, func_id, abi_analysis, recomp) into `src/game/recomp/gen/` | generated C exists and the stamp (XBE hash + the toolkit's recompiler sources + `regen.sh` + `config/*.json` + `recomp_manual.c`) is unchanged |
| `build` | CMake + Ninja, Release | Ninja has nothing to do |
| `package` | the `.app` | binary, icon and root unchanged |

State is `<root>/install/state.json`, logs are `<root>/install/logs/<step>.log`,
icons are `<root>/install/icons/`. An interrupted extraction resumes: every file
is written as `name.part` and renamed when whole, and a file that is already
there with the right size is kept. `--force regen,build` redoes steps, `--only`
and `--from` select them, `--check-disc <path>` only identifies a disc.

`regen.sh` and the CMake project need `<work>/Burnout 3 Takedown/default.xbe`
(a dev worktree has a symlink there). The `regen` step makes the same symlink
to `<root>/Game/Burnout 3 Takedown`; it leaves an existing one alone if its XBE
is the supported one. The source tree it builds in (`--work`) is the checkout
the installer is in; `build/` and `gen/` are gitignored.

## The data installer in the game (BO-103)

Releases ship the built program; only the game's data comes from the player's
disc. The part of the installer above that survives this is the disc step, and
it is now C inside the game's executable (the toolkit's `src/install`, with the
disc's manifest compiled in), so no Python is needed to run it:

```
burnout3 --install <image-or-folder> [--dest <dir>] [--check]
burnout3 --version
```

It reads the same images as `xdvdfs.py` (the same partition offsets) or an
extracted folder, refuses anything whose `default.xbe` is not a supported
build (one of the three discs below: the package's program for that disc
does the install), wants 3 GB free (3.4 GB for a European disc), copies each file through `<name>.part` while hashing
it, compares it with the manifest and renames it, and keeps a journal
(`<dest>/install.journal`), so a run that was killed or cancelled goes on where
it stopped. `<dest>/.complete` (manifest id, program version) is written last.
Files the manifest does not list are not copied. A folder that already is the
destination is checked where it lies. `--check` only says what the disc is.

The manifests are `config/disc/ntsc-u-retail.manifest` (SHA-256, size, path;
716 files, 2,476,107,072 bytes), `pal-retail.manifest` (786 files) and
`pal-ensv-retail.manifest` (761 files), made from disc images by
`tools/release/make_manifest.py`.

One package, three programs (BO-341). Every package has a program for
each disc: the North America one under the package's name (`Takedawn`,
`takedawn`, `Takedawn.exe`), `-pal` (Europe, French, German, Italian) and
`-ensv` (Europe and Australia, English, Spanish, Dutch, Swedish) beside it
([pal.md](pal.md), "Packages"; `src/game/package.h`). Each has all three
manifests compiled in. `--install <disc>` (and `--check`) given to any of them
reads the disc's `default.xbe`, and the program for that disc does the work,
on the same pipe and with the same arguments. A real install writes
`disc.txt` (the build id) in the main program's per-user local folder when
another disc's data is installed too, and its last line says which disc
Takedawn starts from then on and where the other disc's data stays. A start
of the main program becomes the program of the disc installed last (or the
one whose data is whole) before anything else; so does `--data-status`, and
with it the setup window. Each program keeps the folders its package had
before: the European discs `Takedawn PAL` (`takedawn pal` on Linux) beside
the table's `Takedawn`, so no save, setting or install moves. `--install
--check` adds `LOG disc build <id>`, `LOG disc root <dir>`, `LOG disc place
saved|default|named`, `LOG disc default <dir>`, `LOG disc bytes <n>` and `LOG
disc free-needed <n>`: where that disc's program would put it; `--data-status`
adds `LOG data build <id>`. A folder the game has run in holds two more
files, `CACHE/LocalCache*.bin`, which the title writes itself.

stdout is the protocol below with the one step `disc`; `RESULT OK <dest>`.
Failure codes: `no-source`, `source-unreadable`, `not-xbox-image`,
`xbox360-disc`, `no-xbe`, `wrong-game`, `wrong-version`, `truncated-image`,
`damaged-source`, `disk-full`, `no-permission`, `write-failed`, `cancelled`,
`internal`; exit status 1 (2 for a bad command line). To stop it: end the
process (SIGINT and SIGTERM are answered with `cancelled`) or create
`<dest>/install.cancel`.

Where the game looks for its data (`src/game/install.h`), first match wins:

1. a `default.xbe` named on the command line;
2. `--data <dir>`;
3. `TAKEDAWN_DATA`;
4. the working directory when it holds `Burnout 3 Takedown/default.xbe` (a
   checkout; no marker is asked for);
5. the folder chosen in the setup window (BO-197): the first line of
   `data-folder.txt` in the per-user local folder, which is
   `%LOCALAPPDATA%\Takedawn` on Windows and the save folder (the middle column
   below) on macOS and Linux. `--install --dest <dir> --remember` writes it
   before the copy starts, so a stopped run resumes there; the default root
   removes it. Not on Android;
6. the per-user root below.

`--install` without `--dest` uses `TAKEDAWN_DATA`, then case 5, then case 6.
A case 5 folder that is not there gets its own sentence ("is not there. If it
is on a drive that is not connected, ...") and the setup window. The per-user
roots:

| | data root | saves, settings, logs | shader cache |
|---|---|---|---|
| macOS | `~/Library/Application Support/Takedawn/Game` | `~/Library/Application Support/Takedawn` | `~/Library/Caches/Takedawn` |
| Linux | `$XDG_DATA_HOME/takedawn/Game` (`~/.local/share/takedawn/Game`) | `$XDG_DATA_HOME/takedawn` | `$XDG_CACHE_HOME/takedawn` (`~/.cache/takedawn`) |
| Windows | `%LOCALAPPDATA%\Takedawn\Game` | `%APPDATA%\Takedawn` | `%LOCALAPPDATA%\Takedawn\cache` |

The European discs' programs: the same with `Takedawn PAL` (`takedawn pal`
on Linux, where the shader cache stays `takedawn` for all three).

Without a whole install there the title is not started: exit status 3 and
`STEP data FAIL not-installed <text>` / `RESULT FAIL data not-installed` on
stdout (`unverified` when the files are there without the marker, as after the
installer above: `--install` from that folder into the same root checks them
in place).

A start by a person (not `--no-setup`, `--headless`, `RECOMP_HEADLESS`,
`TAKEDAWN_NO_SETUP`; on Linux, with a display) that ends here and has no setup
program to hand over to shows a message box with the same text and the
`--install` line (`src/game/fatal.c`). The same box (`bo3_fatal`) reports the
other start-up failures: the game's `default.xbe` cannot be read, and the
memory the game needs cannot be reserved; it says what failed, what to do and
where the log is (`run.log` in the save folder's `logs`, `takedawn.log` on
Android). A headless run prints to stderr only.

The folders of the port's earlier name are moved once, at the first start,
when the new one is not there yet (never over anything, nothing is deleted):
`Burnout3` / `burnout3` to `Takedawn` / `takedawn` for saves and settings,
`burnout3` (Windows: `xboxrecomp\cache`) for the shader cache. Off Windows the
old save folder name stays as a link to the new one, so the app this installer
made, and a build from before the new name, keep finding it. The Python driver
described in the rest of this document still installs into `Burnout3`; a release
does not use it.

`burnout3 --data-status [--data <dir>]` answers, without starting anything,
where the data is (or would be put) and whether it is whole: `LOG data root
<dir>`, `LOG data place named|saved|default|checkout` (which case above gave
it), `LOG data default <dir>` (case 6), `LOG data files|bytes|free-needed <n>`, `LOG data disc|version <text>`,
then `STEP data OK ...` / `RESULT OK <root>` (status 0) or `STEP data FAIL
not-installed|unverified <text>` / `RESULT FAIL data <code>` (status 3).

`enhance.toml` is read from the per-user settings folder (the middle column
above, or `RECOMP_SAVE_DIR`) when there is one, else from beside the executable
(a checkout's `bin/`, a portable copy). A release's program folder is read-only
(a macOS bundle, Program Files, an AppImage), so the player's file is the first.
`RECOMP_ENHANCE_CONFIG` names another.

## The setup window (BO-103)

`tools/install/gui` is the front end of that installer: the program
`takedawn-setup` (Qt 6 Widgets). It needs no Python, no compiler and no
download; everything it does is one of three command lines of the game's own
executable, whose STEP / PROGRESS / RESULT lines it follows:

| when | command | what the window shows |
|---|---|---|
| it opens | `<game> --data-status [--data <dir>]` | where the data goes, the free space; installed: Play at once |
| a disc is dropped or chosen | `<game> --install <disc> --check` | the disc card: accepted, or why not |
| Install / Resume | `<game> --install <disc> [--dest <dir>]`; with a folder chosen by Change... (or one chosen before) `--dest <dir> --remember` | the progress, the file being copied |
| Change... picks a folder | `<game> --data-status --data <dir>` | its free space; a folder that holds a whole install is checked in place (`--install "<dir>/Burnout 3 Takedown" --dest <dir> --remember`) and kept |
| an older install without `.complete` (`unverified`) | `<game> --install "<root>/Burnout 3 Takedown" --dest <root>` | started by itself, with a note: checked in place, nothing copied, no disc needed |
| "Prepare graphics for smooth first races" is ticked (it is by default) | `<game> [--data <dir>] --no-setup --build-shaders` | a second row; a failure is a warning, the install stays whole |
| Play | `<game> [--data <dir>]`: on Linux the window's process becomes the game (`execve`), elsewhere the game is started and the window closes | |

Stop writes `<dest>/install.cancel` (`<cache>/prebuild/cancel` for the shader
build) and, if the program has not ended after 2.5 s, ends it. Resume runs the
same command again: the journal keeps what was copied. Each failure code of
`--install` has its own headline and advice (`messages.cpp`); the installer's
own sentence is the tooltip and is in "Show details" and "Copy details". A
disc that cannot be used is said on the disc card; a failure of the copy is
said in a note above the progress row.

Where the data goes (BO-197). "Installs to" has Change... and, when the
folder is not the default, Default. Change... is the system's folder dialog,
or on Linux and with a controller the built-in list in its folder mode
("Install here" on top, no disc images). A folder with other files in it gets
a `Takedawn` folder inside; an empty one, one named Takedawn, or one that
holds an install is used as it is (picking its `Burnout 3 Takedown` folder
means its root). Refused, with a note: not a whole path, a file, inside the
program's folder (the window's and the game's, a macOS bundle, an AppImage's
`APPDIR`, on Windows also `<folder>.prev`), not writable (a temporary folder
is made and removed where the data folder would be made). Asked first: a
network drive (Windows `DRIVE_REMOTE`; nfs, smb, afp, webdav, sshfs) and a
removable or external one (Windows `DRIVE_REMOVABLE`; macOS `/Volumes/...`;
Linux `/media`, `/run/media`, `/mnt`). The free space line and the Install
button follow the chosen disk. With `--data` or `TAKEDAWN_DATA`
(the window got `--dest`) there is no Change...: the game would not look
anywhere else. Nothing moves the files: to move an install, move its folder
and pick it with Change....

How the game opens it. A person who starts the game without a whole install
gets the window instead of a silent exit: `bo3_data_enter` (`src/game/install.c`)
looks for `takedawn-setup` in the executable's own folder (`TAKEDAWN_SETUP`
names another) and becomes it (`execv`, so a launcher keeps one process to
watch) with `--program <the game> [--dest <root>]`. Not with `--no-setup`,
`--headless`, `RECOMP_HEADLESS` or `TAKEDAWN_NO_SETUP`, not on Linux without a
display, and not on Windows or Android, where the window is not built yet:
there the `RESULT FAIL data ...` line and exit status 3 are the answer. The window finds the game the other way round: `--program`, else
`Takedawn` / `takedawn` / `burnout3` beside itself, else `bin/` of the checkout
it was built in.

Without a mouse: controller, touch, gamescope. A Steam Deck in Game Mode
has a controller and a touch screen and nothing else, and a person who starts
Takedawn there without data must be able to finish the install.

- Controller (`pad_input.cpp`, `pad_nav.cpp`): the window reads every
  controller with SDL's game controller API, the SDL the game carries (Linux
  builds; `-DTAKEDAWN_SETUP_GAMEPAD=ON` elsewhere, off by default so the macOS
  and Windows packages carry what they did). D-pad or left stick: the focus
  goes to the nearest control that way (the page that scrolls is treated as
  rolled out, and follows the focus). A: press, tick, open. B: back in the disc
  browser, close a box. Start: the main button (Install, Stop, Resume, Play).
  Shoulder buttons: scroll the list, the log or a licence. The focus is drawn
  by a mark of its own in the steel blue (`padFocus` in `theme.cpp`) and does
  not depend on which window the system thinks is active, and the foot of the window says
  what the buttons do ("A Select", "B Back", "Start Install"); it appears with
  the first controller and is always there under gamescope. Only the window in
  front is driven. `--no-pad` reads no controller.
- The disc browser (`disc_browser.cpp`), Linux: "Choose disc..." opens a page
  of the window itself instead of Qt's file dialog: Home, Downloads, every
  mounted card and drive (`/run/media/<user>/<label>`, `/run/media/<label>`,
  `/media`, `/mnt`), and the Xbox folders of EmuDeck and RetroDECK
  (`Emulation/roms/xbox`, `retrodeck/roms/xbox`) when they exist. It lists
  folders, disc images (`.iso`, `.xiso`) and extracted discs (a folder with a
  `default.xbe`: "disc files: choose"; inside one, "Use this folder"), in rows
  48 px high. "System file chooser..." is still there for a mouse (not under
  gamescope). macOS and Windows keep their own dialogs.
- The disc found by itself: at the start, when nothing is installed and no
  disc was named, the same places are searched a few levels deep (breadth
  first, at most 6000 directory entries, hidden folders and Steam's own left
  out, links to folders followed once) for extracted discs and for images of
  400 MB and more; the ones named Burnout first. The game checks up to four of
  them with `--install <path> --check`, and the first it accepts is offered:
  "Found Burnout 3: Takedown / <path> / Use it?" with "Use it" and "Choose
  another...". Nothing is used unasked. `TAKEDAWN_SETUP_NO_SEARCH=1` leaves the
  search out.
- Under gamescope (`GAMESCOPE_WAYLAND_DISPLAY`, or `XDG_CURRENT_DESKTOP`
  `gamescope`; `--couch` and `TAKEDAWN_SETUP_COUCH=1/0` say it outright): the
  window fills the screen without a frame, is scaled so that it is laid out 640
  units high (`QT_SCALE_FACTOR` 1.25 on the Deck's 1280x800; one already set is
  kept), and every control is 44 px and more for a finger. A tap is a click.
- Tests: `tests/pad_test.cpp` (the key repeat from made-up controller states;
  the places and the search over a made-up home and SD card; the window driven
  by presses alone from the first screen to Play; every control reachable and
  finger sized). `--pad-shots <dir>` renders that flow.
  `TAKEDAWN_SETUP_PAD_SCRIPT=<steps>` drives the real program on the real
  screen with a controller made inside SDL (`shots.h` lists the steps);
  `TAKEDAWN_SETUP_TRACE=1` prints the controllers SDL sees, what the search
  found and every press.
  `RECOMP_DEBUG=window_display=<n>` opens the window centred on display n
  (0 = the main one), as it does the game's, and Play hands it on: tests on a
  machine with two displays keep both windows on the one they name.

How it ships (macOS). One bundle with both executables:

```
Takedawn.app/Contents/
  Info.plist                CFBundleExecutable Takedawn
  MacOS/Takedawn            the game
  MacOS/takedawn-setup      the setup window
  Frameworks/               QtCore, QtGui, QtWidgets, QtDBus (and the game's own libraries)
  PlugIns/platforms/libqcocoa.dylib, PlugIns/styles/libqmacstyle.dylib
  Resources/Takedawn.icns, qt.conf, licenses/, enhance.toml.default
```

`tools/install/gui/packaging/make-macos-bundle.sh <out dir>` puts it together
from a built game and a built window (Qt is deployed in a staging bundle where
the window is the main executable, because `macdeployqt` resolves Qt through
the main executable's library paths; then thinned to the game's architecture).
With Qt 6.10.3 from qt.io that is 21.7 MB of Qt beside a 20 MB game. A
Homebrew Qt drags ICU along and comes to 69 MB: do not package with it.

Look. The colours are the "game" table of the toolkit's settings overlay
(`src/overlay/overlay_ui.cpp`, `k_theme_game`), mirrored by name in
`theme.cpp` and nowhere else. The icon is `assets/icon/takedawn.svg`, rendered
to PNG when the window is built (`render_icon.py`: resvg, rsvg-convert or the
`resvg_py` module; `-DTAKEDAWN_ICON_PYTHON=` names a Python that has it). Qt's
own SVG reader is not used: it ignores the clip path.

Build and test.

```
cmake -S tools/install/gui -B tools/install/gui/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=<Qt 6 kit> [-DTAKEDAWN_ICON_PYTHON=<python with resvg_py>]
cmake --build tools/install/gui/build
ctest --test-dir tools/install/gui/build --output-on-failure
```

`protocol` is the line parser and the wording (every code has its own);
`window` drives the real window, offscreen, against `tests/fake_installer.cpp`:
a good disc, each of the failure codes at the check and in the copy, stop and
resume, a program that ends without an answer, an unverified install, a damaged
one repaired from the disc, a named data root, the shader build. Pictures of
every state: `QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=2 takedawn-setup
--program build/fake_installer --size 780x790 --shots <dir>`; with the real
game, `--iso <image> --e2e-shots <dir>` installs into `--dest` or the per-user
folder (run it with a scratch `HOME`).

The Steam "add to library" box of the source installer is in the window on
Linux: `steam_shortcut.cpp` is `tools/install/steam.py`'s shortcut writer in
C++ (see [release.md](release.md), "The Steam box").

## Disc input

`xdvdfs.py` reads the Xbox DVD filesystem (format notes in its docstring). It
looks for the volume descriptor ("MICROSOFT*XBOX*MEDIA" at sector 32 of the game
partition and again at 0x7EC) at the known partition offsets: `0` (xiso, which is
what the test disc is), `0x18300000` (redump: video partition first), `0x0FD90000`,
`0x1FB20000` and a few odd ones. A redump with the video partition is covered
by a synthetic test (`tools/install/tests`); the real test image is a plain
xiso. File names from the image are treated as data: separators, `..` and
`:` are refused, so a hostile image cannot write outside the game folder.

An extracted folder may be the game folder itself or wrap it one level down.

Supported builds, recorded in `tools/install/supported.json` and nowhere else:
Burnout 3: Takedown, NTSC-U (North America) retail, title id 0x4541005B,
`default.xbe` 4,308,992 bytes,
`sha256 9f497acd82adb8edfb72cc31aeeb35010b45d32b13a3f1fc6540717ff24d13f9`;
and the two European retail discs ([pal.md](pal.md)), each its own program.
The recompiler's seeds, the spin-wait list and the entry point (0x001D2807) in
`src/game/main.c` are addresses in one exact file, so each disc has its own
program, and a Japanese disc (or a patched XBE) is refused with its title,
region and hash, not tried. To support another build: add an entry and verify
the regen against it.

An Xbox 360 image (as the Burnout Revenge ISOs here) is recognised and refused
with "has a default.xex".

## Icon

`$$XTIMAGE` is an XPR0 resource: `XPR0`, total size, header size (0x800), then a
Direct3D texture header (format dword: bits 8..15 = D3DFORMAT, 20..23/24..27 =
log2 width/height) and the pixels. For Burnout 3 it is 128x128 DXT1; DXT3 and
DXT5 are decoded too. Compressed Xbox textures are not swizzled, so this is the
plain PC block decode. `$$XSIMAGE` is the same with 64x64 (the save image).

Outputs in `<root>/install/icons/`: `title-128.png` (the decoded image),
`icon-64.png`, `icon-256.png` (upscaled; the source is only 128), `AppIcon.icns`
(16..256 incl. @2x, PNG payloads), `AppIcon.ico` (16, 32, 48, 64 as 32-bit
bitmaps, 128 and 256 as PNG), `save-64.png`. The title image has a black
background (DXT1 without alpha), so the icon is the logo on black.
Dependencies: none beyond the Python standard library (zlib for PNG, own resize);
the repo's `.venv` has Pillow but it is not needed.

## macOS app

```
Burnout 3 Takedown.app/Contents/
  Info.plist              bundle id org.burnout3recomp.takedown (placeholder)
  MacOS/Burnout 3 Takedown   launcher script (CFBundleExecutable)
  MacOS/burnout3             the binary built on this machine
  Resources/AppIcon.icns     from the user's XBE
  Resources/enhance.toml.default
  Resources/install.conf     root=<where the data is>
```

The game reads `Burnout 3 Takedown/` from its working directory, keeps saves in
`~/Library/Application Support/Burnout3` (profile, `Partition*.img`, backups,
crash reports, `settings.toml` of the in-game overlay) and `enhance.toml` beside
the executable, which inside a signed bundle is read-only. The launcher
therefore: reads the root from `install.conf` (`BURNOUT3_ROOT` overrides), shows
a dialog if the data is gone, copies `enhance.toml.default` to `<root>/enhance.toml`
once and points `RECOMP_ENHANCE_CONFIG` at it, `cd`s to `<root>/Game` and
`exec`s the binary with the arguments it got (`--headless` works). The root
defaults to the save folder, so one folder holds data, saves and settings:
uninstalling is deleting the app and that folder. `RECOMP_SAVE_DIR` still moves
the saves.

### Self-contained: what is bundled and what is not

`bundle.py` copies every non-system library the binary links (and theirs,
recursively) into `Contents/Frameworks`, rewrites the install names to `@rpath`
(the executable gets the rpath `@executable_path/../Frameworks`, the libraries
`@loader_path`), signs ad hoc libraries first and the bundle last, and audits
that `otool -L` of the binary and every bundled library names nothing outside
`/System`, `/usr/lib` and `@rpath`. The step fails with `bundle-failed` otherwise.

The first Metal build linked six Homebrew libraries; only two are needed at run time:

| linked | needed on macOS/Metal? |
|---|---|
| sdl2-compat (libSDL2) | yes, bundled |
| libSDL3 | yes: sdl2-compat `dlopen`s `libSDL3.dylib` from `@loader_path`/`@executable_path`; bundled by name under that file name (found by reading the string in libSDL2) |
| libcrypto (OpenSSL) | no: the final binary imports no OpenSSL symbol; dropped at link time |
| libepoxy | no: the toolkit links it on macOS (pkg-config `REQUIRED`) but imports no symbol from it; dropped at link time |
| glslang, spirv-tools (2 libs), vulkan-loader | only by the Vulkan backend (shaders to SPIR-V, MoltenVK as a test bed); the default and only shipped backend on the Mac is Metal |

The driver gets there without touching the toolkit's CMake: it configures with
`-DXBOXRECOMP_VULKAN=OFF` (which the toolkit offers; compiles the Vulkan backend
out) and `-DCMAKE_EXE_LINKER_FLAGS=-Wl,-dead_strip_dylibs` (ld drops libraries
nothing uses). So the packaged app has no `RECOMP_PB_BACKEND=vulkan`. libepoxy is
still a build-time requirement (the toolkit's configure demands it). The result
is two libraries, 3 MB. Verified: `otool -L` is clean for the binary and both
libraries, `codesign --verify --deep --strict` passes, and a headless run under
`sandbox-exec` with `/opt/homebrew` and `/usr/local` unreadable (and with
`DYLD_PRINT_LIBRARIES=1` set inside the sandbox) boots and presents frames, and
the only non-system images dyld loads are `burnout3`, `libSDL2-2.0.0.dylib` and
`libSDL3.dylib` from inside the bundle. The unit test builds a two-level fake
Homebrew, bundles it, deletes the original and runs the result.

The bundle is signed ad hoc (`codesign -s -`); a local build is not quarantined,
so Gatekeeper does not object. The app is not portable between Macs of different
architecture or OS floor, but needs nothing from Homebrew to run.

## Toolchain per platform

| | macOS | Linux | Windows |
|---|---|---|---|
| compiler | Command Line Tools clang | podman + the sniper SDK image (default), or host clang/gcc | portable llvm-mingw 20260922, fetched into the tools dir |
| build | cmake 3.20+, ninja | same | portable cmake + ninja zips, fetched |
| recompiler | Python 3.10+ with `capstone` | same | the driver's own Python (the installer package carries an embeddable one) + the capstone wheel unpacked into `<root>/install/site` |
| libraries | Homebrew: sdl2 (sdl2-compat) with sdl3, libepoxy, openssl@3 (build time; the app carries only SDL) | in the container; host mode: sdl2, libepoxy, vulkan, openssl dev packages | SDL2 (mingw dev package), Vulkan headers + import lib, glslang built static: all fetched into the tools dir |
| disk | ~8 GB free checked | same | same |

On macOS and Linux the toolchain step reports every missing item as a `MISSING` line
with the command to get it and installs nothing but Python's `capstone`; on Windows it
fetches the portable tools. Python's `capstone` (macOS, Linux) is installed by the
driver with pip (network) into `<work>/.venv` if that exists (it is what `regen.sh` uses),
else into a private venv `<root>/install/venv`; `--no-pip` only reports it. Nothing else is fetched.

## Linux, Steam Deck, Bazzite (BO-66)

Implemented in `linux.py` and `steam.py`; tested on the Steam Deck (below).

Build in the Steam Runtime sniper SDK container. The generated C cannot be
shipped, so the machine compiles, and SteamOS has no compiler. As the Deck build
already does, `regen` runs on the host (plain Python + bash) and `build` runs in
`registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest` under podman: the
work tree is mounted at `/work`, the toolkit read-only at `/toolkit`, `cmake`
and `ninja` run inside (`CC=clang`), the binary lands in `<work>/build/linux/bin`.
The binary then runs on SteamOS and Bazzite (SDK glibc and libstdc++ are older).
The toolchain step checks podman and that the image exists, and pulls it when it
does not (several GB; `--no-pull` stops that). `--podman-root` and
`--podman-runroot` use an existing image store (podman refuses a store with a
different run root; the Deck's store lives on the SD card). Second choice,
`--linux-build host`: clang or gcc, cmake, ninja, pkg-config and the dev
libraries (sdl2, libepoxy, vulkan, openssl), reported as `MISSING` lines with the
distro's install command. `auto` takes the container when podman exists.

Package (`--app-dir`, default `<root>/app`):

```
burnout3         the binary
lib/             libraries the host loader cannot find, copied out of the SDK
                 image (on the Deck: libcrypto.so.1.1; found with ldd, not hard-coded)
burnout3.sh      launcher: LD_LIBRARY_PATH, enhance.toml (copied once to <root>), reads
                 <root>/launch.env (KEY=VALUE lines) for tuning, cd to <root>/Game, exec
install.conf     root=<where Game/ and enhance.toml are>
burnout3.png     the icon from the user's XBE (256x256)
burnout3.desktop desktop entry with absolute paths
```

Data, saves and settings: `<root>` defaults to `~/.local/share/burnout3`, which is
also the game's own save folder (as on the Mac).

Opt-in steps, off unless asked for (`--desktop`, `--steam`, `--shaders`, or `--only`):

- `desktop`: copies the entry and icon into `$XDG_DATA_HOME` (`--xdg-data-home`),
  `applications/burnout3-takedown.desktop` and `icons/hicolor/256x256/apps/`.
- `steam`: adds a non-Steam game to `userdata/<id>/config/shortcuts.vdf`
  (Exe = the launcher, StartDir = the app dir, icon = the art) and writes art from
  the user's own title image into `config/grid/`: `<id>_icon.png`, `<id>p.png`
  (600x900), `<id>.png` (920x430), `<id>_hero.png` (1920x620), the logo on black,
  placeholders for art the user may replace. Steam rewrites the file when it
  exits, so the step refuses while Steam runs (`steam-running`); with several Steam
  users it takes the most recently logged-in one (`config/loginusers.vdf`: `MostRecent` = 1,
  else the highest `Timestamp`; SteamID64 minus 76561197960265728 is the userdata folder)
  and logs which; `--steam-user <id>` overrides, and `steam-user-ambiguous` is only for when
  loginusers.vdf cannot decide (checked against the Deck's real file: it picks the right account); `--steam-config <dir>`
  names a config folder explicitly (used for tests, skips the running check).
  Existing entries are never changed (the new one copies the field set of the
  last existing entry), an entry with the same name and Exe is left alone, and the
  file is backed up (`shortcuts.vdf.bo3-backup-<time>`) first. The binary VDF
  reader/writer round-trips the Deck's real 132-entry file byte for byte.

The installer never resets, prunes or removes anything in podman: containers run with `--rm`
and images are only pulled, never deleted, and it touches no store other than the one it is
given (`--podman-root`, or podman's own).

- `shaders` (all systems, BO-93): the installed game builds every shader and pipeline it needs from the
  shipped coverage manifest (`burnout3 --build-shaders`, `docs/shader-prebuild.md`; a few seconds,
  ~2 MB; since BO-208 no scene is played). It is started through the launcher the package step wrote (macOS: the bundle's own
  executable, because Metal's compiled cache belongs to the bundle id; Linux: `burnout3.sh`;
  Windows: `burnout3.exe` with `<root>\Game` as the working directory), with `--cache-dir` when
  `--shader-cache <dir>` is given (tests give a folder of their own; otherwise the game's own cache).
  Its PROGRESS and LOG lines are forwarded under the step. It is never forced and it never fails
  the install: the game works without it. Any failure (`cache-off`, `no-game-data`, `scene-failed`,
  `cancelled`, or the game ending with an error or without a word: `game-failed`, `no-result`,
  `game-not-started`) is reported as `STEP shaders FAIL <code> <text>` plus a `WARN` line and the run goes
  on to `RESULT OK`; the step can be run again (`--only shaders`: the game skips the scenes it has
  finished). A missing installed game is a real error (`no-package`). Stopping the installer
  (SIGTERM/SIGINT) creates the game's `<cache>/prebuild/cancel` file and ends the game; killing the process
  group, as the GUI's Cancel does, ends it too. Tests: `tests/test_shaders.py` (a fake game that
  replays the protocol: built, already built, scene-failed, cache-off, cancelled, silent, crashing,
  and the installer stopped).

No AppImage: what this installer makes is built from the user's
disc and cannot be handed around, so an AppImage's one advantage (one file to
distribute) does not apply. Being portable would also mean bundling SDL2 and the
Vulkan loader, and building it needs appimagetool (a download), for the same
result as this directory plus a desktop entry and a Steam shortcut. (The Qt
installer itself may still be an AppImage, as Xerenge's is.)

## Windows (BO-75)

Implemented in `win.py`; the driver runs natively on Windows under any Python
3.10+ (the installer package is to carry an embeddable one). Nothing is
installed system-wide and no PATH is changed.

- Portable tools, in `--tools-dir` (default `<root>/tools`): llvm-mingw
  (the Windows x86-64 UCRT zip, 20260922, the version the repo's toolchain file is
  pinned to), CMake 3.31.6, Ninja 1.12.1, SDL2 2.32.10 (mingw development package),
  the Vulkan headers plus an import library for `vulkan-1.dll` made with dlltool from
  the names the headers declare (the loader is the GPU driver's), and glslang 16.6.0
  built static once with the same llvm-mingw. This is `tools/win/fetch-deps.sh`
  redone in Python. Found there when present (a directory you prepared works), else
  fetched over https; `--no-fetch` only reports. Every download is checked against the
  sha256 in `pins.json` (recorded when the URLs were first fetched) and unpacked with
  path-escape and link checks. The tools are put on PATH for the processes the
  installer starts, not for the system.
- capstone: the PyPI wheel (a zip) is unpacked into `<root>/install/site`; no
  pip. `regen.py` puts it on `sys.path`.
- regen without bash: `regen.py` is `regen.sh` in Python, stage for stage and
  flag for flag, started with `runpy` so an embeddable Python works. It is used on
  every platform (the Mac and Linux runs went through it). Its generated C
  was compared with `regen.sh`'s on the Mac: all 29 files identical.
- Build: the repo's Windows build (`docs/windows.md`) done natively: the
  `cmake/llvm-mingw-x86_64.cmake` toolchain file, same options as
  `tools/win/build-mingw.sh`, `SDL2.dll` beside the exe. The user's icon is in the
  exe: the installer compiles `1 ICON "AppIcon.ico"` with llvm-mingw's `windres` and
  puts the object on the link line (`CMAKE_EXE_LINKER_FLAGS`), so no CMake file
  changes. Two Windows details: `Popen` finds a bare program name on the parent's
  PATH, not the child's, so the tools are started by full path; and CMake does not
  find SDL2 and glslang through `CMAKE_PREFIX_PATH` once the toolchain file has set
  `CMAKE_FIND_ROOT_PATH` with drive-letter paths, so `SDL2_DIR` and `glslang_DIR` are
  passed outright.
- Install folder (`--app-dir`, default `<root>/app`): `burnout3.exe`, `SDL2.dll`,
  `burnout3.ico`, `enhance.toml` (the exe reads it from beside itself; kept if
  edited), `Burnout 3 Takedown.lnk` (target the exe, working directory
  `<root>/Game` where `Burnout 3 Takedown\` lives, icon the user's .ico) and
  `uninstall.txt`. Saves stay in `%APPDATA%\Burnout3`.
- Opt-in `desktop` step (`--desktop`): copies the `.lnk` to the Start menu and the
  Desktop (`--start-menu-dir`, `--desktop-dir` to redirect).
- Uninstall: nothing is registered, so `uninstall.txt` lists what to delete (the
  install folder, the root, the shortcuts, optionally the tools) and where the saves
  are, which it leaves alone.

## Protocol

stdout of `install.py` carries only these lines; tool output goes to the log files.
Fields are separated by single spaces, the last one is free text.

```
PROTOCOL 1
PLAN toolchain disc icon regen build package [desktop] [steam] [shaders]     (steam: Linux only)
STEP <step> BEGIN <description>
PROGRESS <step> <0.000-1.000> <text>          throttled to about one per percent
LOG <step> <text>          WARN <step> <text>
MISSING <step> <what> <how to get it>
STEP <step> SKIP <why>     STEP <step> OK <summary>
STEP <step> FAIL <code> <message>
RESULT OK <app path>       RESULT FAIL <step> <code>
```

Version 1 is unchanged for existing steps and events. Additions, all backward compatible: the
opt-in steps `desktop` and `steam` (only in PLAN when asked for), and these
failure codes: `steam-running`, `steam-user-ambiguous`, `steam-not-found`,
`no-package`, `lib-copy-failed`, `python-too-old`, `venv-failed`, `pip-failed`,
`bundle-failed`. `RESULT OK` carries the app path on macOS (the `.app`) and the install
directory on Linux and Windows. `desktop` on Windows installs the `.lnk` shortcuts; on
Linux a desktop entry. Also new, additive: `download-corrupt`, `unsafe-archive`,
`glslang-failed`, `vulkan-lib-failed`, `icon-embed-failed`, `shortcut-failed`.

Text a front end may rely on (all `LOG`/`MISSING` lines are free text otherwise):

- The Steam step logs `LOG steam using Steam user <id>: <why>` for the user it chose
  (`<why>` is `the only Steam user`, `most recently logged in (loginusers.vdf)` or
  `--steam-user`); `LOG steam user <id>: <path>` lines list the candidates when it cannot
  decide.
- The toolchain step checks everything in one pass and reports each missing item once as
  `MISSING toolchain <what> <how to get it>` (also with `--no-pip`: a missing capstone is
  listed together with the rest, as `python-capstone`), then fails with
  `toolchain-incomplete` and exit status 3.
- `RESULT OK <path>`: macOS the `.app`; Linux the install directory (it holds
  `burnout3.sh` and `burnout3.desktop`); Windows the install directory, which holds
  `Burnout 3 Takedown.lnk` (the shortcut to run the game), `burnout3.exe`, `SDL2.dll`.

Any exception a step does not handle itself is caught: its traceback goes to `<root>/install/logs/<step>.log` and the
step fails with `io-error` (an OS error: unwritable or missing location, full disk), `bad-xbe` or
`internal-error`, never a bare traceback. Every folder the toolchain step measures (root, work,
app folder, Windows tools folder) is measured through its nearest existing parent, so a folder
that is created later is fine; an unwritable or too-full one is a `MISSING` line (`disk-space`,
`work-folder`, `app-folder`, `tools-space`) and `toolchain-incomplete`.

Failure codes include `toolchain-incomplete`, `no-source`, `no-xbe`,
`unsupported-xbe`, `not-an-xbe`, `bad-image`, `regen-failed`, `configure-failed`,
`build-failed`, `codesign-failed`. Exit status: 0 ok, 1 step failed, 2 usage,
3 toolchain incomplete, 4 unsupported or unreadable disc. A failed tool's last
15 lines are sent as `LOG` and its log path is named.

## Verified

On the M5 MacBook Air (macOS 26.7, Command Line Tools, Homebrew libraries), from a
clean `git archive` of this branch with the disc as a redump-style xiso
(`Burnout 3 - Takedown (USA).iso`, game partition at offset 0): all six steps in one
run at `--nice 19 --jobs 4` while a game was being played: disc 3 s (716 files,
identical to the already extracted folder in the main checkout, apart from the
`CACHE` folder the game itself creates), icon 1 s, regen 233 s (29 source files),
build 170 s (134 Ninja steps), package under 1 s. A second run skips every step in
about 3 s. A headless run of the packaged app (50 s, saves in a scratch folder)
boots through `main`, reads `enhance.toml` from the root, opens the intro movies
from the extracted data and presents frames through Metal.

On the Steam Deck (SteamOS 3.8, Game Mode session idle, no game running, checked
first), from a `git archive` of this branch and of the toolkit, in a scratch root
`~/bo3-installer-test`, with the extracted disc folder as the source and the
existing podman image store on the SD card: toolchain (capstone installed into a
private venv with pip) 7 s, disc copy 5 s (718 files), icon 1 s, regen 206 s, build
in the sniper SDK container 117 s (130 steps, `-j4`, nice 10), package, desktop
entry installed into a scratch XDG dir. The packaged `burnout3.sh --headless`
(50 s, `RECOMP_SAVE_DIR` in scratch) booted on the Vulkan backend (RADV Vangogh),
read `enhance.toml` from the root, opened the intro movies and presented frames; the
binary of this toolkit revision needs no libcrypto 1.1 (the lib-copy path was tested
separately against an older Deck binary: it found `libcrypto.so.1.1` with ldd and
took it out of the image). The Steam step ran on a copy of the Deck's real
`shortcuts.vdf` (132 entries): one entry added, the others byte for byte as before,
art written, a second run did nothing, and with the real config it refused
(it picked the most recent user from the Deck's real `loginusers.vdf` copy, then refused with `steam-running`). The real `shortcuts.vdf` was not
touched. The scratch root was removed afterwards. The sleep inhibitor used was
`systemd-inhibit` around the installer process only, released when it ended.

On a Windows test PC (Windows 11, RX 9060 XT; PowerShell over ssh, installer started detached through
`Win32_Process.Create` so it outlives the ssh session; checked first that no game ran): the
system Python 3.11 ran the driver; tools fetched into `C:\Users\<name>\bo3-tools` (1.2 GB
with glslang built), capstone from its wheel; disc folder copied (the ISO path is untested on
Windows: there was only the extracted folder), icon, regen 159 s, build 31 s (121 steps),
package, Start-menu and Desktop shortcuts into scratch folders. The exe carries the user's icon
(extracted from `burnout3.exe` with `ExtractAssociatedIcon`: the B3 logo), the `.lnk` targets the
exe with the working directory `<root>\Game` and the `.ico`. A 40 s headless run of the packaged
exe (saves in scratch) booted on the Vulkan backend (`AMD Radeon RX 9060 XT`, Vulkan 1.4),
read `enhance.toml` from beside the exe and presented frames. The scratch folder was removed
afterwards; the tools folder was left for reuse.

## Running from a copied source tree

A front end may copy a `git archive` of the game tree and the toolkit to
`<root>/src` and pass `--work` / `--toolkit`; the driver builds in that tree
(`build/` and `src/game/recomp/gen/` inside it) and needs nothing from the repo it was
started in. Checked: macOS with spaces in the work folder, the root and the app
folder (whole pipeline plus a headless run); Windows with spaces and a 138-character
work path (whole pipeline: regen, build, package; longer paths risk Windows' 260-character
limit, and the toolchain step warns above 140); Linux: the container mount of a path with
spaces works (the work tree, toolkit and lib folder are mounted as whole folders). Limits:
a path with a colon breaks podman's `-v`; on Windows the work and tools folders should be
short, and the tools folder is better shared (`--tools-dir`) than copied into every root,
since glslang is built there once.

## User-facing options the driver has that Xerenge's installer does not

Xerenge asks for the disc image, the install folder and a few game settings; the rest is
automatic. The options here, with the reason for each (candidates to hide or drop):

| option | why it exists |
|---|---|
| `--toolkit`, `--work` | the recompiler and the source tree are separate checkouts until the repos are published; a GUI sets them itself |
| `--app-dir` | where the app / install folder goes; Xerenge has one install folder. Could fold into `--root` |
| `--nice`, `--jobs` | keep a machine usable while it builds (used while a game is being tested); a GUI need not show them |
| `--python`, `--no-pip` | pick the interpreter / refuse to download capstone; testing and offline use |
| `--only`, `--from`, `--force` | restart control; the steps already skip themselves, so these are for a GUI's "redo this step" and for debugging |
| `--check-disc` | identify a disc without writing (the GUI's disc picker); Xerenge checks the image hash too |
| `--linux-build`, `--podman-root`, `--podman-runroot`, `--image`, `--no-pull` | build in the SDK container or on the host; the others point at an existing image store (the Deck keeps one on the SD card) or stop a multi-GB pull. Only `--linux-build` is a real choice |
| `--desktop` | install the menu entry / Start-menu and Desktop shortcuts. Xerenge always does; here it is an explicit step on request |
| `--steam`, `--steam-user`, `--steam-config` | the Deck's Steam shortcut with art (Xerenge has none; the Deck needs it). The last two are overrides and test hooks |
| `--xdg-data-home`, `--start-menu-dir`, `--desktop-dir` | redirect where shortcuts go; test hooks |
| `--shaders`, `--shader-cache` | the optional shader pre-build (BO-93): never on by itself; the cache folder is for tests, which must not touch the player's own cache |
| `--tools-dir`, `--no-fetch` | Windows: where the portable tools live / do not download; Xerenge fetches into its install folder |

## Tests

`python3 -m unittest discover -s tools/install/tests` (no game data needed): a
synthetic XDVDFS image at offset 0 and at 0x18300000, resume after an interrupted
file, folder input, an unsupported XBE, path traversal, the XBE header, DXT1
decoding (including the 3-colour transparent mode), icon files.

## Licence notes

Xerenge's repository has no licence file, so no code was copied: the XDVDFS
reader, the XPR0 decoder and the driver are written from the formats and from the
structure described in its README and scripts (a Qt 6 front-end over setup
scripts: verify the image, extract, build, shortcut; AppImage / macOS zip / Inno
wrapper). The toolkit's `tools/xiso` (MIT) does the same job as `xdvdfs.py`.

## Not done in this slice

- No GUI in this slice (the setup window came later), no source or toolkit fetching, no `brew` installation (it reports; `pip install capstone` it does itself).
- No uninstaller (delete the app and the root folder) in this source installer.
  (The packages that replaced it update by a new download put over the old
  one: see "Updating" in [release.md](release.md). This slice's own rule, that a
  changed toolkit or config makes `regen` and `build` redo themselves on the
  next run, belongs to the source installer only.)
- Extraction skips a file whose size matches; only `default.xbe` is hashed. A
  corrupt same-size file from an earlier run is not noticed.
- The bundle id is a placeholder.

## Open decisions for the user

1. GUI toolkit. Xerenge uses Qt 6 (C++, QProcess, linuxdeploy / macdeployqt
   / Inno). The driver already speaks a protocol, so a Qt front-end is a small
   QProcess reader plus a progress bar, and the Xerenge packaging scripts are the
   template, but Qt adds a ~100 MB bundle and a C++ build per platform. Cheaper:
   Tk or a local web page from Python (no new dependency, plain look), or a
   native SwiftUI shell on the Mac only.
2. Where the source comes from once published. Proposal: a pinned
   `tools/install/sources.json` (burnout3 and toolkit, commit + sha256 of the
   tarball) fetched as GitHub tarballs, so no git is needed and a build is
   reproducible; the alternative is `git clone` of the default branches
   (simpler, unpinned). Which toolkit remote: the apexJCL fork lineage this
   branch builds on, or upstream once it has the Metal backend.
3. Toolchain on the Deck. podman with the sniper SDK image (works, needs a
   network pull of a few GB and podman on SteamOS), or a prebuilt toolchain
   tarball the installer unpacks (fast, but a binary I must host and pin), or
   host clang on Bazzite only.
4. Data location on the Mac. Now `~/Library/Application Support/Burnout3/Game`
   next to the saves (one folder, easy to delete, may live on the internal disk
   only). Alternative: an install folder of the user's choice (`--root`).
