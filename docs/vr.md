# VR (OpenXR headset)

`burnout3.exe --vr` plays the race from the driver's seat in a headset: stereo, with the head turning the view. The menus,
loading screens and the crash and replay views are shown on a flat screen in front of the player (the "cinema" view).

## What was checked

| | |
|---|---|
| Headset | Meta Quest 3 over USB or Wi-Fi, through SteamVR (the active OpenXR runtime) |
| PC | Windows 11, GeForce RTX 4090 |
| Disc | NTSC-U |
| Path | OpenXR with a Direct3D 11 swapchain (the default) |
| Modes | races and menus, with a wheel, a pad and the keyboard |

Not checked: any other headset or runtime, an AMD or Intel GPU, a PAL disc (the seat camera stays off there, see
"Addresses"), macOS and Linux (the headset path is Windows only), the Vulkan swapchain (below).

## Running it

Start SteamVR and put the headset on, then run `burnout3.exe --vr` from the game folder. The game waits up to 300 seconds
for the runtime and the headset (`RECOMP_VR_WAIT=<seconds>`). HDR must be off (`RECOMP_GFX_HDR=0` or the Graphics page):
the headset path copies the 8-bit picture raw.

Building needs the OpenXR headers: `tools/win/fetch-deps.sh` fetches them and `tools/win/build-mingw.sh` passes them on
(`XBOXRECOMP_OPENXR_INCLUDE_DIR`). Without them the build works and `--vr` says it has no OpenXR support. The loader is
opened at run time; nothing from the runtime is shipped.

## Settings (the VR page, or the RECOMP_VR_* variables)

| Setting | |
|---|---|
| Graphics API | Direct3D 11 (default) or Vulkan (experimental, below). Read at the start: a restart. |
| Render scale, anti-aliasing, sharpening, texture LOD bias | Used in the headset instead of the Graphics page's. The defaults are 8x, SMAA, no sharpening, no bias: each eye is drawn from half of the frame, so the scale is what the picture is made of. |
| Eye separation, near plane, headset field of view | The picture's geometry. |
| Picture width / height (car), menu screen shape, flat screen width / height | Adjusts the stretch of the car view and of the flat screen. Shown at once. |
| Hide the driver, boost effect, bloom and motion blur | Bloom and motion blur are off in the headset by default (they read the whole picture back and mix the eyes). |

## Controls

- Head: turns and leans the view. Tab, C or the pad's right stick pressed in recentre it; held for about 1.5 s it stores the
  head's position as the car's seat.
- The seat per car: numpad 4 and 6 (left, right), 8 and 2 (ahead, back), 9 and 3 (up, down), with NumLock on; 5 resets and 0
  saves. A nudge saves itself two seconds after the last one, in `BO3_SEATCAM_FILE` (default `seatcam.txt`).
- V cycles through the other camera views the seat camera offers; the right stick, or J, L, I and K, look around.

## How it works

The renderer draws each eye into its half of the frame (side by side, each squeezed to half the width). Each eye's shift
is a push constant added to the clip x of every vertex program (`D3D8_GLSL_EYE_PC`); the HUD is drawn smaller in each
eye. The toolkit's `oxr.c` copies the frame into a shared Direct3D 11 texture and from it into one swapchain per eye (a
single shared swapchain's sub-image rectangles were not honoured by SteamVR). The head pose goes through a ring keyed by
an id carried in the stereo value, so a frame is shown with the pose it was drawn from. The game side is `seatcam.c`: it
replaces the camera the title builds, from the seat point and the head pose, and claims the field of view the frame was
really drawn with.

## The Vulkan swapchain

`Graphics API: Vulkan` (or `RECOMP_VR_API=vulkan`) asks the runtime for a Vulkan swapchain. On the machine above it crashed
in `xrCreateSwapchain` in every attempt; a crash dump of 2026-11-09 puts the fault inside
`XR_APILAYER_NOVENDOR_motion_compensation.dll` (the OpenXR-MotionCompensation layer, an implicit API layer; its file has
no Vulkan code). With that layer present Direct3D 11 works and Vulkan does not. For the Vulkan path the game switches the layer off
for the run (`DISABLE_XR_APILAYER_NOVENDOR_motion_compensation=1`); whether the Vulkan path then displays correctly has not
been checked. Direct3D 11 is the default for this reason.

## Addresses

`seatcam.c` reads the game's memory at fixed guest addresses, all written in `src/game/seatcam_addr.h` once for each disc,
like the rest of the port (`BO3_VA`). Four already had their PAL values elsewhere in the port. One has none:

| | NTSC-U | |
|---|---|---|
| `SC_RFLAGS` | `0x004D61C4` | the render flags: bytes 12 (bloom) and 14 (motion blur) |

Until it has its PAL values a PAL build leaves the seat camera off and says so (`SC_SUPPORTED`).

To find a PAL address, align the two executables (needs both `default.xbe` files and capstone, see [pal.md](pal.md)):

```sh
python3 -m tools.region.xbemap "Burnout 3 Takedown/default.xbe" "Burnout 3 Takedown (PAL)/default.xbe" 0x4D61C4
python3 -m tools.region.xbemap "Burnout 3 Takedown/default.xbe" "Burnout 3 Takedown (PAL-ENSV)/default.xbe" 0x4D61C4
```

It prints the PAL address and how it was found (`how` says what it rests on; an address that is not plainly matched is
flagged, and then it must be checked by hand). Write the two values into `seatcam_addr.h` and drop the `SC_SUPPORTED`
guard, then run `python3 -m tools.region.check`. The same applies to a new address. This one is the render layer flags of [draw-distance.md](draw-distance.md) (HUD, bloom and
motion blur are on and off bytes there).

The driver-box's two car-model keys (`0x00090000`, `0x0008A000`) are the NTSC-U disc's; on another disc a model has
another key and the box falls back to the seat point.
