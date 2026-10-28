# Native plugins

An installed mod (see [mod-folders.md](mod-folders.md), Installed mods) can
bring a native plugin: a shared library that Takedawn loads at the start. The
first version of the plugin API gives a plugin very little: it is started,
can write lines to the game's log, and can be told when the game ends. It
cannot yet see or change anything in the game.

## Read this first

A plugin is ordinary program code. It runs inside the game's process with
your permissions: it can read and change your files, use the network and
crash or hang the game, like any program you start. Nothing about plugins is
sandboxed, and a fault in a plugin is a fault in the game. Run only plugins
from people you trust.

Takedawn decides only **which** files may be loaded and **when**, never what a
plugin does once it runs. Loading a library also runs the library's own
start-up code before Takedawn can look at it, so a module that is refused
because its exports are wrong has still run that code: "refused" means "not
started through the API", not "never ran". Takedawn never unloads a plugin.

## When a plugin runs

At the start, after the mod folders are read and before the game itself
starts, on the game's main thread, once. A plugin is loaded only when all of
these hold:

1. plugins are not turned off: `TAKEDAWN_PLUGINS=0` turns them all off, whatever
   anything else says;
2. this system loads plugins: Windows (`.asi` or `.dll`) and Linux (`.so`);
   macOS (`.dylib`) only with `TAKEDAWN_PLUGINS=1` until it has been tried
   there; never on Android;
3. its mod is usable and on (the mod list; the MODS page);
4. its `mod.toml` names a module for this system, for a plugin API version
   this Takedawn has;
5. the module is a regular file inside the mod's own folder, not in its
   `files` folder, and reached without a link, junction or reparse point;
6. **you approved it**: the approval file lists the mod's id with the SHA-256
   of the module's bytes as they are now.

A plugin that is copied in is therefore not run until you approve it, and a
plugin whose file changes (an update, or someone else's change) is not run
until you approve the new bytes. Files that are not declared are never
loaded: a stray `.asi` in a mod folder, in its `files` folder or beside the
game is ignored.

Plugins are loaded in the mods' order (the folders in `TAKEDAWN_MODS` order,
in each its mods by `id`). A change (a new plugin, an approval, a mod turned
off) is used from the next start: nothing is loaded or unloaded while the
game runs.

A plugin that cannot be loaded or started is listed in the log with the
reason. Its mod's other files are still used, and other mods are not
affected.

## Declaring a plugin (mod.toml)

```toml
id = "example.hello-plugin"
name = "Hello Plugin"
version = "1.0.0"

[plugin]
api = 1                      # the plugin API version the module was built for
windows = "hello.asi"        # one module per system; a system without a key has no plugin
linux = "libhello.so"
macos = "libhello.dylib"
```

- `api` is required: a whole number from 1. A Takedawn that does not have that
  version does not load the module at all (the log says so).
- `windows`, `linux`, `macos` and `android` are paths relative to the mod's
  folder: plain names separated by `/` or `\`; no drive, no `..`, not in
  `files`; ending in `.asi` or `.dll` (Windows), `.so` (Linux, Android) or
  `.dylib` (macOS), in any case. Every system's key is checked on every
  system, so a manifest means the same everywhere.
- A `[plugin]` table that breaks these rules makes the whole mod invalid (the
  MODS page and the log say why): a typo never quietly drops a plugin.
  Unknown keys in `[plugin]` are left alone, for later versions.
- A Takedawn without plugin support ignores `[plugin]`: the mod's files are
  used, its code is not run.

## Approving a plugin

The approval file is `plugin-approvals.toml` beside `settings.toml` (the same
folder as the mod list); `TAKEDAWN_PLUGIN_APPROVALS` names another file, and
an empty value means no approval file (no plugin runs). Takedawn reads it at
the start; the MODS page (below) is the only part that writes it, and only
when you choose so. You can also edit it by hand.

### On the MODS page

Driver Details > Settings > MODS lists a mod with a module for this system
with `CODE` (its code is approved from the next start) or `CODE?` (it is
not). A on such a mod goes:

    OFF  ->  ON (its files only)  ->  APPROVE CODE  ->  ON + CODE  ->  OFF

- **Turning a mod on never approves its code.** ON uses its files only.
- **APPROVE CODE** hashes the module, the way a start hashes it, without loading it. The two lines below say
  `NATIVE CODE RUNS WITH YOUR PERMISSIONS` / `TRUSTED SOURCES ONLY: <module> <first 8 digits of its SHA-256>`.
  - Only A again approves those bytes.
  - Moving the cursor away, B, or another page cancels.
- **Nothing is written before APPLY.** When an approval is waiting, APPLY reads `APPLY - APPROVES CODE - RESTART REQUIRED`.
- **APPLY saves in three steps:** it removes approvals first, then saves the mod list, then adds approvals.
  - A step that fails stops the steps after it, and the page says `NOT SAVED - SEE THE LOG`.
  - What is saved therefore never allows more code than you chose.
  - The mod list and the approval file are two files, each written whole or not at all. A failure between them leaves at
    most fewer approvals than chosen, never more.
- **An approval is for exactly those bytes.** An update, or any other change to the module, shows
  `CODE CHANGED - APPROVE AGAIN` and the code does not run until you approve the new bytes.
- **A mod turned off, or on without its code, loses its approval at APPLY.** Turning it on again asks for approval again.
- **Changes take effect at the next start.** A plugin that runs keeps running until Takedawn exits; the page then says
  `RUNS UNTIL TAKEDAWN EXITS - RESTART REQUIRED`. Nothing is unloaded.
- **The page writes the file whole:** a new file, flushed, then renamed over the old one, the same way as the mod list.
  - It keeps every other entry and replaces or removes only that mod's.
  - Comments in the file, and any key other than `approved`, are not kept.
  - At most 256 entries.
- **The page never writes an approval file it would not fully understand.** Such a file stays as it is, and you can still
  edit it by hand. This covers:
  - a file it cannot read now, or that is not an approval file;
  - a file with an entry not written the way the page writes it (`<id> sha256:<64 hex digits>`).
- **There is no APPROVE when:** plugins are off in this process (`TAKEDAWN_PLUGINS=0`, Android, macOS without
  `TAKEDAWN_PLUGINS=1`), there is no approval file, or the module cannot be hashed (`CODE MISSING - SEE LOG`).

The details line under a code mod shows its code in this start:
- `CODE RUNNING`;
- `CODE NOT LOADED - SEE LOG`: refused or failed; the log has the reason;
- `CODE NOT APPROVED`;
- `CODE CHANGED - APPROVE AGAIN`;
- `CODE NOT USED (MOD OFF)`;
- `CODE OFF (...)`.

It adds `RESTART REQUIRED` when the next start differs. The line below shows the module's name and hash.

```toml
# Native plugins I allow to run (docs/native-plugins.md).
approved = ["example.hello-plugin sha256:50825ef19b83bf3766982137e00a2c6e04945580268b83b263f8a48f5f9f1cb2"]
```

Each entry is a mod id, a space, `sha256:` and the module's SHA-256 (64 hex
digits, either case). At most 256 entries are used; an entry that is not a
string or is too long to be one is skipped, and the log says how many were.
When a plugin is not approved, the log gives the exact entry to add:

```
[PLUGIN] example.hello-plugin: not run: not approved. It is native code and runs with your permissions; to allow this module, add "example.hello-plugin sha256:50825e..." to approved in C:\...\plugin-approvals.toml
```

Check where the module came from before you add it. A file that is not a
list of such strings (not TOML, `approved` not a list, larger than 64 KiB)
approves nothing, and the log says why.

## Writing a plugin

Include [`include/takedawn_plugin.h`](../include/takedawn_plugin.h), the only
file you need, and export two functions with these names:

```c
#include "takedawn_plugin.h"

TD_EXPORT uint32_t TD_CALL takedawn_plugin_api_version(void)
{
    return TD_PLUGIN_API_VERSION;            /* must equal api in mod.toml */
}

TD_EXPORT int32_t TD_CALL takedawn_plugin_init(const td_host_api *api, td_plugin_info *info)
{
    api->log(api->host, TD_LOG_INFO, "hello from API 1");
    info->struct_size = sizeof *info;
    info->on_shutdown = NULL;                /* or a function: best effort at a normal exit */
    return TD_OK;
}
```

[`examples/plugins/hello`](../examples/plugins/hello) is a complete plugin
with its `mod.toml`. Build it as a shared library:

```
clang -shared -Iinclude examples/plugins/hello/hello.c -o hello.asi                 (Windows, clang / MinGW)
cl /LD /Iinclude examples\plugins\hello\hello.c /Fe:hello.asi                       (Windows, MSVC)
cc -shared -fPIC -fvisibility=hidden -Iinclude examples/plugins/hello/hello.c -o libhello.so   (Linux)
cc -dynamiclib -fvisibility=hidden -Iinclude examples/plugins/hello/hello.c -o libhello.dylib  (macOS)
```

The API is plain C: fixed-size integers, function pointers, and structures
whose first field is their size. Nothing from a C runtime crosses it (no
`FILE *`, no memory one side allocates and the other frees), so a plugin may
be built with another compiler than Takedawn's. The rules:

- `takedawn_plugin_api_version` is called first; when its answer is not the
  `api` the manifest declares, `takedawn_plugin_init` is not called.
- `takedawn_plugin_init` is called once, on the main thread, before the game
  starts, and should return quickly. `api` stays valid until the process
  ends. `info` is the plugin's to fill in: on entry every field is zero and
  `info->struct_size` is the size Takedawn made room for; write no field that
  ends past it (a newer header may know more fields than this Takedawn).
- `api->log` may be called from any thread: one line in the game's log as
  `[PLUGIN <mod id>] <text>`, at most 1023 bytes kept, control characters
  shown as `?`.
- `api->mod_id` and `api->host_version` return Takedawn's own strings, valid
  until the process ends.
- `on_shutdown` is called once at a normal exit, newest plugin first, on the
  thread that ends the process. It is not called on a crash, when the process
  is ended from outside, or by the system's quick exit paths: do not depend
  on it.
- Keep the library's own start-up code (`DllMain`, constructors) empty; do
  the work in `takedawn_plugin_init`.

New versions of the API add to it: a structure only gains fields at its end
(`struct_size` says which the other side has), and nothing is removed.

## How it works

`src/game/plugins.c` decides; `plugins_win32.c` and `plugins_posix.c` load.
On every system the bytes that are approved are the bytes that are loaded,
and that is settled before the library's own start-up code runs:

- **Windows.** The module is opened with only reading shared (nobody can
  write, replace, rename or delete it while it is held) and hashed through
  that handle. The handle gives the file's real place, every junction or
  link on the way resolved; every folder of that path, from the drive down,
  is held without delete sharing (none can be renamed, deleted or turned into
  a junction), and the real place is checked once more. `LoadLibraryExW` then
  loads that path, with `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
  LOAD_LIBRARY_SEARCH_SYSTEM32`: the module's own dependencies come from its
  folder or System32, never the current folder or `PATH`, and nothing
  process-wide is changed. After loading, the kernel's name for the mapped
  file must be the held file's - a second check; a mismatch there would be
  found only after the start-up code ran.
- **Linux.** The module is opened without following a link, and copied into
  a memory file (`memfd`) while it is hashed; the copy is sealed so that
  nothing can change it and loaded from `/proc/self/fd/<n>` with
  `dlopen(RTLD_NOW | RTLD_LOCAL)`. A change to the mod folder's file after it
  was read never reaches what is loaded. A system without `memfd_create` or
  `/proc` refuses plugins (the log says why).
- **macOS.** The module is copied while it is hashed into a file in a new
  folder of its own in the user's temporary folder (`0700`), made read only,
  loaded by that path with `dlopen`, and removed afterwards; that path must
  still name the copy after loading. Only the user's own programs can reach
  the copy. The release bundle is sealed ad hoc, without the hardened runtime
  (`tools/release/build-macos.sh`), so the system does not demand a plugin
  signed by the same team; on Apple silicon a module must still be signed,
  and the linker's own ad hoc signature is enough (the copy has the same
  bytes, so its signature holds).

Because Linux and macOS load a copy, a module's own dependencies are found
the system's way (its rpath, `LD_LIBRARY_PATH`, `DYLD_*`): `$ORIGIN` and
`@loader_path` name the copy's place, not the mod folder. Keep a plugin to
one library, or link its dependencies statically.

What this does not cover: a program running as the same user can change the
mod folder, the approval file or (on macOS) the private copy, as it can
change the game itself; a writable mapping of the module made before the game
opened it is not seen. Takedawn checks what loads, not what it does.

`tests/plugins` builds test modules of every kind and checks the rules above
on folders it makes (`ctest -R bo3_plugins`); each test module records the
moment it is loaded, so "never loaded" is checked directly. On Windows it
also hashes a module through a junction, points the junction elsewhere and
checks that the module mapped is the one hashed; on every system it rewrites
a module after the hash and checks that the rewrite is not what loads.

## Troubleshooting

| The log says | Why, and what to do |
|---|---|
| `not run: not approved` | add the line it gives to the approval file, after checking where the module came from |
| `not run: ... is not a regular file in the mod's folder` | the path in `mod.toml` names nothing, a folder, or goes through a link or junction |
| `not run: it needs plugin API N` | the plugin is for a newer Takedawn |
| `not started: it was built for plugin API N, its mod.toml says M` | the module and its manifest do not match: ask the plugin's author |
| `not started: it does not export ...` | not a Takedawn plugin, or built without `TD_EXPORT` |
| `not started: its init returned N` | the plugin's own refusal: its earlier log lines say why |
| `plugins are off (...)` | `TAKEDAWN_PLUGINS=0`, an Android build, or macOS without `TAKEDAWN_PLUGINS=1` |
| `approval file not used` | the file is not a list of strings, or larger than 64 KiB: nothing is approved |
| `no sealed copy can be made` (Linux) | the kernel or a sandbox does not allow `memfd_create` |
| `the module does not load (...)` | the system's own reason: a missing dependency, another architecture |
| `approval not changed: the approval file ...` (MODS page `NOT SAVED - SEE THE LOG`, `APPROVAL FILE ...`) | the page leaves a file it cannot read, or with entries it would not write back, as it is: fix or edit it by hand |
| `approval file not written` | its `.tmp` beside it could not be made or renamed (permissions, a folder of that name): nothing after it was saved; A on APPLY tries again |
| `not hashed: ...` (MODS page `CODE MISSING - SEE LOG`) | the module named in `mod.toml` is not there, or not a regular file |

A mod whose `[plugin]` table breaks the rules is invalid as a whole: its
files are not used either (the MODS page says INVALID and why).

## Compatibility

- A `mod.toml` without `[plugin]` means what it meant before; mods, the mod
  list and the MODS page work as before.
- A Takedawn without plugin support ignores `[plugin]` and uses the mod's
  files.
- The plugin API is versioned (`api`, `takedawn_plugin_api_version`). A
  Takedawn loads only versions it has; structures grow only at their end.

## Verified

Windows 11 (build 26300), clang 23.1.2 (llvm-mingw), the NTSC-U disc,
2026-10-25; scripted pad input. With the example plugin and a mod recolouring
Silver Lake's junction road, over three starts: the MODS page showed the
plugin as CODE?; APPROVE CODE showed the warning and the module's hash, and
moving away cancelled it; APPLY saved exactly the module's SHA-256, and the
plugin did not run in that session. At the next start it was loaded with that
hash, logged "hello from API 1", and the page said CODE RUNNING; revoked, the
page said it runs until Takedawn exits. At the start after that it was not
run. The road mod was used in every race and the mod list was not touched.

Fork CI, warnings as errors, 2026-10-25: `tests/plugins`, `tests/modsmenu`,
`tests/modfs` and `tests/modmanifest` on Linux (Ubuntu 24.04, gcc 13.3; the
plugin and MODS page tests also under AddressSanitizer +
UndefinedBehaviorSanitizer, the MODS page test under ThreadSanitizer, nothing
reported), Windows (gcc 15.2; and with the test modules and the example
plugin built with MSVC 19.51 at /W4, no warnings, loaded by the same tests)
and macOS 15 (Apple clang 17, the usual and a case-sensitive volume, and run
from an application bundle sealed ad hoc as the release is); Android (NDK
clang 21, arm64, API 33): compiles and links with plugins off; not run.

Not checked: the game itself on Linux, the Steam Deck, macOS or Android; the
real Takedawn.app with a plugin (hence `TAKEDAWN_PLUGINS=1` there); a
plugin's `on_shutdown` at a normal exit of the game (the scripted runs end
the process; the tests check it).
