# Sharper menu pictures (BO-311)

The loading screens, the menu backgrounds, the car pictures of the garage, the
event photos, polaroids, postcards and maps of the front end are small: 512 by
256 for a loading screen, 128 by 64 for a car. "Sharper menu pictures" upscales
them 4x once, on the player's computer, from the player's own disc, with an AI
upscaler, and keeps the result as a [texture pack](mod-folders.md#texture-packs).
The game loads it as it loads any pack; nothing in the game's code path is
special to it.

It is optional and off unless chosen:

- Setup window: a box under *Prepare graphics*, *Sharper menu pictures:
  upscale the loading screens and menu art with AI*, with how long it takes and
  the disk it uses. It runs after the data is copied and after the shader
  pre-build, as a step of its own with progress. Cancel stops it; the game is
  installed either way, and a later run goes on from where it stopped.
- Setup window on an update (the data is already whole): the box is offered
  once, unticked, with its own button, as long as no pack is made. Not used, it
  is not shown again (`--upscale-art --dismiss`). On macOS and Linux an update
  starts the game directly, without the setup window, so the offer is in Settings.
- Settings, Graphics: *Upscale menu pictures with AI* (with the percentage
  while it runs; pressing it again stops it), later *Upscale menu pictures
  again* and *Remove the upscaled menu pictures*. As with *Build shaders now*,
  closing the settings stops it. When it is done the row says *Upscaled menu
  pictures: On. You can turn them off on the MODS page*, and the game shows
  them at once: no restart. *Remove* puts the originals back at once too.
- MODS page: the result is the mod *Sharper menu pictures (AI upscale)*
  (listed from the next start when it was made in this one); switching it off
  there and APPLY brings the originals back at once, with no restart asked for.
- Android (see [Android](#android) below): the same rows on the MODS page
  of the settings, not ticked anywhere, and the setup page offers it once
  after the install. The fast model only, run inside the app.

### Live, without a restart

The packs are handed to the toolkit again (`bo3_modtex_live`, modtex.c: this
start's packs, without the changed mod's, and the changed packs after them);
`nv2a_texpack_set` moves the packs' generation on, and the renderer puts every
replaced texture back to the title's and looks it up again at its next bind. A
job the worker started for packs that have changed since fails rather than
read another file (the request carries the generation, toolkit
`nv2a_texpack_req.gen`), so a change from the settings' thread or the menu's
never shows a wrong picture. Checked on the Mac (Metal): the World Tour map
with the pack, *Remove* and the originals, made again and upscaled, one
process (`tools/headless/upscale_live_scene.sh`).

## Not the HUD

An experimental second pack of the race HUD's pictures (Global.txd's signs,
Crash mode icons and bars) was tried and dropped (my call, 2026-10-30): it did
not look good and fought with the HUD's own scaling (`graphics.ui_scale`,
Super-xBR). A folder `ai-upscaled-hud` that a test build made is deleted at
the next start, and by the next upscale run, with a line in the log.

## Who is offered it

The game decides, and tells the setup window (`--data-status` line
`LOG data upscale <on|off|no> <best|fast> <why>`):

| computer | box | quality |
|---|---|---|
| Mac with Apple silicon | ticked | best |
| Windows or Linux with a graphics card of its own (NVIDIA, or an AMD card with memory of its own, which amdgpu shows as `mem_info_vram_vendor`: an APU reports a carve-out of up to some GB as video memory too; Windows: 2 GB of dedicated memory) | ticked | best |
| Steam Deck, Intel Mac, built-in graphics only | shown, not ticked | fast |
| Android | shown, not ticked | fast (the only one in the app) |

`TAKEDAWN_UPSCALE_OFFER=on|off|no[:best|fast]` overrides it (tests).

## Qualities

The models were chosen in the BO-311 comparison (my pick, 2026-10-29):

- best: RealESRGAN_x4plus for every picture, except the newspapers, which
  take the fast model (their small print is unreadable after any model).
- fast: realesr-general-x4v3 at denoise 0.3 for every picture: a much
  smaller network, a softer, more painted look.

From `Data/Global.txd` the menus' own: the logo on the main menu (`B3Logo`), the
EA Trax logo, the medals, the event marks (cup, tick, cross) and the loading
screen's car and bars.

The screens' big title lettering (CRASH NAV, SELECT REGION, ...) is a font
atlas the program carries itself: 128 by 256 DXT5, raw in `default.xbe`'s
`.data` (file offset 0x3cb058, key `8ed622fa02acbb1f`, checked before it is
used). It is added; on the Mac the CRASH NAV title looked the same with it
(the title is probably drawn through a surface of its own size), so its effect
is small. The two other font atlases in the same section (the menus' and the
small print) are left out.

Satellite maps are upscaled 4x and kept at 4x (they were kept at 2x until
my Deck clip: the Crash Nav zoom stretches its small zoom tiles,
`SATMAPU_L2` 128 by 64 and `SATMAPU_L3` 256 by 128, over the whole screen).
That is 99 MB of PNG instead of 20 (fast quality), and with BC1 about 27 MB
of video memory if all 36 maps were resident (a zoom has about 7 MB on the
card at once). The game asks for nearest texels on those zoom tiles (filter
0x01012000, the original's look: big square blocks); a replaced texture with
levels above the title's is filtered linearly instead (toolkit, Metal and
Vulkan), so the 4x tiles are smooth rather than smaller blocks. Not upscaled: icons of
32 by 32 or less, and the noise and shadow textures. Text in the pictures
stays as it was where it is large (logos, headlines, handwriting) and is
redrawn into nonsense where it is tiny; that is the case for every model
tried.

Measured (the 229 pictures of the disc, 13 million source pixels):

| machine | best | fast |
|---|---|---|
| Mac, Apple M5 (Metal through MoltenVK) | about 5 min (5 min 7 s from the setup window, with the download of that time) | about 1 min (from the BO-311 comparison) |
| Linux, AMD Ryzen 5000 APU (Barcelo, RADV) | about 25 min (estimated from one picture; tiles of 64, see below) | 64 s |

## Android

An Android app cannot start a program of its own, so the app does not carry
realesrgan-ncnn-vulkan. It links [ncnn](https://github.com/Tencent/ncnn)
(BSD-3-Clause; the library that program is built on) into `libmain.so` and
runs the same steps in its own process (BO-326):

- The model: the fast one only, `realesr-general-dn030-x4v3`, the same
  bytes as the desktop packages' (SHA-256 `94136d9c...`), made by
  `tools/android/fetch-deps.sh` from the files and hashes of
  `tools/release/pins.env` and packed as `assets/upscaler/` in the APK
  (2.4 MB). The best model (33 MB, and about six times the work) is not in
  the app.
- ncnn 20260526, built by `fetch-deps.sh` from its release's full source
  (SHA-256 pinned), static, on the glslang the game already links (one copy),
  with only the layers this model and ncnn itself use, no int8 or bf16 paths.
- The same pictures: `upscale_tile.c` cuts each picture into the tiles
  realesrgan-ncnn-vulkan cuts (200 pixels, 10 more on every side taken from
  the picture mirrored at its edges, the middle of the 4x result kept, values
  rounded as its shaders do). Everything else is the desktop's code: reading
  the disc, the keys, the groups, the pack, putting it into the running game.
- CPU by default, single precision on the big cores (NEON). The stock
  Adreno drivers are not trusted with ncnn's compute (BO-140). Vulkan only
  when the game runs on the app's own Turnip driver (the bundled
  `libvulkan_freedreno.so`, asked for from the app's folder or taken as the
  fallback for a driver of the player's that did not open): ncnn makes its own
  instance and device on the game's Vulkan loader (it opens `libmain.so`,
  whose `vkGetInstanceProcAddr` is the loader libadrenotools gave the game).
  Any Vulkan error, or a black result, and the run goes on with the CPU from
  that picture; `<files>/takedawn/upscale/vulkan-failed` then keeps it on the
  CPU on that device. A run that ended inside the driver (the app was gone
  before the first picture came back) leaves `vulkan-trying`, which the next
  run turns into `vulkan-failed`.
- A background job: the run is a thread of the game. It goes on while the
  settings are open and while the app is in the background, and holds
  between tiles while the game is played (a race is not slowed; the row says
  *paused while you play*). While it lasts a foreground service
  (`UpscaleService`, type dataSync, with a notification if the app may show
  them) keeps the system from freezing or ending the app in the background.
  It ends with the app's task. Stopped, closed or killed, the next run goes on
  from the pictures already made.
- The setup page: after the install, *Make sharper menu pictures (AI, a
  few minutes)* once, with the install's bar and Cancel, in the setup page's
  own process (the game is not running then, so on the CPU). Not used, it is
  not offered there again (the MODS page keeps it).

Measured on the Mac (its arm64 CPU runs the same NEON code as a phone's): the
app's CPU path against the desktop program's pictures (Metal, the Apple M5),
18 pictures of every kind from the disc: the largest difference of a channel 9
of 255, a mean of 0.09, PSNR 58.7 dB. The same on an x86-64 CPU (ubuntu,
the same numbers), and ncnn's Vulkan path through the loader opened by name
(RADV, three pictures): a channel at most 8 apart, PSNR 59.6 dB. With half-precision arithmetic
(`TAKEDAWN_UPSCALE_FP16=2`, ARMv8.2) it is 2.6 times quicker but further off
(PSNR 47.5 dB, a channel up to 81 apart in a few places), so it is not the
default. The Retroid Pocket 5's time for the whole set is still to be
measured; from the Mac's single-core figure, about 5 to 10 minutes on its big
cores.

The APK grows by 3.2 MB (the model 2.4 MB stored, ncnn 0.7 MB compressed in
`libmain.so`).

Variables (`env.txt`): `TAKEDAWN_UPSCALE_BACKEND=cpu|vulkan` (vulkan: on any
driver, a test), `TAKEDAWN_UPSCALE_THREADS=<n>`, `TAKEDAWN_UPSCALE_FP16=0|2`,
`TAKEDAWN_UPSCALE_TILE=<n>`, `TAKEDAWN_UPSCALE_ENGINE=<folder with models/>`.
`tests/upscale_ncnn` is the comparison tool (it needs an ncnn build and the
disc's data).

## How it is made

`burnout3 --upscale-art [--quality best|fast] [--force]` (`src/game/upscale.c`),
the line protocol of the installer and the shader pre-build: one step,
`upscale`, with `PROGRESS` lines. `--check` says what the computer is offered,
`--status` whether the pack is made, `--remove [--all]` deletes it (with
`--all` the whole upscale cache). `--engine <dir>` uses an upscaler folder
of your own (the program and `models/`).

1. The upscaler comes with every desktop package; nothing is downloaded and
   no internet is needed. It is
   [realesrgan-ncnn-vulkan](https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan)
   (MIT, on ncnn and Vulkan; MoltenVK on macOS) as Real-ESRGAN's own release
   v0.2.5.0 ships it for each system, with RealESRGAN_x4plus from the same
   archive, and the fast model: realesr-general-x4v3 and its WDN twin (upscayl's
   custom-models, a fixed commit) blended 0.3 / 0.7 at build time, as
   Real-ESRGAN's own `-dn 0.3` does (`tools/release/upscaler/blend_ncnn.py`,
   byte for byte the comparison's model). The release scripts fetch them from
   the addresses in `tools/release/pins.env`, check each SHA-256 and cache them
   (`stage_upscaler`, `tools/release/common.sh`):

   | system | archive | SHA-256 | in the package |
   |---|---|---|---|
   | macOS | `realesrgan-ncnn-vulkan-20220424-macos.zip` | `e0ad0558…5c0fa33` | `Contents/MacOS/realesrgan-ncnn-vulkan` (the arm64 part, 12.6 MB, sealed with the app), `Contents/Resources/upscaler/models` |
   | Linux | `realesrgan-ncnn-vulkan-20220424-ubuntu.zip` | `e5aa6eb1…a40c96` | `usr/bin/upscaler/` in the AppImage (11.4 MB program) |
   | Windows | `realesrgan-ncnn-vulkan-20220424-windows.zip` | `abc02804…412802d` | `upscaler\` beside `Takedawn.exe` (6.2 MB program and `vcomp140.dll`) |

   The models are 36 MB on every system. A build of your own finds them with
   `TAKEDAWN_UPSCALE_ENGINE=<a folder with the program and models/>` or
   `--engine`.
2. Read `Data/LoadScrn.bin` and `Data/Frontend.txd` of the installed game
   (P8, DXT1/3/5 textures, `src/game/upscale_art.c`), each picture decoded and
   written as a PNG named by its texture-pack key, the hash the game looks it
   up by. The colour under transparent texels is filled from the neighbours so
   the model sees no hard edge there.
3. Upscale: the upscaler once per model over its folder (its start costs
   a few seconds). If the log says the graphics device failed
   (`vkQueueSubmit failed`: an AMD APU under RADV did this with the default
   tiles, and the upscaler then writes black pictures with a status of 0),
   that model's pictures are made again with tiles of 64, then 32.
4. Pack: the PNGs into `<generated-mods>/ai-upscaled-art/textures/`, with a
   `pack.toml` of `alpha = "original"` (the pictures take the disc's alpha,
   scaled; the upscaler's alpha is not used) and a `mod.toml` (id
   `takedawn.ai-upscaled-art`). It is written beside the old one and renamed
   into place. Then the work folder, the decoded disc art, is deleted.

## Where it is

| what | where |
|---|---|
| the pack (about 410 MB of PNG) | `<local data>/generated-mods/ai-upscaled-art` (macOS `~/Library/Application Support/Takedawn`, Linux `~/.local/share/takedawn`, Windows `%LOCALAPPDATA%\Takedawn`, Android `<files>/takedawn`), or `TAKEDAWN_GENERATED_MODS` |
| a run in progress, its logs, `cancel` | `<cache>/upscale/work` (deleted when the pack is made); on Android `<files>/takedawn/upscale/work`, beside the made mods rather than in the app's cache, which the system may empty |

The `generated-mods` folder is a mod folder of its own: the game takes it
after the folders `TAKEDAWN_MODS` names, and alone when that is not set, so
no setting is needed. `TAKEDAWN_GENERATED_MODS=` (empty) turns it off. The
game's compressed copies of the pack go to the `texpack` cache as for any pack
(about 190 MB).

Nothing made here leaves the computer: the pictures are the game's own art,
made from the player's disc. Do not share the pack.

## What is left out, and why

From a texture dump of the front end (the World Tour, Single Event, the garage,
a loading screen, a race's intro and pause menu), the 2D art that is not in the
packs:

- two of the three font atlases (the menus' and the small print, in
  `default.xbe`): a model turns small letters into new shapes;
- the menu's light swooshes, bars and glows (smooth gradients, 128 to 1024
  wide): nothing to gain from a model; not in the three files either;
- the World Tour's continent outlines (128 by 128, line art);
- the front end's 3D scene (the city behind the menus) and the garage's cars:
  world textures, out of scope as for BO-172.

## Limits

- Text: see Qualities.
- Alpha: the disc's, scaled smoothly; cut-out edges of car pictures are as
  sharp as the original's alpha allows.
- `TAKEDAWN_UPSCALE_GPU=<n>` passes `-g <n>` to the upscaler (another card;
  `-1` is the CPU, far too slow for the best quality).
