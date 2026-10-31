# Contributors

Everyone who has moved this project forward, and what they did. If you've
contributed and aren't listed, or a line here is wrong, open a PR against this
file.

## Maintainer of Takedawn

### xenophobentx
The macOS, Linux, Steam Deck, Windows and Android ports, the Vulkan backend,
the installer, the settings menu and the optional picture and sound features.

## The upstream project

### Ned Heller, [@sp00nznet](https://github.com/sp00nznet)
Creator of [burnout3](https://github.com/sp00nznet/burnout3) and of the
[xboxrecomp](https://github.com/sp00nznet/xboxrecomp) toolkit, which Takedawn
grew from. The recompilation, the move onto the shared xboxrecomp runtime, and
the runtime work that got every mode playing.

### [apexJCL](https://github.com/apexJCL)
The fork of the toolkit that Takedawn's runtime is based on, with the Metal
backend and the first macOS host.

## Code contributions

### [Naitrate](https://github.com/Naitrate)
The High refresh rate setting (#33, with xboxrecomp#3): the game keeps its 60
steps a second and the picture is drawn between them at the display's rate.
Off by default, and the same as the original when off. Shipped in 0.2.1 after
we moved it onto the port's new frame pacing.

### [jillsandwich1998](https://github.com/jillsandwich1998)
The mouse cursor that hides itself after 2.5 seconds without movement and when
a key or a controller button is pressed (xboxrecomp#2). Shipped in 0.2.1.

### [d-b-c-e](https://github.com/d-b-c-e)
Native Windows builds from Git Bash (#12), and the fix for drives with 4K
sectors (xboxrecomp#1).

### [guenthermario](https://github.com/guenthermario)
The PS2 driving layout in two-player games (#102): the setting was only
applied in one-player stages, so split screen kept the Xbox layout.

### [HeartWorthBreaking](https://github.com/HeartWorthBreaking)
Optional mod folders: disc files replaced by files in folders named by
`TAKEDAWN_MODS` (Windows, Linux, macOS; on Android the app's `mods` folder).

### [magicseb](https://github.com/magicseb)
The European (PAL) retail disc as a build option (#28): `BO3_REGION=PAL`, the
address map between the two executables in `tools/region/`, the PAL install
flow and packages that sit beside the NTSC-U ones.

### [ulrikadam](https://github.com/ulrikadam)
Brought the PAL build up to date with 0.2.1 (the high refresh wrappers, the
patch sites and guest addresses the MODS page and soft-body damage added), no
Speed units row on the metric PAL disc, and the disassembler fix for the
downward jump table in memcpy (xboxrecomp#9) that the PAL build needs. Tested
the PAL Windows package on a real disc (#108) and found that the shipped
shader lists had been skipped since 0.2.1. Made tools/release/build-windows.sh
run on Windows in Git Bash (#109).

### [itsDNNS](https://github.com/itsDNNS)
The Language setting (`game.language`, with xboxrecomp#5): the game's text,
speech and films in one of the languages on the disc.

## Issue reports and testing (upstream)

### [@VincxBlox](https://github.com/VincxBlox)
Reported #1, *Unclear instructions in the readme, and not runnable*. It was
right on every count. The README described three build steps that didn't
produce a working game, pointed at a naming tool that wasn't in the repo, and
never said the result would show only a black screen at a steady 32 fps. That
build was a hand-written fork: fake menus and a fake road drawn over a game that
never really started. The report pushed the rewrite onto the xboxrecomp
runtime, where the title boots through its real entry point, plus a README whose
steps are tested from a clean folder and a one-click `Setup.cmd` (both in the upstream repository).
