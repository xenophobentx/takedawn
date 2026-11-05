# Architecture

> Note, 2026-10-11: this page was written for the upstream project's Windows
> build with a software renderer and has not been rewritten yet. The split
> between the two repositories and the data flow still hold. Since then a GPU
> backend (Metal on macOS, Vulkan on Linux, Windows and Android) draws the
> frame and translates the NV2A's vertex programs and register combiners into
> shaders. The software rasteriser remains as a reference. The hosts are POSIX
> as well as Win32, input comes from SDL, and `tools/install/regen.py` also
> drives the pipeline. [windows.md](windows.md), [android.md](android.md) and
> [shader-prebuild.md](shader-prebuild.md) describe the backends.

The code is split over two repositories and runs as one process. This repo holds what is specific to
Burnout 3. [xboxrecomp](https://github.com/sp00nznet/xboxrecomp) holds the recompiler and
the Xbox runtime that every title on it shares.

```
 your disc image
      │  tools.xiso unpack
      ▼
 Burnout 3 Takedown\default.xbe  (+ Data, Tracks, ovid ...)
      │  regen.sh: xbe_parser → disasm → func_id → abi_analysis → recomp
      ▼
 src/game/recomp/gen/*.c          ~29 files of lifted C, gitignored
      │  cmake
      ▼
 bin/burnout3.exe = main.c + gen/ + xboxrecomp runtime libraries
```

## What lives where

| Part | Where | Owns |
|---|---|---|
| Pipeline driver | `regen.sh` | Running the toolkit stages in the one order that works, with this title's paths and seeds. |
| Seeds | `config/seed_functions.json` | Function entries the disassembler cannot find statically: thread start routines and indirect-call targets a run reached. Each has a note saying how it was found. |
| Host entry | `src/game/main.c` | Loads the XBE, maps Xbox memory at the original addresses, brings up the APU and USB, registers the two D3D addresses the runtime needs, and jumps to 0x001D2807. It also handles `--headless`/`--record` and the crash/hang diagnostics. It draws nothing and decides no game state. |
| Overrides | `src/game/recomp/recomp_manual.c` | A hook for replacing one guest function by hand. Burnout 3 currently overrides none. |
| Recompiler | xboxrecomp `tools/` | x86 to C. It emulates x87 precision control (the title runs at 24-bit), SSE compare predicates, and jump tables as C switches. |
| Kernel | xboxrecomp `src/kernel` | The 147 kernel imports on Win32: memory, files (mapping `D:` to the unpacked disc and the HDD partitions to `saves\`), threads, events, DPCs. |
| GPU | xboxrecomp `src/kernel/nv2a_*` | A software NV2A. It executes the pushbuffer the title's own D3D8LTCG writes: vertex programs, register combiners, four texture stages, DXT, fog, clipping, and visibility reports. Rasterisation runs on worker threads. |
| Audio | xboxrecomp `src/apu` | The MCPX APU: voice processor and mixer. There is no DSP56300; the DirectSound doorbell is acknowledged instead. |
| Input | xboxrecomp `src/usb` | An OHCI host controller with up to four emulated Xbox pads, fed from XInput or a pad script. The title's own XPP driver enumerates them. |
| Video | xboxrecomp `src/video` | The window, and XMV movie decoding for the intros, attract loop and tutorials. |

## Data flow in a frame

1. The title's game thread (recompiled code) updates the world and builds its
   RenderWare draw lists.
2. RenderWare calls the title's own D3D8LTCG, which writes NV2A methods into a
   pushbuffer in guest memory and bumps the put pointer.
3. The runtime's executor consumes the pushbuffer. It runs each draw's vertex
   program, clips and rasterises into the guest framebuffer, then advances the
   GPU fence at `[0x0035FB48]+0x30` that D3D8LTCG waits on.
4. On a flip, the frame goes to the window (or to a BMP with `RECOMP_FB_DUMP`),
   and the vblank counter at `[device]+0x1DE8` ticks. The XMV player paces
   movies on that counter.
5. USB and APU interrupts come from controller threads as DPCs. The kernel's
   DPC queue is locked, because both threads insert into it.

## Why the split

The first version of this project was a fork with its own kernel, D3D and
audio, plus hand-written menus, a pseudo-3D road and fake physics drawn over a
game that never got going. What that fork learned about the Xbox is now in
xboxrecomp, shared with HL2, Wreckless and the other titles. Only what is true
of Burnout 3 alone stays here: addresses, seeds and boot defaults. See [boot.md](boot.md) for each of those and why it is needed.
