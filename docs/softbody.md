# Soft-body damage (`graphics.softbody`)

Overlay Graphics page, "Soft-body damage"; `settings.toml` `[graphics]
softbody`; `RECOMP_GFX_SOFTBODY=1`. Off by default; read at start, so a
change applies after a restart.

It replaces the look of a vehicle's damage with a soft-body model: every
race car, and in crash events every traffic vehicle that is hit, is a
structure of nodes and beams that crumples where it is hit, keeps the dent,
tears, bends its chassis, throws the game's own sparks and debris where
metal rips, and loses wheels in a wreck. The car's handling, its
collisions and the title's own damage logic (panels swinging open and
coming off, the Crash scoring) are not touched: this is what you see, not
how the car drives.

Tested on Linux (Vulkan); on other systems it builds from the same code.
A big pile-up costs a few milliseconds of CPU a frame.

The variables below are for development:

| Variable | Does |
|---|---|
| `BO3_SOFTBODY=1` | the damage model on, whatever the setting (`0` off) |
| `BO3_SOFTBODY_LOG=1` | one line per car built, per hit and while a car settles, with the time a step takes |
| `BO3_SOFTBODY_TEST=squash` / `dent` | checks: the car drawn at 60 % height, or a hit into its rear corner solved at once, as soon as it is first drawn |
| `BO3_SOFTBODY_TRAFFIC=0` | traffic off (on by default, in crash events only) |
| `BO3_SOFTBODY_SUBDIV=0` | the full level as loaded (default: its body subdivided, each triangle into four) |
| `BO3_SOFTBODY_WRINKLE=1` | the wrinkles' depth, scaled (0 none) |
| `BO3_SOFTBODY_SPARKS=0` | no sparks where metal tears |
| `BO3_SOFTBODY_FXTEST=1` | test aid: sparks over player 1's roof every flip (`=2`: debris every 30 flips) |
| `BO3_SOFTBODY_AB=<file>` | test aid: while the file exists every car is drawn as loaded and stands still, so the same crash can be pictured with and without the model |
| `BO3_SOFTBODY_PARTS=0.15` | the title's thresholds for panels coming loose and off, scaled (default 0.15; 1 leaves them) |

## What the title does

A race car's model is its `.bgv`, loaded whole into contiguous memory and
drawn straight from it every frame; the car object points at it
(`car + 0x50`). The model has four levels; each is a vertex buffer of
24-byte vertices (float3 position, packed normal 11-11-10, float2 uv) and
up to sixteen part entries. Entry 0 is the body; the others are the doors,
the bumpers, the bonnet, the boot and the wheels, each in its own frame
around a pivot. While a car is whole, the panels are drawn from a copy of
them in the body's frame (the vertices no entry names); once a panel swings
or comes off, the title draws its entry instead with one of sixteen panel
matrices (`car + 0x590`). The vertices never change: the title's damage is
panels moving as rigid pieces, and the vertex shader (one matrix per draw)
places each piece.

The loader turns the file's offsets into addresses: the levels and the
entries are contiguous-window addresses, the vertex and index buffers
physical ones, and a strip record's offset becomes the address of its
indices.

A hit reaches `sub_0012FA40` with the car's physics body (`car + 0x10`, its
world matrix), the contact point, the direction of the hit (into the car)
and its strength, once for each car in a collision.

## How the model works

Two marks in `config/patches.json` call `src/game/softbody.c`:

- `sb_draw` (`sub_0018DE00`, a race car's draw, `esi` the car). The first
  time a car is drawn its structure is built from the full level (below).
  After that, each flip a car is still moving it is stepped and written
  back.
- `sb_impact` (`sub_0012FA40`). The hit becomes a ram driven into the car
  along the hit's direction over eight frames, from where the hit node is
  now, to a depth of `0.0045 · sqrt(strength − 300)` metres (at most 1.6 m;
  below 300 a hit only scrapes). A hit near a contact still going on (within
  0.6 m, the same way, within 30 frames) only raises that ram's target: a
  car held against a wall hits every frame and must not grind itself away,
  while separate crashes add up. A ram stops 0.4 m short of the chassis'
  middle: the cabin holds. Up to eight contacts at once.

  The two vehicles of a crash each get a hit at the same flip, near the
  same place in the world, their directions opposite. Once both are in
  (the last 32 hits are kept, vehicles we do not deform among them), each
  ram takes the other vehicle's shape: up to 900 of its body's vertices
  (kept per model), put into this car's frame, are looked at along the
  ram's direction from its origin, and a height map of 18 × 18 cells of
  0.15 m holds how far its surface lies behind its foremost point (one
  cell's dilation fills the gaps). A node is pushed only where the other
  has something, and less where its surface falls back: a pole leaves a
  narrow notch, a car's nose its shape, a truck's front a flat wall. The
  depth goes with the masses, `2 m_other / (m_self + m_other)` of it within
  0.2 and 1.8, a mass from each model's size (car1's 4.2 m as 1200 kg): the
  truck stays nearly whole, the small car folds. A hit with no partner (a
  wall, a pole of the world) keeps the round ram, its radius growing with
  the strength from 0.45 to 1.05 m.

  A hit from above (its direction's up part below −0.6: the ground under a
  car that rolled onto its roof) is the ground's: flat, across the whole
  face at once, so the roof goes down level, `0.009 · sqrt(strength − 300)`
  deep (at most 0.45 m; a roof landing is a hit of about 1000) and down to
  0.3 m short of the chassis' middle rather than 0.4. Another car's shape
  takes its place if one turns out to be the partner.
- `sb_parts` (`sub_0012FEE0`, a car's damage state set up at each start).
  The title draws each panel's thresholds at random there: a panel's damage
  (a quarter of each hit's, 0..1) past 0.05..0.1 sets it swinging, past
  0.085..0.17 it comes off. Scaled by `BO3_SOFTBODY_PARTS`, panels swing
  and go sooner.

The structure (built when a car is first drawn, as `sb/dyn.py` tunes it):
the welded vertices of the body and the attached panels (the body nodes,
about 1,700) are clustered in 0.22 m cubes, one structure node each at its
cluster's middle (about 400), each beamed to its twelve nearest within
0.75 m; inside the cabin a chassis lattice of 3 × 3 × 5 nodes, its
neighbours beamed (diagonals too), its middle fixed (the car's rigid body
carries the whole); two braces from each body cluster to its nearest
chassis nodes. Two thirds of 1200 kg go on the clusters by their vertices'
share, a third on the chassis.

A flip is four steps. The rams push nodes; the beams pull their ends
towards their rest lengths (three Jacobi passes); a beam stretched or
squashed past its yield strain (2 % body, 6 % braces, 3 % chassis) takes
most of the excess into its rest length, so the dent stays. What is held to
the chassis is solved quasi-statically and damped hard: steel barely gives
under its own inertia, and a position solver this coarse would sag under
it. A node torn from the chassis (no brace left) is moved by what the car's
own motion puts on it in the car's frame: from the car's world matrix over
the last three flips, the frame's acceleration, its turn (centrifugal,
Coriolis, angular acceleration) and gravity, so what hangs swings, flaps
and falls, and moves as a free piece in the world would. A jump in the
matrix (a respawn) or an acceleration past 2000 m/s² starts the history
again. A beam on such a node breaks past 1000 N (its constraint's force).
Such a node meets the road: its height from the car's matrix, the road's
from where the body's lowest point was while the car last stood on its
wheels (a traffic vehicle's when it was first hit); below it, the node goes
back up along the world's up and keeps 0.6 of its speed a step, so what
tears off lands and slides. One that ends up 12 m from its place is lost
and its triangles go.

Where a beam tears, the title's own sparks fly (up to six tears a car a
flip): its spark burst `sub_00182920` (the effects manager at `0x005A9A00`;
callee pops 0x1C: the manager, the position and two velocities as aligned
float4s, the plane's normal, the count as a float, the height above the
road; `AL` set adds seven times as many into a bank the title draws only
while `car + 0x18FA` is set, in a crash). Called from the draw mark with
every guest register, the x87 top and xmm0-7 put back. The title scales a
spark's speed by 0.25 to 0.5, so the velocity passed is 2.67 times the
car's (they stay with it) plus 14 m/s out from the chassis' middle and up.
Sparks are drawn from 33 ms of age on, in world space. Where six or more
beams of a car tore in one flip (a panel ripping), the title's crash debris
flies as well: `sub_001834E0` (callee pops 0x20; `EAX` the second
velocity; the manager, the position, the first velocity, the plane's
normal, the count up to 24, the height, a lighting pair, a flag), with the
lighting the title computes for a car about (0.34, 0.66). Nodes a ram moved keep no speed from it.

Each body node follows its four nearest structure nodes: the weighted sum
of where each would put it, its position plus the body node's rest offset
turned with it. A structure node's turn is the rotation part of how the
vectors to its live neighbours moved, found by iterating from last flip's
(Müller et al., "A robust method to extract the rotational part of
deformations", 2016), so a part that swings round turns with it rather
than shearing.

The chassis nodes are held to their places by springs that give past 2 %
of the car's size, so a hard hit that reaches them bends the chassis for
good. A hit across the car's length (a T-bone, a pole in the door) past a
strength of 8000 also bends the whole chassis like a banana: where it is
held moves along the hit's sideways part by 0.002 · sqrt(strength − 8000)
(at most 0.55 m), most at the hit and falling off over 0.35 of the car's
length, relative to the middle node, so in the car's own frame the ends
swing towards the other car. The chassis nodes move with their pins over
the ram's eight steps; the body follows through its braces and yields. A
body beam or a brace stretched to 1.5 times its first length tears; a
node left with no brace to the chassis hangs (above).

Tears show as holes: the title reads a strip record's indices (`+ 0xC`)
and count (`+ 0x10`) at every draw (`sub_00031AB0` to `sub_001D7D10`), so
once beams tear each level's records point at strips of our own (from the
runtime's contiguous allocator) without the triangles that have an edge
stretched past 1.6 times its length and by more than 5 cm, or past 1.3 times
and by more than 20 cm (a big low-poly triangle would otherwise stretch into
a spike before it tears). Runs of whole
triangles are copied as they are and joined by degenerate ones at the same
parity, so the winding stays. A reset, a repair or a new model puts the
records back.

Writing back: every level's vertices take their node's displacement (the
lower levels the nearest node of the full one); the normal is turned by the
node's deformation gradient, fitted by least squares over its neighbours
(`F^-T`). A part entry's vertices take the displacement at their place on
the car: the part's offset from its pivot is found once by matching its
vertices onto the attached copy, so a door that later swings open keeps its
dent. Wheels have no attached copy and are left alone.

The full level is subdivided before the car is built, so a crumple is finer
than the model's own triangles: its body (entry 0) each triangle into four
at its edges' middles. One buffer of our own holds both streams (stream 0
the 24-byte vertices, stream 1 their 8 bytes after them, so the level's
vertex count stays what the header's two `Data` fields at `+ 0x4C` and
`+ 0x58`, physical, span): the loaded vertices first and where they were,
then one per edge, its position, normal and uv the mean of its ends' and
its stream 1 its first end's. Entry 0's records point at strips of our own,
each new triangle joined to the last by degenerate ones at its own parity.
The part entries and the lower levels keep theirs. A car's body nodes are
the finer mesh's (about 4,500 for car1 instead of 1,700). Freed with the
car; the header and records go back only if the model is still the one we
changed. Wrinkles: where the sheet is squashed (a body node's neighbours
nearer than at rest), its vertices go out along their normal by up to
6 cm times the squash in a fixed value noise of 7 cm cells, drawn only.

The car goes back to its rest shape when the title's sixteen panel slots
(`car + 0x590`) are back as they were when the car was built: none of them
moves while a car just drives, all can on a hit, and a Retry or a repair
puts them back exactly. Two signals use it: the slots coming back after
damage, and the car's phase (`car + 0x134C`) going back to 1 before the GO
with the slots back (a Crash also goes back to phase 1 for its results, with
the car still a wreck).

The network needs no triangles: beams come from distances, normals from the
gradient. It works the same on every model; 65 of the 67 cars match all
their panels (two each have one panel without an attached copy, which then
stays as it is).

All of it is our own design (a mass-spring network with plasticity); no
code or data from another program.

Wheels go only in a wreck: the car's `+ 0x3D0` is 4 while it drives and
flickers between 1 and 0 once it wrecked, until the respawn (or the
results). Then a wheel whose arch we crushed by more than 0.2 m on average
(the nodes within 0.6 m of its attach point, the model's `+ 0xBB0`) gets the
title's full wheel damage (the state's `+ 0xF78`, at `car + 0x90`), and the
title lets it go its own way (its wheel state at `+ 0x4AC` goes to 3, the
wheel flies off). The respawn sets the state up again (`sub_0012FEE0`): the
car drives on with all four.

A wheel that stays on in a wreck goes with its arch, for the draw only: its
matrix (`car + 0x90 + 0x40 w`, three rows and the position at `+ 0x30`, in
the car's frame) moves by 1.5 times the arch's mean displacement (at most
0.35 m) and tilts by 2.5 times the slopes of its sideways displacement,
against height (camber) and against length (toe), at most 0.45 rad each.
The title turns that matrix a little every step rather than building it
anew, so `sb_drawn` (after `sub_0018DE00`'s call to `sub_000303D0`) puts
the title's own back once the car is drawn. Traffic draws its wheels
elsewhere and is left alone.

## Limits

- **Detail is the model's, four times over.** The subdivided full level has
  about 4,500 body nodes for car1; folds finer than that are the wrinkles'
  noise, not physics.
- **Traffic only in crash events.** The music manager's outer state
  (`0x4115E8 + 0x5F8`) is 6 in a crash mode and 5 in any other stage.
  Outside a crash event, and when one starts again, every hit traffic
  vehicle is released (its shared model back, its copy freed), so a city car
  met twice never drives about broken. Traffic vehicles share models: a hit
  one gets its own copy, from the runtime's contiguous allocator
  (`xbox_ContiguousAllocEx`, preferably above 40 MB), with the addresses in
  its levels' headers and entries moved. It is drawn through `sub_000303D0`
  from its own object, so its `+ 0x50` picks the copy (`sb_model`, and
  `sb_stream` for the level header the draw hands to the vertex streams).
  A copy outside the runtime's allocations would be resolved in the base
  view by the toolkit's `dma_resolve` and draw from empty memory.
- **Visual only.** Handling and collision shapes are the title's.
- Not tested: Windows, macOS, Android; split screen; the PAL build.

## Tools

The scripts used to find and check all this live outside the repository
(`~/RE/burnout3-softbody/tools`, `sb/`): guest RAM dumps from a running game
(including the contiguous window, which on Linux is its own host mapping),
a write watch with the guest call chain on Linux (toolkit branch
`watch-linux`), a draw log of every array draw with its buffers and
matrices, an offline copy of the model in Python with a small rasteriser,
and a replay of the hits a game logged.
