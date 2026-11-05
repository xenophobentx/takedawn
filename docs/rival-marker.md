# The marker over rivals: arrow or race position (`display.rival_marker`, BO-241)

Overlay Game page, "Rival marker switch"; `settings.toml` `[display]
rival_marker = "original" | "farther"`; `RECOMP_GFX_RIVAL_MARKER`. Original
by default on every platform.

In a race the title draws a marker over each rival it shows: a small yellow
arrow far away, the rival's race position ("4th") near. This setting moves
the point where it changes look and gives it a band, so the change happens
earlier and does not flip back and forth.

| | Arrow to position | Position to arrow | Checked |
|---|---|---|---|
| Original (as shipped) | depth below 35 | depth above 35 | every 30 steps (twice a second) |
| Farther | depth below 60 | depth above 75 | every step |

Depth is the rival's distance along the camera's view axis, in world units
(metres: a car at 104 mph moves 0.77 units a step).

## How the title does it

`sub_0018F060` (PS2 `CB3RacePosition::DrawPositionMarker`) draws one marker.
`sub_0018EA50`, from the end of the stage's render (`sub_001AE340`), calls
it for every rival with the racer's matrix + 0x30 (racer table `0x64B38C`,
+0x204), lifted by the car's height. The point is projected with the view's
view matrix (`MEM32(0x4D6520)`, `0x4D6200` in a race); the marker is 2D,
drawn at the projected point, its size from the depth.

The look is a slot of the marker object `0x735550` (the race world
`0x60EA00` + 0x126B50): `+ i * 4`, 1 the position, 0 the arrow, all 1 at
the start of a stage (`sub_0018E820`). Slot `i` is the rival's distance rank
from the view's camera, counted down: 0 is the farthest
(`sub_0018EA50` sorts the racers each picture), so a look belongs to a
distance rank, not to a car. In `sub_0018F060`:

| Address | What |
|---|---|
| `0x18F207` | the slot's look tested |
| `0x18F21E` | `movss xmm1, [0x3B175C]`: 35.0; arrow to position when the depth is below it |
| `0x18F230` | `cmp [ebx + 0x18], 1`: only while the object's counter is 1 |
| `0x18F244` | `comiss xmm0, [0x3B175C]`: position to arrow when the depth is above 35.0 |
| `0x18F252` | the same counter test for the way back |

The counter (+0x18) runs 0 to 29, advanced once a step by `sub_0018E890`
(the stage's update, `sub_001AA720` at `0x1AA82D`), which also ranks the
race positions when it is 0. So the title looks at the distance twice a
second; a rival you close on at 20 m/s can be 10 m nearer than 35 when the
look changes. No band: a rival hovering about 35 can change look every half
second. The marker object's mode (+0x20, from the stage's mode object)
overrides the look: 1 always the arrow; 2 (a race) the arrow while the car's
+0x134C is 3.

The rest of `sub_0018F060`, unchanged: the position's size grows with
`((z - 1) * 0.0135 + 1) / z`, at most 320 (`0x396EB0`), the text at least
20 (`0x3A7950`); the marker fades past 175 (`0x3B1CCC`; 100, `0x3A2928`,
with `0x5A3759` set) by 0.04 a unit and is not drawn past `0x60E560` (200 in
a race; `0x60E540`, 125, with `0x5A3759`); near the camera it fades below
2.7 (`0x3A3224`) and is not drawn below 2.0.

## The patches

Four, `config/patches.json`, all at `display.rival_marker = farther`:

- `0x18F21E` (`load_f32`): the distance loaded is `rival_near` (60).
- `0x18F244` (`cmp_f32`, the toolkit's compare of a float in memory): the
  compare's operand is `rival_far` (75). The constant at `0x3B175C` is left
  alone for its other readers.
- `0x18F230`, `0x18F252` (`cmp_u32`): the counter reads 1, so the distance
  is checked every step (and in every picture with High refresh).

`rival_near` and `rival_far` are host variables (`src/game/main.c`,
`RIVAL_NEAR`, `RIVAL_FAR`); `BO3_RIVAL_SWITCH=<near>,<far>` sets others for
a test. At `original` the four calls return what the title loaded, so the
title runs as shipped.

Why 60 and 75: the change moves to where the rival is small on screen. The
position's text has a least size in the title (20, `0x3A7950`), so it does
not shrink with the distance the way the car does; in the dumps the "4th"
over a rival at 75 is about as big as over one at 30. At 60 the change is no
longer a big text appearing over a car that is already close. Checked every step, the change is no longer up to half a second
late. The 15-unit band is about a second of closing speed in a pack, so a
rival running alongside at that distance keeps its look. Past 75 the marker
is the arrow as before, up to where the title stops drawing it (200).

Because the look belongs to a distance rank, two rivals that swap ranks swap
looks. Outside the band each one's look is set again in the same call that
draws it, so neither shows the other's look; inside the band (60 to 75) a
rival can take the other's look until it leaves the band. The title has the
same, with one distance and twice a second.

In split screen both views share the object and its slots
(`sub_001AE2F0` draws view 0, then view 1). As shipped, the two views'
checks fall in the same frame twice a second and the later one wins. With
`farther` each view sets the look from its own depth before it draws, every
step: a rival near in one view and far in the other shows its position in
one and its arrow in the other, steadily.

## Measured

Mac, Metal, headless, out-of-box settings, Single Event race at Silver Lake
with the autopilot, about 70 s of racing a run, the probe
(`BO3_HIREFRESH_MARKS`, src/game/hirefresh.c) logging each picture's slots
and every racer's point; a rival's look from its distance rank. Changes of a
rival's look with its slot kept (the depth there), and changes back within
30 pictures:

| Run | Arrow to position at | Position to arrow at | Back within 30 pictures |
|---|---|---|---|
| Original, 60 | 33.6, 33.7, 34.0 | 36.8 to 46.1 (and 76.6, 96.7, 148.6 as rivals left) | 7 |
| Original, 90 (High refresh) | 31.0 to 34.5 | 35.3 to 46.1 | 28 |
| Farther, 60 | 57.9, 58.5, 59.8 | 75.2 to 78.8 | 0 |
| Farther, 90 (High refresh) | 58.6 to 59.6 | (rivals leaving past 130) | 0 |

As shipped the changes come up to half a second after the distance passes 35,
and most of the quick changes back are rank swaps: a rival takes the look
of the car it swapped distance ranks with. With High refresh that becomes a
flicker: a rival shown and a car behind the camera at about the same
distance swap ranks every step (the one drawn is blended, the one not drawn
stays at its step), and the shown rival's marker alternates between the
arrow and its position from one picture to the next (Original 90, pictures
4249 to 4263). With `farther` the look is set from the rival's own depth in
the call that draws it, so a swap outside the band changes nothing.
