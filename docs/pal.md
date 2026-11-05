# The PAL (Europe) builds

Takedawn translates one exact `default.xbe`. Besides the North America
(NTSC-U) retail disc it can be built for the two European (PAL) retail
discs, which are two builds of the game:

| Build | `BO3_REGION` | Disc | default.xbe SHA-256 | Manifest |
|---|---|---|---|---|
| `ntsc-u-retail` (default) | `NTSC-U` | North America | `9f497acd…4d13f9` | `config/disc/ntsc-u-retail.manifest` (716 files) |
| `pal-retail` | `PAL` | Europe (Fr,De,It) | `6f7f6b46…7afa2` | `config/disc/pal-retail.manifest` (786 files) |
| `pal-ensv-retail` | `PAL-ENSV` | Europe, Australia (En,Es,Nl,Sv), redump 20584, EA-091 | `71c6923b…d68911` | `config/disc/pal-ensv-retail.manifest` (761 files) |

All have the title id `0x4541005B`, so a profile moves between them as it
does between machines (copy `UDATA`). Every package carries all three
programs (below). The Fr/De/It disc carries French,
German and Italian text, speech and films, and no English ones
(`Data/Global{fr,ge,it}.bin`, `tracks/{F,G,I}_DJ*.xwb`,
`ovid/{french,german,italia}30.xmv`, `ovid/Tr{C,D}_{FRA,GER,ITA}.xmv`). Its own
directory table spells the tracks folder `tracks/` (the NTSC-U disc has
`Tracks/`); the manifest was made from a PAL image with
`tools/release/make_manifest.py` and keeps that spelling. Guest paths are
looked up without regard to case on every host, so it changes nothing.

The En/Es/Nl/Sv disc carries English, Spanish, Dutch and Swedish text
(`Data/Global{en,sp,du,sw}.bin`), loading films (`ovid/{englis,spanis,dutch,swedis}30.xmv`)
and the films `ovid/Tr{C,D}_{ENG,SPA,DUT,SWE}.xmv`, but DJ speech in English
and Spanish only (`tracks/{E,S}_DJ*.xwb` and `tracks/*/*/{E,S}_DJRACE.xwb`):
Dutch and Swedish play with the English DJ. 678 of its files are the
Fr/De/It disc's byte for byte.

Its `default.xbe` is not the Fr/De/It one: the same size, built about 40
minutes earlier (certificate version 2, the other's 3), with the code
laid out a little differently (from `-0xD0` to `+0x2C0` against the
Fr/De/It build in the first 0xD4000 bytes, then `+0xA0`) and a few real
differences: how the start picks the language from `XGetLanguage` (Spanish,
else the disc's English; the Fr/De/It build: German or Italian, else
French), a language menu of its own with English, Dutch, Spanish and
Swedish (Dutch and Swedish are no dashboard's languages; the title's
language value is the dashboard's number, 10 for Dutch and 11 for
Swedish), and its loading word (LOADING/LADEN/CARGANDO/LADDAR). Every
site the port patches, wraps or names reads the same in it, at its own
address, so it is a third set of addresses.

## What the PAL game sees

The kernel answers as the North American console it was written for: the
EEPROM's region North America and an NTSC-M, 60 Hz AV pack
(`src/kernel/kernel_hal.c`). So the PAL game runs its 60 Hz path.
Reporting a PAL standard is the toolkit's to add.

The language is `game.language` (toolkit `xbox_GetLanguage`,
`src/kernel/kernel_xbox.c`), English unless set. The game picks its files
from it, and with English, which the Fr/De/It disc does not have, it takes
the French ones (a headless start on 2026-10-22: `Data\GlobalFr.bin`,
`ovid\french30.xmv`). `bo3_languages_init` (`src/game/main.c`,
`src/game/languages.c`) finds the game's text files that are in the disc's
manifest and installed, and hands those languages to the overlay, whose
Language row (Game page) offers them: German, French and Italian on the
Fr/De/It disc (French when unset, as the game shows), English, Spanish,
Dutch and Swedish on the En/Es/Nl/Sv disc, none on the US disc (English
alone). A language the disc lacks is not passed to the kernel.
`tests/languages` checks each disc's list from its manifest.

## Building it

```sh
BO3_REGION=PAL XBOXRECOMP_DIR=../xboxrecomp ./regen.sh --disasm
tools/win/build-mingw.sh -DBO3_REGION=PAL          # or any cmake build with -DBO3_REGION=PAL
BO3_REGION=PAL-ENSV XBOXRECOMP_DIR=../xboxrecomp ./regen.sh --disasm
cmake -S . -B build-ensv -DBO3_REGION=PAL-ENSV ...
```

with the Fr/De/It disc's `default.xbe` at `Burnout 3 Takedown (PAL)/default.xbe`
(else `Burnout 3 Takedown/default.xbe`) and the En/Es/Nl/Sv disc's at
`Burnout 3 Takedown (PAL-ENSV)/default.xbe` (`BO3_XBE` names another).
`regen.sh` then reads `config/pal/`, hands the recompiler
`src/game/recomp/recomp_manual_pal.c` and writes `src/game/recomp/gen.pal`
(PAL), or reads `config/pal-ensv/`, hands it `recomp_manual_pal_ensv.c` and
writes `src/game/recomp/gen.pal-ensv` (PAL-ENSV), so that all three trees
(the NTSC-U one in `gen`) stand in one checkout; the build compiles in the disc's manifest and picks its
addresses of the C (below). Both PAL builds define `BO3_REGION_PAL` (their
per-user folders, what PAL does differently); the second also
`BO3_REGION_PAL_ENSV`. The source installer (`tools/install/install.py`)
does the same by itself for the disc it is given (`bo3_region` in
`tools/install/supported.json`).

## Packages

There is one package per platform, and it carries all three programs
(BO-341, `src/game/package.h`): the player does not need to know which disc
they own. The North America (NTSC-U) program is the main one, under the
package's program name; the two European ones lie beside it with
`supported.json`'s `program_suffix`:

| Build | macOS (`Takedawn.app/Contents/MacOS/`) | Linux (AppImage `usr/bin/`) | Windows | Android (`lib/arm64-v8a/`) |
|---|---|---|---|---|
| `ntsc-u-retail` | `Takedawn` | `takedawn` | `Takedawn.exe` | `libmain.so` |
| `pal-retail` | `Takedawn-pal` | `takedawn-pal` | `Takedawn-pal.exe` | `libmain_pal.so` |
| `pal-ensv-retail` | `Takedawn-ensv` | `takedawn-ensv` | `Takedawn-ensv.exe` | `libmain_ensv.so` |

`tools/release/gen.sh` makes the three trees (`gen`, `gen.pal`,
`gen.pal-ensv`, each stamped) and every platform's script builds the three
programs; `BO3_REGION` is only for a developer's build of one program now.

Each program has every other build's manifest compiled in
(`cmake/release_info.cmake`: `bo3_package.inc`, the builds of supported.json
with the same `package`), so each knows every disc. A shortcut, the Start menu,
Steam and the setup window start the main program; it becomes the program of
the disc whose data is installed, with the same arguments (`execv`; on Windows
`CreateProcess` and a wait for its status), before it does anything else
(`bo3_package_start`, `src/game/install.c`):

1. the disc installed last, when its data is whole: a real `--install`
   writes the build id to `disc.txt` in the main program's per-user local
   folder (`%LOCALAPPDATA%\Takedawn`, elsewhere its save folder), but only
   when another disc's data is installed too or the file is there already, so
   a player with one disc never gets it;
2. else this program's own data, when whole;
3. else the first other disc whose data is whole;
4. else the recorded disc (its setup window then asks for it), else this
   program.

"Whole" is the build's installed data where its program looks by itself: the
folder chosen in its setup window (its `data-folder.txt`), else its per-user
`Game` folder, with the installer's marker naming the build's manifest. A
start told where the data is (`--data`, `TAKEDAWN_DATA`, a `default.xbe` on
the line, a checkout) is not handed over by this rule; the data's marker says
whose it is and the program becomes that one (the same hand-over as before).
`--install <disc>` reads the disc's `default.xbe` against every manifest of
the package and hands the disc to its program, which installs it into its own
folder and says so on the same pipe (`--install --check` also says, in `LOG
disc build|root|place|default|bytes|free-needed` lines, where that program
puts it, and the setup window's "Installs to" and space line follow). A
process another program of the package started does not hand over again
(`TAKEDAWN_SIBLING_HOP`, one step). `--version` is always the program's own.
`tests/package` checks the choice, `disc.txt`, the program names and the table
compiled in (three builds, each disc's `default.xbe` naming its own program).

Installing another disc later switches to it: its install's last line says
"Takedawn starts this disc (...) from now on. The ... disc's data stays in
...; each disc keeps its own saves and settings", and the next start runs
that disc's program. Installing the first disc again switches back (the data
is still there: a few seconds, nothing is copied).

### Where each disc's data is

Each program keeps the per-user folders its own package kept before
(`BO3_USER_DIR`, supported.json `user_dir`), so nothing moves for anyone who
had a per-region package or a source build:

| | North America | both European discs |
|---|---|---|
| Windows | `%APPDATA%\Takedawn`, `%LOCALAPPDATA%\Takedawn` (`Game`, `cache`) | `%APPDATA%\Takedawn PAL`, `%LOCALAPPDATA%\Takedawn PAL` |
| macOS | `~/Library/Application Support/Takedawn`, `~/Library/Caches/Takedawn` | `.../Takedawn PAL` |
| Linux | `~/.local/share/takedawn`, `~/.cache/takedawn` | `~/.local/share/takedawn pal`; the shader cache `~/.cache/takedawn` too |
| Android | `files/Burnout 3 Takedown` (one disc at a time), saves in `files/takedawn` | the same data folder, saves in `files/takedawn pal` |

Saves, settings, mods, the upscaled menu pictures and the installed data are
apart between North America and Europe; the two European discs share theirs,
as their package did (the same title id and save format, and only one is
installed at a time in that folder). The shader cache holds keys, not code
(`nv2a_pcache.h`): on Linux all three share `~/.cache/takedawn`, as they did,
and a key of another disc only means one more shader built ahead. The NTSC-U
program alone moves the port's old-name folders (`Burnout3`); the PAL ones
never do. Portable mode (`portable.txt` or a `data` folder beside the
program) has one folder for every program: the data's marker then says whose
it is, and the saves are shared.

On Android the app has one data folder: installing another disc (the setup
page; with the data installed it opens from "Takedawn: graphics driver",
"Game data: install another disc") writes that disc's files over it, and the
app loads that disc's library from then on (`StartActivity.library`, from the
marker). The setup page loads `libmain.so` when nothing is installed (its
installer takes any of the three discs, with that disc's manifest) and the
installed disc's library otherwise; after another disc's install it opens
again in a fresh process with that disc's library (`RestartActivity`), so the
menu pictures' offer and their folder are that disc's. Files only the old
disc had stay in the folder.

### The separate PAL packages (0.3 release candidates only)

The 0.3.0 release candidates still had a second package per platform, "Takedawn
PAL" (`Takedawn-0.3.0-pal-…`; Windows its own AppId and `Programs\Takedawn PAL`,
macOS `Takedawn PAL.app` with `io.github.xenophobentx.takedawn.pal`, Android
`io.github.xenophobentx.takedawn.pal` with the Fr/De/It program only). No
published release ever had one: 0.2.1 and earlier were NTSC-U only. For a
tester who installed one:

- desktop: its data, saves and settings are in the `Takedawn PAL` folders,
  which the one package's PAL programs use as they are: install the one
  package and start it; nothing is installed again. The old PAL app can be
  removed (Windows: its own uninstall entry, "Takedawn PAL"; it leaves the
  folders).
- Android: an app's folders are its own, so the one app does not see the PAL
  app's. Back up the profile in the PAL app (System > "Export save to home
  folder", or copy `Android/data/io.github.xenophobentx.takedawn.pal/files/takedawn pal`),
  install the disc in the one app, and import it there (System > "Import
  Xbox or PS2 save..."); then uninstall the PAL app.

## How the addresses were found

The PAL executable is the NTSC-U program built again: the same libraries,
the same game code, a few hundred bytes further on (`.text` +0x2F0 by its
end, the XDK sections +0x300, `.data` +0x320 with a few local differences,
and one function moved). `tools/region/xbemap.py` finds every address's
twin from the two executables alone:

- `.text`: both disassembled, each instruction reduced to its mnemonic and
  operands with absolute addresses blanked, the two lists aligned with
  `diff`. A matched instruction maps byte for byte. Branch targets of matched
  `call`/`jmp`/`jcc` pairs outvote the alignment (that is how the moved
  function, NTSC 0x1A8B0 = PAL 0x1B030, was found).
- data and bss: the absolute operands of matched instruction pairs (NTSC A
  was PAL B); an address nothing references takes the shift of its
  neighbours when both agree.
- the XDK sections moved as blocks.

`tools/region/port_config.py` writes `config/pal/*.json` from `config/*.json`
with it and checks each code site: the six instructions from the site must
read the same in both builds, with their absolute operands and branch
targets each other's translation. It then checks every `BO3_VA` / `BO3_SUB`
pair of the C against the map (code ones by their instructions too). It
reports anything that does not hold; one site was read by hand (`REVIEWED`
in the script).

```sh
python3 -m tools.region.port_config NTSC/default.xbe PAL/default.xbe
python3 -m tools.region.port_config NTSC/default.xbe ENSV/default.xbe --target pal-ensv
python3 -m tools.region.xbemap NTSC/default.xbe PAL/default.xbe 0x4D5370 0x1D2879
```

The En/Es/Nl/Sv build's addresses come the same way, from NTSC-U to that
disc (`--target pal-ensv`: `config/pal-ensv/`, the third values). In the C,
every guest address is written once with all three values, NTSC-U first,
then Fr/De/It, then En/Es/Nl/Sv (`src/game/region.h`):

```c
#define CTL_PADS  BO3_VA(0x004AE20Cu, 0x004AE52Cu, 0x004AE5CCu)
BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)();
```

The notes and comments everywhere keep speaking of the NTSC-U addresses.

## Keeping the two in step

`tools/region/check.py` needs neither executable, so it runs on every
change (its tests: `python3 -m unittest discover -s tools/region/tests`):

```sh
python3 -m tools.region.check
```

It fails, for each PAL build, when a site of `config/*.json` has no twin in
`config/pal/` or `config/pal-ensv/` (or the twin differs in anything but its
address), when a function `recomp_manual.c` wraps has no twin in
`recomp_manual_pal.c` or `recomp_manual_pal_ensv.c` (each definition names
its NTSC-U function on its line), when a `BO3_VA` / `BO3_SUB` has not three
values, and when the code of `src/game` names a guest address
(0x11000 .. 0x778600) or a `sub_XXXXXXXX` outside `BO3_VA` / `BO3_SUB`. A number in that range that is no guest
address (an offset in a file, a range bound) says
`region: not an address` on its line. Two uses with the same NTSC-U value
must agree, and one whose PAL value (either) is the NTSC-U one says
`region: same`.

So a new site goes in as: the NTSC-U entry or address as before, then
`port_config.py` with the executables, once per PAL build (or the PAL
values written beside it and checked with `xbemap.py`), then `check.py`.

## What stays the same

`Data/Global.txd`, `Data/Frontend.txd`, `sound/Fe.awd` and
`sound/Generic.awd` are the same files on both discs, and the menu fonts are
at the same offsets of both executables, so the overlay's art, sounds,
fonts and button prompts work unchanged.
