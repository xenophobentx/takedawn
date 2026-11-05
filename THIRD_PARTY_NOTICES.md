# Third-party notices

Takedawn's own code is under the MIT licence ([LICENSE](LICENSE)). This page
lists the work of other people that is inside the downloads or was used to
make them, with the licence of each. Where a version or a copyright line is
given, it is the one recorded in this repository or in the toolkit.

Nothing on this page is game data. The downloads contain no game data.

## Projects this one grew from

| Project | Author | Licence | What came from it |
|---|---|---|---|
| [sp00nznet/burnout3](https://github.com/sp00nznet/burnout3) | Ned Heller (sp00nz) | MIT, Copyright (c) 2026 sp00nz | The repository this project started from: the host entry, the per-title data, the pipeline script and the research notes |
| [sp00nznet/xboxrecomp](https://github.com/sp00nznet/xboxrecomp) | Ned Heller (sp00nz) and its contributors | MIT, Copyright (c) 2026 sp00nz; third-party parts as listed in its `NOTICE` | The recompiler and the Xbox runtime |
| [apexJCL/xboxrecomp](https://github.com/apexJCL/xboxrecomp) | apexJCL | MIT | The fork of the toolkit this project's runtime is based on: the Metal backend and the first macOS host |

The upstream copyright line is kept in [LICENSE](LICENSE).

## LGPL code in the program

Eight files of the runtime were extracted from
[xemu](https://github.com/xemu-project/xemu) and are under the GNU Lesser
General Public License, version 2.1 or (at your option) any later version:

| File | Copyright |
|---|---|
| `src/apu/apu_core.c` (MCPX APU core) | 2012 espes; 2018-2019 Jannik Vogel; 2019-2025 Matt Borgerson |
| `src/apu/apu_dsp.c` (APU DSP) | 2012 espes; 2019-2025 Matt Borgerson |
| `src/apu/apu_vp.c` (APU voice processor) | 2012 espes; 2018-2019 Jannik Vogel; 2019-2025 Matt Borgerson |
| `src/apu/apu_regs.h` | 2012 espes; 2018-2019 Jannik Vogel; 2019-2021 Matt Borgerson |
| `src/apu/apu_state.h` | 2012 espes; 2018-2019 Jannik Vogel; 2019-2025 Matt Borgerson |
| `src/apu/apu_debug.h` | 2020-2021 Matt Borgerson |
| `src/apu/fpconv.h` | 2020-2025 Matt Borgerson |
| `src/nv2a/nv2a_regs.h` (NV2A register definitions) | 2012 espes; 2015 Jannik Vogel |

The toolkit's own `src/apu/apu_shim.h` exists only to host this code and is
released under the same licence.

How Takedawn ships this code:

- It is compiled and linked statically into the program on every platform.
- Every package carries the text of the LGPL 2.1 and this attribution.
- The source code that corresponds to each release, these files included, is
  published with that release: the toolkit repository at the release's commit,
  named in the release notes and in the output of `--version`.

Each package's `SOURCE.txt`, in its `licenses` folder, names the two commits the
program was built from. To use a changed version of these files, build the
program again from the two repositories with the scripts in the game
repository's `tools/release/` (`docs/release.md`); that needs your own copy of
the game's executable, from your own disc, because the recompiled game code is
made from it on your machine and cannot be published as source or as object
files.

The toolkit also holds `src/nv2a/nv2a_core.c` and `nv2a_state.h`, adapted
from xemu's `nv2a.c` and `nv2a_int.h` under the same licence (2012 espes;
2015 Jannik Vogel; 2018-2025 Matt Borgerson). They build into a library this
program calls nothing in, so their code is not in the program.

## GPL code that came with the xemu audio code: removed

One piece that came with the xemu audio code was not LGPL: the state variable
filter of xemu's `hw/xbox/mcpx/apu/vp/svf.h` ("Adapted from SWH LADSPA Plugins
package, modified for xemu. Author: Steve Harris, andy@vellocet. License:
GPLv2"), which the voice processor ran for every voice with its filter on. It
is no longer in the toolkit or in this program. The voice low-pass is now the
toolkit's own `src/apu/apu_lowpass.h` (MIT): the textbook digital state
variable filter (Hal Chamberlin, "Musical Applications of Microprocessors"),
written from the book's description and not from that code. The toolkit's
`NOTICE` says the same and how it was done.

The same check found the declarations of xemu's DSP core state in the
toolkit's `src/apu/apu_state.h` (a structure layout and six sizes, from
xemu's DSP headers, which are GPL-2.0-or-later). Nothing in them was ever
used, the DSP interpreter they belong to was never in the toolkit, and they
are removed too. A comparison of the toolkit's sources against xemu's
`hw/xbox` now finds lines in common only with LGPL files and the BSD ADPCM
decoder. No GPL code from xemu is in the program.

## Other code in the program

These are in the toolkit's source. Its `NOTICE` file is the authoritative
list and is shipped in every package.

| Component | Version | Licence | Copyright | Used for |
|---|---|---|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) | 1.92.9b | MIT | 2014-2026 Omar Cornut | The settings menu |
| [SMAA](https://github.com/iryoku/smaa) reference implementation | commit 71c806a | MIT | 2013 Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro, Diego Gutierrez | Anti-aliasing |
| [Roboto Condensed](https://github.com/googlefonts/RobotoCondensed) Medium, a subset (Latin, Latin Extended, Greek, Cyrillic) | google/fonts commit 69d0549; see the toolkit's `third_party/roboto_condensed/VENDORED.txt` | SIL Open Font License 1.1 | 2011 The Roboto Project Authors | The settings menu's text, and the letters the game's own font lacks. Text: `RobotoCondensed-OFL.txt` |
| [DejaVu Sans Mono](https://dejavu-fonts.github.io), a subset (Latin Extended, Greek, Cyrillic) | see the toolkit's `third_party/dejavu/VENDORED.txt` | Bitstream Vera Fonts licence with the DejaVu changes (Arev: Tavmjong Bah); public-domain parts | Bitstream, Inc.; the DejaVu authors | The settings menu's letters outside basic Latin. Text: `DejaVu-fonts-LICENSE.txt` |
| [Anton](https://github.com/googlefonts/AntonFont), the outline of one letter (W), as artwork | google/fonts `ofl/anton/Anton-Regular.ttf` (SHA-256 in `tools/glyphs/headfont.py`) | SIL Open Font License 1.1 | 2020 The Anton Project Authors | The W that the game's heading font lacks, for the TAKEDAWN ONLINE heading (`assets/glyphs/headfont/w.svg`, drawn into the font's atlas at run time). The font itself is not shipped. Text: `assets/glyphs/headfont/Anton-OFL.txt` in the source |
| [dr_libs](https://github.com/mackron/dr_libs): dr_mp3, dr_flac, dr_wav | commit dfe8377 | Public domain (Unlicense) or MIT No Attribution; dr_mp3 contains minimp3 (lieff, CC0) | David Reid | Decode the player's own music folder (custom music). Not in the Android program's use. Text: `dr_libs-LICENSE.txt` |
| [stb_vorbis](https://github.com/nothings/stb) | commit 2c980bb | Public domain (Unlicense) or MIT | Sean Barrett | Decodes Ogg Vorbis files of the player's own music folder. Text: `stb_vorbis-LICENSE.txt` |
| [stb_image, stb_image_write, stb_dxt](https://github.com/nothings/stb) | commit f4a71b1 | Public domain (Unlicense) or MIT | Sean Barrett | Read the PNG files of texture packs, write the texture dump, compress big pack textures. Text: `stb-LICENSE.txt` |
| [ADPCM-XQ](https://github.com/dbry/adpcm-xq) decoder (through xemu) | | BSD-3-Clause | David Bryant | Decodes the game's ADPCM sound. Text: `ADPCM-XQ-BSD-3-Clause.txt` |
| AMD FidelityFX CAS (the algorithm) | | MIT | Advanced Micro Devices | Sharpening |
| Super-xBR (the algorithm, a C version written from the published shaders) | | MIT | 2015 Hyllian (sergiogdb@gmail.com) | Menu and HUD upscaling (`graphics.ui_scale`). Text: in `xboxrecomp-NOTICE.txt` |
| [FSRCNNX x2 8-0-4-1](https://github.com/igv/FSRCNN-TensorFlow) and [NNEDI3 nns16-win8x4](https://github.com/bjin/mpv-prescalers), as shader files | release 1.1; mpv-prescalers commit b3f0a59d | LGPL-3.0-or-later | 2017-2021 igv; bjin (NNEDI3 by tritical) | Texture upscale (`graphics.texture_upscale`, off by default) and the films' Best mode (`graphics.film_upscale`). Not built into the program: the files sit beside it in `shaders/upscale`, with `COPYING.LESSER`, `COPYING` and a `NOTICE.md` that says where they come from and how to replace them |
| AMD FidelityFX Super Resolution 1 (EASU and RCAS, as agyild's mpv port runs them on one plane, written as compute shaders) | 1.0.2 | MIT | 2021 Advanced Micro Devices, Inc. | The films' Cheap mode (`graphics.film_upscale`). Text: in `xboxrecomp-NOTICE.txt` |
| FXAA 3.11 (the algorithm, written from the paper) | | no code taken | NVIDIA (the algorithm) | Anti-aliasing |
| [glslang](https://github.com/KhronosGroup/glslang) | 16.6.0 on Windows and Android; the Steam Runtime 3 SDK's on Linux | BSD-3-Clause, Apache-2.0 and MIT (Khronos) | | Compiles shaders to SPIR-V for the Vulkan backend. Linked statically on Windows, Linux and Android |
| [libadrenotools](https://github.com/bylaws/libadrenotools) and its linkernsbypass | commit 8fae8ce | BSD-2-Clause | 2021 Billy Laws | Loads the Turnip driver inside the Android app. Linked into `libmain.so`, plus four small hook libraries |
| [ncnn](https://github.com/Tencent/ncnn) | 20260526 (its release's full source, built by `tools/android/fetch-deps.sh`) | BSD-3-Clause, with the notices of its parts (zlib for the NEON math functions) | 2017 THL A29 Limited, a Tencent company | Runs the menu pictures' upscale inside the Android app (see the upscaler's section below). Linked into `libmain.so`. Text: `ncnn-LICENSE.txt` |

The toolkit also credits xemu, Cxbx-Reloaded and xboxdevwiki as references for
hardware behaviour, and Fabian Giesen for the texture unswizzling technique.
No code was taken for those.

## Libraries shipped beside the program

| Component | Version | Licence | Where |
|---|---|---|---|
| [SDL2](https://github.com/libsdl-org/SDL) | 2.32.10 on Windows and Android | zlib | Window, sound and controllers. `SDL2.dll` on Windows, `libSDL2.so` in the Android APK; the Mac app and the AppImage carry sdl2-compat over SDL 3 (below) |
| SDL's Android activity (`org.libsdl.app`) | with SDL 2.32.10 | zlib | In the Android APK's `classes.dex` |
| LLVM libc++ (`libc++_shared.so`) | the Android NDK's | Apache-2.0 with LLVM exception | In the Android APK |
| [Mesa](https://www.mesa3d.org) Turnip (`libvulkan_freedreno.so`) | 26.2.4, built from Mesa's release archive with one fix from Mesa's main branch applied (`tools/android/patches/`) | MIT | The Vulkan driver in the Android APK |
| [Qt 6](https://www.qt.io) (Core, Gui, Widgets, DBus and platform plugins) | 6.10.3 | LGPL-3.0 | The setup window. Linked dynamically. The licence texts and a notice that says where Qt's source is and how to replace the libraries are in the package. Qt contains code of other projects, listed in the next table |

### Inside Qt

Qt's libraries contain code of other projects under their own licences. The
packages that have the setup window carry the notices and licence texts of all
of it in `Qt-THIRD-PARTY.txt`, copied unchanged from the source archive of Qt
6.10.3 (`qtbase-everywhere-src-6.10.3.tar.xz`, the address and checksum are
pinned in `tools/release/pins.env` and written at the top of that file, with
the path of each text inside the archive). In short:

| Component | Licence | Packages |
|---|---|---|
| PCRE2 10.47 (with its SLJIT JIT) | BSD-3-Clause with the PCRE2 exception; BSD-2-Clause | all |
| double-conversion 3.4.0 | BSD-3-Clause | all |
| HarfBuzz 13.2.1 | MIT | all |
| libpng 1.6.56 | Libpng and libpng-2.0 | all |
| FreeType 2.14.3 (with its BDF and PCF drivers and zlib) | FreeType Licence (chosen from FTL or GPL-2.0), MIT, Zlib | macOS, Windows |
| md4c 0.5.2 | MIT | all |
| zlib 1.3.2 | Zlib | Windows |
| Unicode character data 36 and CLDR 48.1 | Unicode-3.0 | all |
| BLAKE2, SipHash, MD4, MD5, SHA-3 (Keccak), tl::expected | CC0-1.0 (BLAKE2 also Apache-2.0) | all |
| SHA-1 (Dominik Reichl) | public domain | all |
| SHA-3 (brg_endian), RFC 6234 SHA-2, Robert Penner's easing, Adobe Glyph List | BSD-2-Clause and BSD-3-Clause | all |
| TinyCBOR 7.0; forkfd (macOS and Linux only) | MIT | all |
| Apache Tika mime type list, Google's emoji-segmenter 0.4.0 | Apache-2.0 | all |
| Image scaling (Imlib2 based), X server region code, WebGradients, the sRGB profile | BSD-2-Clause and Imlib2, X11 and HPND, MIT, the ICC's licence | all |
| Vulkan Memory Allocator 3.2.1 | MIT | macOS, Windows |
| D3D12 Memory Allocator, a D3D12 mipmap shader (Microsoft), Wintab headers | MIT; LCS/Telegraphics licence | Windows |
| Khronos OpenGL headers | MIT | Windows, Linux |
| libdbus-1 constants (QtDBus) | AFL-2.1 or GPL-2.0-or-later | macOS, Linux |
| Cocoa platform plugin code (Apple) | BSD-3-Clause | macOS |
| XCB-XInput | MIT | Linux |
| Wayland protocol descriptions (the wayland platform plugin) | MIT; HPND; LGPL-2.1-or-later | Linux |

No JPEG, SQL, network or ICU code is in the packages. FreeType: portions of the
Qt text rendering are copyright 2026 The FreeType Project
(www.freetype.org), all rights reserved.

The exact versions in a release are in its `BUILDINFO` file. The Mac app
carries sdl2-compat 2.32.74 (`libSDL2-2.0.0.dylib`) over SDL 3.4.18
(`libSDL3.0.dylib`), both built from the projects' release archives; SDL
contains HIDAPI (BSD-3-Clause). The AppImage carries the same two, built
from the same archives inside the Steam Runtime sniper SDK image
(`libSDL2-2.0.so.0`, `libSDL3.so.0`; SDL 3 with the changes in
`tools/release/linux/patches/`: its PipeWire output gives up on a stream
that does not start instead of waiting for ever), and, for the setup window, Qt built
there from its source archive and the small X11 helper libraries Qt's xcb
plugin needs (`libxcb-cursor`, `libxcb-icccm`, `libxcb-image`,
`libxcb-keysyms`, `libxcb-render-util`, `libxcb-util` and the libxcb
extension libraries; MIT/X11 licences, their Debian copyright files are in
the package). glibc, libstdc++, the Vulkan loader, libxkbcommon, FreeType and
fontconfig are the system's. The Windows
program links llvm-mingw's libc++, libunwind and compiler-rt (Apache-2.0 with
LLVM exceptions), the mingw-w64 runtime and winpthreads statically. Qt, where
the setup window is in the package, is 6.10.3.

The Windows setup program is made with [Inno Setup](https://jrsoftware.org/isinfo.php)
6.7.3, under Inno Setup's own licence. The AppImage is an AppDir packed with
`mksquashfs` and the AppImage project's static runtime (MIT), which is the
first part of the file. The setup window bundles no font and uses the system's
fonts.

## Used to build the releases, not shipped

| Component | Version | Used for |
|---|---|---|
| [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) | 20260922 | The compiler for Windows |
| CMake, Ninja | | Build system |
| Vulkan headers | SDK 1.4.363.0 | Windows build |
| [capstone](https://www.capstone-engine.org), the Python module | from PyPI | The recompiler's disassembler |
| Steam Runtime "sniper" SDK container image | | The compiler and libraries for Linux |
| Android SDK and NDK, a JDK | NDK r28 or newer | The Android build |

## Approaches and references, no code taken

| Source | What was learned from it |
|---|---|
| [ThirteenAG/WidescreenFixesPack](https://github.com/ThirteenAG/WidescreenFixesPack) (MIT), the Burnout 3 fix for the PlayStation 2 version | The arithmetic of the widescreen HUD fix and the ultrawide camera patch. The Xbox patch sites were found in this build |
| Nahelam's list of function names for the PlayStation 2 version | Names that helped to find the matching Xbox code |
| SpeedBreaker (GPL-3.0) | The distribution model (a prebuilt program, data from the player's own disc) and the idea of a built-in bug report. No code and no text was taken |
| Xerenge | The flow of the setup window and of the Android build script |
| Dolphin | A per-game list of shader keys compiled at start |
| Unleashed Recompiled | Compiling a known shader list while the logos play |
| apexJCL's BLiNX 2 project (MIT) | The method of the golden-frame comparison in `tools/headless` |

## The menu pictures' upscaler

"Sharper menu pictures" (the setup window's optional step, or Settings,
Graphics: see `docs/upscale-art.md`) runs an AI upscaler that every desktop
package carries: the program unchanged from its makers' release, and the
models. The Android app carries no program: ncnn is linked into `libmain.so`
(the table above) and the fast model below is in the APK as
`assets/upscaler/realesr-general-dn030-x4v3.param` and `.bin`, made by
`tools/android/fetch-deps.sh` from the same pinned files the same way. Linux and Windows: `upscaler/` beside the program;
macOS: `Contents/MacOS/realesrgan-ncnn-vulkan` (the arm64 part of the release's
universal program) and `Contents/Resources/upscaler/models`. The release
scripts fetch them from the addresses in `tools/release/pins.env` and check
each against its SHA-256.

| Component | Version | Licence | Copyright | Use |
|---|---|---|---|---|
| [realesrgan-ncnn-vulkan](https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan), from [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN)'s release v0.2.5.0 (`realesrgan-ncnn-vulkan-20220424-<system>.zip`) | 20220424 | MIT | 2021 Xintao Wang | Runs the models. Text: `realesrgan-ncnn-vulkan-MIT.txt` |
| [ncnn](https://github.com/Tencent/ncnn), built into that program | | BSD-3-Clause, with the notices of its parts | THL A29 Limited, a Tencent company | Text: `ncnn-LICENSE.txt` |
| [glslang](https://github.com/KhronosGroup/glslang), built into that program | | BSD-3-Clause and others | The Khronos Group and others | Text: `glslang.txt` |
| [libwebp](https://github.com/webmproject/libwebp), built into that program | | BSD-3-Clause | Google Inc. | Text: `libwebp-BSD-3-Clause.txt` |
| [MoltenVK](https://github.com/KhronosGroup/MoltenVK), built into the macOS program | | Apache-2.0 | The Brenwill Workshop Ltd. | Text: `MoltenVK-Apache-2.0.txt` |
| `vcomp140.dll`, the Microsoft Visual C++ OpenMP runtime (Windows only), from the same archive | | Microsoft Visual Studio licence, Distributable Code | Microsoft Corporation | Text: `vcomp140-Microsoft.txt` |
| RealESRGAN_x4plus (ncnn files, from the same archive) | | BSD-3-Clause | 2021 Xintao Wang | The model for the pictures. Text: `Real-ESRGAN-BSD-3-Clause.txt` |
| realesr-general-x4v3 at denoise 0.3: its two ncnn weight files from [upscayl/custom-models](https://github.com/upscayl/custom-models) (commit 500e6a0) blended 0.3 / 0.7 at build time, as Real-ESRGAN's own `-dn` does (`tools/release/upscaler/blend_ncnn.py`) | | BSD-3-Clause (Real-ESRGAN's weights) | 2021 Xintao Wang | The quicker model (newspapers, and every picture at the fast quality; the only one in the Android app). Text: `Real-ESRGAN-BSD-3-Clause.txt` |

## Licence texts in the packages

`takedawn-LICENSE.txt`, `xboxrecomp-LICENSE.txt` (MIT), `xboxrecomp-NOTICE.txt`
(the toolkit's attribution file), `xboxrecomp-LGPL-2.1.txt`, `DearImGui-MIT.txt`,
`SMAA-MIT.txt`, `ADPCM-XQ-BSD-3-Clause.txt`, `dr_libs-LICENSE.txt`, `stb_vorbis-LICENSE.txt`, `stb-LICENSE.txt`, `DejaVu-fonts-LICENSE.txt`, `RobotoCondensed-OFL.txt`, `SDL2-Zlib.txt`, `SDL3-Zlib.txt`, `sdl2-compat-Zlib.txt`,
`realesrgan-ncnn-vulkan-MIT.txt`, `ncnn-LICENSE.txt`, `libwebp-BSD-3-Clause.txt`, `MoltenVK-Apache-2.0.txt`, `vcomp140-Microsoft.txt`, `Real-ESRGAN-BSD-3-Clause.txt` (the menu pictures' upscaler),
`HIDAPI-BSD-3-Clause.txt`, `glslang.txt`, `Vulkan-Headers.txt`,
`LLVM-Apache-2.0-with-exceptions.txt`, `MinGW-w64-runtime.txt`,
`winpthreads.txt`, and with the setup window `Qt-NOTICE.txt`,
`Qt-LGPL-3.0.txt`, `Qt-GPL-3.0.txt`, `Qt-THIRD-PARTY.txt`. `SOURCE.txt` names the commits of this
build.

The Android app has one more set, for the parts only it has (and carries
`ncnn-LICENSE.txt` and `Real-ESRGAN-BSD-3-Clause.txt` for its upscale also
in a development build):
`libadrenotools-BSD-2-Clause.txt`, `linkernsbypass-BSD-2-Clause.txt`,
`Mesa-license.txt` (Mesa's licence page for the Turnip driver) with
`Mesa-MIT.txt`, `Mesa-Apache-2.0.txt`, `Mesa-BSL-1.0.txt` and
`Mesa-SGI-B-2.0.txt` (the texts Mesa lists for its files),
`Mesa-Turnip-SOURCE.txt` (the Mesa release the driver was built from, where to
get it, and the change made to it) and that change itself,
`Mesa-Turnip-*-patch.txt`. In the app they are under Licences (the setup page,
and "Takedawn: graphics driver" in the launcher).
