# Split screen: 60 fps and music (`graphics.split_60fps`, `audio.split_music`, BO-116)

Two-player split screen runs at 30 frames a second on the console, from the
start of the event to its end, and without the music a one-player race has.
Two settings change that, both off by default (the console's behaviour) on
every platform:

| Setting | Overlay | `settings.toml` | Environment |
|---|---|---|---|
| 60 fps in split screen | Game page, "60 fps split screen" | `[graphics] split_60fps` | `RECOMP_GFX_SPLIT_60FPS=1` |
| music in split screen | Game page, "Music in split screen" | `[audio] split_music` | `RECOMP_AUDIO_SPLIT_MUSIC=1` |

Both are opt-in guest patches (`config/patches.json`; toolkit
`docs/pipeline/04-lifting.md`, Guest patches) on the title's own choices; no
game code is replaced. A change shows from the next two-player event: the
frame rate is asked for once, when the stage is set up. No restart.

The companion for one player is `graphics.crash_60fps` (`docs/crash60.md`),
which this builds on. The names stay apart on purpose: one is about the
crash cameras of a one-player race, the other about a whole mode with twice
the drawing; a player may want one and not the other.

## The title's two-player modes

Main menu > Multiplayer: Race, Road Rage, Team Crush, Double Impact, Party
Crash. As logged (`BO3_PACE_LOG`: player count `0x73A1C0`, the stage's mode
object):

| Mode | Players at once | Stage | Mode object | As shipped |
|---|---|---|---|---|
| Race | 2 | two-player | `0x4D4E54` | 30 throughout, no music |
| Road Rage | 2 | two-player | `0x4D4EA0` | 30 throughout, no music |
| Team Crush | 2 | two-player | `0x4D4CF4` | 30 throughout; no in-play music in a crash mode |
| Double Impact | 2 | two-player | `0x4D4DFC` | 30 throughout; no in-play music |
| Party Crash | 1 (the pad is passed) | one-player | `0x4D4D4C` | 60, 30 in the crash camera (`graphics.crash_60fps`); no in-play music |

## 60 fps

The half rate is the same mechanism as the crash cameras' (`docs/crash60.md`):
a request byte in the game object (`0x4D538D`), put in force once a frame by
`sub_00016EF0` (presentation interval 2, two simulation steps a frame). In a
one-player stage the update asks for it while a crash camera is on. The
two-player stage (vtable `0x3A939C`) asks once, at the end of its set-up
`sub_000209A0`:

    0x00020AAA  mov byte ptr [0x4D538D], 1     ; the half rate
    0x00020AB1  mov dword ptr [0x4D5390], 0    ; stepper mode 0: the minimum every frame

and its update (`sub_00020AC0`) never looks at the crash camera bytes, so
nothing asks again until the stage is released. Crash cameras exist in split
screen, one per view (the players' bytes `0x73BACA` and `0x73E2AA`), but they
change nothing about the rate, and they have no slow motion (the world
timer's divisor stays 1).

The patch is one site, a new kind (`store_u8`, toolkit
`tools/recomp/patches.py`): the byte stored at `0x00020AAA` is stored again
as 0 while the setting is on. The half rate is then never put in force:
interval 1 and one step a frame (mode 0) for the whole stage. Unlike the
load and compare patches this one writes the title's memory, the one byte,
with the value the front end and the one-player stages store there.

### Game speed

A step is 1/60 s of game time either way. Two-player Race, USA, the first
track, four rivals, both pads driving (`tools/headless/split_scene.sh`,
`pace_report.py`; Metal, headless, 4:3, render scale 1, the game at nice 5):

| run | switch | flips/s | steps/s | steps per flip | flip interval p50 / p95 / p99 / max (ms) | over 20 ms |
|---|---|---|---|---|---|---|
| s_off | off | 30.00 | 60.00 | 2 | 33.3 / 36.3 / 40.2 / 59 | - |
| s_apu | off | 30.00 | 60.00 | 2 | 33.2 / 36.0 / 37.9 / 58 | - |
| fin (final build, both switches by settings.toml) | on | 59.98 | 59.98 | 1 | 16.3 / 19.6 / 20.6 / 28 | 2.8 % |
| s_mus (with the APU and pass traces) | on | 59.61 | 59.61 | 1 | 16.5 / 19.7 / 21.8 / 325 | 3.5 % |
| m_rr (Road Rage) | on | 59.71 | 59.71 | 1 | 16.6 / 19.2 / 22.4 / 41 | 2.2 % |
| m_tc (Team Crush) | on | 59.98 | 59.98 | 1 | 16.7 / 19.5 / 20.6 / 41 | 1.9 % |
| m_di (Double Impact) | on | 59.87 | 59.87 | 1 | 16.7 / 19.6 / 21.2 / 66 | 3.1 % |
| sq_on (load average 6 to 7) | on | 59.05 | 59.05 | 1 | 16.7 / 20.1 / 24.7 / 84 | 5.2 % |
| sq2 (another game running, load average 8, stalls) | on | 53.26 | 53.26 | 1 | 16.8 / 24.5 / 44.6 / 743 | 15.3 % |

- One step a flip with the switch, two without; where the host held 60 the
  game ran at 60 steps a second, as it does at 30.
- The crash cameras in split screen last 180, 240 or 300 world ticks (3, 4,
  5 s) both ways: 302, 240, 240, 300, 318, 182 without the switch in one
  race, 181, 301, 181, 180, 301, 234, 181 with it.
- The countdown, the race clock and the rivals are stepped with the
  simulation. Not compared number by number: no two runs drive the same
  race.
- **A late frame costs game time.** The stage runs in stepper mode 0: every
  frame is one step, so a frame that misses its vertical blank is a 60th of
  a second the game does not make up. At 30 the title has the same rule with
  twice the time for a frame. sq2 shows it: with the host stalling, 53.3
  steps a second, the game 11 % slow. The Mac was never quiet in these runs
  (other agents' builds and games); the clean ones are the upper rows.

### For strong machines only

**Where 60 fps cannot be held, the game itself runs slower while the switch
is on, not just the picture. Leave it off on a Steam Deck and other
handhelds.** The overlay's row says the same. The Steam Deck does not hold
60 in a one-player Crash event with `graphics.crash_60fps` on (56.5 and 52.4
steps a second, the game 6 to 13 % slow, p95 23 to 24 ms, 7.7 W against
5.1 W; 60.0 steps a second with it off; `docs/crash60.md`), and split screen
draws about twice as much (below).

### The cost

`RECOMP_TRACE=metal_frame`, per flip, in the race:

| | render passes | draws, median / 95 % / most | indices, median / most |
|---|---|---|---|
| one player (Race, same track) | 22 (24 at most) | 1028 / 1314 / 1367 | 324 k / 461 k |
| split screen | 40 (42) | about 1900 to 2000 / 2350 / 2470 | 570 k to 636 k / 795 k |

Split screen is very nearly two one-player frames (two views, each with its
reflection, bloom and blur passes), and the switch asks for them in half the
time: four times the work per second of a one-player race at 30. On this Mac
that fits. Split screen itself was not measured on the Deck or on Android. The Deck
holds a one-player race at 60 (0.60 % of frames over 20 ms in a 19-minute
session, `docs/windows.md`, BO-83) but not a crash event's camera at 60
(above), and has not been measured with twice the draws; a Retroid Pocket 5 runs a busy one-player race at 47
to 58 fps (`docs/android.md`) and cannot be expected to hold 60 with two
views: there the switch would slow the game down. Default off everywhere.

### What it looks like

Both views, their HUDs (position, lap, speed, takedowns), the "CRASHED!"
cameras per view, Road Rage's takedown captions, Team Crush's and Double
Impact's junctions and their crash cameras, the results and the autosave
message were looked at in frames of the runs above with the switch on;
nothing was wrong. As with the crash cameras, the HUD's animations run at
their one-player pace, twice what they had at 30 (`docs/crash60.md`).

## Music

The title does not mute the music in split screen and has not forgotten to
start it: it tests the player count. The music manager (`0x4115E8`, updated
once a frame by `sub_00153E50`; the map is in `docs/custom-music.md`) has an
outer state (`+0x5F8`) and an in-game state (`+0x5F4`):

- `sub_00154800`, the outer state 4 (a stage is starting): at `0x0015480D`,
  `cmp dword ptr [0x73A1C0], 1` / `jg`: with more than one player the
  in-game state is forced to 0xD, "no music". The same branch is taken for
  an Xbox Live stage (`0x5A3759`), a network session (`0x463AF4`) and the
  manager's own mute (`+0x62C`).
- `sub_00154A70`, the outer state 5 (in the stage): at its entry the same
  compare; with more than one player it stops the music (`sub_00153310`)
  and returns, every frame.

Why is not in the code. It is a deliberate rule, the same on the PS2, and
split screen already plays two engines and two sets of effects; the consoles'
disc and CPU budget is the likely reason. The crash modes (outer state 6)
have no in-play music at all, for one player either.

The patch is the two compares, a new kind (`cmp_u32`): with the setting on
they read 1, and the manager runs the states it runs for one player. Nothing
else is touched: the song comes through the title's own player and stream,
at the music volume of the title's Audio settings, with its own ducking in
a crash camera (the in-game state goes 2, 9, 1, 2 around one, as in a
one-player race). EA GAMES TRAX and a custom soundtrack
(`docs/custom-music.md`) are the same to it.

Skipping a song (Black) is the title's: `sub_00153E50` asks
`sub_00018280`, which looks at all four pads, so either player can.

### With the setting off

The audio path is the title's own, by construction: the generated code
differs from the integration build's in four statements, each a call that
returns the value the title read unless its setting is on,

    recomp_0000.c  MEM8(0x4D538D) = (uint8_t)RECOMP_PATCH_U32("graphics.split_60fps", "1", 0x0u, MEM8(0x4D538D));   (twice: the set-up is lifted in two bodies)
    recomp_0003.c  _fa = RECOMP_PATCH_U32("audio.split_music", "1", 0x1u, _fa); _fas = (int32_t)(_fa);             (0x0015480D, 0x00154A70)

and with those four lines taken out the generated tree hashes to the
integration build's (`gen.sh`'s tree hash, `526febbc...` at the time). No
file of the audio runtime is changed. As logged: without the setting the
in-game state in a two-player race is 0 and the EA GAMES TRAX wave bank is
opened once (the menu's); with it the state is 2 and the bank is opened
again in the race.

### Voices

`RECOMP_TRACE=apu`, the same two-player race (`tools/headless/
voices_report.py`: the voice processor's active voices, sampled every 60 of
its frames):

| run | active voices: least / median / most (of 256) | 3D voices (of 64) | buffer voices that sounded | stream voices |
|---|---|---|---|---|
| s_apu, as shipped | 4 / 46 / 50 | 0 | 44 | none |
| s_mus, music on (and 60 fps) | 4 / 47 / 51 | 0 | 44 | one, stereo (voice 70), sounding in 81 reports, RMS up to 10 812 |

One voice more, the music's stream; the same 44 buffer voices sounded in
both (engines, tyres, crashes, the HUD's), the loudest as loud. There is no
starvation to speak of: 51 of 256.

A custom soundtrack was run once by hand (three test tones as a folder,
chosen in Driver Details > Settings > EA GAMES TRAX, then Multiplayer >
Race with both switches): the first song played from the start of the race
(`[SOUNDTRACK] song 00010000`), Black on the second pad skipped to the
second, and the third followed.

## Not verified

- Two real controllers, and the two overlay rows on screen: every run was
  headless with scripted pads (the rows' settings are covered by the
  toolkit's `overlay_settings` test; the final run took both settings from
  `settings.toml`).
- Audio by ear: the music was measured (a sounding stereo stream voice,
  voice counts), not listened to; its balance against two engines is the
  title's music volume.
- Turning either switch on in the middle of a two-player event (the rows
  say "from the next two-player event").
- The Deck, Windows, Android, Vulkan, 16:9, ultrawide, render scales above
  1, side-by-side split (the title's other layout).
- Frame time on a quiet Mac: the machine was shared throughout.
- A takedown camera in split screen was not identified in the frames (Road
  Rage counted a takedown; the cameras seen were crash cameras).

## To be confirmed

- The names and texts of the two rows.
- Whether split screen at 60 should step by the clock (stepper mode 1) so
  that a late frame does not slow the game: it would be one more stored
  value at `0x00020AB1`, and a step away from "as the title does it".
- Music in the two-player crash modes: the title has none in play there for
  one player either, so the switch leaves them silent.
