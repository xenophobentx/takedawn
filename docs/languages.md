# Languages from the European discs

The North American disc has English only. There are two European (PAL)
retail discs: "Burnout 3: Takedown (Europe) (Fr,De,It)", with French, German
and Italian, and "(Europe) (En,Es,Nl,Sv)", with English, Spanish, Dutch and
Swedish. Takedawn runs each of them with its own program (docs/pal.md), but a
start runs one disc's data. A player who installed the North American disc
and owns one of the European ones (or both) can add their languages to it
from their own disc: text, menus, films, Stryker's speech and the tutorials'
narration then follow the setting **Language** (Settings, Game). The game
itself stays the North American one, with its saves and settings; nothing of
the European disc's program is used, and no game data is shipped.

## For the player

1. **Add them.** In the setup window, *More languages* (shown once the game's
   data is installed): *Choose European disc...*, then *Add languages*. From
   a running game: Settings, Game, *Add languages from a European disc*
   opens that window; when it is done, *Restart the game* there restarts it.
   From a terminal: `takedawn --install-languages <image-or-folder>`.
2. **Choose one.** Settings, Game, **Language**: English (as shipped)
   and the installed ones: French, German, Italian from one disc, Spanish,
   Dutch, Swedish from the other. It applies at the next start (the Restart
   row). In Dutch and Swedish the DJ and the tutorials' narration stay
   English, as on their own disc (see below).

Speed units are a setting of their own (Game, Speed units): the North
American program shows mph whatever the language.

## What is copied

`--install-languages` tells the two discs apart by their `default.xbe` and
copies the files of that disc's list, each checked against its SHA-256 while
it is copied, to its own folder beside `Burnout 3 Takedown`, with the same
journal and `.complete` marker as the game's own data (the toolkit's
`src/install`):

- `config/disc/pal-languages.manifest` (Fr,De,It disc): its `default.xbe`,
  which only says which disc it is, and 35 files a language, 106 files and
  733 MB in all, to `<data>/Languages/FrDeIt`.
- `config/disc/pal-languages-2.manifest` (En,Es,Nl,Sv disc): its
  `default.xbe` and the 45 files the North American disc has not, but for
  its English text (`Globalen.bin`, `Headen.bin`; the North American one
  has its own): 46 files and 370 MB, to `<data>/Languages/EsNlSv`.

| | French | German | Italian |
|---|---|---|---|
| Text | `Data/GlobalFr.bin`, `HeadFr.bin` | `GlobalGe.bin`, `HeadGe.bin` | `GlobalIt.bin`, `HeadIt.bin` |
| Language film | `ovid/french30.xmv` | `german30.xmv` | `italia30.xmv` |
| Tutorial films | `ovid/TrC_FRA.xmv`, `TrD_FRA.xmv` | `_GER` | `_ITA` |
| DJ speech | `tracks/F_DJ*.xwb` (12), `tracks/<region>/<track>/F_DJRACE.xwb` (18) | `G_` | `I_` |

| | Spanish | Dutch | Swedish |
|---|---|---|---|
| Text | `Data/Globalsp.bin`, `Headsp.bin` | `Globaldu.bin`, `Headdu.bin` | `Globalsw.bin`, `Headsw.bin` |
| Language film | `ovid/spanis30.xmv` | `dutch30.xmv` | `swedis30.xmv` |
| Tutorial films | `ovid/TrC_SPA.xmv`, `TrD_SPA.xmv` | `_DUT` | `_SWE` |
| DJ speech | `tracks/S_DJ*.xwb` (12), `.../S_DJRACE.xwb` (18) | none: English | none: English |

The second disc's other 711 files are the North American disc's, by hash;
only its three executables differ.

Every other file of the two discs is the same (678 of them by hash, the
front-end textures and fonts among them); the European disc has no English.
The manifest is the European disc's full manifest (PR #28,
`config/disc/pal-retail.manifest`) filtered to these files.

## How the game uses them

The two executables are the same program built for two regions. In the North
American one the language index is not limited (`sub_001B5C60`: the
dashboard's XC_LANGUAGE 6, Italian, gives index 6), but the files it opens
are:

- **Text**: `sub_000173B0` always names `Data\GlobalUs.bin` and `HeadUs.bin`.
  The European build's counterpart (around `0x17480` there) picks GlobalFr,
  GlobalIt or GlobalGe; the North American `.text` never refers to them (the
  names are in a `.rdata` table nothing reads).
- **Films**: by film number (`0x383600 + id * 4`), and the North American
  build asks for the English ones (`englis30`, `TrC_ENG`, `TrD_ENG`).
- **DJ banks**: by the prefix `E_`.

So `src/game/langpack.c` changes the language by name and leaves the program
as it is (XC_LANGUAGE stays English). The choice is the toolkit's
`game.language` (the Language row, as on the PAL build): `bo3_languages_init`
(`src/game/main.c`) offers the languages it finds, and with the European
languages installed `bo3_langpack_languages` adds French, German and Italian
to them. When one of those is chosen, the kernel is told nothing (it keeps
reporting English to the North American program) and a path hook
(`xbox_PathRedirectHook`, chained like the mod folders') reads

    Data\GlobalUs.bin       -> Languages/<disc>/Data/Global{Fr,Ge,It,Sp}.bin
    Data\HeadUs.bin         -> Languages/<disc>/Data/Head{Fr,Ge,It,Sp}.bin
    ovid\englis<n>.xmv      -> .../ovid/{french,german,italia,spanis}<n>.xmv
    ovid\Tr{C,D}_ENG.xmv    -> .../ovid/Tr{C,D}_{FRA,GER,ITA,SPA}.xmv
    tracks\...\E_<name>.xwb -> .../tracks/.../{F,G,I,S}_<name>.xwb

only where the manifest lists the counterpart, spelled as the manifest has
it, so a case-sensitive file system finds it. It runs before the mod
folders: a mod's file still comes first.

**The tutorials' narration** is not in the films (`TrC_*.xmv` and `TrD_*.xmv`
are video only) but in `ovid\movie.xwb`, the same file on both discs, which
holds it in six languages (waves 4/5 English, 12/13 German, 17/18 Italian
...). The wave comes from a table by film number (`MEM32(0x383808 + id * 4)`,
read in `sub_00014FB0`): TrC_ENG (11) has wave 5, TrC_ITA (14) wave 18,
TrD_ENG (19) wave 4, TrD_ITA (22) wave 17. Since the North American build
asks for film 11 and 19, `langpack.c` gives those two entries the chosen
language's waves once, at start, and only when they hold the English waves
(5 and 4), as found in this build. The table's film numbers: 10-17 TrC_DUT,
ENG, FRA, GER, ITA, JAP, SPA, SWE; 18-25 TrD_ the same; their waves 5/5/9/13/18/
21/27/5 and 4/4/8/12/17/20/26/4. Spanish has its own (27, 26); Dutch and
Swedish have the English ones, on their own disc too.

**Dutch and Swedish** are `game.language`'s choices 9 and 10 since 0.3
(`languages.h`). No dashboard has them: the En,Es,Nl,Sv program sets them in
the title itself (`recomp_manual_pal_ensv.c`). Here nothing of the kind is
needed: as for the other languages the kernel reports English and the
files are read by name, so `langpack.c` offers them like the others. Their
disc has no DJ banks of theirs and gives their tutorial films the English
narration, so both stay English, as there.

## The pieces

| Where | What |
|---|---|
| `config/disc/pal-languages.manifest`, `pal-languages-2.manifest`, `cmake/release_info.cmake` | the two lists, compiled in (`bo3_langs_manifest.inc`, `bo3_langs2_manifest.inc`, `BO3_LANGS_ID`, `BO3_LANGS2_ID`) |
| `src/game/langpack.c`, `langpack.h` | the two discs (their lists, folders under `Languages`, `bo3_langpack_disc`) and which are installed (`bo3_langpack_installed`); the path hook and the tutorial waves |
| `src/game/install.c` | `--install-languages <image-or-folder> [--dest <dir>] [--check]`: `--install`'s protocol with the step `languages`, never another disc's program; `bo3_add_languages_run`, the overlay row's program |
| `src/game/main.c` | `bo3_languages_init` asks `bo3_langpack_languages` for the installed languages; *Add languages* registered on the North American program while a disc's are missing |
| toolkit `src/overlay` | `game.language` and its Language row (upstream); *Add languages from a European disc* (`xbox_OverlaySetAddLanguages`) |
| `tools/install/gui` | *More languages*; `--from-game --game-pid`: opened by the game, no Play while the game runs, exit status 10 asks the game to restart, Play again if the game is closed |

## Tested

On Linux (CachyOS, AMD, Vulkan), with the NTSC-U data installed and a PAL
disc image (its 786 files matching PR #28's manifest):

- `--install-languages`: the PAL image accepted, the NTSC-U disc refused
  (`wrong-version`), 106 files copied, a second run `SKIP already installed`,
  a removed file copied again alone.
- Italian and French: text, menus and the language film read from
  `Languages` (log), English unchanged; on screen, Italian menus, text,
  Stryker in World Tour and in races, the driving tutorial's narration.
- The second disc (En,Es,Nl,Sv, 761 files): told apart from the first by its
  `default.xbe`, 46 files copied to `Languages/EsNlSv`, a second run skipped;
  with both installed the Language row's set is 0x63D (English, German,
  French, Spanish, Italian, Dutch, Swedish), Spanish reads
  `Languages/EsNlSv` (log) and the main menu is in Spanish (headless shot),
  Italian still `Languages/FrDeIt`.
- On 0.3 (2026-11-05): both discs installed again by `--install-languages`,
  the North American disc refused naming both European ones, a second run
  skipped; headless the main menu in Italian, Spanish, Dutch and Swedish
  (its accented capitals drawn); on screen the Language row with all seven
  and Italian as before.
- The setup window: a wrong file refused with its reason, the PAL image
  accepted, the copy with its progress, then installed; opened from the game,
  *Restart the game*, and the game closed by hand (Play comes back, the
  language is read from the settings file, not locked).

Not tested: Windows and macOS; the Crash tutorial's narration on screen (the
same table and swap as the driving one); German, Spanish, Dutch and Swedish
in a race on screen.
