# Potato mode (`game.race_clock`, BO-159)

On a machine that cannot hold 60 fps, the shipped game plays the start of a
race in slow motion: every frame runs one 1/60 s step of the simulation,
however long the frame took. At the end of the first crash camera the title
switches itself to a mode that runs as many steps as the time since the last
frame holds. The rest of the race keeps real time. The switch
`game.race_clock` puts the race in that mode from its start. Overlay
Game page, "Potato mode"; `settings.toml` `[game] race_clock`;
`RECOMP_GAME_RACE_CLOCK=1`. Off by default on every platform: the race runs
as shipped.

It is an opt-in guest patch (`config/patches.json`; toolkit
`docs/pipeline/04-lifting.md`, Guest patches) of the kind `store_u32`: the
one store that sets the stepper's mode at a one-player stage's start is
stored again with the value the title's own code stores later. No game code
is replaced and no new timing logic is added. The value is sampled when the
stage starts, so a change shows from the next race.

## The stepper

`sub_001B58E0` (timer object `0x4D5338`) returns, once a frame, how many
fixed 1/60 s steps the next frame runs. It keeps a lag accumulator in real
time (the host's counter behind `rdtsc`), turns the lag into whole steps,
caps them at the frame's maximum (4 at full rate, 8 at the crash cameras'
half rate) and drops the rest of the lag at the cap. The physics, the AI, the
world timers and the race clock advance by whole steps only. Its mode is
`0x4D5390` (game object `0x4A71A0` + 0x2E1F0, copied into the timer every
frame):

| mode | steps a frame |
|---|---|
| 0 | the minimum (1, or 2 at half rate), whatever the time |
| 1 | by the clock: the minimum plus the lag in whole steps, at most the cap |
| 2 | menus, pause and results: as 1, without dropping the lag at the cap |

The accumulator and the cap run the same way in modes 0 and 1. The only
difference is one line at the end (`loc_001B5A04`: in mode 0 the result is
replaced by the minimum). At a steady 60 the lag never reaches a step, and
both modes run one step a frame.

## The title's own condition

The stores of the mode, as shipped:

| site | where | value |
|---|---|---|
| `0x14BCF` | front end set-up | 1 |
| `0x1A11B` | start of a one-player stage (Race, Road Rage, Crash), at the load | **0** |
| `0x1A18B` | the stage's update: a crash camera starts the half rate | 0 |
| `0x1A19E` | the stage's update: the half rate ends (the end of a crash camera) | 1 |
| `0x1A872`, `0x1AED1` | stage end | 1 |
| `0x20AB1` | two-player stage set-up | 0 |
| `0x27D9D`, `0x2849A`, `0x28963`, `0x2993A` | pages of the pause and results flow | 2 |
| `0x28200`, `0x29AEE` | exits of two of those pages | 1 |

So a shipped race runs in mode 1 from the end of its first crash camera to
its finish, apart from the crash cameras. Mode 0 covers the stretch from the
load to the first crash camera, the crash cameras themselves, and split
screen. On a console locked to 60 the two modes run the same steps, so the
difference never showed there.

Pausing a race with Start and resuming with Start did not reach the
pause-flow stores: the mode stayed 0 or 1, whichever it was, in both test
runs. Which menu path reaches them is not known yet.

The patch is at `0x1A11B` only: there the stage start stores 1 instead of
0. Every other store stays the title's, so the crash cameras keep mode 0 and
their half rate, split screen keeps mode 0, and the pause and results pages
keep their own stores. From the GO, a race with the switch on runs the way
a shipped race runs after its first crash camera.

## Evidence

All runs on the Mac (M-series), headless, scratch profile, one at a time.
`BO3_PACE_LOG` per flip (stepper mode, steps, the global step counter),
`tools/headless/race_drive.sh` (the title's AI drives Silver Lake Southbound,
3 laps). A slower machine was simulated with the null backend and a fixed
cost per draw (`RECOMP_PB_BACKEND=null RECOMP_PB_NULL_DRAW_US=20` gives about
42 fps in a race, `=28` about 31 fps).

`regen.sh` output: the new toolkit with the old `patches.json` is byte for
byte the old output; with the new entry the only difference is one added
line after the title's store in `sub_0001A000`:

```c
MEM32(0x4D5390) = 0;
MEM32(0x4D5390) = RECOMP_PATCH_U32("game.race_clock", "1", 0x1u, MEM32(0x4D5390)); /* patch 0x0001A11B */
```

With the switch off that line stores back the value just stored.

### At 60 fps (setting off and on, three races each)

| run | mode at the stage start | steps from stage start to GO (flips) | first stage frames (steps) | race time / wall time GO to finish | best lap |
|---|---|---|---|---|---|
| off 1 | 0 | 270 (270) | 1 1 1 1 | 395.18 / 397.68 s | 119.43 s |
| off 2 | 0 | 270 (270) | 1 1 1 1 | 382.10 / 386.81 s | 121.85 s |
| off 3 | 0 | 270 (270) | 1 1 1 1 | 397.72 / 404.01 s | 124.78 s |
| on 1 | 1 | 270 (268) | 1 1 3 1 | 392.35 / 395.56 s | 122.82 s |
| on 2 | 1 | 270 (267) | 1 1 4 1 | 392.38 / 395.33 s | 128.97 s |
| on 3 | 1 | 270 (260) | 1 1 4 1 | 407.37 / 408.27 s | 130.45 s |

- From the stage start to the GO is 270 steps in every run. With the switch
  on, the lag of the load is caught up in one frame of 3 or 4 steps right
  after the stage appears, with the cars on the grid before the GO.
- The other frames of more than one step before the first crash (2 to 6 a
  race with the switch on) each follow a host frame longer than 16.7 ms: the
  Mac was busy with other work. With the switch off those frames are lost
  time instead (59.87 to 59.98 steps a second before the first crash, against
  60.02 to 60.22 with it, the entry frame included).
- The races themselves differ from run to run with the switch off as well
  (the AI's race is not repeatable on this build: race times 382 to 398 s
  off, 392 to 407 s on). The spreads overlap.
- Crash cameras: mode 0, half rate in force, 2 steps a flip, in every run.

### Below 60 fps (one race each)

| run | fps in the race | steps a second, stage start to first crash | race time / wall time GO to finish | best lap |
|---|---|---|---|---|
| ~42 fps, off | 38 before the first crash | 38.2 | 392.35 / 439.69 s (wall +12.1 %) | 116.95 s |
| ~42 fps, on | 42 | 60.0 | 396.30 / 397.93 s (wall +0.4 %) | 119.42 s |
| ~31 fps, off | 30 before the first crash | 29.7 | 395.18 / 448.57 s (wall +13.5 %) | 119.43 s |
| ~31 fps, on | 31 | 60.0 | 378.75 / 380.02 s (wall +0.3 %) | 123.25 s |

- With the switch on the race keeps real time from the stage start: race
  time and wall time agree within 0.4 %. Off, the stretch to the first crash
  runs at the frame rate (38 or 30 game steps a second) and the race takes
  12 to 14 % longer on the wall.
- The best laps with the switch on (119.42 s at ~42 fps, 123.25 s at
  ~31 fps) fall inside the 60 fps spread above: the same steps run, in
  bigger batches. The race clock counts steps, so it reads right in both
  modes; with the switch off the player gets slow motion while it counts.
- Steps a flip with the switch on: mostly 2 at ~31 fps, 1 and 2 at ~42 fps,
  never more than 4.
- Crash cameras: unchanged, mode 0 and half rate, 2 steps a flip.

### The other paths

| check | result |
|---|---|
| Crash mode, 60 fps (`crash60_scene.sh crash`) | mode 1 from the stage start with the switch on; the crash camera mode 0 and half rate both ways, 919 steps / 460 flips both ways |
| Crash mode, ~31 fps | the run-up 30.2 steps/s off, 59.9 on; the camera 919 / 918 steps, 460 / 459 flips, mode 0 and half rate both ways |
| Split screen race (`split_scene.sh race`) | mode 0 and half rate throughout, the same both ways: the two-player stage has its own store |
| `graphics.crash_60fps` alone | the whole race in mode 0 (the half rate never starts, so the store of 1 at its end never runs) |
| `graphics.crash_60fps` with `game.race_clock` | the whole race in mode 1, the crash cameras included, 1 step a flip at 60 |
| Pause and resume (Start, Start) after the GO | the world timer stops and starts again; the stepper's mode does not change (0 off, 1 on), so the switch survives a pause |

Crash mode's score tour differed in length between runs (631 and 800 steps
at 60 fps, 630 and 844 at ~31 fps); `docs/crash60.md` found the same spread
across runs with no switch at all.

## What is left

- A frame of up to 4 steps right after the stage appears when the load
  left lag behind (3 or 4 steps in every run here), with the cars stationary
  on the grid before the countdown. The shipped game runs the same kind of
  catch-up frame after any long frame once it is in mode 1.
- Below 15 fps the game still slows down (4 steps a frame at most), as the
  shipped game does after its first crash.
- The HUD's animations follow frames, not steps, in both modes.
- If flips came faster than 60 (vblank pacing switched off by hand), the
  race would run fast from its start instead of from its first crash. The
  default pacing holds every flip to the 60 Hz vblank.
- Not yet measured on the Steam Deck, the Poco F4 or a Windows machine below
  60 fps. Before the switch could default to on: the Deck at a TDP that holds
  60 and one that gives about 40 fps, the Poco F4 (18 to 20 fps in BO-148:
  3 to 4 steps a flip expected), a Windows host at 60, and an Android soak
  with the switch on.

## Re-test on the current heads (potato mode, off by default)

The setting is off by default and its help text says to leave it off on a
fast PC. Re-run after the rebase onto the current heads, Mac, headless,
scratch HOME, one instance at a time, the same autopilot race (3 laps).
The Mac was busy with other work during these runs (load average about 15),
so times are noisy; compare ranges.

| arm | race time, s | best lap, s |
|---|---|---|
| base (build without the patch), 4 races | 396.3, 390.6, 415.3, 387.5 | 119.4, 116.9, 123.0, 127.5 |
| patch built in, setting off, 4 races | 393.5, 387.5, 394.9, 403.5 | 123.4, 127.5, 127.8, 124.3 |
| setting on at 60 fps, 3 races | 396.7, 405.9, 381.8 | 125.4, 127.1, 118.4 |

- Off against base: the generated code differs by one store that writes
  back the value just stored, and the golden menu and race scenes are inside
  the off-against-off spread. The off best laps sit about 4 s above the base
  mean, but the ranges overlap (the AI's race is not repeatable) and the load
  was high; a quiet-machine A/B would settle it.
- On at 60 fps: best laps 118.4 to 127.1 s, race times 382 to 406 s, inside
  the spread of the other two arms. Earlier run: 122.8 to 130.5 s on against
  119.4 to 124.8 s off.
- A slow host (null backend, a fixed cost per draw), the stretch from the
  stage start to the first crash:

| run | fps | steps a second | whole race, game time against wall |
|---|---|---|---|
| off, 28 us/draw | 26.9 | 26.9 | -9.5 % |
| on, 28 us/draw (host load made it ~22 fps) | 22.6 | 56.7 | -5.1 % (crash cameras at 16 fps run below real time) |
| off, 12 us/draw | 32.5 | 32.5 | -6.1 % |
| on, 12 us/draw | 50.1 | 59.9 | -0.1 % |
| on, 20 us/draw | 37.8 | 59.1 | -1.2 % |

  Lap times with it on: 127.3 s (12 us), 123.3 s (20 us), 127.5 s (28 us),
  all inside the 60 fps spread.
