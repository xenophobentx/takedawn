# Making a release (BO-103)

A release is a set of prebuilt programs, one per platform, all compiled from
the same generated code. None of them contains game data: the player's own
disc supplies that at first start ([installer.md](installer.md), "The data
installer in the game").

```
tools/release/release.sh 0.1.0
```

builds everything it can reach, smoke-runs it, and leaves in `dist/0.1.0/`
(git-ignored):

| file | made by | on |
|---|---|---|
| `Takedawn-0.1.0-macos-arm64.dmg` | `build-macos.sh` | this Mac |
| `Takedawn-0.1.0-windows-x64-setup.exe` | `build-windows.sh` | cross-built here; the setup compiled on a Windows machine |
| `Takedawn-0.1.0-linux-x86_64.AppImage` | `build-linux.sh` | a Linux machine with podman, in the Steam Runtime sniper SDK container |
| `Takedawn-0.1.0-android-arm64.apk` | `build-android.sh`, `sign-android.sh` | built on the Android build machine, signed here |
| `SHA256SUMS`, `BUILDINFO` | `release.sh` | |

None of these scripts uploads, tags or pushes anything, and none
has a step that could. A person publishes.

Nothing is code-signed on any platform (decided); what the player sees at
first start is under [Unsigned](#unsigned-what-the-player-sees).

## One generated-code tree

The recompiled C is made once, on the Mac, by `tools/release/gen.sh`: the
repository's `regen.sh` with the guest patches (`config/patches.json`) and the
spin-wait list. It then writes `src/game/recomp/gen/regen.stamp`:

```
key e1005046d21e29c8        the regen key (cmake/release_info.cmake)
tree 541c0149...            sha256 over "<sha256>  <name>" of every recomp_* file
files 29
patches 80/80               patch sites that went in / listed, from the recompiler's report
spin_waits 2
```

Every platform compiles this tree and no other:

- the Windows cross build and the Mac build read it in place;
- the Linux build copies it with rsync and compares the tree hash and key on
  the build machine;
- a release build (`-DBO3_RELEASE=ON`) refuses, at configure time, a tree
  without the stamp, with another regen key than this checkout's inputs
  give, with files that do not hash to the stamp, or without the patches;
- the program says what it was built from: `takedawn --version` ends with
  `regen <key>` and `gen <tree> patches <in>/<listed>`, and `release.sh`
  compares those lines between all the artefacts it built.

`tools/install/regen.py` (the old user-side installer's regen) passes no
`--patches`; it is not used for a release, and a tree made by it has no stamp.

The Android build passes `-DBO3_RELEASE=ON` too and gets the same
configure-time checks. The Linux and Android builds compile a copy of the
sources that has no git: the scripts leave a `BUILD_COMMIT` file in each copy
(the toolkit's `cmake/build_id.cmake` reads it), so every program names the
same two commits.

## The name

A release build names the program `takedawn` (`Takedawn.exe`,
`Takedawn.app`). The CMake target is still `burnout3`, and a build without
`-DBO3_RELEASE=ON` is what it always was: `bin/burnout3`. The release build
trees are in `build/release/<platform>/`, so they never replace a developer
binary.

## Settings

Machine names, addresses and paths are not in any committed file. They come
from the environment or from the git-ignored `tools/release/local.env`
(`tools/release/local.env.example` lists them): the toolkit checkout, the
Windows and Linux build machines and the one folder each script may use on
them, a disc image for the smoke run.

Everything fetched from outside is pinned by version and SHA-256 in
`tools/release/pins.env` (SDL for macOS, the sniper image by digest, the
AppImage runtime, Inno Setup) and `tools/win/fetch-deps.sh` (llvm-mingw,
SDL2 for Windows, the Vulkan headers, glslang).

## macOS

`build-macos.sh`: arm64, Release, Metal only (the Vulkan backend, a MoltenVK
test bed, is compiled out), deployment target macOS 13.0.

```
Takedawn.app/Contents/
  Info.plist                 io.github.xenophobentx.takedawn
  MacOS/Takedawn             the game (CFBundleExecutable)
  MacOS/takedawn-setup       the setup window; the game opens it when its data is missing
  Frameworks/libSDL2-2.0.0.dylib   sdl2-compat 2.32.74
  Frameworks/libSDL3.0.dylib       SDL 3.4.18  (libSDL3.dylib links to it)
  Frameworks/Qt{Core,Gui,Widgets,DBus}.framework   Qt 6.10.3, arm64 only
  PlugIns/platforms/libqcocoa.dylib, PlugIns/styles/libqmacstyle.dylib
  Resources/Takedawn.icns    from assets/icon/takedawn-macos.svg
  Resources/qt.conf
  Resources/licenses/        THIRD_PARTY.txt, the licence texts, SOURCE.txt
  Resources/shaders/upscale/ the texture upscaler's shader files (*.metal, VERSION, NOTICE.md, COPYING*, upstream/)
```

- **No Homebrew on the player's Mac.** SDL is built here from the pinned
  source archives, with `@rpath` install names and the deployment target, into
  `~/.cache/takedawn-deps`; these are the versions the Mac builds have always
  run on (Homebrew's `sdl2` is sdl2-compat over SDL3), but Homebrew's own
  copies only run on the macOS they were bottled for. The program's rpath is
  `@executable_path/../Frameworks`. The script fails if any binary in the
  bundle names a library outside `/System`, `/usr/lib` and the bundle, is not
  arm64, or needs a newer macOS than the target, and if the build log has an
  API-availability warning. libepoxy and OpenSSL are needed by the toolkit's
  configure at build time only; the program links neither, and with Metal as
  the only backend there is no glslang, SPIRV-Tools or Vulkan loader either.
- **The setup window** (`tools/install/gui`) is built by the same script
  against a qt.io kit (`pins.env`: Qt 6.10.3, fetched once with the pinned
  aqtinstall into the cache: only `qtbase`; Homebrew's Qt would drag ICU in)
  and deployed by `tools/install/gui/packaging/deploy-qt-macos.sh`, which the
  development bundle script uses too: macdeployqt on a staging bundle where the
  window is the main executable, thinned to arm64, with a check that the Qt in
  the bundle is the one the window was linked with. About 23 MB of Qt.
  `--no-setup` builds the app without it.
- **Signature.** No identity, no notarisation. The linker's ad hoc signature
  on each binary is what lets arm64 code start at all. On top of it the bundle
  is sealed ad hoc (`codesign --sign -`: no certificate, no Apple service):
  a bundle with only linker signatures does not verify ("code has no resources
  but signature indicates they must be present"), which macOS reports for a
  downloaded copy as "damaged" with no way round it. `TAKEDAWN_MACOS_SEAL=0`
  leaves the seal off.
- **The disk image**: `hdiutil`, compressed, with the app, a link to
  `/Applications` and `Read me first.txt`.

`smoke-macos.sh` mounts the image read-only and, with `HOME` set to a scratch
folder (so the user's own Takedawn folders are never touched), runs the app
from the volume: `--version`; the setup window's screenshot mode (it draws
its pages with the image's Qt and asks the game for the data status); a start
without data (exit status 3,
`RESULT FAIL data not-installed`); `--install <disc> --check` and the install
itself from `TAKEDAWN_DISC`; a headless start driven to the main menu with
`tools/headless/scene_lib.sh`. It also checks that dyld loads nothing from
outside the image and the system. One game at a time: it waits while another
instance runs.

## Windows

`build-windows.sh`: cross-built on the Mac with llvm-mingw, as every Windows
build of the port has been ([windows.md](windows.md)).

```
Takedawn\
  Takedawn.exe         window program (no console), icon and version resource
  SDL2.dll             2.32.10
  takedawn-setup.exe   the setup window; the game starts it when its data is
                       missing (CreateProcess, and waits for it)
  Qt6Core.dll, Qt6Gui.dll, Qt6Widgets.dll, libc++.dll, libunwind.dll
  platforms\qwindows.dll, styles\qmodernwindowsstyle.dll
  licenses\
  shaders\upscale\    the texture upscaler's LGPL shader files (BO-245): *.comp, VERSION,
                       NOTICE.md, COPYING*, upstream\; without them texture upscale is off
  README.txt
```

The window is cross-built against the qt.io kit for llvm-mingw (the host kit
of the same version supplies moc and rcc); its DLLs are gathered from the
import tables, which is what windeployqt would copy. `--no-setup-window`
leaves it out.

The script fails if the exe imports a DLL other than SDL2, the Vulkan loader
(the driver's) and Windows' own. `Takedawn.exe` started from a terminal
writes to that terminal (`main.c` attaches to the parent's console); with its
output redirected it writes to the pipe, which is how the setup window and
the scripts read it.

The setup exe is `tools/release/windows/takedawn.iss`, compiled with Inno
Setup 6.7.3 on the Windows machine: per user, no administrator rights,
`%LOCALAPPDATA%\Programs\Takedawn`, a Start menu entry, an optional desktop
icon, an uninstaller that says where the data and saves are and leaves them.
Run over an install it updates it (see Updating below). No zip. `windows/remote.ps1` is what runs on that machine, inside
`TAKEDAWN_WIN_DIR` only: `Takedawn.exe --version`, `--install <disc> --check`,
the compile (Inno Setup is looked for in `<dir>\tools\innosetup` and fetched
in its portable mode when missing: no registry entry, no Start menu entry),
and a **payload test**: a variant of the setup without uninstaller, registry
entry or shortcuts is installed into `<dir>\itest`, every file is compared
with the program folder, the installed exe is run, the folder is removed.
The real setup exe is not run by the script.

Without `TAKEDAWN_WIN_HOST` the program folder and the `.iss` are the result,
and the script prints the one command that is missing.

On Windows itself the script runs in Git Bash. Set `TAKEDAWN_PYTHON`, since
`python3` there is often the Store's stand-in. `TAKEDAWN_WIN_HOST=local` does
the Windows step on this machine, with PowerShell and `cp` instead of ssh
and scp. The setup window is then built with the Windows Qt kit's own `moc`
and `rcc`.

### Testing the setup exe on a machine that has someone's saves

The program takes its per-user folders from the environment: `%APPDATA%`
(saves, settings) and `%LOCALAPPDATA%` (game data, shader cache). A test sets
both to scratch folders for the processes it starts, and nothing of the
profile is read or moved; the setup exe itself still installs to the real
`%LOCALAPPDATA%\Programs\Takedawn` (Inno Setup asks Windows, not the
environment). The one-time move of the old `Burnout3` and `xboxrecomp\cache`
folders is tested the same way, on copies put under the scratch folders.

Without that, any start that reaches the folders moves them: the game, its
`--data-status`, and so the setup window in every mode (`--show-grab` too),
since it asks the game for the data's state. `--version` and `--install
--check` do not. The first release build's check on the test PC moved its
owner's `%APPDATA%\Burnout3` this way; a development exe of the old name then
starts with empty saves until the folder is copied back.

First run of the real setup exe (0.1.0, Windows 11, a scheduled task in the
desktop session, not elevated): silent install 10 s, 33 files, 64 MB, Start
menu entry, per-user uninstall entry, no prompt; without data the game
started the setup window (a child process, the game waits for it); the window
installed the 716 files from an extracted disc folder in 14 s; the game moved
the copied old folders, kept `settings.toml` byte for byte, reached the menu
and a race (racing present to present p95 17.05-17.09 ms, p99 17.10-17.22,
none over 20 ms); the uninstaller removed the program and its entries and
left data, saves and settings; installing again, and over an install, kept
them.

## Linux and the Steam Deck

`build-linux.sh`: built in the Steam Runtime 3 "sniper" SDK container (pinned
by digest), so the program needs glibc 2.29 and runs on SteamOS, Bazzite and
desktop distributions alike. podman is asked only to pull that image and to
run containers with `--rm`.

```
AppDir/
  AppRun                     LD_LIBRARY_PATH=usr/lib, exec usr/bin/takedawn
  takedawn.desktop, takedawn.png
  usr/bin/takedawn           the game
  usr/bin/takedawn-setup     the setup window; the game becomes it (execv) when
  usr/bin/qt.conf            its data is missing and there is a display
  usr/lib/libSDL2-2.0.so.0   sdl2-compat 2.32.74 ...
  usr/lib/libSDL3.so.0       ... over SDL 3.4.18
  usr/lib/libQt6{Core,Gui,Widgets,DBus,XcbQpa,WaylandClient}.so.6
  usr/lib/libxcb-*.so        the X11 helper libraries Qt's xcb plugin needs
  usr/plugins/platforms/{libqxcb,libqwayland,libqoffscreen}.so
  usr/plugins/wayland-shell-integration, wayland-decoration-client, platformthemes
  usr/share/licenses/takedawn/
  usr/share/takedawn/shaders/upscale/   the texture upscaler's shader files (*.comp, VERSION, NOTICE.md, COPYING*, upstream/)
```

Three things are built in the container, the first two once (kept in
`<dir>/sdl-sniper` and `<dir>/qt-sniper` on the build machine, keyed by the
source archive and the image):

- **SDL** from the same pinned archives as the Mac's (`linux/build-sdl.sh`).
  The image's own SDL is not carried: it is linked for the Steam Runtime and
  names libasound, libpulse, libdecor-0, libXss, libXtst and others outright,
  so it does not load where one of them is missing. Built here SDL opens its
  back ends at run time and needs only the C library to load; the script
  fails if that is not so.
- **Qt** (`linux/build-qt.sh`): qtbase 6.10.3 from the pinned source archive,
  with the xcb, Wayland and offscreen plugins, no OpenGL, no ICU. The qt.io
  binaries of this version need glibc 2.34 and do not run in the image (or on
  the systems the game itself still runs on). About three minutes on 16
  cores. The window is compiled with g++ 10, as Qt is: the image's clang 11
  crashes on Qt 6.10's headers.
- **the game and the window** (`linux/in-container.sh`), which also sorts
  every library they link three ways: what every system has and must supply
  itself (glibc, libstdc++, the Vulkan loader, X11, Wayland, libxkbcommon,
  FreeType, fontconfig, D-Bus), what a system may lack (carried, with its
  Debian copyright file), and anything else, which fails the build until
  someone decides. libxkbcommon and libxkbcommon-x11 are also carried as a
  pair in `usr/lib/xkb-fallback`, which AppRun uses only where the system
  lacks one of them (the AppImage catalog's test runs on Ubuntu 22.04
  minimal, which has no libxkbcommon-x11). Linked with `--as-needed`, the game needs neither OpenSSL
  nor libepoxy.

The AppDir is packed with `mksquashfs` and the pinned static AppImage runtime
(no libfuse needed on the player's machine).

**The Steam box.** On Linux the window offers "Add Takedawn to my Steam
library" (off unless ticked). It is `tools/install/gui/steam_shortcut.cpp`,
the old installer's `steam.py` in C++ with its rules: existing entries are
never changed (an entry with the same Exe counts as ours, whatever its name),
the file is backed up first, and with several Steam users the one who logged
in last is taken or nothing is done. Steam rewrites `shortcuts.vdf` when it
exits, and on a Steam Deck it is always running (Desktop Mode too), so when it
runs the window asks "Steam has to close for a moment to add Takedawn. Close
Steam and add it?"; on Yes it runs `steam -shutdown` (Flatpak: `flatpak run
com.valvesoftware.Steam -shutdown`) with the AppImage's library variables
removed, waits up to 45 s until no Steam process is left, writes the shortcut
and starts Steam again. It does not close Steam when a game started from Steam
is running, when the setup itself was started from Steam (Game Mode: the
library has Takedawn already, and closing Steam would end the session), on No,
or when Steam does not close in time; then the note names the manual way
(Desktop Mode: right-click `Takedawn.AppImage`, "Add to Steam"; or Steam's
"Add a Non-Steam Game to My Library"). The shortcut starts
`~/Applications/Takedawn.AppImage` (the stable name; it survives updates) when
that file exists, else the AppImage running now (`APPIMAGE`), else the game's
program; no launch options (under gamescope any window is fullscreen). Tests
(`steam_test`, and window tests, one with a stand-in `steam` script on Linux
in a PID namespace with no real Steam) use files they make themselves;
`TAKEDAWN_STEAM_CONFIG` names a config folder outright and
`TAKEDAWN_STEAM_COMMAND` replaces the `steam` program.

`smoke-linux.sh` runs the AppImage on the build machine in a scratch HOME,
under the shared lock, without putting anything on its screen
(`linux/smoke-remote.sh`): `--version`; no data and no display (exit status 3,
the RESULT line, no window); no data and a display (the game becomes the
setup window, inside the mounted AppImage); the window's pages off screen,
on X11 under a headless gamescope of its own (the xcb plugin and the carried
helper libraries), and the Wayland plugin's loading; `--install` from a disc
folder on that machine (`TAKEDAWN_LINUX_DISC`, read only); a headless boot to
the main menu on the machine's Vulkan driver.

## Android

`build-android.sh` (its own header says how) builds the unsigned APK on the
Android build machine from the same stamped tree and `sign-android.sh` signs
it on this machine, where the key lives; the key is never copied or printed.
`release.sh` runs both when `TAKEDAWN_ANDROID_HOST` is set and lists only the
signed APK in `SHA256SUMS`.

## Updating (BO-100)

An update is a new download put over the old one; nothing is rebuilt and
nothing is asked of the disc again. The player's data is not in the
program: saves and settings, the game data installed from the disc, and the
shader cache are in the user folders (Windows `%APPDATA%\Takedawn` and
`%LOCALAPPDATA%\Takedawn`, macOS `~/Library/Application Support/Takedawn` and
`~/Library/Caches/Takedawn`, Linux `~/.local/share/takedawn` and
`~/.cache/takedawn`). Settings keys a build does not know are kept with a log
line, so going back to the older version is safe. The same text is in each
package's own README (`tools/release/{windows,linux}/README.txt`,
`macos/READ-ME-FIRST.txt`). Settings > Advanced > Check for updates tells
when a newer release exists (one request, only when pressed; nothing is
downloaded); on Android there is no such row yet.

- **Windows**: run the new setup over the old; it finds the install by its
  uninstall entry and folder and calls it "Update Takedawn X to Y" (a
  downgrade or the same version says so too). It refuses, exit code 7 with
  `/VERYSILENT` and nothing changed, while `Takedawn.exe`,
  `takedawn-setup.exe` or `SDL2.dll` cannot be opened for writing, that is
  while the game or its setup window runs (`CloseApplications` is off on
  purpose: the Restart Manager would close a running game without asking in a
  silent run; the game's own single-instance lock only covers a second game
  on the same save). Then the program folder is renamed to `Takedawn.prev`
  (one previous version is kept, the uninstaller removes it), the new files
  go into a fresh folder, and if the installation does not finish the previous
  folder is put back (`DeinitializeSetup`; exercised by `/TESTFAIL=1` in the
  payload-test variant). The folder, and so the Start menu entry, the desktop
  icon and a Steam shortcut to `Takedawn.exe`, stay the same.
- **macOS**: quit Takedawn, open the new dmg, drag Takedawn onto Applications
  and choose Replace. The new copy is unsigned like the old: macOS blocks it
  again and "Open Anyway" in System Settings > Privacy & Security (or
  `xattr -dr com.apple.quarantine`) is needed once more. Saves, settings and
  cache are outside the app.
- **Linux and the Steam Deck**: the AppImage is the whole program. Steam (and
  a `.desktop` file) store the file's *path*, so the file must keep its path
  and name: the README tells the player to name it `Takedawn.AppImage` in a
  folder of their own and, for an update, to `mv` the new download over it
  (`mv` is a rename, which also works while the old file is open; the game
  should be closed anyway). The release asset keeps its versioned name
  (`Takedawn-<version>-linux-x86_64.AppImage`, which `SHA256SUMS`, `BUILDINFO`
  and the update check use); a shortcut made from a versioned file name keeps
  starting that file. The Steam box of the setup window stores the path the
  AppImage was started from (`APPIMAGE`), so it also follows the stable name.

**Tests** (`tools/release/update-test/`; scratch profiles only, one game at a
time, never while someone plays on that machine). `update-test.sh --old <app>
--new <app> --disc <disc> --saves <ref saves> --work <scratch>` runs old, new,
old again on macOS (a `Takedawn.app`), Linux (an AppImage; it also starts the
game through a stored-path launcher, as a Steam shortcut does) and Windows
(under Git Bash: a program folder; the setup exe is `setup-test.ps1`'s job). It
installs the data from the disc, writes settings, mixer files, a shader
cache and a profile the game saves itself, replaces the program, and checks:
the same hashes of everything the player owns, `--data-status` and
`--build-shaders --status` unchanged, NEW boots to the menu on the same
Profile 1, a second copy is refused (exit 3), OLD put back boots on the same
data. `drive.sh` is its headless driver; it stops the game by process id (on
Windows the Windows pids of the program and its children: killing the bash
subshell leaves `Takedawn.exe` holding the save lock). Started over ssh on Windows, redirect the output to a file on that machine and read the file (a piped ssh session was cut short in two of four runs). `setup-test.ps1 -Dir
<folder> -OldSetup <exe> -NewSetup <exe> [-Disc <folder>]` runs two Windows
setups over each other inside one folder: update refused while the program
runs, update, the markers for saves/settings/data/cache untouched, `.prev`,
old over new, and a forced failure that must leave the old version. By default
the setups are the `/DPayloadTest` variants (no registry, no shortcuts, no
uninstaller); `-Real` runs the real setups, which write
`HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\{8C1E5B0A-64D3-4C7E-A1F2-0D9B7E53A6C4}_is1`,
`%APPDATA%\Microsoft\Windows\Start Menu\Programs\Takedawn.lnk` and the
uninstaller, and needs `-IKnowItWritesTheRegistry`: it saves a player's existing key and shortcut first (same AppId), ends with the new uninstaller and checks that the key, the shortcut, the folder and `.prev` are gone, and puts the saved ones back. `setup-gui-shot.ps1` (from a scheduled task with `/IT`: it needs the desktop) runs the real wizard over an install, photographs each page and uninstalls again.

## What release.sh checks, and what it cannot

Checked: the version argument against `VERSION`; clean checkouts (or
`--allow-dirty`, which the artefacts and `BUILDINFO` then show); the stamp of
the generated code and the patches; each program's `--version` against this
checkout (version, both commits, regen key, manifest id, tree and patches)
on macOS (from the mounted image), on Windows (the program folder and the
test install) and on Linux (the AppImage); the same lines against each other;
on macOS and on Linux the install from the disc and a headless boot to the
main menu, and the setup window's pages.

Not checked by any script, and listed in `BUILDINFO` under "not verified":
starting the game on Windows; a window on a real screen on Linux; the real
Windows setup; a Steam Deck; a machine that never had a compiler, Homebrew or this repository on it;
the first-start prompts below; controllers, sound, a full race.

## Unsigned: what the player sees

- **macOS**: "Takedawn cannot be opened because Apple cannot check it" (or
  "not verified"). System Settings > Privacy & Security > Open Anyway; on
  macOS 14 and earlier, right-click > Open. Or
  `xattr -dr com.apple.quarantine /Applications/Takedawn.app`. Once. The disk
  image carries these steps in `Read me first.txt`.
- **Windows**: SmartScreen, "Windows protected your PC": More info > Run
  anyway, for the setup exe (and possibly once for the program).
- **Linux**: nothing; the file must be made executable.

## Licences in the packages

Every package carries `licenses/` (`Contents/Resources/licenses`,
`usr/share/licenses/takedawn`): `THIRD_PARTY.txt`
(from `tools/release/licenses/THIRD_PARTY_NOTICES.md`, whose content follows
the repository's notices page), the licence texts under the names the setup
window's "About and licences" lists, the toolkit's `NOTICE`, the
LGPL-2.1 text for the xemu-derived APU files, and `SOURCE.txt`, written at
build time with both commits, which says where the corresponding source is:
the two repositories at those commits, published with the release. The
release page must therefore carry (or link) both source trees at the
release's commits.

The Android APK carries the same set as `assets/licenses/` (it has no
folder beside the program), plus the texts of the parts only it has
(`tools/android/licenses/`: libadrenotools, linkernsbypass, Mesa's Turnip
driver, and the Mesa release and patch it was built from). The app shows them
under **Licences** (the setup page; "Takedawn: graphics driver" in the
launcher). `build-android.sh` stages the set with `copy_licenses`, the build
machine packs it (`tools/android/build-apk.sh`, `BO3_LICENSES_DIR`), and
`build-android.sh` and `release.sh` fail when the unsigned or the signed APK
lacks `assets/licenses` or one of the main texts (`check_apk_licenses` in
`common.sh`).
