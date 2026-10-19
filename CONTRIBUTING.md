# Contributing to Takedawn

Thank you for looking. This page says how to report a bug, where a fix
belongs, what must never be committed and where code may come from.

## Reporting a bug

A useful report has:

- the system, the graphics card and its driver, the controller, and how the
  game was installed;
- what you did, what you expected and what happened, and roughly how long
  into the session;
- the settings that matter: aspect ratio, render scale, anti-aliasing, the
  power mode, and whether the widescreen HUD fix or ultrawide was on;
- the crash report folder, if the game wrote one, and a frame-time trace for
  a performance problem. The README's
  [Reporting a bug](README.md#reporting-a-bug) section says where they are and
  how to make them.

A photo or a screenshot of the fault helps. Before you send a report folder,
read it: it contains paths from your machine.

Never attach or link game files: no disc images, no extracted files, no
generated C, no built game program, no shader cache. Reports that do are
removed.

## Where a fix belongs

Takedawn is two repositories in one process. This one holds what is true of
this game alone. The toolkit holds the recompiler and the Xbox runtime.

| The problem is | It goes in |
|---|---|
| wrong generated code, an x86, x87 or SSE instruction the recompiler handles badly, a jump table the disassembler misreads | the toolkit: `tools/recomp`, `tools/disasm` |
| the picture is drawn wrong, a kernel call misbehaves, controllers, sound, films, frame pacing, the settings menu | the toolkit: `src/` |
| an address only this game has (a seed function, a patch site, a boot default), the scenes of the shader pre-build | here: `src/game/`, `config/` |
| the installer, its window, packaging | here: `tools/install/` |

A change in this repository that needs a toolkit change names it in the commit
message.

## Setting up

Follow [docs/building.md](docs/building.md). You need your own disc. [docs/running.md](docs/running.md) covers the scripted
tools: pad scripts, frame dumps and the reference scenes in `tools/headless/`.

Before you open a pull request:

- run the change in at least one race and one menu path, and say in the pull
  request which modes, which system and which graphics backend you checked;
- if it touches frame pacing, presentation or input, include frame times
  before and after from the same scene (`RECOMP_TRACE=pacing=all,frametime=<file>`);
- run the tests that cover it: `python3 -m unittest discover -s tools/install/tests`
  for the installer driver, `python3 -m tools.region.check` when you add or
  change a guest address, a patch site or a wrapped function (the PAL build's
  twin, [docs/pal.md](docs/pal.md)), and the toolkit's own tests for the
  runtime.
  [docs/testing.md](docs/testing.md) lists them, with the scratch profile to
  use, the game's built-in AI driver for unattended races and the other test
  tools;
- say what you did not check. An honest "not tested on Windows" is more
  useful than silence.

## Rules for what lands

- Never commit generated or extracted material: nothing from
  `src/game/recomp/gen/`, `build/` or `Burnout 3 Takedown/`, no disassembly
  listings, memory dumps, captured command buffers, shader key lists or shader
  caches. All of these contain the game.
- Screenshots and recordings belong in the issue or the pull request. The
  only pictures in the repository are the README's, in `docs/screenshots/`:
  frames of the port running, taken by us. No logos, box art, title-screen
  art or files taken from the game.
- Do not edit generated C by hand. If it is wrong, fix the recompiler or the
  data in `config/`.
- A patch that changes the game's behaviour is opt-in, lives in
  `config/patches.json` with a note that says how the site was found, and does
  nothing when its setting is off.
- No private data in commits: no IP addresses, host names, user names or home
  paths in scripts, documents or logs.
- Commit subjects in the imperative ("Map the second pad's rumble endpoint"),
  with the reason in the body.
- The runtime's C builds with clang on every system. Keep new code in the
  style of the file around it.
- Documents are in English and state what was verified, on which machine and
  when. Do not write that something works on a system where nobody ran it.

Ids like BO-116 in docs, comments and commit messages are the project's
internal task numbers. They stay so that the history, the docs and the code
agree.

## Where your code comes from

Contributions must be your own work, or come from a source whose licence is
compatible with MIT: MIT, BSD, zlib, Apache-2.0 or public domain.

- Do not port code from GPL projects, such as the GPL parts of xemu,
  Cxbx-Reloaded, RPCS3, MAME's GPL drivers or GPL recompilation projects.
  Rewriting it line by line does not change its origin. Reading such a project
  to understand how the hardware behaves is fine. Carrying its code or its
  structure across is not. If you saw a fix elsewhere, describe the bug and it
  can be written fresh.
- Do not copy from a project that has no licence file. No licence means no
  permission.
- If something came from elsewhere, say so in the pull request with a link,
  keep any copyright header, and add the component to
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- AI-assisted contributions are welcome if you have read the change,
  understand it, have tested it yourself and have checked that it does not
  reproduce another project's code. You are the author of what you submit and
  answer for it, and it is held to the same bar as every other change.

## Licence of contributions

By contributing you agree that your contribution is released under the MIT
licence of this repository ([LICENSE](LICENSE)). Add yourself to
[CONTRIBUTORS.md](CONTRIBUTORS.md) in the same pull request if you like.
