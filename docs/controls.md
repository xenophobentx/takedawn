# Driving controls: Xbox layout or PS2 layout

`input.control_scheme` (overlay: Input > Driving controls; settings.toml;
`RECOMP_INPUT_CONTROL_SCHEME=xbox|ps2`) chooses the controls used while
driving. `xbox`, the default, is the game as shipped. `ps2` is the
PlayStation 2 version's layout. Menus are not changed by it: the front end,
the garage, the pause menu, a crash event's captions, the results and the
overlay read the pad as they always do. A change in the overlay holds from
the next frame.

## The two layouts

From the two NTSC-U manuals ("Complete Controls"), and for the Xbox column
also from the title itself (below).

| Action | Xbox (as shipped) | PS2 | PS2 layout on an Xbox-shaped pad |
|---|---|---|---|
| Accelerate | Right trigger | Cross, or right stick up | bottom face button (A), or right stick up |
| Brake / reverse (when stopped) | Left trigger | Square, or right stick down | left face button (X), or right stick down |
| Steering | Left stick | Left stick | left stick |
| Look back | X | L1 | left shoulder (LB) |
| Change view | Y | Triangle | top face button (Y) |
| Boost | A | R1 | right shoulder (RB) |
| Impact time (while crashing) | hold A | hold R1 | hold right shoulder (RB) |
| Aftertouch (while crashing) | Left stick | Left stick | left stick |
| Crashbreaker (crash events) | B | R2 | right trigger |
| Next music track | Black (RB on a modern pad) | L2 | left trigger |
| Pause | Start | Start | Start / Menu |
| (nothing) | White (LB), right stick | Circle | right face button (B) |

Neither version has a horn. The Xbox manual does not list the music track
button; in a race Black brings up the EA Trax banner with the next song (and the
log shows the music bank opened again); White opened nothing. The Xbox version does not read the right stick
for the pedals (a race with the stick held up or down only coasts), so in the
PS2 layout the host turns the stick's travel into the triggers.

Sources: Xbox manual, pages 2 and 4,
<https://archive.org/details/Burnout_3_Takedown_2004_Electronic_Arts>; PS2
manual, pages 3 and 7,
<https://archive.org/details/ps2_Burnout_3-_Takedown_USA>.

### Analog and digital

- On the original Xbox pad the triggers and all six face buttons are
  pressure-sensitive; on the DualShock 2 so are Cross and Square, which is
  how the PS2 version has an analog throttle on a button. A modern pad's face
  buttons and shoulders are switches. So in the PS2 layout the two face-button
  pedals are all or nothing (the title is given the trigger at 255), and the
  analog throttle and brake are on the right stick, as on the PS2: the first
  quarter of its travel reads as nothing, the rest runs from 1 to 255.
- The triggers stay analog on the pad but drive two switches here
  (crashbreaker, next track): pressed from a pull of more than 30 of 255, and
  the title is given the button at 255.
- What the PS2 layout gives up against the Xbox one: the analog pedals under
  the index fingers.

## The keyboard

The keyboard is a pad for player 1 (the toolkit's `src/input/keyboard.c`, on
by default on Mac, Windows and Linux; `RECOMP_KEYBOARD=0` turns it off, and on
Android it is off unless `RECOMP_KEYBOARD=1`). The map below is the default;
the player can change it (next section). It always has the Xbox layout: while a key is held the PS2 layout is not applied, so these
are the Xbox buttons whatever Driving controls says. A controller works beside
it: buttons are ORed, a trigger is the larger of the two, and a held key sets
its stick axis.

| Key | Xbox pad |
|---|---|
| W or Up | Right trigger (accelerate) |
| S or Down | Left trigger (brake, reverse) |
| A or Left | Left stick left (steer) |
| D or Right | Left stick right (steer) |
| Up, Down, Left, Right | D-pad |
| Space | A (boost, select) |
| Backspace | B (Crashbreaker, back) |
| Shift | X (look back) |
| Ctrl | Y (change view) |
| Q | White |
| E | Black (next music track) |
| Enter | Start (pause, select) |
| Esc | Back; Start while driving (pause) |

### Changing the keys (BO-267, GitHub #57)

Settings > Controls has a row for each of the twelve buttons below the arrows
(Start, Back, A, B, X, Y, White, Black, accelerate, brake, steer left and
right), each with two keys. Enter on a row waits for a key; Left and Right pick
which of the two; Delete empties it; Esc or B cancels. A key does one thing:
one that another button has is refused and the row keeps waiting. The four
arrows stay the d-pad of the menus, and may also be keys of a button, as they
are for driving by default. "Reset keyboard keys to the defaults" (and "Reset
this page") restore the table below.

What can be bound: letters, digits, the arrows, Space, Enter, Esc, Backspace,
Shift (either), Ctrl (either), Tab, Page Up and Down, Home, End, Insert and
the punctuation keys. F1, F7, Alt, Delete and the keypad are not.

They are saved in `settings.toml` as text of the buttons that differ from the
table, `[input] keys = "boost=Shift;look_back=Space"`, with the names `start`,
`back`, `boost`, `crashbreaker`, `look_back`, `change_view`, `white`, `black`,
`accelerate`, `brake`, `steer_left`, `steer_right`; `RECOMP_KEYBOARD_KEYS` takes
the same text and wins over the file. A name or key it does not know, and a
key it would give to a second button, is left out. The pause on Esc while
driving follows the Back button's key.

The button prompts follow the keys (BO-267). In the keyboard prompt set
(`input.glyphs`, assets/glyphs/keyboard) a button whose first key is not its
default shows a key cap of that key: the letter or digit, or a short word
(TAB, ESC, PGUP, HOME, END, INS, CTRL, F2, NUM4), a symbol for Space, Backspace,
Shift, Enter and the arrows, and the punctuation keys as themselves. A long
word is made smaller to fit. Only the first key of a button is shown (the
second when the first is empty). The cap is drawn at run time by
`src/game/keycap.c` in the style of the baked ones (same outline, face, lip,
highlight and stroke alphabet: `tools/glyphs/draw.py` and
`tools/glyphs/gen_keyfont.py` make its table) over a copy of the baked picture,
for the cells that changed (`src/game/glyphs.c`), and the textures are sent to
the GPU again when a key is changed in the menu. With the default keys nothing
is drawn: the baked pixels are used as they are. `keycap_scene.sh`
(WINDOW=1, CAPTURE=1) takes the prompt with the default key, with Boost on Z,
and with Space given back; the first and the last are the same pixels.

Esc pauses a race, like in a PC game. While you drive (the same test as the
PS2 layout's, below) a press of Esc is Start for as long as it is held, so the
pause menu is not closed by the press that opened it. Everywhere else, the
pause menu included, Esc is Back as before. An Esc that was already down when
the race began (held through the countdown) is still a Back. Enter pauses as
well. Neither manual gives Back a use in a race, so nothing is lost, and a
controller's Back is not touched (Back and Start together still open the
settings on a pad, F1 on a keyboard).

The settings change pages with Q and E (like the bumpers), Tab and Shift+Tab, or
Page Up and Page Down, which a Mac keyboard does not have. Cmd, Ctrl or Alt
with Q or E is left to the system.

The arrows are the D-pad and also steer, accelerate and brake. The menus take
the D-pad and the stick as one direction, so an arrow is one step in a menu
(checked: one Right arrow moves Region by one place, and a
100 ms Down moves one line; a key held for about 300 ms repeats, as a held pad
does). F1, and Cmd and comma on a Mac, are the settings'; while they are open the
title sees no keys, and the keys held when they close stay unseen until let go.

### Keyboard and controller as two players

By default the keyboard and the first controller both drive player 1. With
Controls > First controller at Player 2 (`input.first_pad = "player2"`,
`RECOMP_INPUT_FIRST_PAD=player2`) the keyboard is player 1 alone and the
first controller to press a button is player 2, so two people can play
split screen with one keyboard and one controller. Needs a toolkit with
`input.first_pad`.
The PS2 driving layout follows the same split: in the two-player stage that
controller goes by player 2's event, and in the one-player stage and Party Crash
it goes by player 1's, so a lone controller keeps the PS2 layout there.

## Touch (Android)

Without a controller the Android build shows on-screen controls (the
toolkit's `src/input/touch_pad.c`). They follow the game. In a race: a stick
on the left half that starts where the thumb lands, and GAS, BRAKE, BOOST,
BREAKER (Crashbreaker), LOOK (look back) and VIEW on the right. In menus and
the pause menu: a d-pad, A B X Y, LT and RT. Pause and the settings menu sit
at the top, left of the score board. BOOST keeps the accelerator down, so
one thumb can boost, and a finger can slide between GAS and BOOST without
lifting. The touch controls always use the Xbox layout, like the keys.

The controls are placed on the screen, not on the picture: aspect, fit,
ultrawide and render scale do not move them. They keep clear of the HUD's
corners (POS, LAP, the speedometer, the boost bar) and of the score board.
On a tablet they stay the size they have on a phone.

Settings, on the Controls page of the settings menu: Touch controls (Auto hides
them while a controller is used and shows them at the next touch; Always;
Off), their opacity and size, and Accelerate automatically. The settings
menu itself works by touch: drag a list to scroll it, tap a row to change
it, drag or tap a slider.

## Back and the settings

Because Back + Start opens the settings, the game sees Back a moment late: a
tap arrives when you let go, a hold after a quarter of a second (View and Menu
on a Steam Deck). Start alone is not delayed.

## The title has no layout choice of its own

This was checked first, because switching a layout would have been better than rewriting
the pad:

- Its Controllers screen (pause menu > Controllers) has one line per
  controller: Vibration on / off. The string table (Data/Globalus.bin) has
  CHANGE CONTROLLER SETTINGS, CONTROLLERS, CONTROLLER 1 to 4 and Vibration
  on / off, and no name for a layout.
- It reads its actions through one table, 31 entries of channel numbers at
  0x384168 (.rdata), whose only reader is sub_00019A10: accelerate is the
  right trigger's channel, brake the left's, and so on. There is no second
  table and no index that selects one.

So the PS2 layout is a rewrite, by the host, of the pad state the title has
just read.

## How "only while driving" works

The title reads its pads in one place: sub_001B57E0, once a frame, which
holds the title's only `XInputGetState` call. The host runs right after that
call, on the title's own thread (the `ctl_pad` mark in `config/patches.json`;
`src/game/controls.c`), before the title turns the bytes into the channels
that every menu and every car reads. Whether the title is driving is read
from its memory at that same moment, so the layout follows the title's state
on the same frame.

Driving is all of:

| What | Where | Evidence |
|---|---|---|
| The game module is the current one, and is not being left | `[0x4D5370]` is the object with vtable 0x3A9ACC, or 0x3A939C in the two-player stage (the front end's is 0x3A9E7C); `[0x4D53B4]` is not 6 | the trace: the front-end vtable through boot, menus, garage, the training video and the loading screen; 6 from "Quit > Yes" until the module changes |
| Nobody has paused | `[0x4D537C]` is -1 (else the pausing player) | the stage update tests it before it looks at Start (sub_00026050 at 0x26609); 0 on the frame after Start, -1 on the frame Resume takes effect |
| The title takes a pause now | byte `[0x752D33]` is 0 | tested in the same place; set while the event is left after the results |
| A crash event's fly-by and captions are not up | byte `[0x4D0767]` is 0 | 1 through the fly-by, and from the captions until A leaves them |
| The results are not up | not both: bit `0x2000` (the results camera) of `[0x4D0740]`, and byte `[0x4D0738]` | the title reads the byte the same way (`sub_00167DC0`): the byte is 1 once that camera reaches the end of its path, and only the camera's start clears it, so it is stale once the camera is gone |
| The event is in play, or counting down | `[0x73B51C]` is 2, or below 2 with the countdown's length at `[0x73B520]` set (not -1.0, not 0) | 2 from the GO; 3 when the event ends for the player; the length (4.5 s in a race, 3 s in a crash event) is written when the countdown begins |

In the two-player stage the last three rows are each player's own, and a pad
goes by its player's: the title's port 2 is player 1's, port 3 player 2's.
The race records (results, intro) are `0xC50` apart (two of them, built in a
loop of two in the game object's constructor, at `0x4CFB20`; the title's own
`sub_00167DC0` and `sub_00167DF0` read the results bytes `0x4D0738` and
`0x4D1388` and the intro bytes `0x4D0767` and `0x4D13B7`), the event records
(phase, countdown) `0x27E0` apart (`sub_00025050` steps both in one loop over
the players). A player whose event is over is back on the menu layout while
the other still drives.

### Why the results byte needs its camera

A one-player event's end (the stage update at `0x2674C`, `sub_00167EC0`) posts
event 12 to the player's state machine. Only the wrecked-car state (`0x73BF9888`)
acts on it, and starts camera state 13 (the object at record + `0xBD0`, vtable
`0x3A93BC`). Its enter, `sub_00159750`, is the one place that clears the byte,
and its update, `sub_00159A80`, the one place that sets it. The score screen
(`sub_00072CC0`) waits for the byte or for a press.

The byte outlives the camera. In a Trailer Trash run the byte went to 1 245
flips after the finish, and the replay's camera took over 9 flips later, with
the results still up. Quitting the event and starting the next leave the byte
at 1. The title does not mind, because it reads the byte only with the state
bit set (`sub_00167DC0`). A gate that read the byte alone took every later
event for a results screen, Xbox layout from the first frame, until a press
skipped the camera at the end of a later event (GitHub #52, #111, #113).

Over the results the gate is off without the byte. Phase 3 holds until the
replay sets it back; the countdown then reads -1 until `[0x752D33]` is set, on
the same flip the countdown's length comes back. The replay's loop (phase 1,
2, 3 and round again) stays behind that byte.

The countdown counts as driving because the boost start is on the
accelerator. A crash counts as driving: the impact time, the aftertouch and
the crashbreaker are driving controls.

### Handing over to a menu

A button that is down when driving ends would otherwise turn into a press
nobody made: the bottom button, held to accelerate, is A in the pause menu,
and A resumes. So a button that is down at the hand-over, and that the title
did not see down under the PS2 layout, reads released until it is let go;
the next press of it is the menu's at once. The title's menus act on presses,
not on held buttons (with the Xbox layout, A held into a pause does not
resume either), so nothing else is needed. Going back to driving nothing is
held back: the button that confirmed "Resume" may stay down and accelerate.

## What it does not touch

- Scripted input. A pad driven by `RECOMP_PAD_SCRIPT` / `RECOMP_PAD_LIVE`
  keeps the Xbox layout whatever the setting is: `rt` in a test, in the scene
  drivers or in the shader pre-build's race accelerates on every machine.
  (`BO3_CTL_SCRIPTED=1` makes a script stand in for a controller, for
  `tools/headless/controls_scene.sh`.) Once a controller takes its pad back
  from a script, the setting applies to it.
- The keyboard: while a key is held the pad state is left as it is, so
  the keys keep their documented Xbox buttons (see below).
- Rumble: the title's motor speeds do not pass through this.
- Replays: a replay is started from the results screen, where the layout
  is already off, and it stays off (the results byte). Not checked: whether a replay reads
  the pad for anything.
- The overlay: while it is open the title reads a neutral pad anyway.
- Button prompts: they show the game's own button, not the PS2 layout's.
  In Crash mode the Crashbreaker prompt shows B (or Circle), while the PS2
  layout fires it with R2 / the right trigger. BO-147 tracks prompts that
  follow the layout while driving.

## With the Nintendo face layout

`input.face_layout = nintendo` is applied first, as the host pad is read: the
button printed A is the title's A, in menus and in the Xbox driving layout.
The PS2 layout is applied after it and goes by position, not by the letter:
the bottom button accelerates, the left one brakes and the top one changes
the view on any pad, as the thumb finds them on a DualShock; the right-hand
button does nothing while driving. In menus the face layout alone holds: the
right-hand button, printed A, confirms.

The button prompts follow it (`input.glyphs`, `assets/glyphs`): a prompt shows
the button that triggers the action, as the chosen pad prints it. With the
Nintendo layout the title's A is the right-hand button, so the Xbox and Steam
sets show B where the game says A, the PlayStation sets the circle, Positional
the right-hand dot, and the Nintendo set shows A. The table is in the
toolkit's `src/input/pad_glyphs.h`. The settings menu follows the same layout
(its A selects, its B goes back) and names the buttons the same way.

A pad script standing in for a controller (`BO3_CTL_SCRIPTED=1`) gets the
face layout too, applied by `controls.c`, because a script's buttons are
written into the report after the host pad is read and no face layout reaches
them otherwise. Its `a` is then the bottom button and its `b` the right-hand
one, as SDL names them on any pad. (Before this the scene failed with both
settings on, 20 of 25, on the Retroid Pocket 5 and on the Mac alike: the
script's `a` was taken for the right-hand button. A controller was never
affected.) A script that is not a stand-in gets neither setting.

## Tests

- `tests/controls` (`bo3_controls_test`, `ctest -R bo3_controls`): every row
  of the table, the right stick, the Nintendo layout under it, the hand-over,
  and the whole chain from a host pad's buttons (SDL and XInput) through the
  toolkit's face layout and map to what the title reads, for both face
  layouts, driving and in a menu.
- The toolkit's `input_map` (the setting's plumbing, controller against
  keyboard) and `overlay_settings` (the setting itself).
- `tools/headless/controls_all.sh <rundir-prefix>` runs the default set of
  the scene below: ps2 race, xbox race, ps2 crash, and ps2 race with
  `FACE=nintendo`.
- `tools/headless/controls_split_scene.sh <rundir> race|roadrage|teamcrush|doubleimpact|partycrash`:
  the same for the two-player modes (both pads), checked by `controls_split_check.py`.
- `tools/headless/controls_stale_scene.sh <rundir>`: Crash with the results camera left to
  arrive, then Road Rage and Race in one process, with the results byte still 1: both must
  have the PS2 layout (`controls_stale_check.py`).
- `tools/headless/controls_scene.sh <rundir>` with `SCHEME=ps2|xbox`,
  `EVENT=race|crash` and `FACE=xbox|nintendo`: a scripted run through boot, the menus, the garage,
  loading, the countdown, driving, a pause with the accelerator still held,
  Resume, a second pause, Quit to the front end; or a crash event's captions,
  crash, results and Continue. It asserts on `BO3_CTL_LOG` (the title's state,
  the gate, each pad change as read and as given) and `BO3_DRIVE_LOG` (the
  speed).

## Not verified

- A controller in the hand: every run above is a pad script standing in for
  one. The built-in pads of the Steam Deck and the Retroid Pocket, Windows
  (native XInput and SDL) and Linux were not run.
- Two players: run headless on Linux (Vulkan) with two scripted pads standing in for
  controllers, `tools/headless/controls_split_scene.sh <rundir> <mode>`: Race, Road Rage,
  Team Crush and Double Impact give both pads the PS2 layout from the countdown, and in
  Team Crush and Double Impact each pad goes back to the menu layout when its own
  player's event ends, while the other still drives. Party Crash plays on the first
  player's records and the first pad throughout (it is the one-player stage), so it
  goes by player 1 like a one-player event. Not run: keyboard plus controller
  (Controls > First controller), a pause by player 2, real controllers, Windows, Android.
  Also reported working in split screen on macOS with two controllers.
- A race to its finish line, Road Rage, Burning Lap and online: the end of an
  event was traced in a crash event and by quitting a race.
- The crashbreaker itself was not triggered in a run (it needs the wrecks);
  the right trigger was seen to reach the title as B.
