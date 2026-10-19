# The PAL (Europe) build

Takedawn translates one exact `default.xbe`. Besides the North America
(NTSC-U) retail disc it can be built for the European (PAL) retail disc:

| Build | default.xbe SHA-256 | Manifest |
|---|---|---|
| `ntsc-u-retail` (default) | `9f497acd…4d13f9` | `config/disc/ntsc-u-retail.manifest` (716 files) |
| `pal-retail` | `6f7f6b46…7afa2` | `config/disc/pal-retail.manifest` (786 files) |

Both have the title id `0x4541005B`, so a profile moves between them as it
does between machines (copy `UDATA`). The PAL disc carries the French,
German and Italian text, speech and films beside the English. Its own
directory table spells the tracks folder `tracks/` (the NTSC-U disc has
`Tracks/`); the manifest was made from a PAL image with
`tools/release/make_manifest.py` and keeps that spelling. Guest paths are
looked up without regard to case on every host, so it changes nothing.

## What the PAL game sees

The kernel answers as the North American console it was written for: the
EEPROM's region North America and language English (`XC_LANGUAGE` 1,
toolkit `src/kernel/kernel_xbox.c`) and an NTSC-M, 60 Hz AV pack
(`src/kernel/kernel_hal.c`). So the PAL game runs its 60 Hz path, in
English, for now. Reporting a PAL standard or another language is the
toolkit's to add.

## Building it

```sh
BO3_REGION=PAL XBOXRECOMP_DIR=../xboxrecomp ./regen.sh --disasm
tools/win/build-mingw.sh -DBO3_REGION=PAL          # or any cmake build with -DBO3_REGION=PAL
```

with the PAL disc's `default.xbe` at `Burnout 3 Takedown/default.xbe`.
`regen.sh` then reads `config/pal/` and hands the recompiler
`src/game/recomp/recomp_manual_pal.c`; the build compiles in the PAL
manifest and picks the PAL addresses of the C (below). The source installer
(`tools/install/install.py`) does the same by itself when the disc it is
given is the PAL one (`bo3_region` in `tools/install/supported.json`).

## Packages

`BO3_REGION=PAL tools/release/gen.sh`, then `BO3_REGION=PAL` with each
platform's script (`build-windows.sh`, `build-macos.sh`, `build-linux.sh`,
`build-android.sh`, or `release.sh`). A PAL package is its own app beside
the NTSC-U one, so neither overwrites the other's files or install:

| | NTSC-U | PAL |
|---|---|---|
| package files | `dist/<version>/Takedawn-<version>-…` | `dist/<version>-pal/Takedawn-<version>-pal-…` |
| work folder | `build/release` | `build/release-pal` |
| app name | Takedawn | Takedawn PAL |
| Windows (Inno Setup) | its AppId, `Programs\Takedawn` | `/DRegion=PAL`: another AppId, `Programs\Takedawn PAL` |
| macOS | `Takedawn.app`, `io.github.xenophobentx.takedawn` | `Takedawn PAL.app`, `….takedawn.pal` |
| Android | `io.github.xenophobentx.takedawn` | `io.github.xenophobentx.takedawn.pal` (the Java classes keep their package) |
| per-user folders | `Takedawn` | `Takedawn PAL` |

The PAL program keeps per-user folders of its own (`%APPDATA%\Takedawn PAL`,
`%LOCALAPPDATA%\Takedawn PAL\Game` on Windows), so it never mistakes the
NTSC-U build's installed data for its own (another manifest: the setup
would install over it); it does not move the port's old-name folders
(`Burnout3`), which are NTSC-U's. The setup window and the READMEs name the
PAL disc.

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
python3 -m tools.region.xbemap NTSC/default.xbe PAL/default.xbe 0x4D5370 0x1D2879
```

In the C, every guest address is written once with both values, NTSC-U
first (`src/game/region.h`):

```c
#define CTL_PADS  BO3_VA(0x004AE20Cu, 0x004AE52Cu)
BO3_SUB(sub_001D2879, sub_001D2B99)();
```

The notes and comments everywhere keep speaking of the NTSC-U addresses.

## Keeping the two in step

`tools/region/check.py` needs neither executable, so it runs on every
change (its tests: `python3 -m unittest discover -s tools/region/tests`):

```sh
python3 -m tools.region.check
```

It fails when a site of `config/*.json` has no PAL twin in `config/pal/`
(or the twin differs in anything but its address), when a function
`recomp_manual.c` wraps has no twin in `recomp_manual_pal.c` (each PAL
definition names its NTSC-U function on its line), and when the code of
`src/game` names a guest address (0x11000 .. 0x778600) or a `sub_XXXXXXXX`
outside `BO3_VA` / `BO3_SUB`. A number in that range that is no guest
address (an offset in a file, a range bound) says
`region: not an address` on its line. Two pairs with the same NTSC-U value
must agree, and a pair whose PAL value is the NTSC-U one says
`region: same`.

So a new site goes in as: the NTSC-U entry or address as before, then
`port_config.py` with both executables (or the PAL value written beside it
and checked with `xbemap.py`), then `check.py`.

## What stays the same

`Data/Global.txd`, `Data/Frontend.txd`, `sound/Fe.awd` and
`sound/Generic.awd` are the same files on both discs, and the menu fonts are
at the same offsets of both executables, so the overlay's art, sounds,
fonts and button prompts work unchanged.
