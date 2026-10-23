# Mod folders

A mod folder holds files that replace files on the disc. The game reads the
folder's copy instead of the disc's; a file the folder does not have is read
from the disc as before. Nothing is written to the game data, and switching a
folder off brings the original files back.

This is for people making or trying mods of the game's own files: a track's
textures or props in its `static.dat`, a menu file, a race's data. It does not
add anything the game cannot already load: no new tracks, cars or menu entries,
and no textures larger than the ones they replace.

Every build has it: Windows, Linux and the Steam Deck, macOS and Android
(`src/game/modfs.c`: the Win32 file API on Windows, POSIX elsewhere). The rules
below are the same on all of them.

## Turning it on

Set `TAKEDAWN_MODS` to the folder before starting the game. Several folders are
separated by `;` on every system (not `:`, which is part of a path there), up
to eight. The first folder named wins when two have the same file.

Unset, there is no mod folder and nothing changes. Files are looked up when the
game opens them, so a change takes effect at the next start of the game (a
track's files at the next load of that track at the earliest). A relative path
is taken from the folder the game starts in.

**Windows** (Command Prompt):

```
set TAKEDAWN_MODS=D:\Burnout3Mods\Fixes;D:\Burnout3Mods\MyTrackMod
Takedawn.exe
```

**Linux**, from a shell:

```
TAKEDAWN_MODS="$HOME/takedawn-mods/MyTrackMod" ./burnout3
```

The installer's launcher (`burnout3.sh`, `tools/install/linux.py`) also takes
`KEY=VALUE` lines from `launch.env` in the install root, so a line
`TAKEDAWN_MODS=/home/me/takedawn-mods/MyTrackMod` there sets it for every
start.

**Steam Deck**, with the native Linux build added to Steam: the game's
Properties, Launch Options:

```
TAKEDAWN_MODS="/home/deck/takedawn-mods/MyTrackMod" %command%
```

Put the folder somewhere the `deck` user can read and write, in the home folder
or on the SD card (`/run/media/...`), not inside the game's install folder. The
home folder and an SD card SteamOS formatted are case-sensitive (ext4): that is
handled, see Layout. The Windows build under Proton is the Windows case: its
paths are Wine's (`Z:\home\deck\...`).

**macOS**: a program started from the Finder or the Dock does not get a shell's
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

**Android**: no variable is needed. A folder named `mods` in the app's files
folder is the mod folder when it exists:

```
/sdcard/Android/data/io.github.xenophobentx.takedawn/files/
    Burnout 3 Takedown/
    mods/
        Tracks/US/C3_V1/static.dat
```

It is reached the same way as the game data (adb, or USB file transfer); the
app asks for no permission for it. A `TAKEDAWN_MODS=...` line in `env.txt`
there names other folders instead (folders the app may read: its own), and an
empty `TAKEDAWN_MODS=` turns the `mods` folder off.

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
loose files, as before. A mod's subfolder is not read as part of its folder's
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

A folder whose full path is not plain ASCII is ignored (on Windows the path is
read in the guest's code page; elsewhere, in the same way, a `\` in it is not
allowed either). A folder that does not exist is ignored. On Windows a folder
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

With diagnostic logging on (the settings' Advanced > Diagnostic logging, then
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
`TAKEDAWN_MODS` and, if a folder was taken, chains a function onto the
toolkit's `xbox_PathRedirectHook`. For a `D:\` or `\Device\CdRom0\` path whose
file a folder has, it returns the file's host path: `\\?\<path>` on Windows,
the absolute path elsewhere. The toolkit's path translation hands either to the
host unchanged. Every other path goes on down the chain. The result buffer is
per thread. With the variable unset (and, on Android, no `mods` folder), the
hook is not installed.

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
one that keeps each "[MOD] ... -> ..." line to once, as before.

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
