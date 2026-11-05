# High refresh rate (`graphics.high_refresh`)

Frames at the display's refresh rate, the simulation at its own 60 steps a
second, and the pictures between two steps blended. Overlay Display page (Frame rate),
"High refresh rate"; `settings.toml` `[graphics] high_refresh`;
`RECOMP_GFX_HIGH_REFRESH=1`. Off by default on every platform.

The setting needs no other switch. The menus, the game's own 30 Hz phases
and split screen run as shipped, at the title's own steps and interval on the vblank:
a crash camera with "60 fps crash cameras" (`graphics.crash_60fps`) off (the
half rate asked for, `0x4D538D`, or in force, `0x4D538E`), and two-player
split screen at either rate (players `0x73A1C0`). The frames go back to the
display's rate when the phase ends.

It adds at most one 60 Hz step of delay, the one an interpolation cannot
avoid: a picture is a blend of the last two steps at the frame's own flip
time, never further behind (see [The latency](#the-latency)).

The game code is in `src/game/hirefresh.c`: nine wraps in
`src/game/recomp/recomp_manual.c`, no guest patch. The toolkit side
(xboxrecomp) is the display's rate and refresh grid (`xbox_DisplayRate`,
`xbox_DisplayGrid`: the refreshes the presenter reports, else the window
system's stated refresh, else the SDL display mode), the host flip grid
(`xbox_FlipHostRate`, `src/kernel/kernel_pacing.c`; `src/kernel/flip_grid.h`,
used by `flip_pacing_swap` in `src/kernel/nv2a_pb_exec.c`) and the grid's
state for the title (`xbox_FlipHostGridState`).

## Why 60 was the game's own speed

`CB3Game::Update` (`sub_000165F0`) runs the steps in `0x4D53AC` (game object
`0x4A71A0` + 0x2E20C), and the stepper `sub_001B58E0` chooses them at the
end of the frame before, with a minimum of 1 (`0x16C2D`;
[race-clock.md](race-clock.md)). A host that flips faster than 60 runs the
game faster. The step loop itself (`0x16AB6`) handles a count of 0.

The stepper cannot just be given a minimum of 0:

- Its period (timer `0x4D5338` + 0x20, in 733.33 MHz ticks) is 1/60 s in a
  stage but 1/30 s in the front end (24444444 ticks). There the minimum
  alone gives 60 steps a second. With a minimum of 0 the menus ran at
  30.00 steps a second.
- It counts no time before its first step (`0x1B58FB`: no steps taken, no
  time added). A 0 from the first frame never started the clock, and the
  boot hung at step 73.
- A paused race holds the timer (+0x34). The stepper then returns its
  minimum every frame, and the pause menu runs in the steps (the stage's
  update is skipped while a player has paused, `0x16BA1`).

## What it does

The steps. The title's stepper runs as shipped, but its count is not
used. At the start of `Update` the count is the number of steps that bring
the simulation up to the picture's time: 0 when it is there already, at
most 4. The menus' mode 2 keeps the lag beyond the cap and the other modes
drop it, as the title does.

The picture's time is the host time the frame will flip at: the edge of the
toolkit's grid its swap will be held to. The toolkit publishes, at every
swap, the swaps it has seen, the last one's edge and the grid's period;
the game counts its own swaps (`sub_00350DB0`, D3D's swap) and so knows how
many frames are on their way, each one period later, and which edge this
frame gets. The pictures are then exactly one display refresh apart, as the
display shows them. The wall time between `Update`s is not used: `Update`
runs ahead of its flip by a varying amount (3.7 to 10.6 ms between Updates at
144 Hz), and the raw wall time moved the car 0.85 to 1.15 times its average
per picture.

Active only while the game's state (`0x4D53B4`) is 5 in a stage (the
stage `0x4D5370` has a mode object at + 0x1B8): races and the pause menu,
with one player and no half rate. The front end runs as shipped at 60: its
showroom (Select Car) draws several cars through the same RenderWare frames
within one picture, and blending two steps' states put them in each other's
place (the big car jumped, changed or went missing from picture to picture
at 120 Hz). A frame found somewhere else entirely within one step (more
than 8 units from its first take) is not blended at that take; where it is
back, it is. The other states of
`Update`'s jump table (`0x16CC8`: loads, the stage's set-up, hand-overs)
step once a frame by their own code and keep flips at 60.

The flips. The game asks the toolkit for a host grid at the display's
rate. The toolkit holds each flip until the grid's next edge instead of the
swap's vblank, with a timed wait (the last half millisecond is spun only on
a grid faster than 200 Hz). The emulated vblank goes on at 60 Hz for
everything else that counts it (the XMV player, D3D's vblank counter).

The grid's period and phase are the display's as its refreshes are
reported: the refreshes the vblank lock learns from (Metal's on-glass times,
Vulkan's present_wait, Windows' DXGI vblank), learned while the grid runs
even with the lock off; else the window system's stated refresh (Vulkan's
`VK_GOOGLE_display_timing`, gamescope's) as a period alone; else the SDL
display mode's whole Hz. Each edge is the refresh nearest one period after
the last, so the grid cannot walk off the panel. With SDL's whole Hz alone
a 174.96 Hz panel read 175, and over a minute the frames queued up to 3
refreshes deep in a FIFO swap chain (8 at 143.86 Hz read as 144; the
toolkit's `tests/flip_grid`); on the display's grid none queue and every
refresh shows one new frame. The edge is the frame's vblank for the
presenter, so Metal's timed present aims each frame at its own refresh. The
pacing watchdog decides nothing while the grid runs.

The pictures between steps. Two kinds of object move:

- RenderWare frames: the camera and everything else hung on an `RwFrame`.
  RenderWare 3.7's `_rwFrameSyncDirty` (`sub_001E15E0`; dirty roots at
  `0x75935C`, LTM at frame + 0x50, child + 0x98, sibling + 0x9C) runs in
  `RwCameraBeginUpdate` (`sub_001D91B0`), before the device builds the view.
  After it, each moved frame's LTM is recorded with the step count
  (`0x4A1EB4`). Each frame that moved at the last step gets, in place, its
  last two states blended at the picture's time: one step behind the clock,
  at the lag left after the frame's steps. `RwFrameGetLTM` (`sub_001DD280`)
  does the same for frames the pipelines ask for while a picture is drawn.
  The blends are put back to the true LTMs at the start of the next
  `Update`, so the simulation never reads one. A frame's state is taken
  once a step: the first sync after the steps is the truth. A later sync in
  the same picture can rebuild an LTM from a parent that already holds a
  blend, and that must not become the value put back. A sync while the
  `Update`'s steps still run (the step count short of where they end) is
  not taken, nor does it start the picture.
  The record of a frame is kept by its address, so it outlives the frame.
  No mark is kept for good on a frame found elsewhere within a step. A
  permanent mark ("shared", for the showroom) was inherited by a frame made
  later at the same address. When the view's camera frame had it, the camera
  was never blended again: the world moved only at the steps, under a car
  blended to the picture time. On the Steam Deck at 90 Hz the car then
  jumped up and down against the road from picture to picture (BO-241,
  the owner's recordings from int027; he had seen it before). See the
  evidence below.
- Vehicles: race cars and traffic are not frames. `CB3RaceCar::Draw`
  (`sub_0018DE00`) and `CB3TrafficSystem::Draw` (`sub_001A4710`) pass the
  vehicle's own world matrix (car + 0x10, laid out as an `RwMatrix`) to
  `sub_000303D0`. The blend is written there and stays for the rest of the
  picture, as a frame's does; the true matrix is put back at the swap and
  again at the next `Update`. What the title draws on a vehicle after its
  body reads the same matrix: the player car's lights (`sub_00187C70`:
  brake, head and tail lights, with esi the matrix) and boost flames
  (`sub_001871E0`, from car + 0x10), drawn right after the body
  (`sub_0017F730`, `0x18DF94`), and the traffic's lights after all their
  bodies (`0x1A52DB`, `0x1A531E`). They are queued as additive sprites for
  the picture (three queues in the effects manager `0x5A9A00`, emptied at
  each view's start by `sub_0017F670`). With the blend only in place for
  the body's call, the glows were drawn at the last step while the body was
  a third of a step to a whole step behind it (the blend's u is 0.33 to
  0.99 at 90 Hz): at 120 mph a glow sat up to 0.9 m inside the car and the
  car's own rear hid part of it, by a different amount in each picture.
  The brake lights flickered and the boost flames were mostly hidden
  (BO-241, seen on the Steam Deck at 90 Hz).
  Where the title writes the matrix itself after the draw (the shake,
  +0x19AB, `0x18DEF2`: its own copy put back), the blend is gone and
  nothing is put back.
- The view matrix: the view being drawn (`0x4D6520`, the race's is
  `0x4D6200`) holds a 4x4 view matrix at +0x00 (the camera's inverse times
  the view window, `sub_001C8870`) and its `RwCamera` at +0x58.
  `sub_001C8A60` builds both from the camera the steps left (+0x40): the
  camera frame's modelling matrix and, from the same, the view matrix,
  before the frame sync. The sync blends the camera frame, so the world is
  drawn from the blended camera while the view matrix is at the step's.
  The rival marker (`sub_0018F060`: the triangle, the position number and
  the label above a rival, at the car's point projected with that matrix;
  its size and the triangle/number switch from the point's depth) would then
  be drawn from the step's camera over a car drawn from the blended one: up
  to a step's camera move off its car, by a different amount in each picture.
  Far away that is under a pixel; a rival a few metres off at speed had its
  "4th" jump by 10 to 50 pixels of the 640x480 frame from one picture to
  the next (BO-241). So after the camera frame's blend the view matrix is
  rebuilt to match it, V' = B^-1 M V (M the camera's true LTM, B its blend;
  M V, the view window part, is kept), and put back with the blends.
  `BO3_HIREFRESH_VIEW=step` leaves it at the step's camera (the A/B arm).
- The racers and the marker's camera point, at the frame sync. The rival
  marker (`sub_0018EA50`) gives each slot its look, the arrow or the
  position number, by distance rank from the view's game camera (view +
  0x68, + 0x30), not by car: it sorts the racers (the table at `0x64B38C`,
  + 0x204: the car's matrix) each picture and the looks stay with the
  ranks. Every racer's matrix is blended at the frame sync, before anything
  of the picture is drawn, and the camera point the same way (taken once a
  step, lerped, put back with the blends), so the ranks are of one time; the
  draw finds the blend in place. Blending a car only when it is drawn left
  one behind the camera at its step, and the camera point too, while a rival
  in view was up to a step behind: two racers at about the same distance
  swapped ranks from picture to picture and the rival's marker flickered
  between arrow and number (Original mode with High refresh). A car drawn
  from its own copy (+ 0x19AB, `0x18DEF2`) is left to the draw. `BO3_HIREFRESH_RACERS=draw` blends at the draw only
  (the A/B arm).

The position is lerped; the axes are lerped and made orthogonal again
(Gram-Schmidt) at their lerped lengths. Nothing is blended across a move of
more than 8 units in a step or a turn of more than about 45 degrees: a
camera cut, a reset, or a frame whose address was reused.

The HUD. `sub_000535E0` is updated once a frame with one step's time
(`MEMF(0x4AE1FC)`; [crash60.md](crash60.md)), so at 144 Hz its animations
would run 2.4 times fast. Its time is that value times the picture's time
since the last over 1/60 s.

The players' cameras. `sub_00167940` updates a player's cameras once a
frame, after the steps (from `sub_000170B0`), with the frame's steps times
one step's time; each camera in force (the player's record + 0xC20, a bit
each) through its vtable (+ 0x10) with that time, except cameras 0 and 14,
which get one step's time (`MEMF(0x4AE1FC)`) whatever the steps: they take
a frame to be a step. The cameras run once a step as shipped: not at all in
a picture without a step (the picture between is the blend), and n times
with one step's time each after n steps. Run once a picture instead, they
ran 1.5 to 2.4 times as often as the steps. After a side hit the chase
camera swung three or four times where the console swings once (BO-241,
seen on the Steam Deck at 90 Hz), and in the look-back view the player's
car, which that view hides, was drawn in every picture without a step (its
inside showed). Headless at host:90 with the
executor slowed as on the Deck (60-90 pictures a second, a fifth of the
Updates with two steps): look back held for 4 s, the car hidden in every
picture (before: drawn in 1 of 3); after side hits the car's sideways place
in the camera's view changed direction 1-4 times in the 2 s after (mean 2.0)
against 1-9 (mean 4.2) before.

The body's rock after a slam. When a rival slams the car, the title
rocks its body for 1.25 s (`sub_0018DA00`, from the car's step: a decaying
turn of the car's matrix, about 2.5 swings, into a copy at car + 0x1930,
+ 0x19AB set). `CB3RaceCar::Draw` puts the copy in car + 0x10 for the body
and the lights and its own matrix back after. The copy is taken and
blended under its own record and nothing is put back for it (the title
does), and a later sync never blends a vehicle's matrix again. Otherwise the
vehicle blend takes the copy as the car's matrix, a later frame sync of the
same picture blends the car again in place, and the swap puts the rock back
as the car's true matrix: the simulation steps on from the rocked car, which
then fishtails (BO-241, the owner's Steam Deck at 90 Hz: three or four
swings where the console rocks once). Headless at host:90, a rock started by a
test switch (`BO3_HIREFRESH_WOBBLE=<race s>`): the car's true yaw rate during
it went -344 to +386 deg/s with 4 sign changes before, -31 to +74 with 1
after (High refresh off: -19 to +70, none); the drawn rock is within -7.5 to
+5.4 degrees of the car's step (off: -6.0 to +5.6).

What is put back. Every vehicle matrix the blend touches is
snapshotted at the picture's first frame sync, after the steps and the
title's per-frame updates and before anything is drawn. While a matrix still
holds a blend of ours at the swap, the snapshot is put back, never a value
the title wrote to draw (it may be made from our blend). A matrix the title
put something else in for the draw (an override: the slammed car's rock) is
blended under a record of its own; a value the title writes after our last
blend stays, the title's. With diagnostic logging on, a 10 s line in
hirefresh.log counts the overrides drawn and the values left to the title
at the put-back, by owner (car, rival, traffic, camera, view, frame).
`BO3_HIREFRESH_OVR=1|2` (test) draws every other vehicle from its matrix
moved 0.5 units up and puts it back after the draw (1) or leaves it (2).
Headless at host:90: the forced rock gave a true yaw rate of -31 to +70 deg/s
with no sign change (High refresh off: -19 to +70, none), 111 car overrides
counted, nothing left to the title; with the traffic override put back by
the "title", 4300-9000 rival and 4700-8400 traffic overrides per 10 s and
nothing left to it; a race with 60 fps crash cameras and real crashes: 58
rival overrides (a rival's rock), nothing left to the title.

The executor's idle time. While the flips are held to the host grid
the toolkit's executor spins instead of napping when it has nothing to do
(the hold to the grid's edge, the waits between the title's kicks): on the
Steam Deck at 90 Hz the naps cost it about 13% of its clock against 60 Hz
mode, and High refresh reached 76 pictures a second on the glass instead of
89-90 (BO-241). It is on whenever the grid runs, so High refresh off changes
nothing. An idle guard keeps it from running a core for nothing: it spins
only while the frames leave under 30% of the period to hold for (off again
above 40%), so a pause menu that holds most of the period naps.
`RECOMP_DEBUG=hr_spin=0` naps instead.

The sparks. The sparks from scrapes and hits (`fxspark`, the six
particle systems of the effects manager `0x5A9A00` at + 0x39DD0, 0xF0
each) are drawn as streaks through the last pictures, not where the
simulation left them. `sub_00038200`, called once a picture from the
effects render (`sub_0017F7E0`, `0x17F9C9`), keeps a history of 18 entries
a player (`0x5492B0` + player x 0x5A0, 0x50 each: the picture's
world-to-clip matrix from `0x4D6670` and its time). Entry 0 is the picture
itself; when it is 1/96 s (`0x386EF8`) past entry 1 the history moves down
one first, and in every picture, while `0x4D53BC` or `0x4A4B90` is 0, the
older entries' times are put back by half the frame's time (`0x4D53B0`). `sub_000367C0` (six calls, `0x17F9E1`)
places each spark at each entry's time from its start, velocity and gravity,
through that entry's matrix, one quad from entry to entry, while the entry
is younger than the spark's life (0.049 s, the descriptor's + 0x5C) and up
to the first two entries with the same time (`0x3688C`). The quads are the
title's own, built on the CPU in clip space with a fixed width and sent as
inline vertices (begin QUADS, `SET_VERTEX4F`); no point or line state.

The title takes these times from the race clock (`0x60EA20`), which moves
only at the steps. A picture between two steps then has the time of the one
before but is 1/60 s past entry 1, so the history moves down and its two
newest entries have the same time: that picture draws no spark at all, and
the next one only the newest quad. At 60 every picture draws three quads,
0.05 s of a spark's path; at 90 the pictures drew 0, 1 and 2, at 120 none and
one in turn, at 144 mostly none. The sparks got thinner as the rate rose
(BO-274, seen on the Steam Deck and on Windows). To avoid that, the push and
the six draws get the picture's time: the race clock at the picture's place between its last two
steps, and as the frame's time the clock's advance since the last picture,
both put back after each call. The history is then one of pictures, as the
title meant it (1/96 s or more apart): the streak is the same 0.05 s of
the path as at 60, in more and shorter quads, and the sparks are where the
cars are, at the picture's time. `BO3_HIREFRESH_SPARKS=step` keeps the
steps' clock (the A/B arm), `=ab` takes the two in turn, 15 steps each;
`BO3_HIREFRESH_SPARKLOG=<file>` writes a line a push for player 1 ("S": the
swap, the step, the steps of the Update, whether the picture's time was
given, the race clock, the time given, the frame's time, a step's time, the
quads a spark gets and the entries' ages in ms) and what each draw put in
the push buffer ("Q", 160 bytes a quad).

Measured on the Bazzite (Linux, Vulkan, headless, out-of-box settings,
Silver Lake, 2026-10-21): High refresh off at 60, 3 quads a spark in every
picture (ages 0 to 50 ms); on at 120 before the fix, none in every picture
between steps and 1 (16.7 ms) in the others; after, 6 in every picture
(0 to 50 ms, 8.3 ms apart); at 90, 0, 1 and 2 before and 5 (0 to 56 ms)
after. A scrape along the barrier with `=ab`: about 250 spark quads in the
pictures with a step and none between them on the steps' clock, about 1400
in every picture on the picture's (the 60 Hz run's scrape drew 550 to 950).
With High refresh off the wraps never act (no picture took the picture's
time in three 60 Hz runs).

Not blended: what is not on a frame or a vehicle matrix. The other
particles (smoke, debris), skid marks and the deformed car body's
vertices still move once a step.

## Evidence

Linux (NixOS 26.11, kernel 7.2), AMD Ryzen 7 9800X3D, NVIDIA GeForce RTX
5060 Ti (driver 610.57.04), Vulkan backend, headless, render scale 1,
2026-10-18. Without a display, `RECOMP_FLIP_PACING=host:<Hz>` stands in for
the display's rate. It is plain vblank pacing unless the title asks
for the grid. Autopilot races (`tools/headless/race_drive.sh`), the pause
menu by `MIX=1`, and `BO3_PACE_LOG` per flip.

| Run | Flips/s | Steps/s |
|---|---|---|
| Setting on, 175 Hz, race | 174 to 176 | 59.8 to 60.1 |
| Setting on, 175 Hz, pause menu | 176 | 60.0 (142 before the held-timer fix) |
| Setting on, 60 fps split screen off | 60 | 60.0 (the setting stays off) |
| Setting on, 260 Hz | 230 to 257 (this machine cannot hold it) | 60.0 |

Player 1's car as drawn, once a picture (`BO3_HIREFRESH_TRACE`), its
movement per picture against the average of the 9 around it:

| Rate | p5 | p50 | p95 |
|---|---|---|---|
| 144 Hz, wall-clock frame time | 0.85 | 1.00 | 1.15 |
| 144 Hz, flip-grid frame time | 0.97 | 1.00 | 1.00 |
| 175 Hz | 0.90 | 1.00 | 1.01 |

Of 6515 pictures with the car moving at 144 Hz, 3794 ran no simulation step,
and the car was in a new place in every one.

Frame times (`RECOMP_TRACE=pacing=all,frametime=`; `tools/perf/framestats.py`,
the last 40 s of the same autopilot race, both 60 fps switches on):

| | flips/s | frame_ms p50 / p99 | late swaps | ex_busy mean | process CPU |
|---|---|---|---|---|---|
| Setting off | 59.72 | 16.67 / 16.75 | 0.1% | 2.66 ms | 86% |
| Setting on, 175 Hz | 173.92 | 5.71 / 7.27 | 0.5% | 2.56 ms | 94% |

Frame dumps from the race at 175 Hz showed the cars, the HUD and the
takedown camera drawn as at 60. Played on the same machine, 2026-10-18, at
render scale 4, with the setting turned on from the overlay, under KDE
Plasma 6 (Wayland): on a 3440x1440 175 Hz display in a window and in
fullscreen (SDL's desktop fullscreen, the mode on every desktop host), and
on a 2560x1440 260 Hz display, both with variable refresh enabled. The
display's rate from SDL, the locked row, races, the pause menu and
two-player split screen worked as above, and the rate followed the window
when it was moved from one display to another (Meta+arrow keys). On a
1920x1080 60 Hz display (no variable refresh) the setting stays out of the
way (it acts only above 61 Hz). An earlier build of the same
blending, started by environment variables, was played in a window at 175
Hz; that is where the pause menu's speed was found. The toolkit's own tests
pass (13 binaries, the overlay's 264 settings checks among them).

### The follow-up (BO-241), macOS

MacBook (Apple silicon), Metal, headless, render scale 1, 2026-10-19,
`RECOMP_FLIP_PACING=host:<Hz>` as above. About 60 s of an autopilot race on
Silver Lake with a 10 s pause menu in the middle, `BO3_PACE_LOG` per flip.
Flips a second outside the title's half rate (the crash cameras, with
60 fps crash cameras off, fell back to its 30 in every run and came back):

| Run | Race flips/s | Race steps/s | Pause flips/s | Pause steps/s |
|---|---|---|---|---|
| On, 90 Hz | 85.2 (one 0.84 s host stall), 90.0 after the pause | 58.1, 60.00 after | 90.0 | 60.08 |
| On, 120 Hz | 119.9, 119.8 after the pause | 60.14, 60.10 | 120.0 | 59.95 |
| On, 144 Hz | 143.8, 143.6 after the pause | 60.07, 60.07 | 143.8 | 60.00 |
| Off, 90 Hz | 59.9 | 59.93 | 59.8 | 59.77 |
| Off, 120 Hz | 60.0 | 60.00 | 60.0 | 59.99 |

Frame dumps of the race and the pause menu at 120 Hz: the cars, the HUD
and the menu as at 60.

Two-player split screen, 60 fps split screen on, the setting on at 120 Hz
(before the front-end change, 4:3, render scale 1): the grid stops at the stage's start ("flips held to the vblank again") and
the race runs at 59.8 flips and 59.8 steps a second (9 s; the run was cut
short).

With the macOS release build's out-of-box settings (16:9, render scale 3,
SMAA, sharpening, Menu/HUD 2x, far vehicles and scenery; headless, so the
dumps are the game's 640x480 frame), after the front-end change:

| Run | Flips/s | Steps/s |
|---|---|---|
| On, 144 Hz, race | 141.7 (two host stalls of up to 0.2 s), 144.0 after the pause | 59.62, 60.01 after |
| On, 144 Hz, pause menu | 143.5 | 60.06 |
| Off, 144 Hz, race / pause | 60.0 / 60.0 | 59.99 / 59.98 |
| On, split screen with 60 fps split screen off | 33.1 (the title's 30, with the load) | 59.90 |

Each picture's time against the edge its swap was then held to
(`BO3_HIREFRESH_STATS`): 144 of 144 a second within 0.5 ms (0.000 ms off),
two frames on their way, outside the seconds with a host stall. The Select
Car showroom, 21 pictures in a row: the car turns smoothly with the
setting on, as with it off.

The brake lights and boost flames (BO-241), Mac, Metal, headless, the
out-of-box settings, 60 fps crashes on, a build that switches between the
old one-call blend and the fix every half second (30 steps) within one run,
so both see the same drive. Brake glow: saturated red pixels at the car's
rear, the change of each picture against its two neighbours. Boost flame:
blue pixels at the exhausts while boosting.

| Rate | Brake glow change, old / fixed | Boost flame pixels, old / fixed |
|---|---|---|
| 90 Hz | 8.1 % / 2.3 % and 0.8 % | 112 / 350 |
| 120 Hz | 9.1 % / 3.5 % (medians 7.0 % / 1.0 %) | 65 / 183 |

At 90 Hz the old glow ran a three-picture cycle, for example 718, 612, 552,
707, 591, 529 pixels; with the fix 714, 713, 707. The highlights in the
upper half of the picture (street and tunnel lamps, sky) change as much
between two pictures of one step as between two steps (7.2 % / 8.8 % and
5.3 % / 5.3 %): nothing there follows the steps. The player car's drawn
path through a crash with its slow-motion camera and the reset after it
(`BO3_HIREFRESH_TRACE`): no reversals; the reset is one snap (17.6 units),
then even steps of 0.25 to 0.29 units a picture.

The rival marker (BO-241), Mac, Metal, headless, the out-of-box settings,
Single Event race with the autopilot, every picture dumped for 900
pictures, one build switching between the step's view matrix and the fix
every 60 steps (`BO3_HIREFRESH_VIEW=ab`). `BO3_HIREFRESH_MARKS` logs, at
each swap, each rival's point projected with the view matrix the marker
used and with the view the world was drawn with (the camera frame's LTM
in place). Old view, the marker's offset from its car and that offset's
change from one picture to the next, pixels of the 640x480 frame, p50 / p90:

| Rival's depth | 120 Hz offset | 120 Hz change | 90 Hz offset | 90 Hz change |
|---|---|---|---|---|
| 2.7-10 (number) | 6.1 / 27.4 | 3.7 / 17.3 | 11.4 / 35.0 | 8.0 / 24.0 |
| 10-20 (number) | 4.0 / 11.6 | 2.2 / 5.8 | 1.2 / 5.9 | 0.9 / 4.3 |
| 20-35 (number) | 1.2 / 7.0 | 0.5 / 5.1 | 0.5 / 2.3 | 0.4 / 1.7 |
| over 35 (triangle) | 0.8 / 2.9 | 0.5 / 2.1 | 0.5 / 2.4 | 0.4 / 1.7 |

With the fix both projections agree within 0.01 px in every picture. In the
pixels, the yellow position text's centroid against the car's drawn point,
its change from one picture to the next (p50 / p90): 90 Hz, rival at 2.7 to
10, 7.9 / 20.4 old against 3.7 / 7.3 fixed (same run, 9 pairs fixed); 120 Hz,
separate old and fixed runs, 6.6 / 14.9 old (rival at 2.8 to 10) against
1.0 / 4.1 fixed (at 10). M V, the view window part, is the same matrix in
every picture (diagonal -0.417, -0.741, 1 in the race; its translation
within 1e-4 of 0), so the rebuilt matrix is the one the title builds from
that camera.

The ranks (BO-241), Mac, Metal, headless, the out-of-box settings with
Original marker and High refresh at host:90, Single Event Silver Lake with
the autopilot, ~3000 pictures a run, one build: at each swap the racers
ranked by distance from the marker's camera point as `sub_0018EA50` ranks
them (`BO3_HIREFRESH_MARKS`). A pair of racers whose order changes and
changes back within three pictures is a rank swap that the picture timing causes;
a marker that changes look and back within 30 pictures
is the flicker:

| Arm | runs | pair order changes | changed back in 3 pictures | look back in 30 |
|---|---|---|---|---|
| 60 Hz (no blend) | 2 | 31, 35 | 0, 0 | 7, 4 |
| 90 Hz, racers blended at the draw only (`BO3_HIREFRESH_RACERS=draw`) | 4 | 24-145 | 7-111 | 1-23 |
| 90 Hz, every racer and the camera point at the frame sync | 3 | 16-47 | 0-3 | 0-3 |

Before the fix, the player's distance from the camera point went 4.78, 5.01,
5.24 over each three pictures at a steady chase camera (the car blended, the
point at its step); with it the distance holds at 5.32. Each racer's move from picture
to picture is as even as the player's.

## The latency

The setting adds at most the one 60 Hz step a blend between two steps
needs. The picture is drawn for the time its frame will flip at (the grid
edge its swap is held to), between the last two steps; nothing is queued
for it: the flips are held to the display's own refreshes, so a FIFO swap
chain never holds more than the frame on its way (`tests/flip_grid`), and
a Metal window aims each frame at its own refresh. Input-to-photon on
against off (BO-74, `tools/perf/latency_run.sh`) is still to be measured.

## Not verified

- Windows, the Steam Deck (a fixed 90 Hz panel), Android, a ProMotion or
  other variable-refresh Mac display: not run. Windows compiled (llvm-mingw).

Debug: `BO3_HIREFRESH_STATS=1` logs once a second what was blended;
`BO3_HIREFRESH_TRACE=<file>` writes player 1's car position per picture;
`BO3_HIREFRESH_CAMREL=<file>` writes player 1's car against the camera the
world is drawn with, once a picture, and whether each was blended;
`BO3_HIREFRESH_POKE=<swap>` (test) takes the view's camera frame a second
time 20 units away in that picture, as a cut within a step would. With
diagnostic logging on, a line every 10 s says when frames were found
elsewhere within a step and when the view's camera was not blended in more
than a tenth of the pictures between steps, and after a side hit on the
player's car (its sideways speed changing by more than 0.3 units a step in
one step, High refresh on or off) one line for the 2 s after it: the
Updates by the steps they ran, the pictures where the car and the camera
were not blended alike, the takes found elsewhere, the car's yaw rate (sign
changes, peak), the car's sideways place in the camera's view (range, sign
changes of its move), pad 1's steering stick (range, sign changes), the
pictures with the body's rock, and how far the drawn yaw was from the step's.
These lines also go to <save dir>/logs/hirefresh.log.
CAMREL also writes, at each Update before its steps, the car and the camera
as the simulation holds them ("S" lines), and why the car was not blended
at a picture.
