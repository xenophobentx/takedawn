# Mod folders

A mod folder holds files that replace files on the disc. The game reads the
folder's copy instead of the disc's; a file the folder does not have is read
from the disc. Nothing is written to the game data, and switching a
folder off brings the original files back.

This is for people making or trying mods of the game's own files: a track's
textures or props in its `static.dat`, a menu file, a race's data. It does not
add anything the game cannot already load: no new tracks, cars or menu entries.
A replaced file keeps the size its format allows; to show a texture larger than
the original, use a [texture pack](#texture-packs).

Every build has it: Windows, Linux and the Steam Deck, macOS and Android
(`src/game/modfs.c`: the Win32 file API on Windows, POSIX elsewhere). The rules
below are the same on all of them.

## Turning it on

Put mods in the default mods folder; the game reads it at the start when it
exists, with nothing to set (BO-327: a Steam Deck in Game Mode has no shell to
set a variable from). The settings' Mods page names it on its first line:

| Where | The default mods folder |
|---|---|
| Linux and the Steam Deck | `~/.local/share/takedawn/mods` |
| macOS | `~/Library/Application Support/Takedawn/mods` |
| Windows | `%APPDATA%\Takedawn\mods` |
| Portable mode (docs/windows.md) | `data/mods` next to the program |
| Android | `mods` in the app's files folder (below) |

Make the folder if it is not there yet. Each mod is a folder of its own in it,
with a `mod.toml` (Manifests, below); loose files there work too, as in any
mod folder.

`TAKEDAWN_MODS` names more folders, read before the default one. Several are
separated by `;` on every system (not `:`, which is part of a path there), up
to eight. The first folder named wins when two have the same file. Set but
empty (`TAKEDAWN_MODS=`), it turns the default folder off.

With neither, there is no mod folder and nothing changes. Files are looked up when the
game opens them, so a change takes effect at the next start of the game (a
track's files at the next load of that track at the earliest). A relative path
is taken from the folder the game starts in.

Windows (Command Prompt):

```
set TAKEDAWN_MODS=D:\Burnout3Mods\Fixes;D:\Burnout3Mods\MyTrackMod
Takedawn.exe
```

Linux, from a shell:

```
TAKEDAWN_MODS="$HOME/takedawn-mods/MyTrackMod" ./burnout3
```

The installer's launcher (`burnout3.sh`, `tools/install/linux.py`) also takes
`KEY=VALUE` lines from `launch.env` in the install root, so a line
`TAKEDAWN_MODS=/home/me/takedawn-mods/MyTrackMod` there sets it for every
start.

Steam Deck, with the native Linux build added to Steam: the game's
Properties, Launch Options:

```
TAKEDAWN_MODS="/home/deck/takedawn-mods/MyTrackMod" %command%
```

Put the folder somewhere the `deck` user can read and write, in the home folder
or on the SD card (`/run/media/...`), not inside the game's install folder. The
home folder and an SD card SteamOS formatted are case-sensitive (ext4): that is
handled, see Layout. The Windows build under Proton is the Windows case: its
paths are Wine's (`Z:\home\deck\...`).

macOS: a program started from the Finder or the Dock does not get a shell's
variables, so start the game's own executable in the bundle from Terminal:

```
TAKEDAWN_MODS="$HOME/Library/Application Support/Takedawn/mods" \
    /Applications/Takedawn.app/Contents/MacOS/Takedawn
```

(or wherever `Takedawn.app` is). Keep mod folders out of the app bundle:
changing a file in it breaks the bundle's seal (`codesign --verify`). A folder
under `~/Library/Application Support/Takedawn` (the save folder) is a good
place; folders the system guards (Desktop, Documents, Downloads) may need
Terminal to be allowed to read them.

Android: no variable is needed. A folder named `mods` in the app's files
folder is the default mods folder:

```
/sdcard/Android/data/io.github.xenophobentx.takedawn/files/
    Burnout 3 Takedown/
    mods/
        Tracks/US/C3_V1/static.dat
```

It is reached the same way as the game data (adb, or USB file transfer); the
app asks for no permission for it. A `TAKEDAWN_MODS=...` line in `env.txt`
there names more folders, read before it (folders the app may read: its own),
and an empty `TAKEDAWN_MODS=` turns the `mods` folder off.

## Layout

A mod folder mirrors the disc. To replace the disc's
`Tracks\US\C3_V1\static.dat`:

```
MyTrackMod/
    Tracks/US/C3_V1/static.dat
```

Names are matched without regard to case, as on the disc (the game itself asks
for some files in other cases than the disc has). On a volume that does not
fold case (Linux, the Steam Deck, a case-sensitive Mac volume) each part of the
path is taken as the game writes it when the folder has it, else the one entry
that differs only in case. If a folder has two or more entries that differ only
in case (`Data` and `DATA`) and none is spelled as the game asks, that file is
refused and the disc's is read (the log says so, once): which one the system
lists first is not a rule. Give each name one spelling.

## Installed mods (mod.toml)

A mod folder can also hold mods, each in a subfolder of its own with a
`mod.toml` that names it. The mod's replacement files mirror the disc in its
`files` subfolder:

```
MyMods/                          <- the folder TAKEDAWN_MODS names (Android: mods/)
    Tracks/US/C3_V1/static.dat   <- loose files work as before
    car-pack/
        mod.toml
        files/Tracks/US/C3_V1/static.dat
    sound-pack/
        mod.toml
        files/Sound/...
```

Only these top-level keys are read:

```toml
id = "example.car-pack"        # required: 1-64 of a-z 0-9 . _ -, starting with a letter or digit
name = "Example Car Pack"      # required, up to 128 bytes
version = "1.0.0"              # optional, up to 32 of A-Z a-z 0-9 . + -, starting with a letter or digit
author = "Someone"             # optional, up to 128 bytes
description = "What it does."  # optional, up to 1024 bytes
```

The file is UTF-8 and at most 8 KiB. `name`, `author` and `description` may not
contain control characters (no line breaks), nor the Unicode line and paragraph
separators or the marks and controls that change the direction of text.

`mod.toml` is read by the toolkit's configuration reader, which takes a part of
TOML: tables, `key = value` lines with strings (basic and literal, on one line),
integers, floats, booleans and arrays of those, comments, quoted and dotted
keys. It does not take inline tables (`a = { b = 1 }`), arrays of tables
(`[[a]]`), nested arrays, dates and times, or multi-line strings (`"""`), and
a file that has one anywhere, under any key, is refused. That part is the
format: keys a later version adds will keep to it, and other keys and tables
written in it are left alone, so a manifest written for a later version still
works here. A key given twice, a known key that is not a string (a number, an
array, a table such as `[version]`), a `\u0000`
escape anywhere in the file, or text outside that part of TOML makes the mod
invalid. An invalid mod is listed in the log and replaces nothing.

Which file is read when several have it:

1. the folders `TAKEDAWN_MODS` names, in that order;
2. in each folder, its loose files first, then its mods in order of `id`
   (byte order, so `a.mod` before `b.mod`). The subfolder's name plays no part:
   renaming it does not change the order.

Two mods with the same `id` (in one folder or in two) are both refused: the
log says which. A folder named twice in `TAKEDAWN_MODS`, under the same or
another path (a link, a junction, a short name), is taken once, in its first
place; it still counts toward the eight.

A subfolder is a mod when it has an entry named `mod.toml` (in lower case, on
every system: `MOD.TOML` is not a manifest), whatever that entry is:
a `mod.toml` that is a folder, a link or not readable makes an invalid mod.
Only subfolders one level down are looked at. A folder with more than 1024
entries is not searched for mods (the log says so), and at most 64 mods are
taken, valid or not: the folders in order, in each the subfolders by name
(byte order); the log names the first one left out. A `mod.toml` directly in
the folder `TAKEDAWN_MODS` names is not a manifest: that folder's own files stay
loose files. A mod's subfolder is not read as part of its folder's
loose files, even when it has the name of a folder on the disc. A subfolder
that is not taken as a mod (past the 64, or in a folder that is not searched)
is part of the loose files, like any other folder; so, on Windows, is a mod's
subfolder reached by its short (8.3) name.

A mod's files follow every rule below. Its subfolder, its `files` folder and
its `mod.toml` must not be links (or junctions), and `mod.toml` must be a
regular file. The `files` folder may be spelled in any case, but only once:
`files` and `Files` side by side make the mod invalid. A mod whose folder's
path is so long that no file below it would fit (see What is refused) is
invalid too.

The list of a mod's files is made at the start, so a file added to a mod while
the game runs is used from the next start, as is a mod added or changed. For a
mod with more than 4096 files, or folders more than eight levels below `files`,
no list is made: each file is looked up in it on disk instead, as for loose
files, which is slower (the log says so).

## Turning mods on and off

Every mod is on unless the mod list names it as off. The mod list is
`mod-list.toml`, beside the game's `settings.toml` (in the save folder when
there is none); `TAKEDAWN_MOD_LIST` names another file instead (an empty
value: no mod list, every mod on). It is read at the start when a mod folder
has installed mods:

```toml
# Takedawn: mods that are off, by id (docs/mod-folders.md). Read at start.
disabled = ["example.car-pack", "example.sound-pack"]
```

- The file is the game's own; it can be edited by hand while the game is not
  running. No file means every mod is on.
- A change is used from the next start, like a new mod. An off mod's files are
  not used; it is still listed in the log, with `(off)`, and its subfolder is
  still not part of the loose files.
- Ids of mods that are not installed stay in the list, so a mod that is taken
  out and put back stays off.
- An entry that is not a mod id is skipped, and the log says so. The list keeps
  at most 256 ids. Skipped entries and ids past 256 are dropped when the game
  next writes the list.
- A file that is not a list of ids (not TOML, `disabled` not a list, larger
  than 64 KiB) leaves every mod on, and the log says why. When the game next
  writes the list, that file is first kept as `mod-list.toml.bad` (replacing
  an older one).
- A file that cannot be read at that moment (another program has it open)
  also leaves every mod on; the game does not write the list then, so nothing
  in it is lost.
- The game writes a new `mod-list.toml.tmp`, flushes it to the disk and
  renames it over the list. If it stops part way, or the write fails, the list
  stays as it was (the log says so).

Turning a mod off is also how to choose between two mods that have the same
file. The order itself is fixed (see Which file is read).

### The MODS page

With a mod folder in use, the game's own Settings menu (Driver Details >
Settings) has one more line, MODS. It opens a page in the same look, with the
same sounds and the same buttons as the rest of the menu:

```
A.FIRST  ON                  <- a mod and whether it is on
B.SECOND  OFF*               <- * : different at the next start
BROKEN  INVALID              <- a mod that cannot be used (the reason below)
APPLY - RESTART REQUIRED     <- A: save the choices
V1.2  BY ANN  1 CONFLICTS    <- about the line under the cursor
FIRST MOD, WITH A DESCRIPTION.
```

- A on a mod turns it on or off (a choice, not yet saved). A on APPLY saves
  the choices in the mod list; they are used from the next start of the game
  (SAVED - RESTART REQUIRED). If the list cannot be saved, the line says
  NOT SAVED - SEE THE LOG and the choices stay, to try again. With
  `TAKEDAWN_MOD_LIST=` (empty: no mod list) nothing can be saved.
- B goes back to Settings. Choices not applied are kept until the game quits.
- Six mods fit on one page; with more, four are shown at a time, with
  PREVIOUS PAGE and NEXT PAGE.
- Names, versions, authors and descriptions are shown in the menu's font:
  letters in upper case, digits and plain punctuation as they are, any other
  character as `?`, long text cut.
- A mod that cannot be used (an invalid `mod.toml`, an id another mod has) is
  listed with INVALID or SAME ID; with the cursor on it, the details line says
  why (for a shared id, the other mod's folder name).
- The last line shows the description of the mod under the cursor, else how
  many mods are installed and how many are on.

`TAKEDAWN_MODS_MENU=0` hides the page. Without a mod folder there is no MODS
line, and the Settings menu has its usual lines.

The page needs four marks in the generated code (`si_items`, `si_select`,
`mm_flow_sel`, `mm_flow_back`, config/patches.json). The build checks that
the generated code calls each of them (cmake/modsmenu_marks.cmake): generated
code made before them (no `regen.sh` after an update) builds a game without
the MODS line, with a warning, and a release build stops. At the start the
game also checks that the two flow marks reach the page; if not, the log says
`[MODS] the MODS page is not listed: ...`.

### Conflicts

At the start the log names every file of a mod that is on that another place
also supplies, and which one is used:

```
[MOD] conflict: tracks/us/c3_v1/static.dat: mod example.car-pack over mod example.other-pack, folder 2
```

Places are a mod (`mod <id>`) or a folder's loose files (`folder <n>`); the
one used is the one a lookup takes. The first 32 conflicts are listed, then a
count. The files compared are those in the lists of mods that are on (see
below): a file that two folders' loose files share, or that loose files and a
mod without a list share, is not reported. With more than 1024 different files
in those lists, mods with a list are compared by their lists alone and the
others not, and the log says so.

### The Mods tab of the settings (BO-316, BO-327)

The Takedawn settings (F1, or Back and Start on a pad) have a Mods tab before
System (`src/game/modstab.c`, on the toolkit's host page, `overlay.h`
`xbox_OverlaySetHostPage`). Its first line names the default mods folder
("Mods folder: ~/.local/share/takedawn/mods", the table above), the second
says how many mods are installed and in use, or, with none, "No mods are
installed. Put each mod in its own folder in the mods folder, then start the
game again." A third line names the first `TAKEDAWN_MODS` folder when the
variable names any. Then one row for each mod with its name, version and
author from `mod.toml` and a switch, with the toolkit's NEXT START tag after
the name (lit while a saved change waits for the next start); a mod that cannot
be used is dimmed and says why in the help line. A mod turned on or off is
saved to the mod list at once (BO-338): there is no Apply row. A note under the
list says "Restart the game to use the saved mods." while a change waits, and
the line naming `TAKEDAWN_MODS`'s folders is an advanced row (the page's "Show
advanced settings" switch). Under the list, the "Sharper menu pictures" rows
(BO-311, `upscale.c`: Upscale menu pictures with AI, Remove the upscaled menu
pictures), which are on this page instead of the Graphics page: they make a
mod of their own. "Reset this page to the defaults" (with mods installed) switches every
mod that can be used back on and saves the list, as a reset does on the other
pages; it asks once more first.

It is the same list and the same choices as the MODS page: one state, kept in
`modsmenu.c` and used by both through `src/game/modsmodel.h`. A mod turned on
or off in the tab shows on the MODS page (which redraws its lines at its next
frame) and the other way round. The tab saves the choice as it is made; the
MODS page keeps its APPLY line, which saves its choices to the mod list as
above (used from the next start). A choice made there and not applied is saved
by the next change in the tab, and the tab says so under the list.

Where a change is put to use without a restart (the generated packs of the
upscaler, BO-311), there is one place to wire it: `bo3_mods_set_apply_hook`
(modsmodel.h). Both views call the same apply, which saves and then calls the
hook with the number of mods changed; a hook that returns 1 has put them to
use, and the apply line then says APPLIED instead of that a restart is needed.
The tab lists whatever modfs reports (`bo3_modfs_mod`), so a pack modfs lists
shows in both views with nothing more to do. `tools/headless/modstab_scene.sh`
drives it (WINDOW=1).

## Texture packs

A mod can carry a `textures` folder of PNG files. A texture the game loads
that has a file in a pack is drawn from the PNG instead, at the PNG's own
size. Dolphin and PCSX2 load texture packs the same way: a file is named by
the content of the texture it replaces, so no disc file is touched.

```
MyPack/
    mod.toml
    textures/
        pack.toml                          <- optional
        7f43ae6699b33558.png               <- a texture
        81ad2f599a7fe2fe-708d8ee4b91dfab8.png   <- a paletted (P8) texture
```

A pack is a mod like any other: it is listed on the MODS page, turned on and
off there (APPLY, then restart) and ordered by its id. A mod can hold both
files (`files`) and textures. A mod folder's own `textures` folder, beside the
mods, is a pack too. The folder is named `textures`; on a volume that folds
case any spelling works, elsewhere give it once, in any case.

### Names

`<hash>.png` is 16 hexadecimal digits: the hash of the texture's bytes in the
game's memory (level 0, as the console stores it). A paletted texture (P8) is
`<hash>-<palette>.png`: the game recolours one set of indices with several
palettes, and each palette is its own picture. A P8 texture with no file for
its palette uses `<hash>.png` if there is one. Hex digits and `.png` may be
in either case. Any other name (`readme.txt`, `.png.bak`) is ignored.

The game writes these names for you (see Making a pack).

### Size, mips and filtering

The PNG can be any size up to 8192 by 8192 (and at most 8192 by 4096 in
area). The scale is read for each texture from the PNG against the original:
2x, 3x, 4x, a PNG that is 4x wide and 2x high, a PNG smaller than the original.
Different textures in one pack can have different scales.

- The levels below the PNG are made when it is loaded (a 2x2 average of each
  channel). A texture the game draws with one level gets the levels down to its
  own size, so a small picture still looks right.
- The game's filtering, wrapping and coordinates stay as they are. The game
  limits how far a texture may be blurred; that limit moves down with the
  scale, so a PNG 4x as large starts two levels up.
- A texture an alpha-tested draw uses (trees, fences) gets levels that keep as
  many texels above the test as level 0 has, as the game's own textures do
  (the setting is `alpha_mips`).

### Alpha

`textures/pack.toml` sets how a pack's alpha is chosen. Both keys are
optional; anything else in the file is ignored.

```toml
alpha = "auto"       # auto (default), original or png
compress = "auto"    # auto (default) or off
```

- `auto`: the PNG's alpha when the file has an alpha channel; a file without
  one takes the original's alpha, scaled up smoothly to the PNG's size.
- `original`: always the original's alpha, scaled up the same way. The file's
  alpha channel is ignored. Use it for art from an upscaler that cannot make
  alpha.
- `png`: the file's alpha; a file without it is opaque.

The colours under an alpha of 0 are never changed. The game uses some alpha
as a mask (shine on a car, a cut-out) and still reads the colour there, so
whatever your tool left under it is what the game draws.

### Which mod wins

A texture can be in more than one pack. The first pack in file order wins,
the order used for replaced files: each mod folder named in `TAKEDAWN_MODS`
in turn, in it its own `textures` folder, then its mods by id. A pack that is
off is not looked at.

### Made on this computer

The game also takes the folder `generated-mods` in its per-user data folder as
a mod folder, after the ones `TAKEDAWN_MODS` names (and alone when that is not
set). "Sharper menu pictures" ([upscale-art.md](upscale-art.md)) writes its
texture pack there, as the mod `takedawn.ai-upscaled-art`; it is on the MODS
page like any other. `TAKEDAWN_GENERATED_MODS` names another folder, an empty
value turns it off.

### Making a pack

Run the game with the dump switch to get every texture as a pack file:

```
RECOMP_DEBUG=texrep_dump=/path/to/dump ./burnout3
```

On Windows set `RECOMP_DEBUG` the same way before `Takedawn.exe`, on a Mac
start the executable inside the app from Terminal (see Turning it on).
Each texture the game loads is written once as a PNG with the name a pack
uses (`<hash>.png`, or `<hash>-<palette>.png` for P8), as the game decoded
it, before any other change. Play to the screens you want; a track's textures
are written when the track loads. `index.tsv` in the same folder has a line
for each file: hash, palette hash (0 if none), width, height, the format
number, levels. `texrep_dump_mips` also writes the game's own lower levels as
`<name>_m<level>.png`; they are not part of a pack.

Edit or upscale the PNGs and put the ones you changed in a mod's `textures`
folder, with the same names. Files you leave out are not replaced. The dumped
files are the game's own pictures: keep them on your machine.

The dump covers every texture the game loads from its memory, from the HUD to
road signs. Video frames, screen copies and cube maps cannot be replaced and
are not written. A texture the game reads as signed data (water bumps) is
written, but a pack does not replace it.

### Loading and memory

The PNGs are read, scaled, mipped and compressed by worker threads, so the
game does not stop for them. A texture shows the original until its
replacement is ready, which is a moment on first sight. The index of file
names is read at the start.

Big pictures are compressed to BC1 (no alpha) or BC3 (alpha) when the graphics
card can read those formats, which makes them a quarter to an eighth of the
size. A pack of 4x art for every menu and loading picture is about 840 MB raw
and about 210 MB compressed. Compressed results are kept in a `texpack` folder
beside the shader cache, up to 4 GB, so the next start does not decode the PNG
again. Pictures under 128 by 128, and packs with `compress = "off"`, stay
uncompressed.

What the graphics card keeps for packs is capped: 1 GB, 256 MB on a Steam Deck
and on Android. Over it, the pack texture used longest ago goes back to the
original and is loaded again if it is used again. `RECOMP_DEBUG=texpack_cap_mb=<n>`
sets the cap, `texpack_swap_mb=<n>` the bytes a frame may upload (24 by
default), `texpack_compress=0` turns compression off, `texpack_log` prints a
line for each texture, and `texpack=<folder>[;<folder>]` loads packs without
the mod system.

Android drivers mostly cannot read BC textures, so a pack there stays
uncompressed and the cap matters more.

## What is refused

The disc's own file is read instead when:

- the folder has no such file, or the name is a folder;
- something below the mod folder is a link: a symbolic link, and on Windows a
  junction or any other reparse point (a mod folder cannot point outside
  itself). The mod folder itself may be reached through links: its full path
  is resolved once, at the start;
- the file is not a regular file (a pipe, a device);
- a path part is `.` or `..`, ends in a dot or a space, or holds a drive,
  `:`, a character Windows does not allow in a name (`< > " | ? *`), a control
  character or a byte that is not plain ASCII;
- two names in the folder differ only in case (see Layout);
- the resulting path is too long for the toolkit: more than 255 characters
  on Windows (the toolkit puts `\\?\` in front of it), 259 elsewhere.

These rules are the same on every system, so a mod folder that works on one
works on the others.

A folder's own path may hold any letters (a user name with an accent). On
Windows, where the toolkit reads the path in the guest's code page, such a
folder is used by its short 8.3 name; on a volume with short names turned off
it is ignored. Elsewhere a `\` or a control character in it is not allowed. A
folder that does not exist is ignored. On Windows a folder
must be on a drive letter (`C:\...`): a network share (`\\server\share\...`)
and a path written with `\\?\` or `\\.\` are ignored.

The checks are made when the game opens a file; the toolkit then opens the
path. Someone who can change the mod folder while the game runs could swap a
part for a link in between: a mod folder is trusted as much as the files in it.
Nothing in a mod folder is run; the game only reads it.

## What is not checked

The game's own loaders read the file. Nothing checks that a replacement is a
valid file of its kind: a damaged or mismatched file can make the game draw
garbage, show the missing-texture colour (magenta), stall or crash. Replace a
file only with one made for the same file of the same disc (the NTSC-U disc
Takedawn installs, manifest `81dc20fd647fb0b1`), and keep its size unless you
know the format allows otherwise.

## Seeing what happened

At the start, every folder taken and every one ignored is written to the log,
in every build:

```
[MOD] folder 1: D:\Burnout3Mods\MyTrackMod
[MOD] ignored (not a folder): D:\Burnout3Mods\Typo
[MOD] mod example.car-pack 1.0.0: D:\Burnout3Mods\MyTrackMod\car-pack
[MOD] mod ignored (mod.toml: id is missing): D:\Burnout3Mods\MyTrackMod\broken
```

With diagnostic logging on (the settings' System > Diagnostic logging, then
a restart; or `RECOMP_LOG_DIAG=1`), each replaced file is written too, once:

```
[MOD] D:\tracks\US\C3_V1\static.dat -> D:\Burnout3Mods\MyTrackMod\tracks\US\C3_V1\static.dat
```

On Linux, macOS and Android the host path is the folder's own spelling, for
example `/home/deck/takedawn-mods/MyTrackMod/Tracks/US/C3_V1/static.dat`.
A file refused because two names in a folder differ only in case is written
the same way, with diagnostic logging on.

The log is stderr; `RECOMP_STDIO_LOG=<file>` writes it to a file
([running.md](running.md)). On Android it is `takedawn.log` in the app's
files folder and logcat (tag `takedawn`).

## How it works

`bo3_modfs_init` (called after the soundtrack is set up) reads
`TAKEDAWN_MODS`, then takes the default mods folder (`bo3_modfs_default_dir`,
set by `main.c`) when it exists, and, if a folder was taken, chains a function onto the
toolkit's `xbox_PathRedirectHook`. For a `D:\` or `\Device\CdRom0\` path whose
file a folder has, it returns the file's host path: `\\?\<path>` on Windows,
the absolute path elsewhere. The toolkit's path translation hands either to the
host unchanged. Every other path goes on down the chain. The result buffer is
per thread. With the variable unset and no default mods folder, the hook is
not installed.

On Windows each part is checked with `GetFileAttributesA`. Elsewhere the lookup
walks from the mod folder down with directory descriptors (`openat` with
`O_NOFOLLOW`, `fstatat` with `AT_SYMLINK_NOFOLLOW`) and reads a directory
(`fdopendir`) only when a part is not there as written.

At the start `bo3_modfs_init` also looks for installed mods (one level down,
names sorted) and reads each `mod.toml` with the toolkit's TOML reader
(`recomp_cfg`), then fixes the order of the places a lookup tries: each
folder's loose files, then its mods by id. For each mod it lists the files
below `files` (sorted, lower case), so a lookup skips a mod that does not have
the file without touching the disk. A lookup through a mod that has it checks
the path from the mod folder down, the mod's subfolder and `files` included,
in the same way as a loose file. The mods and their lists do not change while
the game runs (`modfs.h`), so reading them takes no lock; the only lock is the
one that keeps each "[MOD] ... -> ..." line to once. The saved mod
list has a lock of its own, so saving it never holds a lookup up. Mods that
are off have no place in the lookup. The
mod list's file is named by `main.c` (`bo3_modfs_state_file`) and changed
through `bo3_modfs_set_enabled`, the call a front end uses.

`tests/modfs` checks the rules on folders it makes in the temporary directory
(no game data): `ctest -R bo3_modfs`. `tests/modmanifest` checks `mod.toml`
itself: `ctest -R bo3_modmanifest`.

## Verified

Windows 11 (build 26300), clang 23.1.2 (llvm-mingw 20260922), Vulkan on an
AMD Radeon RX 9070 XT, the NTSC-U disc; 2026-10-21. Silver Lake (US/C3_V1)
Single Event race, from the main menu: with the variable unset, the game is
unchanged and the retail texture is uploaded; with a folder holding a
`static.dat` whose junction texture was recoloured, the recoloured texture is
drawn and the original is not uploaded. Both finish a full three-lap race with
the game's AI driver. A folder that does not exist, an empty folder and a file
reached through a junction leave the retail file in use; of two folders with
the same file the first one's is read; a malformed texture record is drawn in
the missing-texture colour without a crash. The path refusals, a drive root as
a folder and eight threads looking files up at once were checked with
synthetic files. No measurable change in frame times with the variable set.

Windows, after the POSIX version: the game builds with the same regen key and
patches; `tests/modfs` and the first version's checks pass; the cases above,
plus invalid paths, mixed case, two folders and the custom soundtrack's hook in
the same chain, were run in a race again. Lookups cost what they did before.

`tests/modfs` elsewhere (2026-10-21, warnings as errors, no game data):

- Linux (Ubuntu 24.04, gcc 13.3, case-sensitive `/tmp`): 67 of 67 pass, and
  again under AddressSanitizer + UndefinedBehaviorSanitizer and under
  ThreadSanitizer with nothing reported.
- macOS (Apple clang 17): 60 pass, 1 skipped (the case ambiguity needs a
  case-sensitive volume) on the usual volume; 67 of 67 on a case-sensitive
  APFS volume.
- Android (NDK clang 21, arm64, API 33): compiles and links; not run.

Not checked: the game itself with mod folders on Linux, the Steam Deck (the
Linux code; the Windows build under Proton neither), macOS or Android; on
Android the `mods` folder default on a real device. A passing test does not
rule out the check-then-open window in "What is refused". Files other than
`static.dat` in a race are not checked.

Texture packs (2026-10-29), a test pack of 42 PNGs (40 loading screens at 4x
with red and blue swapped, two P8 car pictures at 4x and 2x, one of them a PNG
without alpha), the front-end tour of `tools/headless/fe_scene.sh` (main menu
to the race loading screen), out-of-box settings:

- Vulkan on Linux (Ubuntu, AMD integrated GPU, RADV), headless, and Metal on a
  Mac (Apple M5): the car pictures are swapped in a flip after first sight
  (128x64 to 512x256 and 256x128, BC3), a loading screen about ten flips after
  (512x256 to 2048x1024, BC1, 1.3 MB). The screens show the pack's colours,
  the car pictures keep their cut-out shapes. With the mod off on the MODS
  page's list, the log has no texture pack line and the original pictures
  are drawn. The names `texrep_dump` wrote on Linux matched the names worked
  out from the disc's files for the 17 pack textures that run drew, including both
  P8 pictures.
- A cap of 1.4 MB sent the two car pictures back to the original when the
  loading screen needed the room. A second run on the same cache folder read
  two of three results from the disk cache instead of the PNGs.
- Not checked: Windows and Android (they build the same code), the Steam
  Deck, a full pack of all 252 menu and loading pictures in play.
