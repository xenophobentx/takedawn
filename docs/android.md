# Android build (SDL2 + Vulkan, arm64)

State on 2026-10-11 (BO-85), reference device Retroid Pocket 5 (Snapdragon
865, Adreno 650, Android 13): the APK builds and installs, the title boots
through its own entry point, draws through the Vulkan backend on Mesa's Turnip
driver (the RP5's stock driver is Vulkan 1.1 and cannot run it), full screen,
with sound, the built-in pad, the settings overlay and the user's save from
another machine. A scripted race on Silver Lake holds 60 at the title's own
640x480 and does not hold it at 2x ("Measured" below). Not done then: touch
controls and the shader pre-build. The vblank lock to the display (the
Choreographer) and render scale 1.5 came later; see "Defaults on Android now".

The Android build reuses the two halves that exist: the arm64 code path of the
macOS build (the A64 MMIO decoder, the recompiled C as it is) and the Linux
host (the `__linux__` branches of the runtime: the 4 GB guest window, memfd
views, SIGSEGV traps, SDL2 window, audio and pads, the Vulkan backend). The
Android-only code is small and in its own files: `android/` here (manifest,
two activities), `tools/android/` (build scripts), and in the toolkit
`src/platform/android_host.[ch]` plus `#if defined(__ANDROID__)` blocks where
bionic differs from glibc.

## Build

On a Linux PC (the same machine that ran `regen.sh`, or one the generated C
was copied to). Needs: the Android SDK with build-tools and one platform, the
NDK (r28 or newer: 16 KB ELF alignment by default), a JDK 17+, cmake 3.20+,
ninja, python3, curl, zip.

    ./regen.sh                       # as on every platform: src/game/recomp/gen from your default.xbe
    tools/android/fetch-deps.sh      # once: SDL2, glslang, libadrenotools, Mesa Turnip (below)
    tools/android/build-apk.sh       # out/burnout3-android-arm64.apk

Environment: `ANDROID_HOME` (default `~/android-sdk`), `ANDROID_NDK_HOME`
(default: the newest under it), `ANDROID_API` (minSdk, default 33),
`BO3_ANDROID_DEPS` (default `~/.cache/bo3-android-deps`), `XBOXRECOMP_DIR`
(default `../xboxrecomp-android` or `../xboxrecomp`), `BO3_KEYSTORE`,
`BO3_ANDROID_TURNIP=0` (no Turnip in the APK), `DEBUGGABLE=1`, `STRIP=0`.
`fetch-deps.sh` also needs git, python3 with venv (it installs meson, mako and
pyyaml into a venv in the deps prefix), bison, flex, pkg-config and a host C++
compiler (Mesa's build runs `glslangValidator`, built from the glslang source).
No Gradle: `aapt2`, `javac`, `d8`, `zipalign` and
`apksigner` are called directly, the way Xerenge's `scripts/build-android-apk.sh`
does it (the structure is taken from there, the code is not).

Built on a Linux desktop with NDK r30 (30.0.16248370), build-tools 37.0.0,
platform android-37.0, Temurin JDK 21, cmake 3.31.6: the APK is 13.3 MB
(10.6 MB without the Turnip driver).

Install and start:

    adb install -r out/burnout3-android-arm64.apk
    adb shell am start -n io.github.xenophobentx.takedawn/.StartActivity
    adb logcat -s takedawn

The APK is signed with a debug key made on first use (`BO3_KEYSTORE`). Android
only updates an installed app from an APK signed with the same key: keep the
key, or uninstall first (the uninstall dialog offers to keep the data).
Release APKs are signed again with the project's own key by
`tools/release/sign-android.sh`, see below.

## What is in the APK, and what is not

| in the APK | size | from | licence |
|---|---|---|---|
| `lib/arm64-v8a/libmain.so` | 25.3 MB stripped (120 MB with symbols) | the game: the recompiled C of the disc's `default.xbe`, the toolkit runtime, and linked in: glslang 16.6.0, Dear ImGui 1.92.9b, libadrenotools | the toolkit's; glslang BSD-3-Clause / Apache-2.0 / MIT (Khronos); Dear ImGui MIT; libadrenotools and its linkernsbypass BSD-2-Clause (Copyright 2021 Billy Laws) |
| `libSDL2.so` | 1.2 MB | SDL 2.32.10, https://github.com/libsdl-org/SDL | zlib |
| `libc++_shared.so` | 1.4 MB | the NDK | Apache-2.0 with LLVM exception |
| `libvulkan_freedreno.so` | 18.9 MB | Mesa 26.2.0-devel Turnip `git-7fdde2f8f8`, whitebelyash's experimental build (his gen8 work, https://github.com/whitebelyash/mesa-tu8; render-pass dependency `CCU_CLEAN_COLOR \| WAIT_FOR_ME`), prebuilt: `assets/bundled_driver/vulkan.wb26_2_rp_pair_ccu_color_a725.so` of https://github.com/SansNope/UnleashedRecomp-Android/releases/download/v0.6.1/UnleashedRecomp_0.6.1.apk (APK sha256 `05717f16...74c34`, driver sha256 `87f371a2...b22b0`, as UnleashedRecomp-Android's `driver/README.md` gives it), where it is the "A725 Performance" driver; taken out and checked by `fetch-deps.sh`, packed unstripped. Runs with `TU_DEBUG=sysmem,nobin`. See "The A725 Turnip build (BO-152)". `BO3_ANDROID_TURNIP=mesa` packs the Mesa 26.2.4 build of `fetch-deps.sh` instead (https://archive.mesa3d.org/mesa-26.2.4.tar.xz, sha256 `bce5f7fb...832e9`, with `tools/android/patches/mesa-26.2.4-kgsl-zero-timeout.patch`) | MIT (Mesa's `docs/license.rst`) |
| `libhook_impl.so`, `libmain_hook.so`, `libfile_redirect_hook.so`, `libgsl_alloc_hook.so` | 51 KB | libadrenotools, https://github.com/bylaws/libadrenotools at commit `8fae8ce` (2024-09-10) | BSD-2-Clause |
| `classes.dex` | 94 KB | SDL's activity (`org.libsdl.app`), `StartActivity`, `GameActivity`, `GpuDriver` | zlib; this repository's |
| `assets/licenses/` | about 60 KB | the release's licence set (`tools/release/common.sh`, `copy_licenses`) and `tools/android/licenses/` (libadrenotools, linkernsbypass, Mesa); shown by the app's **Licences** page (`LicensesActivity`) | |
| manifest, resources | | `android/` | |

Not in the APK: the game data, saves, settings. `libmain.so` holds the game's
compiled code, generated from `default.xbe`; the release APK is built from the
stamped generated tree ([release.md](release.md)) and the player's own disc
supplies the data ("The Takedawn package"). The repository holds no generated C
and no APK.

## The Takedawn package (0.1.0, BO-103)

The app is `io.github.xenophobentx.takedawn`, label "Takedawn" (the PAL build,
[pal.md](pal.md), is `io.github.xenophobentx.takedawn.pal`, "Takedawn PAL"), with a second
launcher entry "Takedawn: graphics driver". versionName is `VERSION`'s first
line; versionCode is major x 10 000 000 + minor x 100 000 + patch x 1 000 +
build (0.1.0 is 100000), so every release is above the one before. The icon
is adaptive: `assets/icon`'s two layers as vector drawables and a one-colour
layer for themed icons (`tools/android/make-icon.py` writes them; the
results are committed). Its folders, all the app's own:

    /sdcard/Android/data/io.github.xenophobentx.takedawn/files/
        Burnout 3 Takedown/    the game's data, installed at the first start
        .complete              the installer's marker (manifest id, version, 716 files)
        install.journal        only while an install is under way
        takedawn/              saves (UDATA, TDATA), settings.toml, crash/, reports/
        driver/                optional: a driver package put there by hand
        env.txt                optional
        takedawn.log           the log of the last start

**The first start** (`SetupActivity`, in a process of its own). The APK holds
no game data. The page asks for an image of the player's own disc, Burnout 3:
Takedown, North America (NTSC-U) retail, with the system's file picker; no
storage permission is asked for. The open file goes to the toolkit's
installer (`disc_install.c` through `install.c`'s JNI functions) as a file
descriptor: every one of the manifest's 716 files is hashed while it is
copied, the journal makes a stopped install go on where it was when the same
image is chosen again, and `.complete` is written last. The page shows the
file in hand and a bar, keeps the screen on, and Cancel (or Back) stops after
the file in hand. Each of the installer's answers gets its sentence and what
to do about it: not an Xbox image, an Xbox 360 disc, another game, another
region or revision (only NTSC-U retail is supported), a truncated or damaged
image, no room (3.0 GB free are wanted), an unreadable file. The game does
not start until the marker is there and names this program's manifest
(`StartActivity.dataComplete`). A game folder copied into the app's folder by
hand, the way before this page, is checked where it lies and nothing is
copied (716 files in 16 s on the RP5). From adb: `am start -n
io.github.xenophobentx.takedawn/.SetupActivity --es image <an image under the
app's files folder>`.

**The driver, in three places.** The launcher's "Takedawn: graphics driver"
page; a button on the setup page that opens it and names the present choice;
and the overlay's Graphics page while the game runs: "Graphics driver in
use" (what this start loaded), "Driver at the next start" (pressing it steps
through the Turnip in the app, the device's own driver and a package already
picked), "Driver packages..." (the driver page over the held game, for the
file picker; Back returns to the race) and, once the choice is no longer what
is running, "Restart now to use it". A driver is loaded before the title
starts, so every change is for the next start and the rows say so; the
restart ends the process and a small page in a process of its own starts the
app again. Automatic is the Turnip in the app on every Adreno, also where the
device's own driver is new enough: that is the one the game is tested with.

Checked on the RP5 with the release-pipeline APK (2026-10-12): an update over
the earlier build of the same key keeps the data; a 2.48 GB image picked in
the system's picker installs in about 30 s (cancelled after 14 s with 253
files kept, 463 copied on the second pick); the community package
`turnip_v24.3.0_R9v2.zip` picked in the picker draws (Vulkan 1.3.296) and
passes its trial; a zip whose library is no driver costs one start and the
next is on the bundled driver with the note; the same after "Restart now"
with the device's Vulkan 1.1 driver chosen in the overlay.

Bloom and Motion blur (the title's own passes, Graphics page), busy race at
1.5x, one run each: on, 17.9 render passes a frame, GPU 3.4 to 4.3 ms a
frame by draw count, 1.0 % of frames over 18.5 ms; off, 13.0 passes, GPU 2.7
to 4.0 ms, 0.5 %. Both hold 60 (no or one second under 57), so both stay on
by default; off is worth 0.5 to 0.75 ms of GPU time a frame.

**An install of the earlier package** (`io.github.xenophobentx.burnout3`,
"Burnout 3 (recomp)") is another app to Android: the two stay installed side
by side and neither sees the other's files, so nothing is moved
automatically. To carry over, with a PC (adb or USB file transfer), from
`Android/data/io.github.xenophobentx.burnout3/files/` to
`Android/data/io.github.xenophobentx.takedawn/files/`:

- saves and settings: the contents of `burnout3/` (`UDATA`, `TDATA`,
  `settings.toml`) into `takedawn/`;
- the game data: the folder `Burnout 3 Takedown` as it is (move it, to save
  2.5 GB); Takedawn's first start checks it in place.

Then the old app can be uninstalled.

**A release build** (`tools/release/build-android.sh`, called by the release
script): the sources and the stamped generated code are copied to a Linux
machine with the Android SDK (`TAKEDAWN_ANDROID_HOST`, in the environment or
the git-ignored `tools/release/local.env`), `tools/android/build-apk.sh` with
`SIGN=0` makes the aligned, unsigned APK there, and it comes back. The build
machine never has the signing key. `tools/release/sign-android.sh` signs on
the machine that holds the key (`~/.takedawn-signing/`, never copied): it
runs the SDK's `apksigner.jar` with a Java runtime unpacked in the user's
cache, gives the password as a file (`--ks-pass file:`), verifies the result
and prints the certificate's SHA-256 fingerprint only. The project's
certificate: `d5020116e1fb87fb4d4317f33253203e71e0181a4b7ff76e69c7dda6c5c565ca`.
`tools/android/build-apk.sh` alone still signs with a development key; such a
build and a release do not update each other.

## Game data on the device

(How the data got there before the setup page, and still a way for a test
device.) Everything the app reads and writes is in its own external files directory,
which needs no permission and which adb and a USB file transfer both reach:

    /sdcard/Android/data/io.github.xenophobentx.takedawn/files/
        Burnout 3 Takedown/    the unpacked disc (default.xbe, Data, Tracks, ...): about 2.4 GB
        takedawn/              saves (UDATA, TDATA, the emulated HDD), settings.toml, crash/
        enhance.toml           optional, as beside the executable elsewhere
        env.txt                optional: NAME=value lines, the RECOMP_* variables of docs/running.md
        takedawn.log           the log of the last start (takedawn.log.1: the one before)

The shader cache goes to the app's internal cache directory
(`/data/data/<package>/cache/xboxrecomp`), which Android may empty.

Getting the data there:

    adb push "Burnout 3 Takedown" /sdcard/Android/data/io.github.xenophobentx.takedawn/files/

(over Wi-Fi adb 2.4 GB took 1 min 50 s), or copy the folder over USB file
transfer. For a first test 0.77 GB is enough (boot, menu, a race on Silver
Lake): everything but `Tracks/EU`, `Tracks/AS`, `Tracks/crash*.rws`,
`dashupdate.xbe` and, of `Tracks/US`, all but `C3_V1`, `C3_V2` and `C5_V1`
(the boot reads `C5_V1/enviro.dat`). The app's first start makes the folder; without `default.xbe` in it
the app shows a page that says where to copy to. `dashupdate.xbe` (58 MB) was
left out in the device runs and the boot did not ask for it.

## The GPU driver

Read from the RP5 with `adb shell cmd gpu vkjson`, and the same as the public
report https://vulkan.gpuinfo.org/displayreport.php?id=36612 ("Moorechip
Retroid Pocket 5"):

- device `Adreno (TM) 650`, driver 512.746.0, Vulkan 1.1.128 (the system
  loader itself reports 1.3.0);
- of what the backend requires (`src/d3d/nv2a_vk_core.c`: API 1.3, dynamic
  rendering, synchronization2, timeline semaphores; it also draws with the
  1.3 dynamic states and host query reset): offered as extensions are
  `VK_KHR_synchronization2`, `VK_KHR_timeline_semaphore`,
  `VK_EXT_extended_dynamic_state`, `VK_EXT_host_query_reset`,
  `VK_KHR_create_renderpass2`; not offered are `VK_KHR_dynamic_rendering`
  and `VK_EXT_extended_dynamic_state2` (depth bias enable), and
  `VK_KHR_present_id` / `VK_KHR_present_wait`;
- features: anisotropy 16x, precise occlusion queries, BC textures;
- the surface (from the gpuinfo report): at least 3 images, FIFO and MAILBOX,
  no IMMEDIATE; formats `R8G8B8A8_UNORM`, `R8G8B8A8_SRGB`, 565, 1010102,
  RGBA16F, no `B8G8R8A8` (the presenter already takes `R8G8B8A8_UNORM`).

Of 300 Adreno 650 reports read from gpuinfo, every stock driver up to
512.863.1 reports Vulkan 1.1, with one exception (a 512.819.2 on Android 17
reporting 1.3). So on this device the log says

    [vulkan] no Vulkan device: no Vulkan 1.3 device with a graphics queue
    [vulkan] RECOMP_PB_BACKEND=vulkan: no device, staying on the CPU rasteriser
    [PRESENT] window up: SDL opengles2 renderer, vsync on; guest runs on a pthread

Decided (user, 2026-10-11): Mesa Turnip through libadrenotools, the way
Xerenge and the Android emulators do it. A Vulkan 1.1 downport of the
backend (render passes in place of dynamic rendering in shared code) was
rejected.

**The driver (first version).** Mesa 26.2.4, built from Mesa's own
tarball by `tools/android/fetch-deps.sh` the way
https://docs.mesa3d.org/android.html describes (`-Dplatforms=android
-Dvulkan-drivers=freedreno -Dfreedreno-kmds=kgsl -Degl=disabled
-Dgallium-drivers=`; KGSL is the interface of Qualcomm's Android kernels, the
RP5's 4.19 included). The tarball is pinned by the sha256 of Mesa's release
notes; no prebuilt package was used. The APK bundles the A725 build below
instead; this build is what `BO3_ANDROID_TURNIP=mesa` packs. What this source
says about the Adreno 650 against what the backend asks for:

| the backend needs (`nv2a_vk_core.c`) | stock 512.746.0 (from the device) | Turnip 26.2.4 (from its source) |
|---|---|---|
| Vulkan 1.3 device | 1.1.128 | 1.3 on Adreno 6xx (`tu_device.cc`: 1.4 for a7xx and up); `docs/features.txt`: "Vulkan 1.3 -- all DONE: ... tu"; the 650 is in `freedreno_devices.py` |
| dynamicRendering | no (`VK_KHR_dynamic_rendering` missing) | yes |
| synchronization2, timelineSemaphore | as extensions | yes |
| extended dynamic state 1 and 2 (cull mode, depth and stencil state, depth bias enable) | 1 only | both |
| hostQueryReset, occlusionQueryPrecise, samplerAnisotropy | yes | yes |
| `VK_KHR_present_id` / `present_wait` (optional: the display lock) | no | listed by the driver, but the swapchain on Android is the system loader's: to be seen on the device |

On the device (2026-10-11): `[vulkan] replacement driver
…/lib/arm64/libvulkan_freedreno.so opened through libadrenotools`, `[vulkan]
device up: Turnip Adreno (TM) 650 (Vulkan 1.3.354, driver 0x6802004)`,
`[vulkan] backend up on Turnip Adreno (TM) 650: 2 frames in flight …`,
`[PRESENT] Vulkan swapchain: 1920x1080, 3 images, RGBA8, FIFO (vsync)`. And
`[vulkan] no VK_KHR_present_wait on Turnip Adreno (TM) 650 (extensions
missing …)`: it does not reach the app through Android's loader.

**How it is loaded.** libadrenotools opens a private copy of the system's
`libvulkan.so` whose driver lookup ends at the replacement; no root, nothing
outside the app. Its header requires the hook libraries in the app's native
library directory (hence `extractNativeLibs`) and the driver in app-internal
storage, never on shared storage. On Android the runtime calls Vulkan through
a table of pointers filled from that loader (the toolkit's
`src/d3d/android_vk.[ch]`, force-included by the build: the Vulkan sources
themselves are not changed, and `libmain.so` has no `libvulkan.so` dependency
left). SDL makes the window's surface with the same loader, because the
exported `vkGetInstanceProcAddr` it finds is the game's.

**The A725 Turnip build (BO-152).** The Mesa 26.2.4 Turnip built from Mesa's
release hung the Retroid Pocket 5's Adreno 650 every 2 to 14 minutes in races
on 2026-10-13 (kgsl "GPU hang detected", RBBM_STATUS 0x00E59005, sometimes
ending in VK_ERROR_DEVICE_LOST), whatever the game's settings and whichever
fixed-function paths were switched off. UnleashedRecomp-Android, another
recompiled game, saw the same freeze on the RP5 (its issues #140 and #170);
its "A725 Performance" driver stopped it there. That build, with the
`TU_DEBUG=sysmem,nobin` that app sets for it, ran on the RP5 with no hang.
It is the Turnip in the APK since: `fetch-deps.sh` downloads that app's
v0.6.1 release APK (pinned by sha256), takes the driver out and checks it
against the sha256 in that app's `driver/README.md`; nothing of that app
itself is used. The binary calls itself Mesa 26.2.0-devel `git-7fdde2f8f8`;
that commit is not in whitebelyash's public repository (checked 2026-10-13),
so the source is his gen8 branch as published there. Mesa is MIT; the APK
carries Mesa's licence texts and `Mesa-Turnip-SOURCE.txt` saying all this.
On an Adreno 6xx with a driver of the player's choosing, the runtime still
defaults to `TU_DEBUG=nobin,noubwc`; with the driver in the app,
`sysmem,nobin` on every Adreno. A `TU_DEBUG` in `env.txt`, even empty, wins.

**Which driver a start uses** (`GpuDriver.java`; the choice is in logcat as
`GPU driver: ...`, and the runtime logs `[vulkan] replacement driver ...`):

1. a driver in `<files>/driver/`: a `.so`, or an emulator-style driver package
   (`.zip` with `meta.json`); it is copied to internal storage and preferred,
   so another Turnip build can be tried without rebuilding the APK;
2. the Turnip in the APK, when the device's own driver is older than Vulkan
   1.3 and the GPU is an Adreno (`/dev/kgsl-3d0` exists);
3. the device's own driver.

`RECOMP_ANDROID_DRIVER=system`, `bundled` or `user` in `env.txt` forces one.

**The player's choice** ("Takedawn: graphics driver", a second entry in
the launcher; `DriverActivity`, `GpuDriver`). Before the order above comes
what was chosen there: *Automatic* (the order above), *Mesa Turnip, in the
app*, *This device's driver*, or a *driver package* picked with the system's
file picker: the `.zip` with `meta.json` that the emulators' driver pages
distribute, or a bare `.so`. A package is checked before it replaces the
present one (it is a zip with a `meta.json` whose `libraryName` is in it, or
an ELF file; the library is a 64-bit ARM shared object of 0.5 to 400 MB) and
is kept, with its name, version, file name and sha256, in the app's internal
storage (`driver-custom/`), where an update of the app leaves it; "Remove the
driver package" deletes it. The page shows what the last start ran on (the
device name, its Vulkan version, Mesa's version for a Turnip), and the
overlay's Graphics page shows the same for the running game. From adb:
`am start -n io.github.xenophobentx.takedawn/.DriverActivity --es import
<a file under the app's files folder>` or `--es choose auto|bundled|system|custom|remove`,
in a debuggable build only (the page is exported, so any app could send
these; a release build ignores them and says so in the log).

Turnip drives Adreno GPUs only. On any other GPU (Mali, Xclipse) the bundled
driver, a package and the driver folder are never loaded, the driver page
offers neither, and Automatic is the device's own driver.

A driver other than the one in the app is on trial at each start it has not
passed: the activity leaves a note that the game removes after 120 frames
presented through Vulkan. A package the loader cannot open is replaced by
the bundled driver in the same start, with a message on the screen. A driver
that opens and then crashes, or lacks what the backend needs (the game then
falls to its CPU rasteriser), leaves the note in place: the next start goes
back to Automatic (the bundled driver on an Adreno; the bundled driver itself
when a file in the driver folder would come first under Automatic), says so
(a short toast, the whole note on the driver page), and the choice stays
there until the player chooses again. A driver the player picked that passes
its trial and loses the device later (VK_ERROR_DEVICE_LOST) is dropped the
same way: the box after the lost device says the next start is on Automatic
(BO-163). So a bad driver costs one start, never a loop; a game ended by the
player in its first two seconds counts as a failed trial too. The backend's fence-per-frame scheme
(`vk_poll=0`) is the default on Android whatever the driver.

**When no driver will do**, the start page says so (the device's Vulkan
version, where a driver goes) and starts on the CPU rasteriser only when
asked; if a chosen driver then fails in the game, a message box says so in a
player's words (the reason is in the log) before the CPU rasteriser takes
over. Its buttons: "Use built-in" (back to Automatic and a restart; on an
Adreno when the start was not on the bundled driver), "Driver file..." (the
driver page on the file picker; Adreno only) and "Continue". When the bundled
driver is the one that failed, the box says what a driver file is and where
players get one.

## What was checked on the device (2026-10-11)

Small native tests in `/data/local/tmp` (shell user), then the app itself.

| question | result |
|---|---|
| page size, kernel | 4096, Linux 4.19.157 (`getconf PAGE_SIZE`, `uname -r`) |
| a 4 GB `PROT_NONE` window anywhere, memfd views placed in it with `MAP_FIXED`, 28 mirrors, an anonymous view at +2 GB | all mapped, mirrors alias; `MAP_FIXED_NOREPLACE` over a live view is refused (EEXIST) |
| a trapped page | SIGSEGV, `si_code` 2 (SEGV_ACCERR), `si_addr` exact; a trap and return is about 1.2 µs |
| the toolkit's A64 decoder test (`tests/mmio_decode_a64`) | 23 traps, 0 failures |
| the same inside the app (ART's signal chain, the app's SELinux domain) | `xbox_MemoryLayoutInit: complete`, `RAM mirror: 28/28 views mapped`, `[OHCI0] operational`, `[APU] started by the title`: the trapped registers are served; the crash report's backtrace shows `art::SignalChain::Handler` calling the game's handler |
| window, audio, pad | `[PRESENT] display 0 mode 1920x1080 at 60 Hz`; `[SDL2] audio initialized: driver openslES (48000 Hz stereo ...)`; `[INPUT] controller "Xbox Wireless Controller" 2022:3002 player 0 opened` (the built-in pad) |
| the guest core | `[KERNEL] guest threads on one host core (cpu 7)` in one start, `(cpu 6)` in another: see "Known problems" |
| the title | boots, the vblank interrupt runs (`[NV2A] vblank 600: last 600 in 9984 ms`), the LOADING screen is on the display (CPU rasteriser) |

Both starts were short (16 s and 14 s) and with a reduced data set; the menu
was not reached.

## Measured (RP5, Turnip 26.2.4, 2026-10-11)

Scripted race on Silver Lake (`tools/android/scene-launch.sh` under
`tools/headless/to_race.sh`, accelerator held), `RECOMP_TRACE=pacing=all,
frametime=<file>`, 1920x1080 swapchain, 4:3, no AA, anisotropy 8, the user's
profile. First numbers, one run each; the scale-2 run includes a visit to the
overlay by the user.

| | frames | guest flip p50 / p95 / p99 | over 18.5 ms | flips per second |
|---|---|---|---|---|
| boot, menus, loading, scale 1 | 7 487 | 16.67 / 16.69 / 16.71 ms | 2 frames late | 60 |
| race, scale 1 (640x480) | 8 045 | 16.67 / 17.67 / 20.98 ms | 3.5 % | median 60, p5 58 |
| race, scale 2 (1280x960) | 2 768 | 16.68 / 20.82 / 22.79 ms | 20.1 % | median 58, p5 51 |

Where a frame's time goes (the trace's per-frame columns, on-time frames): the
executor (render) thread is the limit, 8.1 ms busy at scale 1 (backend draws
4.9, present 0.8, the pushbuffer walk 2.4) and 10.4 ms at scale 2 (draws 5.3,
present 2.7); the guest thread's own code takes about 4.9 ms. At scale 2 the
present step, which waits for the GPU, took over 5 ms in two thirds of the
slow frames. `top -H` in the race: the executor about 60 % of a core, the
guest's main thread about 29 %. Pipeline compiles on the render thread: 14 in
a first race (354 ms in all, the worst 123 ms), none in a later 9-minute
session: the key list of BO-93 is kept in the app's cache folder and
pre-warmed at the start (`pre-warm 24 + 35 + 72 in 4.0 ms (done)`).

### Hand-played races at 2x (the user, track C2_V1, 2026-10-11)

| | pacing, aspect | race | over 18.5 ms | worst second / p5 | present step |
|---|---|---|---|---|---|
| A | sleep, 4:3 | 124 s | 17.0 % | 47 / 49 fps | 2.9 ms |
| control | spin, 16:9 | 71 s | 13.5 % | 47 / 52 fps | 3.5 ms |
| B, `TU_DEBUG=sysmem` | spin, 16:9 | 39 s | 0.9 % | 58 / 58 fps | 0.9 ms |
| B2, `TU_DEBUG=sysmem` | spin, 16:9 | 149 s | 11.6 % | 49 / 51 fps | 2.7 ms |

B was a quiet 39 s; over two laps sysmem is no gain, and it is not a default.
What is known about the dips: in the slow frames the executor's present step
takes over 5 ms (975 of 1035 in B2); the backend never waited for an earlier
frame (`0 frame waits` in every session), so the two frames in flight overlap
and the GPU is not behind; GPU busy read 15 to 45 % at 587 MHz; no throttling
seen, though a CPU zone read 78 to 81 °C with Power mode Maximum. The display
lock (the Choreographer's refresh times, `android_vsync.c`) is on and changed
little (16.2 % in a scripted race). What blocks inside the present step is
not known; `RECOMP_TRACE=vk_prof` splits it. At 1x the same race holds 60.

### What the traces said (2026-10-11, later the same day)

Two things were tried after the table above, in scripted races
(`tools/headless/to_race_busy.sh`: steering, boost, the brake, crashes):

| busy race, 107 s | over 18.5 ms | flip p95 / p99 | executor busy (draws, present) |
|---|---|---|---|
| FIFO, 2x (a quiet cruise, for scale) | 16.2 % | 20.66 / 22.47 ms | 11.0 ms (5.7, 2.4) |
| MAILBOX, 2x | 35.8 % | 21.64 / 23.53 ms | 13.3 ms (5.9, 4.7) |
| MAILBOX, 1.5x (960x720) | 6.2 % | 18.83 / 22.50 ms | 9.4 ms (5.3, 1.5) |

In the quiet cruise MAILBOX at 2x had 1.5 % slow frames against FIFO's 16.2 %.

- **Perfetto, FIFO** (`bo3-fifo.pftrace`, kept on the build PC in
  `~/bo3-android/device/`): the present thread sleeps in `dequeueBuffer`
  inside `vkAcquireNextImageKHR` for 10.4 ms of every frame; the executor is
  Running 83 % of the time and its submits never block (longest 1.9 ms).
- **Perfetto, MAILBOX, busy 2x** (`bo3-mailbox.pftrace`): the `dequeueBuffer`
  wait is gone (present and queueBuffer under 1 ms a frame); the executor is
  Running 71 %, asleep 27 % in many short sleeps (about 4.7 ms a frame, the
  size of the trace's present step at 2x), waiting for a core 1.3 %.
- **simpleperf, executor thread** (`bo3-perf-mailbox.data`): 44 % of its
  cycles are the spin of the vblank wait (`flip_pacing_stall ->
  xbox_VblankWaitUntil -> clock_gettime`, which goes into the kernel on this
  device); draws 30 %, the pushbuffer walk about 12 %, Turnip 7.6 %. No
  per-draw timer is read with tracing off.

So: MAILBOX with the display lock removes the queue's share of the dips, and
the rest scales with the pixel count; 1.5x is the default for that reason.
What the dips follow is the draw count: under about 1 050 draws a frame (2x;
1 100 to 1 250 at 1.5x) the present step is half a millisecond, above it 4 to
7 ms, while the cost per draw stays 5 to 6 µs. An off-CPU profile of the
executor in a busy 2x race (`bo3-offcpu-2x.data`) names one blocking call:
Turnip's `wait_timestamp_safe` (a KGSL wait for the GPU to reach a submit),
9.6 % of the thread's wall time; the 50 µs naps while the title has nothing
queued are 13 % and the vblank wait 33 %. With `vk_occ=fixed` (no occlusion
queries, no mid-frame submits: 49 800 submits against 64 500) the same race
has 4.1 % slow frames against 6.1 %: part of it, not all. The GPU clock sat
at 587 MHz of a 670 MHz table in every sample (governor msm-adreno-tz), 37 %
busy.
What the executor slept on is in the next section. MAILBOX shows one frame a refresh here because the
title is paced by the display-locked vblank; no two presents closer than 8 ms
were seen in 5 900. Not checked: the lock while the app is in the background.

### The dips in traffic: a "poll" that waits for the GPU (2026-10-11, night)

Named with `RECOMP_TRACE=vk_frame=<file>` (one CSV line per flip from the
Vulkan backend: submits by reason, render passes begun, cleared, loaded,
resumed and empty, the time in each driver call that can wait, the frame's
GPU time from timestamp queries) in the busy 2x race.

- **Not the submits.** A frame makes 2.2 queue submits (one when the frame is
  complete, one at the flip for the copy into the window's slot, 0.2 at an
  occlusion report), 3 at most; `vkQueueSubmit` took 0.15 ms a frame. The
  "ten submits a frame" read earlier was a miscount (64 491 submits belonged
  to 24 600 presents of the whole session, not to the race's 6 000 flips).
- **One call waits.** In the frames over 18.5 ms the present step was 6.46 ms,
  of which 6.12 ms was a single `vkGetSemaphoreCounterValue` (11.9 ms the
  longest seen); on-time frames lost 2.2 ms a frame to it inside the vblank
  hold, where it did not show.
- **Why.** Turnip on KGSL has no timeline semaphore of its own; Mesa emulates
  it with a list of pending submits and asks each "are you done" with
  `IOCTL_KGSL_DEVICE_WAITTIMESTAMP_CTXTID` and timeout 0 (Mesa 26.2.4:
  `vk_sync_timeline_gc_locked` -> `kgsl_syncobj_wait` ->
  `wait_timestamp_safe`). The KGSL kernel takes timeout 0 as "for ever"
  (`adreno_drawctxt_wait`), so the question returns when the GPU has finished.
  The same question is asked inside every `vkQueueSubmit` that signals such a
  semaphore (`vk_sync_timeline_alloc_point_locked`): with only our polls
  removed the wait moved into the flip's submit (6.7 ms in slow frames,
  12.4 % slow). `vkGetFenceStatus` goes the same way. So the backend's two
  frames in flight never overlapped: each flip waited for its own frame's GPU
  work, and a frame whose recording ended late (many draws) had no vblank
  hold left to hide that in. That is the threshold in draws, and why the
  wait grew with the pixel count: it was the GPU's time for the frame.
- **The GPU is not the limit.** GPU time per frame from the timestamps: 2x
  4.8 to 5.4 ms mean, p95 5.9 to 7.2, 9.1 the longest; 1.5x 4.5 mean, p95 5.5.
- Fixed upstream on 2026-09-30, after 26.2.4: Mesa main
  `e984ef294eaf` "tu/kgsl: Prevent indefinite wait times for 0 timeout waits"
  and `6cb65e613254` "tu/kgsl: Always round up the wait timeout" (merge
  request 44838): the poll reads the retired timestamp instead.

Two fixes, independent of each other:

1. **The backend** (toolkit `b830672`, `nv2a_pb_vulkan.c`): on Android it
   does not use the timeline semaphore. A fence per frame, signalled by the
   frame's last submit and waited for (with a real timeout) when the frame's
   slot is taken again two flips later; what is known done is what such a
   wait has shown; occlusion counts are read from the query pool without
   waiting. Works with any Turnip, also one the player puts into
   `<files>/driver/`. Other platforms keep the poll (a native timeline
   semaphore answers it at once, and occlusion counts come back within the
   frame); `RECOMP_DEBUG=vk_poll=0|1` switches.
2. **The driver** (`tools/android/patches/mesa-26.2.4-kgsl-zero-timeout.patch`,
   applied by `fetch-deps.sh`): the two upstream commits, adapted by hand to
   26.2.4.

| busy race, one run each | over 18.5 ms | flip p95 / p99 | present step mean / p95 | executor busy p95 |
|---|---|---|---|---|
| 2x, before | 5.6 % | 18.67 / 21.46 ms | 1.83 / 6.94 ms | 16.0 ms |
| 2x, polls removed only | 12.4 % | 20.19 / 23.32 ms | 2.25 / 7.25 ms | 17.1 ms |
| 2x, fix 1 | 0.6 % | 17.07 / 17.57 ms | 0.16 / 0.20 ms | 9.5 ms |
| 1.5x, before | 2.2 % | 17.35 / 19.74 ms | | |
| 1.5x, fix 1 (a heavier route: 1 320 draws a frame against 1 000) | 3.8 % | 17.56 / 23.58 ms | 0.11 / 0.18 ms | 10.8 ms |

The scripted route differs from run to run (a crash changes what follows), so
single runs compare badly; with fix 1 the executor is no longer what a slow
frame waits for at either scale.

What followed, small and without a change to the picture (toolkit `c1919c9`,
`2fb1035`; the Vulkan smoke test passes on MoltenVK with each, validation
layer on, also with a submit forced at every pass boundary):

- **Clears.** A `CLEAR_SURFACE` of colour and depth opened a depth-clearing
  pass, ended it at once without a draw and opened a colour-clearing one that
  loaded the depth just cleared. Now one pass clears both, and a depth clear
  on the open pass clears inside it: 24 passes a frame became 18, none empty,
  depth loads 11 became 5. All platforms.
- **The frame goes to the GPU in parts** (`RECOMP_DEBUG=vk_batch=<draws>`,
  300 on Android, off elsewhere): when a render pass begins and 300 draws (or
  2 ms with any draw) have gone since the last submit, what is recorded is
  submitted. Never inside a pass, and an occlusion report no longer ends the
  open pass for its submit (it goes with the next one; after 8 ms on its
  own). 6.5 submits a frame, 0.29 ms in all; the GPU now works on a frame for
  12.8 ms of wall time beside the CPU instead of 6 to 7 ms after it. The
  frame's fence is still signalled by its last submit.
- **Dynamic state** that has not changed is not set again (some 15
  `vkCmdSet*` and a pipeline bind per draw before). All platforms.
- **The vblank spin** reads the CPU's counter (`cntvct_el0`, 19.2 MHz,
  124 ns a read on the RP5) instead of `clock_gettime`, which is a system
  call on this device; Android arm64 only, with a check at the first use.

One busy 2x race after these (5 544 frames; the scripted car stayed in a
dense part of the city, 1 440 draws a frame against 1 040 in the run before,
so the totals do not compare): executor busy p95 10.5 ms, present step
0.13 ms, GPU 6.2 ms a frame (p95 7.9), 5.4 % of frames over 18.5 ms. By
draws per frame, before -> after: 1 200 to 1 350 draws 1.6 % -> 0.9 % slow;
1 350 to 1 500 draws 7.4 % -> 5.3 %; over 1 500 (none before) 7.4 %. In those
slow frames the executor waits 12 ms for the title to submit: the title's
main thread spends 3.3 to 5.4 ms in XDK library code other than Direct3D
(0.1 ms in an on-time frame). Which library (DirectSound is the guess, with
many cars sounding) is not resolved; that, not the renderer, is what is left
in dense traffic. The stretches at 30 a second in a race are the title's own
(it asks for every second refresh during a takedown or crash camera).

Next candidates, not done because they touch what is drawn when: moving a
target to the sampled layout when its pass ends for a target switch instead
of ending a later pass for it (5 passes a frame still go back to a target
already drawn), and not storing or loading depth for passes that do not need
it.

Defaults on Android now: render scale 1.5 (the Vulkan backend sizes in
halves; 1, 2, 3, 4 in the overlay), 16:9 with the widescreen HUD fix, the
linear filter, FXAA, sharpening 0.30 and Menu/HUD upscaling 2x (the owner's decision
of 2026-10-12; FXAA as the anti-aliasing pass; the upscaling's hold is gone,
`nv2a_texscale_held()` is 0, see ui-scale.md), MAILBOX present with the Choreographer
display lock, anisotropy 4, Fullscreen on, Power mode Balanced, rumble
strength 2.0 on the device's own vibrator (see Rumble below), the executor
thread on the fastest core and the guest threads on a
core of the next kind (no gain measured, kept). Input > Face buttons has a
Nintendo layout (A-B and X-Y mirrored) on every platform; the default is the
Xbox layout. Options, not defaults: `RECOMP_PRESENT_VSYNC=1` (FIFO),
`RECOMP_ANDROID_SWAPCHAIN_HEIGHT=<rows>` (not measured), `TU_DEBUG=sysmem`
(no gain), `RECOMP_ANDROID_VSYNC=0`, `RECOMP_ANDROID_EXEC_CPU=off`,
`RECOMP_ANDROID_VIBRATION_FLOOR=<speed>`.

### Rumble

The game's rumble plays on the device's own vibrator, because the built-in
pad of a handheld has no motors. An external pad does not rumble on
Android: SDL 2.32.10's Android joystick driver has no rumble at all
(`ANDROID_JoystickRumble` is `SDL_Unsupported`), so a Bluetooth or USB pad
stays still whatever it is. With Input > Vibration on Auto (the default) the
device shakes only when the pad in use is its own (Android's
`InputDevice.isExternal()` says no): a phone on the table does not buzz for a
pad held elsewhere. "This device" shakes it whatever the pad; "Controller"
and "Off" leave it still. The log says which it took the pad for:
`[RUMBLE] pad 2022:3002 is this device's own ...`.

Strength (Input > Rumble strength, 0 to 3 in quarters) is the same curve as
on every other host: 1.0 is the Xbox pad's own, the default here is 2.0, and
the strongest rumble is full at any setting. The game's idle hum (about 1 %
of full) is silence, as it was on the console's motors;
`RECOMP_ANDROID_VIBRATION_FLOOR=<speed>` moves that threshold (0: none), it
is no longer needed for the hum. The vibrator is given one of 16 amplitudes,
and a new one only when the level has moved a whole step.

Spin lead in Balanced mode, busy 1.5x race: 8 ms 2.3 % slow frames, 2 ms
4.0 %, 0 (pure sleep) 35.9 %; 8 ms stays. The sleeping thread comes back
late on this kernel, which is also what the 2x dips look like.

Scripted tests on a device: `tools/android/scene-launch.sh` is the `LAUNCH`
of the `tools/headless` scenes. Its variables go into `scene-env.txt`, which
the app reads at that start only and deletes; the player's `env.txt` is never
written. A test start sets `pad_script_only`, so the device's own pad cannot
take the pad from the script.

A scene never runs on the player's saves: `scene-launch.sh` gives the app a
save root and a settings file of its own (`ANDROID_SAVE_DIR`, default
`<files>/scene-saves`; `ANDROID_SETTINGS`, default `<files>/scene-settings.toml`;
`ANDROID_SAVES_FROM` / `ANDROID_SAVES_FROM_DEVICE` seed the root with a profile
before each start), refuses a root that is the player's own (`takedawn/`,
`burnout3/`, or around them), and after the start checks the app's
`[SAVE] dir:` line, stopping the app if it names another folder.
`ANDROID_ALLOW_REAL_SAVES=1` is the explicit way to run on the real saves.

### The device's vibrator and the built-in pad

Input > Vibration "Auto" shakes the device only for the device's own pad.
`InputDevice.isExternal()` does not say which that is: the RP5's built-in pad
(2022:3002, "Xbox Wireless Controller") is a USB device inside the case and
reports itself external, and Auto was silent on it. The rule now
(`android_vibration.c`, `pad_is_own`): not external, or a pad without a
vibrator of its own that is the only pad or controller number 1 (a built-in
pad is there from boot; a pad paired later gets the next number; the pads
Android can rumble report a vibrator). The log says what it decided:
`[RUMBLE] pad 2022:3002 is this device's own ... (Android: external, no
vibrator of its own, controller 1 of 1)`. Still wrong: one motorless pad on a
phone without controls shakes the phone (Vibration > Controller or Off).

## Saves and settings

`<files>/takedawn/` holds what the save folder holds elsewhere: `UDATA`
(the profiles), `TDATA`, the emulated HDD images (`Partition*.img`: 5.2 GB
apparent, sparse, a few KB real), `settings.toml`, `crash/`. A save from
another machine is `UDATA/4541005b/` copied there (the user's Mac profile
loads). Reinstalling over the app (`adb install -r`, the same key) keeps it;
uninstalling removes it unless "keep app data" is ticked in the dialog;
"Clear storage" removes it; "Clear cache" removes only the shader cache. The
overlay opens with Back+Start, Select+Start on the RP5, or with the system's
Back (button or gesture; in the overlay it goes back like B). The game is left
through the app switcher; Back never ends it (`GameActivity.onBackPressed`
sends SDL's Back key, `SDL_HINT_ANDROID_TRAP_BACK_BUTTON` is set).

The Android defaults (render scale 1.5, 16:9, HUD fix, linear filter, sharpening
0.30, Menu/HUD upscaling 2x, anisotropy 4, MAILBOX, Fullscreen, rumble strength) are defaults: a value in `settings.toml` wins (checked on
the device with a file asking for 3x: `render.scale = 3`, 1920x1440 targets).
A changed setting is written 0.6 s after the change while the overlay draws,
when the overlay closes, and, on Android, when the app leaves the foreground
(an app swiped out of the recent apps is ended without a word). A scripted
test start does not write the file: a row its environment overrides keeps
what the file said.

## How it works

- **Activity.** `StartActivity` (the launcher entry) starts `GameActivity` when
  `Burnout 3 Takedown/default.xbe` is there. `GameActivity` extends SDL2's
  `SDLActivity`: it puts the app's directories into the environment
  (`RECOMP_ANDROID_DIR`, `XDG_DATA_HOME`, `XDG_CACHE_HOME`, `env.txt`'s lines)
  and SDL loads `libSDL2.so` and `libmain.so` and calls `SDL_main`, which is
  `main()` of `src/game/main.c`.
- **Host.** `recomp_android_host_init()` (toolkit) sends stdout and stderr to
  logcat (tag `takedawn`) and to `takedawn.log`, and makes the files directory
  the working directory. From there on it is the Linux host.
- **Memory.** No fixed low addresses: the guest's 4 GB are a reservation
  wherever the kernel puts it and every access adds `g_xbox_mem_offset`, as on
  Linux and macOS. `/proc/sys/vm/mmap_min_addr` (32768 on the RP5) does not
  matter.
- **Library, not executable.** `libmain.so` is linked `-Bsymbolic`, so the
  24 000 lifted functions call each other directly. The guest registers are
  thread-local variables: in a shared library loaded with `dlopen` they are
  reached through TLS descriptors (64 TLSDESC relocations), a call per
  variable per function, where the Linux and Windows executables add a fixed
  offset to the thread pointer. bionic has no static TLS for a `dlopen`ed
  library ("Neither bionic nor musl currently allocate any surplus TLS
  memory", bionic `docs/elf-tls.md`), so the initial-exec model is not
  available. What it costs is not measured.

## Known problems

- **2x in a race**: the wait that broke it is fixed ("The dips in traffic");
  repeat runs and the default scale are open.
- **Minimum Android 13 (API 33)** as built: the crash and frame-time code uses
  `backtrace()`, which bionic has from API 33. Lower needs a fallback
  (`_Unwind_Backtrace`); below API 30 also `memfd_create`, and below 29 ELF
  TLS itself.
- **Vblank lock.** `RECOMP_VBLANK_SYNC=display` normally takes the display's
  refresh times from `VK_KHR_present_wait`, which no Android driver here offers.
  On Android they come from the Choreographer (`android_vsync.c` in the
  toolkit; `RECOMP_ANDROID_VSYNC=0` turns that off, and the emulated vblank
  then runs on its own 60 Hz timer beside the panel's).
  `VK_GOOGLE_display_timing` (offered by the stock driver) is not used.
- **Three swapchain images at least** on this surface (two on the desktop
  hosts): one more frame of latency possible; to be measured (BO-74).
- **The guest core.** Guest threads are pinned to the fastest core the process
  may use at that moment. The RP5's kernel takes cores in and out of use
  (cpu 6 was missing from the mask in one reading, cpu 7 in another), so the
  choice varies and the pinned core may be taken away later.
  `RECOMP_GUEST_CPUS=token` (the macOS rule) or `=all` are the alternatives.
- **The panel is mounted sideways** (1080x1920). The swapchain is made with
  the identity transform and the compositor turns the picture; Android then
  answers every present with VK_SUBOPTIMAL_KHR, which on Android no longer
  rebuilds the swapchain (it did, every frame, and the app was killed after
  two minutes). Drawing pre-rotated would spare the compositor's turn; not
  done, its cost not measured.
- **Coming back from the background** was a black screen until the
  presenter learned to make the window's surface and swapchain again (and the
  title is held while the app is away). Seen working on the RP5 for Home and
  for the recent-apps screen (`the window's surface was replaced` in the
  log); the notification shade needs nothing. Screen off and on showed the
  picture again but logged no life-cycle event, so that case is not proven,
  and a long sleep is not tried.
- **Background.** Leaving the app destroys the window's surface; the Vulkan
  presenter's handling of that on Android is not written.
- **No shader pre-build** (it starts the executable again, which an app
  cannot): shaders compile on first use.
- **No touch controls, no on-screen menu button.** Pad only; the overlay opens
  with Back+Start (Select+Start on the RP5). The overlay's size on a 5.5"
  panel is not looked at (Xerenge's XR-21 was about exactly this).
- **A missing data file ends the run**: `NtReadFile` on the failed handle
  faults (seen with a data set that lacked `Tracks/US/C5_V1`).
- **16 KB pages**: the ELF files are 16 KB aligned, and the runtime runs on
  16 KB pages on macOS, but the Linux path's guest-window bookkeeping counts
  in 4 KB pages and has not run on a 16 KB Linux kernel. The RP5 has 4 KB.
- **The log** carries the full boot trace (3 000 lines in 16 s) to logcat and
  to external storage.

## Devices

Only the Retroid Pocket 5 has run the game. The rest of this table is read
from Mesa 26.2.4's source (`src/freedreno/common/freedreno_devices.py`)
and from the makers' and reviewers' pages, and says "expected", not "works".

| device | SoC, GPU | Android (as sold) | Mesa 26.2.4's Turnip knows the GPU (the APK's A725 build was not checked) | state |
|---|---|---|---|---|
| Retroid Pocket 5 | Snapdragon 865, Adreno 650 | 13 | yes | **tested**: all of this document |
| Retroid Pocket 6 | Snapdragon 8 Gen 2 (QCS8550), Adreno 740 | 13 | yes (FD740, KGSL chip id `0x43050a01`) | expected; not run |
| AYN Odin 2 | Snapdragon 8 Gen 2, Adreno 740 | 13 | yes (FD740) | expected; not run |
| AYN Odin 3 | Snapdragon 8 Elite, Adreno 830 | 15 | listed (Adreno 830, KGSL chip id `0x44050001`); the a8xx code is new in the 26 series (26.2's notes still list a8xx fixes) | uncertain; not run |

What cannot be known without the devices, in the order it would bite:

1. **Which driver a start picks.** Automatic uses the bundled Turnip only
   when the device's own driver is older than Vulkan 1.3. A device whose own
   driver says 1.3 (likely on the Adreno 740 and 830; not checked) runs the
   game on Qualcomm's driver, which this backend has never met; "Mesa Turnip,
   in the app" on the driver page is the other choice there.
2. **libadrenotools on Android 14 and 15** (the Odin 3 ships 15): it swaps
   the driver through the linker's namespaces, which newer Android versions
   and some makers' builds restrict. Not checked. If it cannot open a driver,
   the start falls to the next one and says so.
3. **Page size.** A device with 16 KB pages needs the guest-window
   bookkeeping of "Known problems".
4. **The built-in pad and the vibrator**: each maker's pad has its own ids
   and its own answer to `isExternal()` (the rule is above).
5. **The panel**: 120 Hz panels (RP6, Odin 3). The display lock follows the
   Choreographer; the title wants 60, and what a 120 Hz mode does to the
   pacing is not tried.

Sources: https://wiki.lineageos.org/devices/RP6/,
https://retrocatalog.com/retro-handhelds/retroid-pocket-6,
https://droix.net/blogs/ayn-odin3-review/, https://liliputing.com/?p=185086.

## Test plan (RP5)

Before anything: `adb shell df -h /sdcard` (the data needs 2.4 GB; keep 2 GB
free), battery over 15 %, nothing else in the foreground. The device is shared
by several projects: take its lock first
(`adb shell mkdir /data/local/tmp/gametest.lock`, an `owner` file in it,
`rm -r` afterwards).

1. Install, push the full data, start. Expect in `adb logcat -s takedawn`:
   `host directory: ...`, `[PRESENT] display 0 mode 1920x1080 at 60 Hz`,
   `xbox_MemoryLayoutInit: complete`, `[SDL2] audio initialized`,
   `[INPUT] controller ... opened`. No status bar: the SDL window has
   `SDL_WINDOW_FULLSCREEN`, which makes SDL's activity go immersive (without
   it SDL turns the bars back on whatever the theme says: 1920x1026), the
   theme is `Theme.DeviceDefault.NoActionBar.Fullscreen` with the cutout mode
   `shortEdges`, and `GameActivity` re-applies immersive sticky on focus.
2. The driver: `GPU driver: the Mesa Turnip driver in the app ...` (logcat),
   `[vulkan] replacement driver .../libvulkan_freedreno.so opened through
   libadrenotools`, `[vulkan] device up: Turnip Adreno (TM) 650 (Vulkan
   1.3...)` and `[PRESENT] Vulkan swapchain: ... images, ... FIFO (vsync)`.
   If it fails: the message box's text, and `adb logcat` around it.
3. Intro movies, the menu with the built-in pad (A, B, Start, the sticks, the
   d-pad, both triggers as analogue), sound, the music.
4. A Single Event race on Silver Lake, three laps. Frame times: put
   `RECOMP_PRESENT_STATS=1` in `env.txt`; `RECOMP_RENDER_SCALE=1` first, then
   1.5 (60 fps at 1x-1.5x is the expectation to test).
5. Latency (BO-74): `RECOMP_TRACE=pacing=all` and the present statistics, to
   compare with the Deck and with Xerenge on the same device.
6. The overlay (Select+Start): readable, pad navigation, settings kept in
   `takedawn/settings.toml` across a restart.
7. Save, quit, start again: the profile is there. Home button and back into
   the app. A second pad over Bluetooth (split screen).
8. A 20-minute race: temperature, battery drain, the cores
   (`adb shell top -H -p $(adb shell pidof io.github.xenophobentx.takedawn)`).
9. A crash report: `takedawn/crash/` after a fault.

## Sources

- Xerenge's Android build (the user's reference; no code taken):
  `scripts/build-android-apk.sh`, `rexglue/android/`, tasks XR-21 and XR-22.
- SDL2's `android-project` (manifest, `SDLActivity`) and `docs/README-android.md`.
- https://vulkan.gpuinfo.org (report 36612 and the Adreno 650 list).
- https://github.com/bylaws/libadrenotools (`include/adrenotools/driver.h`).
- https://docs.mesa3d.org/android.html (building Turnip with the NDK),
  https://docs.mesa3d.org/relnotes/26.2.4.html (the checksum), and the Mesa
  26.2.4 source (`docs/features.txt`, `src/freedreno/vulkan/tu_device.cc`).
- https://developer.android.com/guide/practices/page-sizes (16 KB pages).
- bionic `docs/elf-tls.md` (TLS in `dlopen`ed libraries, TLSDESC on arm64),
  and the NDK's headers for what bionic has at which API level
  (`pthread_setaffinity_np` 36, `backtrace` 33, `memfd_create` 30, no
  `pthread_cancel`, no `explicit_bzero`).
