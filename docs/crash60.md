# 60 fps crash cameras (`graphics.crash_60fps`, BO-111)

The title runs at 60 frames a second and halves that to 30 while a crash or
takedown camera is on: every platform measured 33.3 ms flips in those phases
(`docs/windows.md`, `docs/android.md`). One setting keeps them at 60: overlay
Game page, "60 fps crash cameras"; `settings.toml` `[graphics]
crash_60fps`; `RECOMP_GFX_CRASH_60FPS=1`. It defaults to off (the console's
behaviour) on every platform. On is an opt-in guest patch
(`config/patches.json`; toolkit `docs/pipeline/04-lifting.md`, Guest patches):
where a stage's update chooses the rate, its "a crash camera is on" byte
reads 0, and the title takes the path it already has for the full rate. No
game code is replaced and nothing is written to the title's memory. The
choice is made every frame: a change shows at once, in the middle of a
camera too.

## How the title halves its rate

Names in brackets are the PS2 build's (Nahelam's symbol list,
`SLES_525.85.txt`).

- The game object is at `0x4A71A0`. Two bytes in it are the half rate:
  `+0x2E1ED` (`0x4D538D`) asked for, `+0x2E1EE` (`0x4D538E`) in force.
- `sub_00016EF0` (`CB3Game::UpdatePreSimulation`), once a frame: when the two
  differ it copies the asked byte to the one in force and sets
  `D3DRS_PRESENTATIONINTERVAL` (render state 0x7F, through the deferred
  state cache `sub_00015750`) to `0x80000002` or `0x80000001`: a flip every
  second or every vertical blank. The swap (`sub_00350DB0`) hands that to the
  GPU with each flip.
- `sub_000165F0` (`CB3Game::Update`), at `0x16C17`: the same byte chooses the
  arguments of the simulation stepper `sub_001B58E0`: minimum 2 and maximum 8
  steps a frame with it, 1 and 4 without. A step is 1/60 s of game time. The
  stepper's mode is `+0x2E1F0` (`0x4D5390`, used while `+0x2E1F4` is 3):
  0 takes the minimum every frame (the frame rate is the game's clock), 1
  counts steps by the real clock, 2 is the pause and results menus'.
- `sub_000CBA80`, a HUD element's update: with the byte in force it doubles
  the time it is given. The HUD is updated once a frame with one step's time
  (`sub_000535E0` with `MEMF(0x4AE1FC)`), so at 30 frames a second its
  animations run at half pace; this one element was compensated.

Those three are the only readers of the byte in force. Interval and step
count are not set in two places that could disagree: both follow the one
byte.

Who asks:

| Site | When | Asks for |
|---|---|---|
| `sub_0001A130` (`0x1A17A`), the update of a one-player stage (vtable `0x3A9ACC`: Race, Road Rage, Crash) | the first player's crash camera byte `MEM8(0x73BACA)` set, half rate not in force | half rate, mode 0 |
| the same | the byte clear, half rate in force | full rate, mode 1 |
| `sub_0001AC30` (`0x1ACB5`), the update of the other one-player stage class (vtable `0x3A9E5C`; not with its mode objects `0x4D4FFC`, `0x4D5070`) | the same byte, the same two cases | half / full rate |
| `sub_000209A0` (`0x20AAA`), the two-player stage's set-up | always | half rate, mode 0: split screen runs at 30 throughout |
| `sub_00014860` (front-end set-up), `sub_0001A830` (stage release), `0x167DB` | always | full rate |

`0x73BACA` is a byte of the player record (the second player's is `0x27E0`
further on). It is set from the impact to the end of what the title shows of
a crash: in a race the crash camera and the takedown camera, in Crash mode
the crash camera (Impact Time, the Crashbreaker), the tour of the wrecks that
adds up the score, and the replay behind Crash Results. The renderer, the
HUD and the cameras read it too; the patch touches none of those reads.

The pause menu and Crash Results set the interval to 1 themselves
(`sub_00027D50`, `sub_000281C0`, `sub_00028450`: mode 2) without touching
the two bytes. The front end is set up at the full rate and stays there. On
the Xbox build nothing else asks for the half rate: films, loading screens
and menus are not in the 30 Hz mode (the PS2 build has a separate
`CB3Game::SetFrontendFrameRate`, which is why its "60 FPS Menus" patch
exists; there is no counterpart to patch here).

## The patch

| Site | Kind | The title's instruction | Patched to |
|---|---|---|---|
| `0x0001A17A` | `cmp_u8` | `cmp byte ptr [0x73BACA], bl` (bl = 0) | the compare reads 0 |
| `0x0001ACB5` | `load_u8` | `mov al, [0x73BACA]` | reads 0 |

With the byte read as 0 the update takes its "no crash camera" branch: the
half rate is never asked for, and if it is in force (the switch was turned
on inside a camera) the full rate is asked for in that frame. Everything
downstream is then the title's own full-rate state: interval 1, one to four
steps, the HUD element uncompensated. The compared byte needed a patch kind,
`cmp_u8` (toolkit `tools/recomp/patches.py`): the lifter keeps a compare's
operands in a snapshot every later branch reads, and the patch rewrites the
left one. With the setting off `regen.sh` output differs from the unpatched
build by the two gated statements only.

The two-player stage is not patched: its 30 is not a camera's but the whole
mode's, chosen once at set-up (two views to draw).

The PS2 "60 FPS Crashes" patch (Nahelam; SLUS-21050 `0x1320D8`, a branch in
`CB3Game::UpdatePreSimulation`) skips the block that puts the asked rate in
force. The same could be done here at `0x16F5F`, but the title would then
keep asking: the asked byte stays 1 and the mode 0 after the camera, and
switching the setting off mid-race would drop the race to 30 until the next
crash ended. Stopping the question leaves no such state.

One difference from the title's own state after a camera: leaving the half
rate, the title sets the stepper's mode to 1 (steps by the clock) for the
rest of the stage; a stage starts in mode 0 (one step a frame). With the
switch on the half rate is never entered, so the mode stays 0, as it is from
the start of every race until its first crash. At a steady 60 the two are
the same (one step a frame). On a host that cannot hold 60 they are not:
mode 0 slows the game down with the frame rate, mode 1 takes two steps in a
late frame. See Risks.

## Does the game keep its speed?

It has to: the same slow motion, the same camera lengths, the same scores,
only twice the pictures. The title's own counters say so, and they do not
depend on how busy the host is: `BO3_PACE_LOG=<file>` (`src/game/main.c`)
writes the pacing state at every flip, `tools/headless/crash60_scene.sh`
drives the scenes and `tools/headless/pace_report.py` reads the logs.

Why it can: a simulation step is 1/60 s whatever the rate. At the half rate
the title takes two steps and draws one picture every second vertical blank;
with the switch it takes one step and draws one picture every blank. The
world timer (`0x60EA00`) counts steps, divided by its slow-motion divisor
(`+0x18`; 5 in a race's crash camera); everything timed in the world hangs
on it. What runs once a frame is given the frame's simulated time, steps x
the step (`+0x2E210`, `sub_000170B0`): the cameras (`sub_00167940`) and the
audio manager (`sub_001CA530`, at `0x170ED`).

### Crash mode, one event, the same input every run

Single Event > Crash > 1. Trailer Trash, the accelerator held from the
countdown, nothing else (Metal, headless, 4:3, render scale 1). The crash is
not the same from run to run even so, with or without the switch: the title
falls into one of a handful of outcomes. Per run: the camera's world phase
in world timer ticks, the steps the world stood still at an impact, the
steps of the tour that adds up the score, the camera's whole length in
steps, flips and seconds, and the score on Crash Results.

| run | switch | score | world ticks | impact stills (steps) | tour (steps) | camera (steps) | flips | seconds | steps/s | flip interval p50 / p95 / p99 / max (ms) |
|---|---|---|---|---|---|---|---|---|---|---|
| off1 | off | not captured | 917 | 0 | 615 | 1548 | 782 | 26.61 | 58.2 | 33.2 / 40.9 / 70.6 / 116 |
| off2 | off | $92,460 | 913 | 54 | 1121 | 2108 | 1062 | 37.73 | 55.9 | 33.1 / 48.5 / 110.8 / 190 |
| off3 | off | $92,460 | 913 | 54 | 1121 | 2108 | 1062 | 35.64 | 59.1 | 32.9 / 39.1 / 56.0 / 100 |
| off4 | off | $103,219 | 917 | 0 | 615 | 1548 | 782 | 26.08 | 59.4 | 33.2 / 37.0 / 43.2 / 91 |
| off5 | off | $98,776 | 913 | 54 | 801 | 1788 | 902 | 30.04 | 59.5 | 33.2 / 36.9 / 41.3 / 49 |
| it_off | off | $96,758 | 917 | 0 | 1224 | 2157 | 1086 | 36.18 | 59.6 | 33.2 / 37.7 / 45.2 / 54 |
| c0_off | off | $92,460 | 913 | 54 | 1121 | 2108 | 1062 | 35.38 | 59.6 | 33.1 / 35.7 / 38.9 / 49 |
| on1 | on | $96,758 | 916 | 0 | 1260 | 2191 | 2191 | 40.65 | 53.9 | 16.9 / 28.1 / 54.7 / 152 |
| on2 | on | $92,460 | 914 | 54 | 1123 | 2108 | 2108 | 36.59 | 57.6 | 16.8 / 22.2 / 29.1 / 49 |
| on3 | on | $91,348 | 913 | 90 | 660 | 1681 | 1681 | 28.03 | 60.0 | 16.2 / 20.4 / 21.1 / 32 |
| on4 | on | $96,758 | 916 | 0 | 1259 | 2190 | 2190 | 36.54 | 59.9 | 16.6 / 20.0 / 21.9 / 53 |
| it_on | on | $83,045 | 914 | 54 | 931 | 1916 | 1916 | 32.36 | 59.2 | 16.7 / 20.0 / 23.5 / 140 |
| c0_on | on | $92,788 | 914 | 54 | 932 | 1917 | 1917 | 31.98 | 59.9 | 16.4 / 19.2 / 20.1 / 30 |
| q_off | off | not captured | 917 | 0 | 614 | 1547 | 781 | 26.01 | 59.5 | 33.3 / 35.3 / 35.7 / 36 |
| q_on | on | not captured | 916 | 0 | 624 | 1555 | 1555 | 25.91 | 60.0 | 16.6 / 18.4 / 18.7 / 19 |

The seconds and intervals of all but the last two are from a Mac that ran up
to five game instances of other work at the time (load average 10 to 50),
the game at nice 5: the steps are exact, the seconds are noisy. on1 and off2
also dumped 48 frames inside the camera. q_off and q_on are the build after
the merge of the integration branches, on the quiet machine (no other game,
load average 3), without frame dumps.

- The quiet pair fell into the same outcome (no impact stills, the short
  tour; off4's, $103,219): the camera took 26.01 s without the switch and
  25.91 s with it, 1547 and 1555 steps. No flip was later than 19 ms with
  the switch (36 ms without, where 33.3 is the rate).

- The camera's world phase is 913 to 917 ticks (15.2 s) in every run, with
  the switch or without.
- With the switch every flip is one step (2191 flips, 2191 steps); without,
  two (1062 flips, 2108 steps but for the one frame of the change).
- The same outcome was reached both ways: $92,460 with 54 steps of impact
  stills and a tour of 1121 steps three times without the switch, and once
  with it (tour 1123). That crash took 2108 steps in all four; in seconds
  35.38, 35.64 and 37.73 without, 36.59 with (57.6 steps a second on the
  loaded machine; 35.13 s is 2108 steps at 60).
- Scores: $92,460 to $103,219 without (six runs), $83,045 to $96,758 with
  (six). $92,460 and $96,758 came up on both sides. $83,045 did not come up
  in the six plain runs without the switch, but it did without the switch
  with the boost button held in the camera (below), so it is one of the
  title's own outcomes; twelve runs are too few to say the distribution is
  the same, they show no outcome the title does not have.
- The tour of the wrecks was 2 to 35 steps longer with the switch where the
  score was the same ($92,460: 1121 / 1123; $96,758: 1224 / 1259, 1260):
  at most 3 % (0.6 s of 20). Its stops end on a frame, and there are twice
  as many frames; nothing else was found to explain it and it was not
  pursued.
- The replay behind Crash Results is a crash camera too (the byte is set,
  the world timer starts again at the impact's tick): 30 without, 60 with,
  and it shows the crash that happened.

Two more pairs with the boost button (Impact Time) and the stick held for
six seconds from about one second into the camera: $83,045 without the
switch (913 ticks, 54, tour 907), $103,219 with (916, 0, 623): both outcomes
of the table above. The world timer's divisor stays 1 in Crash mode's camera
either way.

One run changed the switch inside the camera (`SWITCH_AT=120`,
`BO3_FX_AT`): on 120 flips in, off 360 flips later. The rate followed three
flips after each change (303 flips at 30, 359 at 60, the rest at 30), every
part at 60.0 steps a second, and the crash was the $92,460 one again by its
counters: 913 ticks, 54, tour 1120, 2107 steps.

### From the impact to Crash Results, phase by phase

`pace_report.py --crash` splits a Crash event by the title's own state: the
camera while the world timer runs, the stills at impacts, the tour (world
timer stopped), the hand-over to Crash Results (the game's state 4, the
"LOADING" screen), Crash Results. On the integration build of 2026-10-12
(`game ba94b6e7e3`, `toolkit 2ad61ccfde`), quiet machine, with two earlier
runs of the other outcome beside them:

| run | switch | outcome | camera, world running: s / ticks / steps | stills: s / steps | tour: s / steps | hand-over: s / flips | impact to tour, s | impact to camera's end, s | impact to Crash Results: s / steps |
|---|---|---|---|---|---|---|---|---|---|
| f_off | off | short tour | 15.32 / 917 / 919 | 0 / 0 | 10.27 / 615 | 4.57 / 137 | 15.32 | 25.58 | 30.15 / 1672 |
| q_on | on | short tour | 15.28 / 916 / 917 | 0 / 0 | 10.40 / 624 | 2.28 / 137 | 15.28 | 25.68 | 27.96 / 1678 |
| off2 (loaded host) | off | $92,460 | 17.02 / 913 / 919 | 1.53 / 54 | 18.71 / 1121 | 4.56 / 137 | 18.55 | 37.26 | 41.83 / 2232 |
| f_on | on | $92,460 | 15.28 / 914 / 917 | 0.90 / 54 | 18.72 / 1123 | 2.28 / 137 | 16.18 | 34.90 | 37.18 / 2231 |

- Nothing runs at double speed: the camera's world phase is 15.3 s and 913
  to 917 ticks both ways, the tour as long in seconds as in steps.
- One phase is shorter with the switch, by 2.3 s: the hand-over. It is 137
  frames either way (the title counts frames there: one step a flip even at
  the half rate), and without the switch the half rate is still in force
  through it, because nothing asks for the full rate until Crash Results is
  up: 137 frames at 30 are 4.57 s, at 60 they are 2.28 s. It is the loading
  screen between the tour and Crash Results; no game time passes in it. Not
  patched: holding a loading screen for two more seconds serves nothing.
- A figure of "29 s without the switch, 15 s with it, for the same 900
  flips" is the script's, not the game's: `to_crash.sh` took its Crash
  Results shot 900 flips after the impact line. 900 flips later the runs
  above were at:

  | run | switch | seconds | steps | where |
  |---|---|---|---|---|
  | q_off | off | 28.3 | 1566 | Crash Results |
  | off2 | off | 30.5 | 1769 | the hand-over (the long tour) |
  | q_on | on | 15.0 | 900 | the camera still on: the tour (world stopped, byte set) |
  | on2 | on | 16.4 | 900 | the same |

  With the switch 900 flips are 900 steps, 15 s of the same game: the
  script stopped in the middle of the tour and called it results.

The drivers count camera offsets in the title's 30 Hz flips now
(`cam_flips` in `scene_lib.sh`: twice the flips when
`RECOMP_GFX_CRASH_60FPS=1` is in the environment or after the run
directory): `to_crash.sh`, `hud_scene.sh` and `fx_scene.sh`. `to_crash.sh`
with the switch then takes its shot 1800 flips, 30 s, after the impact line
(29 s without). What the shot shows still depends on the outcome, as it
does without the switch: the long tour is not over at 30 s. A settings file
that turns the switch on is not seen by the drivers; `crash60_scene.sh`
waits by the clock after the impact and does not care.

### Race and Road Rage: the crash camera's slow motion

A drive into walls and traffic with the boost held. The crash camera slows
the world to a fifth (divisor 5), goes back to full speed for a moment at
each impact, and lasts a set number of world ticks: 180, 240 or 300 were
seen.

| run | switch | camera | world ticks | steps | flips | seconds | steps/s |
|---|---|---|---|---|---|---|---|
| rr_off (Road Rage) | off | 1 | 300 | 1257 | 629 | 22.94 | 54.8 |
| | | 2 | 181 | 771 | 386 | 14.70 | 52.5 |
| | | 3 | 241 | 977 | 489 | 18.44 | 53.0 |
| rr_on (Road Rage) | on | 1 | 181 | 729 | 729 | 13.61 | 53.6 |
| | | 2 | 299 | 1217 | 1217 | 22.17 | 54.9 |
| rr2_off (Road Rage) | off | 1 | 299 | 1237 | 619 | 20.62 | 60.0 |
| | | 2 | 282 | 1215 | 608 | 20.25 | 60.0 |
| | | 3 | 239 | 957 | 479 | 15.95 | 60.0 |
| | | 4 | 300 | 1193 | 597 | 19.88 | 60.0 |
| | | 5 | 301 | 1249 | 625 | 20.82 | 60.0 |
| race2_off (Race) | off | 1 | 240 | 949 | 475 | 16.21 | 58.5 |
| | | 2 | 239 | 1005 | 503 | 16.75 | 60.0 |
| | | 3 | 241 | 1039 | 520 | 17.32 | 60.0 |
| race_on (Race) | on | 1 | 301 | 492 | 492 | 8.25 | 59.7 |
| | | 2 | 240 | 240 | 240 | 4.07 | 59.0 |
| | | 3 | 301 | 397 | 397 | 6.62 | 60.0 |
| | | 4 | 179 | 432 | 432 | 7.20 | 60.0 |
| | | 5 | 180 | 276 | 276 | 4.60 | 60.0 |
| race2_on (Race) | on | 1 | 300 | 1252 | 1252 | 20.87 | 60.0 |
| q_on2 (Race, quiet machine) | on | 1 | 179 | 753 | 753 | 12.55 | 60.0 |
| | | 2 | 179 | 725 | 725 | 12.08 | 60.0 |
| | | 3 | 181 | 740 | 740 | 12.33 | 60.0 |

- The camera's length in world ticks is one of the same values both ways,
  and a 300-tick camera in slow motion is 1217 and 1252 steps with the
  switch (rr_on, race2_on; 20.87 s at a steady 60) and 1193 to 1257 without
  (rr_off, rr2_off; 19.88 to 20.82 s at a steady 60): the divisor does what
  it did, and the camera is as long. A 180-tick one: 771 steps without
  (rr_off), 725 to 753 with (rr_on, q_on2).
- In slow motion the timer gives one tick in five steps both ways (142
  steps, 28 ticks at 30; 238 steps, 47 ticks at 60); the full-speed moment
  at an impact is 0.2 s of frame time both ways: 10 steps in 5 frames, 11 in
  12.
- race_on's cameras are short in steps because that drive (an earlier input
  list) spent most of them at full speed, the boost button not held; they
  are in the table for the tick counts and the frame times.
- A race starts with the stepper in mode 0; after its first crash camera
  the title leaves it in mode 1 (rr_off: 1 to 4 steps a frame on the loaded
  host). With the switch it stays in mode 0.

### What asks for 30, as logged

Flips with the half rate in force, in the runs without the switch: all of
them with the crash camera byte set but for the stretch after Crash mode's
camera (124 flips, 4.1 s, in most runs), when the game's state is 4 (the
hand-over to Crash Results) and the byte is already clear, and the two or
three flips the change takes to come into force. Boot logos, the intro film (`Titles30.xmv`), the attract
loop, the menus, the garage, the loading screens, the Crash Training film
(`TrC_ENG.xmv`), the junction's intro flyby, the captions and the countdown
are at 60 already (interval 1, one step a flip, stepper mode 1 in the front
end and 0 in a stage). With the switch on there was no flip at the half rate
in any run.

## Frame time at 60 in the camera

Metal, Apple silicon, headless, render scale 1, the game at nice 5; q_on on
the quiet machine, the others beside other agents' games. Where the machine
let the title hold 60 before the crash, it held it in the camera too:

| run | part | fps | flip interval p50 / p95 / p99 / max (ms) |
|---|---|---|---|
| q_on (quiet) | Crash junction, the run-up (18.4 s) | 60.0 | 16.8 / 18.5 / - / 20 |
| q_on (quiet) | crash camera and tour (25.9 s) | 60.0 | 16.6 / 18.4 / 18.7 / 19 |
| q_on (quiet) | replay behind Crash Results (18 s) | 60.0 | 16.5 / 18.4 / - / 21 |
| on4 | Crash junction, the run-up (18 s) | 60.0 | 16.8 / 20.2 / - / 30 |
| on4 | crash camera and tour (36.5 s) | 59.9 | 16.6 / 20.0 / 21.9 / 53 |
| on4 | replay behind Crash Results | 59.9 | 16.7 / 19.5 / 21.1 / 33 |
| on3 | crash camera and tour (28 s) | 60.0 | 16.2 / 20.4 / 21.1 / 32 |
| c0_on | crash camera and tour (32 s), 60 frames dumped in it | 59.9 | 16.4 / 19.2 / 20.1 / 30 |
| q_on2 (quiet) | Race, three crash cameras (12.1 to 12.6 s each) | 60.0 | 16.6 / 18.5 / 18.9 / 19 |
| race2_on | Race, crash camera (20.9 s) | 60.0 | 16.6 / - / 18.9 / 25 |
| race_on | Race, cameras 3 to 5 | 60.0 | 16.4 to 16.6 / 19.5 / 20.2 to 22.1 / 25 to 42 |

The spread around 16.7 ms is the same in the run-up, outside any camera (p95
18.5 on the quiet machine, near 20 on the loaded one): it is the host's
pacing, not the camera's. GPU time was not
measured; the camera draws what the race draws (no radial blur, the
letterbox on top).

## What looks different

- Motion: 48 consecutive frames of the camera, dumped both ways, differ
  from one to the next by a smooth series with the switch (mean absolute
  difference 11.8 to 20.0 of 255 over 47 pairs, no pair near 0: no picture
  is shown twice), and the replay's series has the live crash's shape.
- The blur at the camera's first cut (the whole picture streaked for about
  a third of a second in the outcomes where the camera swings) is there
  both ways: still there in the sixth frame at 30 and in the twelfth at 60,
  looked at frame by frame for the same outcome. Its strength per frame was
  not measured.
- The "LOADING" screen between Crash mode's tour and Crash Results is up
  for 2.3 s instead of 4.6 (137 frames either way; above).
- The HUD is updated once a frame with one step's time, so what it animates
  in the camera (the "CRASHED!" title flying in, the Crashbreaker strip, the
  dollar labels over the wrecks, the Impact Time prompt) runs at the pace it
  has in the rest of the race, twice the pace it had at 30, except the one
  element the title compensates (`sub_000CBA80`), which keeps its pace. Not
  measured element by element; no frame looked wrong.
- Sparks, smoke, debris and the crash's physics are stepped with the
  simulation: unchanged in time.

## For strong machines only

**Where 60 fps cannot be held, the game itself runs slower while the switch
is on, not just the picture. Leave it off on a Steam Deck and other
handhelds.** The overlay's row says the same. With the switch the title
takes one simulation step a frame, so a frame that comes late is game time
that is not made up; at its own 30 it has twice as long for the same scene.

Measured on the Steam Deck in a Crash event (the owner's, 2026-10-12): 56.5
and 52.4 steps a second with the switch on, the game 6 to 13 % slow, the
flip interval's p95 at 23 to 24 ms, and 7.7 W against 5.1 W; with it off
60.0 steps a second.

## Risks and what is not verified

- **A host that cannot hold 60 in a crash scene.** With the switch on the
  stepper stays in mode 0, where a late frame slows the game down instead
  of taking a second step (the title's own behaviour from the start of a
  race to its first crash camera, but with the switch it holds for the
  whole race and in the densest scenes). This Mac held 60; the Steam Deck
  did not (above). A Retroid Pocket 5 runs a busy race at 47 to 58 fps
  (`docs/android.md`), so it may not hold 60 in a crash scene either and
  should keep the switch off until measured there: at 30 the same scene has
  33.3 ms a frame. A possible remedy, not tried because it changes the title
  outside the cameras: the stepper in mode 1 (steps by the clock) while the
  switch is on.
- Latency was not measured. Outside the cameras nothing changes; in them a
  frame is 16.7 ms instead of 33.3.
- The takedown camera: no scripted drive produced one (every camera
  captured in Race and Road Rage, nineteen of them, was a "CRASHED!" one).
  It is expected to go by the same byte (the title has no other request
  for the half rate in a one-player stage), not seen.
- The second one-player stage class (`sub_0001AC30`): patched by reading,
  no run reached it (what mode uses it is not known; online or the party
  modes are candidates).
- Audio: not listened to. By the code read, its timing follows the
  simulation (the audio manager is advanced once a frame by steps x the
  step, `0x170ED`), and steps per second are unchanged; no audio code is
  touched.
- Vulkan, Windows (D3D11 and Vulkan), the Deck, Android: not run. The patch
  is in the generated code every platform compiles; the interval reaches
  each backend the way the title's own interval 1 does.
- Split screen stays at 30 (not patched), not run.
- 16:9, ultrawide and render scales above 1 in the camera: not run with the
  switch.
- The overlay row on screen: no window in these runs; the setting is
  covered by the toolkit's `overlay_settings` test.

## Running the checks

    export HOME=<scratch>/home RECOMP_SETTINGS=<scratch>/home/settings.toml
    export SAVES=<reference saves> STOP_AT_END=1 CAPTURE=1
    tools/headless/crash60_scene.sh <scratch>/off crash
    tools/headless/crash60_scene.sh <scratch>/on  crash RECOMP_GFX_CRASH_60FPS=1
    tools/headless/pace_report.py <scratch>/off <scratch>/on --phases

Never with the player's own `HOME`: a start with it moves the player's save
folders (`tools/release/test-user-dirs.sh`).
