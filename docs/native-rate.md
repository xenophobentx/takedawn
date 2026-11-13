# Native frame rate (`graphics.native_rate`) and Loading screens (`game.loading_min`)

High refresh (`docs/high-refresh.md`) draws at the display's rate while the
game keeps its own 60 steps a second: most pictures are a blend of two
steps. **Native frame rate** steps the simulation itself at the display's
rate instead: on a 170 Hz display, 170 steps a second, each 1/170 s long,
so every frame is a real step and the pads are read as often (the title
reads them once a step). Menus and loading screens run at that rate too.
Overlay Display page, under Frame rate, "Native frame rate"; `settings.toml`
`[graphics] native_rate`; `RECOMP_GFX_NATIVE_RATE=1`. Read at start. Off by
default: everything runs as shipped. It turns on High refresh rate, 60 fps
crash cameras and Potato mode (`docs/race-clock.md`), which it needs. A
split-screen race and online play keep the shipped step (below); a
split-screen load runs at the display's rate like any other. All three programs
(NTSC-U and both PAL discs).

**Loading screens** keeps a race's loading screen up at least 3, 5, 8 or 10
seconds (Game page; `[game] loading_min`; `RECOMP_GAME_LOADING_MIN`), for
a player who likes to look at them: with Native frame rate the loads take
about a third of the time (below). Off by default.

The code is `src/game/simrate.c`, with hooks in `src/game/hirefresh.c` and
six more wraps (`recomp/recomp_manual.c`, `recomp_manual_pal.c`,
`recomp_manual_pal_ensv.c`).

## The step

The title keeps its step as data. `sub_00015A60` sets `0x49C120` (the step
in seconds) and `0x49C11C` (in whole ms) and hands the stepper
(`sub_001B58E0`, timer `0x4D5338`) its period in ms through `sub_001B5880`,
which `sub_00016E00` calls again at every scene change. The game's timers
(`sub_001B5A80`) copy `0x49C120` into their + 0x1C and turn steps into
seconds with it; two are set up inline (the game object's at `0x4AE1E0`,
whose step `0x4AE1FC` the HUD and the time played read, and `0x49FA98`).

With the setting on, while one player plays, the wrap of `sub_001B5880`
scales the period to 1/N s, the step globals say 1/N, and every timer made
so far whose + 0x1C holds exactly the old step gets the new one (and back
when a split-screen race starts: the player count is 2 from the first frame
of its load, which keeps the short step and so flips at the display's rate;
the race itself, the running state, gets the shipped step, as High refresh
leaves split screen alone). High refresh then runs its frames with a step of
1/N (`STEP_S`): a step a picture while the machine keeps up, its blend at
most one short step long. N is the display mode's stated rate
(`xbox_DisplayModeRate`), chosen once: the measured rate wanders while the
game starts (a 170 Hz display measured 111 once). `RECOMP_FLIP_PACING=host:<Hz>`
gives it for a run without a display.

## Online play

The title's netcode assumes the shipped step on every peer, so while an
online session is up (`xnet_hle_session_up`: signed in to the online menus,
a game's lobby, its race, its results; Internet or Local network) the
simulation keeps the shipped step: the next Update sees the session and
puts the step back to 1/60 s and the stepper's period to what the title
asked, as for split screen, and a scene change made during the session
leaves the short step off. The row is locked meanwhile, shown Off, its hint
saying why (the toolkit's `XS_OFF_ONLINE` flag on the setting:
`xbox_SettingHeldOnline`). Once the session is over the short step comes
back at the next scene change, the next loading screen. In the front end
High refresh is as shipped then too (`wanted_rate` reads the short step,
not the setting). This holds until two native-rate peers and a mixed pair
have been tried against each other.

## What counted calls

Physics, AI, timers and the race clock follow the step. Code written for 60
calls a second keeps its own calls a second:

| What | How it counts | Here |
|---|---|---|
| The pause menu, results pages, the signature takedown's photos | menus count steps, transitions read the step (stepper mode 2, or held) | the shipped steps with the shipped step's time while frozen (`bo3_sim_before_update`) |
| The race audio manager (`sub_0013EF40`) | holds, cooldowns, ramps and fades a call | the shipped calls a second (`bo3_sim_legacy_calls`) |
| The HUD's update (`sub_000535E0`) | its elements count calls (the entrance's ~15 frames ...) | the shipped calls a second, each with the shipped step |
| The chase camera (`sub_0015E550`) | eases by factors to the power of n = round(60 t + 0.5): 120 easings a second at 1/60 s, 1 a step (stiffer) at 1/170 | the shipped easings a second, dithered (the 60 at `0x3F720C` set for the call) |
| Camera 0/14 (`sub_0015C130`, the look-back view) | turns 3 degrees a call in its mode 3 | the constant scaled for the call |
| Camera 13 (`sub_00159A80`) | eases 5% to its target a call | 1 - 0.95^(shipped/N) for the call |
| The players' cameras (`sub_00167940`, run n times after n steps by High refresh) | the record's clock + 0xC2C gains the frame's step count x one step a run | the step count reads 1 in each run (below) |

The look-back view too: the record's message handler (`sub_00168380`, at
record + 0x158) keeps at + 0x30 "look back asked for", which the look-back
event sets once a frame and each run's update clears after putting the
chase camera back when it is not set. With n runs a frame the second found
it cleared, so a held look back flipped between the two views in every
frame of two steps (seen with the boost held, which lowers the frame rate).
Each run now sees the frame's input. High refresh below 60 fps had it too.

The last one is High refresh's too: with n runs of n steps each, the clock
gained n x n steps, so the race intro's blend camera (camera 10, timed by
that clock) ended in half a second whenever frames carried more than one
step. At 60 steps a second that needs a frame rate under 60; at 170 it is
any frame with two steps.

## The front end and the loads

The front end runs at N too, with High refresh on there and no blend (a
picture is a step: the showroom's cars, which a blend puts in each other's
place, BO-241). The loads and the stage's set-up (game states 7 and 4, not
the running state 5) step once a frame by their own code; their frames are
held to the display's rate instead of the vblank, so a race's set-up
(`sub_001AA100`, a piece a frame) takes about 1.7 s at 170 Hz instead of
4.6 s.

**Loading screens** wraps that loader, for a race's set-up only (its stage
has a mode object, `+ 0x1B8`, from the start; the front end's set-up after a
race shows no loading screen, and holding it looked like a freeze): a "done"
that comes before the chosen time is answered 0, and the loader is not called again (it has
nothing left to do) until the time is up, then 1. The loading screen keeps
animating at the display's rate meanwhile.

## Limits and choices

- The three settings it needs are set in the process environment as well
  as the values (`bo3_sim_settings`): the settings are read again once the
  defaults are in (`xbox_SettingsFinish`), and set only as values they went
  back to the file's before the first frame (a profile without High refresh
  stood still at the attract screen). Their rows are locked, their hint
  saying Native frame rate holds them (the toolkit's `xbox_SettingLock`),
  and the values are put back before every Update.
- The timers are found as they are set up and kept in a list; one is
  rewritten only while its + 0x1C holds exactly the old step, so memory
  reused for something else is left alone unless it holds that very float.
- A timer's time is its whole steps (+ 0xC) times the step global, read
  at every tick (`sub_001B5AC0`), so changing the global (at a freeze, and
  when the setting goes on or off) would rescale every timer's time by
  N/60 at once. Its step count is rescaled with it, rounded up, so the time
  stays where it was. Without that the front end's clock (game timer
  `0x4AE1E0`, time `0x4AE200`; also the pads' idle clock) jumped ×2.8 in
  the pause menu: a race restarted from there opened its page "in the
  future", never took its intro down at the GO, later pages piled on it,
  and the game could crash in the front end some races later.
- A scene's own period (the front end asks for 1/30 s) is replaced by 1/N:
  High refresh decides the steps everywhere the short step is in force, so
  the stepper's count is not used there.
- The chase camera's 60 (`0x3F720C`) is read by that function only; the 3.0
  and 0.05 of cameras 0/14 and 13 are shared constants, changed for the
  call only and read by nothing those two call (checked three calls deep).

- The menus' own timing is real time: a held d-pad's repeat starts after
  0.23 s and repeats every 0.07 s with the setting on or off (the cursor,
  `0x550A98`, watched while Down was held).

- The headless scenes wait a number of flips between presses, written for
  60 flips a second. With the setting on a picture is a step, menus and
  loads included, so `scene_lib.sh` counts those waits for the run's rate:
  when the log has the "[SIMRATE] native frame rate" line, a wait of n
  flips (`press`, `press_until`, `wait_flips`, a `shot`'s offset, and
  `$(flips n)` in a driver's own `wait_flip`) is n x N / 60 flips, N from
  `RECOMP_FLIP_PACING=host:<Hz>`, and `cam_flips` counts as with 60 fps
  crash cameras (`SCENE_RATE=<Hz>` overrides). The reference drivers'
  hand-written waits go through `flips`; the analysis scenes (draw, fx,
  crash60, hraudit and the like) still count flips at 60. `to_race.sh`,
  host:170, the setting on, the default profile: the race the presses
  choose in 11 of 13 runs (one on a random track, a press on Select
  Options lost; one stood at car select after its A was taken), against
  1 of 4 with the old waits; with the setting off 4 of 4.

- A machine slower than its display: a frame takes as many short steps as
  the time asks, up to 4 of the title's steps' worth, rounded up (12 of
  1/170 s; it was rounded to 11, which slowed the game from 15.5 fps
  down). Headless at host:170, every frame held longer by a test probe,
  Single Event race, game seconds per second: 75, 60, 30 and 20 fps 1.00
  (0.98 to 1.00); 16.4-16.9 fps 1.00 (13.5-16 fps before the rounding:
  0.87 to 1.00). Under 15 fps the game slows as it does with High refresh
  as shipped (11.2 fps 0.75 there), a little more with the setting (9.5
  fps 0.67 with the same hold), since the frame then carries 12 steps of
  simulation in place of 4. A load steps once a frame, as shipped, so a
  load's seconds read slower at a low frame rate in either.

## Tested

Linux (AMD, Vulkan), a 170 Hz display, on screen: races, the race intro,
the chase camera in turns and after hits, the look-back view, HUD, crash
and takedown cameras, the pause and results menus, the signature
takedown's photos (against a video of the original: flashes, hold and
return within 0.1 s), menus, loads, and Loading screens at 5 and 10 s.
Headless at 170 (`RECOMP_FLIP_PACING=host:170`): full AI races within the
60 Hz spread of race times, full-throttle speed against game time within
0.5% of 60 for the first 6 s; full AI races at 120 and 240 too (382 and
403 s, against 389 to 409 s at 170: the AI's race time varies run to run); the PAL build into a race. On screen on the
same display set to 59.95, 120 and 144 Hz (2026-11-04), NTSC-U: a race,
its intro, the chase and look-back cameras, a crash, the pause menu and
the menus. At 59.95 Hz the short step never came on and High refresh
stayed off (the game as shipped); at 120 and 144 the step was 1/120 and
1/144 s. The 144 Hz run was on a loaded machine (load average 31),
mostly 47 to 110 fps, two to four steps a frame, and looked right. The
display has no VRR. On 0.3 (2026-11-05), headless at 170 with Local
network play: the short step on in the menus, off as the online menus
signed in (`[SIMRATE] off`, two of three timers put back), still off after
leaving them, and on again at the next race's load. Split screen, Race, at
170: the load (two players from its first frame) 2.4 s at the display's
rate against 6.6 s before, the step put back to 1/60 s on the race's first
frame, the race at 59.4 flips a second (60.0 before). Not tested: Windows,
macOS, Android; a VRR display; split screen on screen; the PAL builds on
screen; an online race between two copies; the locked row on screen.
