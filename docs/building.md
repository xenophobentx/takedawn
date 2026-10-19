# Building from source

For developers. Players do not need any of this: the downloads are ready to
run (see the [README](../README.md)).

The repository holds no game code and no game data. A build translates your
own `default.xbe` into C and compiles that, so you need your own disc of the
supported release (North America retail, `default.xbe` SHA-256
`9f497acd82adb8edfb72cc31aeeb35010b45d32b13a3f1fc6540717ff24d13f9`, recorded
in [`tools/install/supported.json`](../tools/install/supported.json)), or of
the European release with `BO3_REGION=PAL` ([pal.md](pal.md)).

The generated C (`src/game/recomp/gen/`), the build output and the disc files
are ignored by git. Never commit or share them.

## What you need

- A checkout of this repository and a checkout of the toolkit (the
  recompiler and the Xbox runtime) beside it. `XBOXRECOMP_DIR` names it; the
  default is `../xboxrecomp`.
- Your extracted disc folder at `Burnout 3 Takedown/default.xbe` in the
  checkout (a symlink is fine).
- Python 3.10 or newer with the `capstone` module, CMake 3.20 or newer and
  Ninja. `regen.sh` uses the checkout's `.venv` when there is one:
  `python3 -m venv .venv && .venv/bin/pip install capstone`.
- For the button prompts of other pads: `resvg` (Homebrew, or `cargo install
  resvg`), `rsvg-convert` or the Python module `resvg-py`. Without one of them
  the build works and shows only the game's own Xbox prompts. A release build
  requires it.
- Per platform:

| Platform | Compiler and libraries | Details |
|---|---|---|
| macOS (Apple silicon) | Xcode Command Line Tools; from Homebrew `sdl2`, `sdl3`, `libepoxy`, `openssl@3` | below |
| Linux, Steam Deck | `podman` and the Steam Runtime "sniper" SDK container, so the result runs on SteamOS; or the host's clang or gcc with the sdl2, libepoxy, vulkan and openssl development packages (untested) | [installer.md](installer.md) |
| Windows | llvm-mingw, cross-built from macOS or Linux, or natively with the same toolchain file | [windows.md](windows.md) |
| Android | Android SDK and NDK r28 or newer, a JDK 17 or newer; built on a Linux PC | [android.md](android.md) |

The toolkit is the `takedawn` branch of
[xenophobentx/xboxrecomp](https://github.com/xenophobentx/xboxrecomp). Each
release has a tag there, `takedawn-<version>`, at the commit its packages were
built from:

```sh
git clone https://github.com/xenophobentx/takedawn
git clone -b takedawn https://github.com/xenophobentx/xboxrecomp
git -C xboxrecomp checkout takedawn-0.2.0   # the toolkit of release 0.2.0
```

## Translate and build

```sh
XBOXRECOMP_DIR=../xboxrecomp ./regen.sh --disasm    # writes src/game/recomp/gen/
cmake -S . -B build/mac -G Ninja -DCMAKE_BUILD_TYPE=Release -DXBOXRECOMP_DIR=../xboxrecomp
cmake --build build/mac
```

`regen.sh` runs the toolkit's stages in the one order that works for this
game (XBE parser, disassembler, library function identification, calling
convention analysis, recompiler) and applies the opt-in patches of
`config/patches.json`. If the generated code is wrong, fix the recompiler or
the data in `config/`. Do not edit the generated files.

Windows and Android have their own scripts:

```sh
tools/win/fetch-deps.sh && tools/win/build-mingw.sh           # bin/burnout3.exe, bin/SDL2.dll
tools/android/fetch-deps.sh && tools/android/build-apk.sh     # out/burnout3-android-arm64.apk
```

In the source tree the executable and these outputs still carry the working
name `burnout3`.

## Run

Run the program from the checkout. With the disc folder there it starts the
game from it. [running.md](running.md) covers the command line, the save
folder, the bug report, pad scripts, frame dumps and the reference scenes in
`tools/headless/`.

## The source installer

`tools/install/install.py` and the Qt window in `tools/install/gui/` are the
earlier installer, which built the game on the player's machine from the
disc: tool check, disc copy, translate, build, package. Releases no longer
work that way, but the driver is still a convenient one-command build for
developers:

```sh
python3 tools/install/install.py --source "<your image or folder>" --toolkit ../xboxrecomp
```

Its steps, options and line protocol are in [installer.md](installer.md), the
window in [installer-gui.md](installer-gui.md). One thing to know: its
`regen.py` does not pass the patch list yet, so a game built by it lacks the
km/h, widescreen HUD, ultrawide, bloom and motion blur switches. Use
`regen.sh` for a build with them.

## Releases

A release is the program alone, built for each platform from one tree of
generated code, with the game data left to the player's disc.
[release.md](release.md) describes the scripts in `tools/release/`, what each
package contains and what the scripts check:

```sh
tools/release/release.sh 0.1.0      # builds and smoke-runs; uploads nothing
```

A release build (`-DBO3_RELEASE=ON`) names the program `takedawn`. A build
without it is `bin/burnout3`, as before. The data installer inside the program
and the setup window are in [installer.md](installer.md).

## Tests

- Installer driver: `python3 -m unittest discover -s tools/install/tests`
  (no game data needed).
- Runtime: the toolkit's own tests, from its build folder.
- Reference scenes and golden frames: [running.md](running.md).
