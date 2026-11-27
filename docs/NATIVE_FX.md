# Native PS2 effects (red orbs, Cerberus's ice)

`tools/native_fx.py` exports the game's own effect records from the disc into `native_fx_runtime.h`;
`psp/testroom/nfx.c` plays them. It generalises the em008 subset in `vanguard_fx.inc` (Vanguard keeps its own
path). Times are 60 Hz frames and distances are centimetres.

## What is ported

| Record | Ported | Notes |
| --- | --- | --- |
| V timeline | yes | Children start at their authored delays. Each has a translation, a rotation in degrees and a scale. V children nest. |
| E kind 1 | yes | Billboard: texture rect, width and height, scale curves, colour keys, screen spin (+0x14c). |
| E kind 2 | yes | Quad in its frame's XZ plane, turning about its normal (+0x148). E153's frost lies flat at rotation 0. |
| E kind 5 | yes | MOD mesh (+0x28 names the M entry): scale curves, colour keys and turn rates (+0x144). |
| G (generator) | yes | An emitter (`FUN_001b1970` / `FUN_001b1cd0`); see Generators below. |
| P (particle program) | yes | `pfx.c`: Line00, Poly00, Poly01, Sprt00, Line01 and their fake layers; see P effects below. Line02 no. |

The layouts are those already used for em008: scale curves at `+0x88/+0x8e/+0xa8/+0xb4`, colour keys from
`+0x174` (20 bytes each, 0x80 = 1.0), life `+0x80` (0 = while the owner lasts) and spread `+0x5c`.

Inferred, not decoded:

- (Decoded since) A model element whose texture chunk is the 16-byte `"1"` (0x31) reference is dropped by the game
  (`FUN_001f1760`) unless its spawner hands it a texture (+0x7a2 / +0x79c: Cerberus's shells do); the port's twin /
  M54 fallback below covers those spawner-given cases. And an element with **+0x211** set takes its textures from
  model **+0x212** (its geometry stays +0x28's): 54 of the shared pack's model elements do, the Devil Trigger's rune
  lattice among them. `native_fx.py` / `prepare_demo.py` export that model, `nfx.c` draw_model binds its texture.

- E kind 5 records with `+0x1d` or `+0x56` set are drawn additively (glows); the others alpha-blended.
- An effect model whose texture chunk is the 16-byte reference takes the textures of a twin model with the same
  vertices (M43 is M15). Failing that, it takes the icicle model M54's ice texture.
- E2, the 18 m white shockwave sphere in V72/V180, is not exported. Drawn with an ordinary blend it fills the
  screen with white slabs.

A looping handle (the game's effect `+0x3a` flag) keeps its elements alive until `nfx_kill`, and `nfx_move`
follows a moving owner (the `+0x90` matrix pointer). Orbs use this, and so do the icicle, sphere and shard shells.

## Generators

A G record (96 bytes) emits one child, again and again. The exporter keeps G records in a bank's V table with
their id | 0x8000, so the banks and the cutscenes' blobs keep their layout. Its fields, as `FUN_001b1cd0` reads them:

| Offset | Meaning |
| --- | --- |
| +0 | Where the children go: 0 the world, fixed where made; 1 / 2 on the emitter, moving with it |
| +1 | Its path: 0 a line (speed +4 losing +8 a frame, along (0, +0x10, +0x14) degrees, `FUN_001b2ab0`); 1 a model's points (not ported: it stays put); 2 still |
| +0x18, +0x1c, +0x34 | i32: first delay; interval, plus a random number masked by +0x34 |
| +0x20, +0x30 | i32 life in frames; +0x30 set: no end. A cutscene's emits until the demo ends (PCSX2: demo 123's G79 burns on for seconds); elsewhere the port ends it with its life unless a looping owner lasts [inferred] |
| +0x24, +0x26 | The child's kind (0 P, 1 E, 2 G, 3 V) and u16 id |
| +0x28, +0x2c, +0x59 | The children's frame turned (0, +0x28, +0x2c) degrees; byte +0x59 a random spread on +0x28 |
| +0x38, +0x3c, +0x40 | Scale +0x38 going to +0x3c over u16 +0x40 frames |
| +0x50, +0x54 | With +0x40 = 0: +0x38 times a random factor, 1 unless the two are a unit or more apart; then +0x50 plus a share of the difference [inferred: the code adds no +0x50] |
| +0x44 / +0x48 / +0x4c | Random offsets within those ranges (whole centimetres), in the children's frame |

The children of a G play once (a looping owner doesn't make them loop), and those with no life of their own (life
0) last while the G emits. A P child starts a particle burst (P effects, below). Checked in PPSSPP: Cerberus's fight runs the same frame for
frame with and without generators, and his punch's V166 now raises its own taller ice spikes (34 elements become
82 at the peak).

## Red orbs

`CItemOrb` (vtable `006057a0`, typeinfo `005f5188`). `FUN_002f2690` picks the orb's effect by its size byte:

| Size | Value (`005aa540`) | Effect | Children |
| --- | --- | --- | --- |
| 0 | 1 | V45 | E46 orb ×1.2 |
| 1 | 5 | V186 | E46 ×0.7 |
| 2 | 20 | V187 | E46 ×2 and one E47 ring |
| 3 | 100 | V188 | E46 ×3 and two rings |
| 4 | 1000 | V189 | E46 ×4 and three rings |

- E46 is the demon-face orb, T13 rect (0, 0, 64, 128). E47 is the ring, T13 (64, 0, 64, 64). Both are in
  `static_effect.pac` (GDATA 167).
- The colour is overridden with mode 2 at `+0x9b` from `005aa590`: red is (80, 0, 0).
- Collecting an orb plays E185 in the same colour (`FUN_002f3750`).
- `FUN_002f4510` splits a burst into thousands, hundreds, twenties, fives and ones. It then breaks larger pieces
  down until there are at least seven.
- The flight and homing are still the port's estimate.

## Cerberus (em025, GDATA 130 chunk 23)

Effect ids are those its code passes to `FUN_00208590(type, id, ...)` (type 0 P, 1 E, 2 G, 3 V):

| Use | Effect | Source |
| --- | --- | --- |
| Icicle shell | V117, loop: E195/E196 on model M54 | `0037f080` |
| Icicle landing mark | E291 (0x123) | `0037f450` |
| Icicle shatter | V171 (0xab) | `0037f2a0` |
| Sphere held | V101 (0x65) on the body | `0032c020` |
| Sphere charge | V97 (0x61), 160 / speed frames | `0032c320` |
| Sphere in flight | V89 (0x59), loop: E140 = M3 ×10, rolling 10° a frame | `0032c320` |
| Sphere launch | V72 (0x48) on the body | `0032c320` |
| Sphere, shard end | V237 (0xed) | `0032c570`, `0040dd40` |
| Shard | V48 (0x30), loop: E140 at half size | `0040db60` |
| Punch, ev1 at the paw | V115 (0x73) | `002a0510` |
| Punch, ev3 at the paw | V166 (0xa6): ice spikes M55, debris M4-M6, frost | `002a0510` |
| Icicles, ev3-9 | V151, V150 at bones 16 and 21, E251 at the paws and chest | `002a3710` |
| Icicles, ev10 | V118 (0x76) | `002a3710` |
| Breath | V320 (0x140) at the mouth; V321 (0x141) from phase 1 (V314: frozen floor M116-M119) | `002a2320` |
| Armour piece breaks | V126 (0x7e) | `2a84b0` |
| Re-ice, each part | V293 (0x125) | `002a56b0` |
| Chain break | V180 (0xb4) | `002a4e10` |
| Scratch, swipe, dash wind-up | V173, V174, V172 | `002a1ad0`, `002a1ba0`, `0029f980` |

| Punch, ev2-5 at the paw | P58 (0x3a) at 2 and 4, P59 (0x3b) at 3 and 5: ×1.4 (-50, 10, 0), ×1.2 (-40, 0, 70), ×1.2 (-40, 0, 70), ×1 (-20, 20, 0) in the paw's frame | `002a0510` |

Stand-in: **the breath mist** (G44/G23/G45, generators of V73 / E281 / V75 started in `002a2530`): E284 clouds along
the breath spheres and the phase-1 sweep box, where and how often the port's choice. The punch's mist takes the
paw's offsets in the body's frame and doesn't ride the paw [estimate].

## Not done

- P class Line02 (no pack the port loads uses it).
- Element velocities and gravity: elements stay where they're placed.
- The GS blend mode per element.
- The 16-byte texture references.
- E2's shockwave.

## Shadows and the rooms' lighting

- **The rooms' baked lighting is their vertex colours**, and the port already draws with it: every stage vertex
  carries the PS2 mesh's RGB (SCM `colors`, `tools/scm_ps2.py`), written by `tools/prepare_stage.py` (times
  `STAGE_BRIGHTNESS`, 1.25) and modulated with the texture in `stage.c`. Sampled rooms span the whole range
  (st000 median 29, st200 median 83 and 0-255, st300 median 67), so corners and creases are already dark in the
  game's own data. No ambient occlusion is faked on top.
- **Soft floor shadows** (`fx_draw_shadows`, `fx.c`): Dante's disc (42 cm, alpha 170, gone 5 m up) and now one
  under every enemy and boss (`enemy_draw_shadows`, `enemy.c`), sized per kind (Cerberus 3.3 m, Geryon 2.6 m,
  Arachne 1.5 m, Beowulf 1.2 m, the Hell Vanguard 75 cm, most others about 46 cm; fitted by eye, estimates) and
  fading with height over 5 m plus twice the radius. None under the Gigapede (on the walls) or the Leviathan
  (Dante's inside it). The game's own shadow records (SHW, the zone's shadow type) aren't decoded.
- How they're drawn: one untextured draw for all of them (8-sided disc, solid to half the radius, then a ramp to
  nothing, in the vertex colours), on the **drawn** floor (`stage_shown_floor`: the mesh's floor triangles can sit
  centimetres above the collision floor) with a depth bias of 96, a little over the decals' 64. Before this, Dante's
  textured disc 3 cm over the collision floor lost the depth test to the street's grime layers and never showed.
- Cost (PPSSPP, test street, 12 Hell Prides on screen, real clock, 60 s): 54.22 FPS average and 347 missed
  frames before and after, identical; the display-list build went from 7337 to 7690 microseconds a frame.
  Shots: `analysis/testroom/shadow_bench_after_400.png`, `shadow_vergil_150.png`, `shadow_cerberus_380.png`.

## P effects (particle programs)

Not VU programs, as first thought: C++ classes with the VU only drawing. `FUN_00208590(0, id, ...)` calls
`FUN_001b4520`, which looks the id up in the P table (0x1e53c60, filled by `FUN_001ff540` from a pack's P entries) and
makes the class that the record's parameter block's byte +1 names (`FUN_001b45e0`). `tools/native_fx.py` exports the
P records a bank's V and G records reach, raw, with the T sheets and A atlases their sprites use; `psp/testroom/pfx.c`
runs them; `nfx.c` starts them for V children and G children of kind 0 and draws them after its elements.

Classes (vtables in SLUS_213.61; RTTI names from the decompilation's `rtti.txt`; start = vtable +0x28, tick = +0x2c):

| +1 | Class | Make / vtable | Start / tick | Primitives |
| --- | --- | --- | --- | --- |
| 0 | CPtclLine00 | `001b5290` / `00601620` | `001b55c0` / `001b5cb0` | 24 streaks of one line |
| 1 | CPtclPoly00 | `001ba100` / `006016c0` | `001ba340` / `001bb310` | 16 triangles |
| 2 | CPtclPoly01 | `001bd810` / `00601800` | `001bda50` / `001bdc40` | 16 triangles: a ring |
| 3 | CPtclSprt00 | `001bbe10` / `00601760` | `001bc050` / `001bca90` | 12 sprites |
| 4 | CPtclLine01 | `001f6490` / `006018f0` | `001f66d0` / `001f6e40` | 12 streaks of two lines |
| 5 | CPtclLine02 | `001f7a10` / `00601990` | `001f7c50` / `001f7d80` | 24 line points (not ported) |

`FUN_001b4680(this, prim, count)` sets the GS primitive (+0xa0: 0 lines, 1 triangles, 2 sprites, table `0058e9e0`)
and the count (+0xa1). Each sub-record makes a "fake" (CFPtcl*, `FUN_001b60c0` / `001bb980` / `001bd2a0` ...): it
draws the main one's vertices again (`FUN_001b4e40` points its packet at the parent's) in a frame and colours of its
own, and goes when its parent goes.

### The record (`FUN_001b3bf0`)

u32 2 at +0, then u32 offsets from +0x10 at +0x10: [0] the parameter block, [1] where the sub-record pointers are
written, [2..] the sub-records (0xb0 bytes each). The block's u16 +0xc0 counts the sub-records.

Common to every class (offsets in the block):

| Offset | Meaning |
| --- | --- |
| +1 | Class (above) |
| +0x20, +0x30, +0x40 | The emitter's translation, rotation (radians, `FUN_00131700`) and scale (vec4 each, copied to +0xd0 / +0xe0 / +0xf0) |
| +0x50 | Sprites: 1 the emitter faces the camera at its owner's point (`FUN_001b2dc0`, +0xa2) |
| +0x52, +0x58, +0x5e | s16 rates a frame: translation /16, turn /65536 of a circle, scale /4096 (`FUN_001b38c0`) |
| +0x64 | Flags: bit 0 blending on (the GS's ABE); byte 1 the GS ALPHA register (0x44 normal, 0x48 additive, 0x41 (Cd - Cs) As + Cd) |
| +0x68 | i32 life in frames; then it ends, or with +0x98 (its owner looping) starts again |
| +0x6c | Colour keys: four of 0x14 bytes (s16 frames, then four RGBA corners, 0x80 = 1.0); +0xbc on, +0xbd loop, +0xbe how many colours (`DAT_00590010`: 1, 2 or 4) |

Colour (`FUN_001f8ce0` / `FUN_001f8e40`): from key i towards key (i + 1) & 3 over key i's frames, for i = 0, 1, 2; then
done, or again with the loop flag. One colour fills every corner; two put the second on corners 1 and 2. The
colour is applied as a burst starts (the game ticks it before the first draw), so key 0's unused corners never show.

Sprites (class 3):

| Offset | Meaning |
| --- | --- |
| +0xd0 | s16 T (the sheet; `FUN_001b3350` reads its size) |
| +0xd2, +0xde | s16 x3 /16: speed out from the middle, base speed |
| +0xe4 | f32 x3: drag, a frame (`FUN_001b3b70`: each speed goes towards 0 by it, not past) |
| +0xf0, +0x106 | s16 x3 /16: the box the particles start in, and the inner box (halved) they're pushed out of (`FUN_001b4990`) |
| +0xf6 | s16 A: the atlas (`FUN_001ec2f0`) |
| +0xf8, +0xfa | u16 /16: half width and height |
| +0xfc | f32 gravity a frame (on y); +0x100 = 1: along the world's y, not the emitter's |
| +0x102, +0x104 | s16 /2048: growth of the half sizes a frame |
| +0x10c | f32 x3: the drag's own loss a frame |
| +0x118 | 1: a random picture of the atlas, held; 0: the atlas plays (`FUN_001ec210`) |

Each particle starts at q, a random point of the box (pushed out of the inner box onto a random face), with speed
`base + q * speed / max(box, 1) - q` (the "- q" is in the MIPS, 0x1bc958). A tick: drag, gravity, then the position
moves by the speed. Sprites face the camera.

Triangles (class 1): gravity f32 +0xd0 (world with +0xee), speed +0xd8, base +0xde, box +0xe4, inner box +0xf0, drag
+0xf8, its loss +0x104. +0xea = 1: shards, three corners jittered within s16 +0xec /16 that all move with the speed;
0: streaks, corners at the head, behind it by +0xd4 and ahead by +0xd6 (/16) along the speed (the second corner's y
taken by +0xd4, as the code reads). Three colours.

Ring (class 2): radii f32 +0xd0 (outer) and +0xd4 (inner), growing by +0xd8 / +0xdc a frame; 8 segments of PI/16
(a quarter turn) in the emitter's XZ plane. Three colours.

Streaks (class 4): gravity +0xd0 (world with +0xea), speed +0xd8, base +0xde, box +0xe4, inner box +0xec, no drag.
Two lines a particle: head to +0xd4 behind it, head to +0xd6 ahead. Two colours.

Single streaks (class 0): gravity +0xd0 (world with +0xe8), the tail +0xd4 behind the head, speed +0xd6, base +0xdc,
box +0xe2, inner box +0xea, no drag; one line a particle, head to tail. Two colours.

Sub-records (fakes): translation, rotation, scale at +0 / +0x10 / +0x20, rates at +0x30 / +0x36 / +0x3c, colour keys
at +0x48 (flags +0x98). A sprite fake takes its parent's T and A.

### Direct P spawns (`FUN_00208590(0, id, matrix, flags)`)

Most P effects come as children of V / G records, which nfx starts by itself. These code paths start one directly;
a port of them calls `nfx_spawn_p(bank, id, pos, yaw, 0)` (the P must be in the bank's export: native_fx.py's
`write_bank(..., p_ids)`):

| Caller | P | Where, when | Port |
| --- | --- | --- | --- |
| `002a0510` Cerberus's punch | 58, 59 | on the paw, events 2-5 | yes |
| `003ac750` Agni & Rudra | 44 (0x2c) | dust at the brother (+0x450) as he lands: state 1 (`003a2fb0`), the dashes (`003b0450`), the hops (`003b09c0`), the slam (`003a8200`, phase 0x1e → 0x1f); also states 36 (`003a5ee0`) and 55 (`003a8c60`) | yes (not 36, 55) |
| `003ac770` / `003ac790` Agni & Rudra, states 46-49 (`003a7510` and kin) | V0x7b (P56), G0x1d5 (P44) | at motion frame 65 at him, the G riding him and looping until 77 (`003ac7e0`) | yes |
| `003a6820` Agni & Rudra, state 39 (dead) | 15 (0xf), 44 | P44 at bone +0xcf8 from motion frame 220, P15 at bone +0xcc0 from 280 | yes (at his feet) |
| `003c9870` Nevan's bats | 0xcb, 0xfb, 0xeb (203, 251, 235) | one at random riding each bat (+0x250), 12 bat sprites, life -20000: never runs out (`pfx.c` counts a life down only from -10000 up) | yes (`nevan_boss.c` bats_particles, nfx bank 7) |
| `003b67a0` / `003b7a50` Nevan's 21 (fall) / 27, `003c8810` a bat's strike | G0x1a8 (P38), G0x97 (P293), V0x110 (P31) | at a bone of hers (taken 100 up at her middle); where the bat hits | yes (`nevan_boss.c` act, bats_update) |
| `00496540` / `00496a80` Lady's rocket (CEm034Shl02) | V0x179 (P337), V0x21f (P2, 3, 16, 18) | the first rides it in flight, the second as it bursts | yes (`lady_boss.c` rockets_fx, nfx bank 8) |
| `004f46d0` / `004f4a60` / `004f50d0` the Arachne's web (CEm010Shl00) | V0x29e, V0x2a3 (P412), V0x308, V0x309, V0x2a2, V0x304 (P412) | in flight; on Dante (V0x308 held on him); where it misses; as he breaks free | yes (`enemy.c` an_webs_update, nfx bank 9) |
| `0045a330` the Fallen's spear (CEm011Shl00) | V0x367 (P3, 16), V0x368 (P91) | as it's let go; where it ends | yes (`enemy.c` Fallen glue, nfx bank 10) |
| `00462f90` Mission 19's mirrors (CStageSetMirror) | V0x1ea (P113) | each mirror as it shatters | yes (`mission19.c`, nfx bank 11, GDATA 758); the lit glow E0x2e0 is a model with an undecoded 1-byte texture reference: stand-in kept |
| `004203a0` / `004203c0` a struck crystal (CStageSetCrystal) | V0x1c (P78), V4 (P24) for type 2, V554 (P160, 345, 346) for type 3 | at it | yes (`nfx_crystal_break`: Mission 13's 233, Secret Mission 10; V554 from GDATA 526 as bank 12) |
| `00493590` the Nausika's blows (CStageSetNausika) | V0x22c / V0x22b (P257, 258 / P259) | where a blow lands | yes (`nausika.c`, GDATA 694 as bank 13; light / heavy by the hit record's +2 [inferred]) |
| `00492630` the Nausika | V0x352 (G512: P443 every 8 frames) | looping on a bone | yes (bone taken 150 up at its middle) |
| `003fb580`, `003ff050`, `003ffcd0`, `003ffec0` the Arachne's landings (states 0x23, 0x32, 0x28, 0x2b) | 44 | dust at its six legs' bones (+0x104 matrices); 0x32's events 1 / 2 one at its body | yes (any landing, legs 110 cm out round it) |
| `00264b90` | 44 | (Dante's code range; not traced) | no |

### Still not played (P-bearing effects found by a census of every `FUN_00208590` call with a constant id)

| Owner | Effect (P) | Why |
| --- | --- | --- |
| The Gigapede's head orb / ground wave (CEm023Shl03 / Shl04, `0051a470`, `00519ac0`) | V0x39e, V0x38c (P464) | the head attack's shells aren't ported |
| The Damned Bishop's fireball (CEm017Shl00, `00513430`) | V0x210 (P245, 309) | the Bishop's spell isn't decoded (`bishop.c` stands in) |
| CEm022Shl00 (`0043ea30`) | V0x36c (P349) | its owner isn't identified yet |
| The Soul Eater's state 0x11 (`00433e00`) | V0x1d8 (P306, 466) | that state isn't ported |
| Agni & Rudra's states 36, 55 (`003a5ee0`, `003a8c60`), `003a94c0`, `003aa960` | P44, V0x30f / V0x2e5 / V0x2f6 | their landings and steps aren't located in the port's scripts |
| Nevan's swarm bats as she's struck (`003c8ab0`) | V0x386 (P339) | the swarm's break on her hit isn't ported |
| Dante's own moves and shells (`002d...`, `002fbf20`, CPl000Shl10 ...) | many | the moveset's owners' code |
| CUIDStatusFile (`0042f3d0` ...) | V0x198 / 1be / 36d | the status screen isn't ported |

### Atlases (A records, `FUN_001ec360`)

+2 frames a picture, +3 the last picture, +4 loop, +5 where a loop restarts; pictures from +6, 10 bytes each: s16 x,
y, width, height, then an s8 centre. UVs are x / width .. (x + width - 1) / width (`FUN_001b35b0`).

### The port

- One `Inst` per main and per fake (96 at most, a start past that is dropped and counted; demo 8 and 24 peak at 89; 24 particles each), with the owner's frame followed while the
  owning V / G lives (`nfx_frame`), which it keeps alive while it plays (a reference, `nfx_unref` at its end); `nfx_kill` on the root
  ends them at once.
- Drawn after nfx's elements: sprites as textured triangles, the rest untextured, depth tested, not written.
- Host checks: `python tools/tests/run_pfx_logic.py` (life, motion, colour keys, the hollow box, loops and the owner's
  reference, on a synthetic sprite record).
- Stand-ins: the GS's 0x41 blend has no PSP equation and takes Cd - Cs As; a camera-facing emitter (+0x50) takes its
  axes from the eye and world up, not the camera's own up (`FUN_001b2dc0` builds them with FUN_00131920 from the camera's
  eye, target and up, then turns them half round x, `FUN_00105668` = sceVu0RotMatrixX: right, screen-up, toward the camera,
  as the port's); a fake sprite
  layer faces the camera though the game's are turned by the parent's emitter; the sheet's top is taken as the
  sprite's top.
- Cost (PPSSPP, Mission 2's street, 31 looping instances of 12 Cerberus P records, about 350 primitives): the
  display-list build 7.2 ms against 6.7 ms without (seconds 1-6 of PERF.TXT); the update didn't move.
  Demo 8 (Lady's missiles, 358 instances started over 22 s): 7.8-8.7 ms against 7.3-8.1 ms with `nopfx 1` in its
  smoke-heavy seconds, no missed frames at 60.

### Checked against the PS2 (PCSX2)

Mission 3's opening (demos 5 and 123) recorded unskipped in PCSX2 (`analysis/ref/pfx/ps2_m3_opening.mp4`, local) and
the port's demo 123 (`analysis/testroom/demo123b.mp4`), side by side in `analysis/ref/pfx/demo123_ab_strip2.png`: Lady's
missile shots, the muzzle flash, the missiles' smoke trails and bursts and the burning demons match in place, size and
colour. The one difference found, the demons' fire (G79 → E275) going out after a second where the PS2's burns on, was
the no-end generator rule above, now fixed.

## Blade trails

The game's weapon trails are their own class (vtable 0x605e20, 0x6c0 bytes; `FUN_00309030(type, frame)` makes one,
`FUN_0030bce0` feeds it a frame while the attack window is open, `FUN_00309d30` updates it, `FUN_0030a530` /
`FUN_0030ad70` draw). Its type indexes a table of 0xb0-byte entries at 0x5ad4b0 (0x5ace80 under a flag at 0x1d0cd1f):
- +0 kind: 0 / 1 a strip of points (two a sample: entry +0x30 and +0x40 in the frame, a 0x4340-byte buffer), 2 / 3 a
  model (entry +0x74, from the effect model bank) bent through the frame's last 11 matrices (a 0x2c0 ring); 3 also takes
  the blend +0x70 / +0x71 and +0x98 / +0x9c.
- +4 the sampling interval, +8 / +9 / +10 alpha up to, step and floor, +0xc frames without a sample before it fades,
  +0x10 colour.
Dante's: the shared weapon object (`FUN_002953c0`, fed by `FUN_002954b0` while player +0x24ed is set) makes type 0, kind 3,
model **M34** of his pack (a 34-segment strip 127 cm long whose texture's left part is a soft white streak); Agni &
Rudra's object (`FUN_00296ae0`) types 1 / 2, kind 2, **M30** (fire, on Agni, while +0x24ed & 0x80) and **M31** (wind, on
Rudra, while & 0x40) of A&R's pack. The port (`fx.c` trails, `main.c`) draws a strip over the frame's path, 127 cm out
from the weapon's grip, textured with those models' textures (`native_fx.py` exports them as 10034 / 10030 / 10031) over
the rectangle the models map, added, fading over 0.14 s. Not ported: the models' own bending through the 11 matrices
(their bone weights), and which other weapon classes make type 0 (the port draws it for Rebellion and Nevan, as before).

## Dante's effects

| Effect | Where | Port |
| --- | --- | --- |
| E&I V0x22, Shotgun V0x23, Spiral V0x228, Kalina Ann V0x21b | the muzzle flash, by shot kind (`FUN_002fbf20`; tables `0x5aa630` / `0x5aa680`: ±31 cm on the gun bone, turned down the barrel) | `bullets.c`, nfx banks 14-18 (GDATA 102-106) |
| E&I's charge state (V0x129 + V0x15c, V0x145, impact V0x32) | the shot's kind (`FUN_002688c0` actions 130-133 -> `FUN_002698d0` -> `FUN_0026a520` / `FUN_002fbf20`): a shot from an action other than 130 with no burst left is kind 5, the charge shot (Gunslinger's 131 / 133 come from a 90-frame hold and release), and starts a burst: the next shots stay kind 5 for config chunk 11 +0x670 / 0x674 / 0x678 = 3 / 5 / 7 shots by Gunslinger level; Gunslinger's ordinary shots are kind 1, and kind 7 / 8 / 9 from the +0x2d0 / 0x2d4 / 0x2d8 = 8 / 6 / 4th of a run (+0x7db1, up to 50); a 25-frame timer (+0x2cc) runs down while he isn't in 130 / 131 and clears both (0x260cc8). Kind 5's flash is V0x129 and V0x15c, 7-9's V0x145; 5 and 7-9 land as V0x32; their shells' records (0x5a5a30): damage 10 / 30 / 12 | `ei_shot_kind` (`main.c`), `bullets_set_shot_kind`; level 2 |
| Artemis V0xd3 | riding its shot (CPl000Shl10, `FUN_005232b0`) | yes |
| V0xa (V0x32 charged) | where a bullet lands (`002efaa0`) | E&I and Shotgun |
| Kalina Ann V0x21f | its rocket's burst [inferred] | yes |
| V6 (guard types 0-2), V0x265 / 0x264 / 0x267, V0x269 | a Royalguard block (`FUN_00224db0`), near the attacker (0x267 on his hips); a guard break's V0x269 on his left forearm, within 25 cm (`FUN_004416f0`). The type is the judgement's (`FUN_002e7f10` -> `FUN_004416f0`): +0x3c8c, frames since the guard press (99 while it's up), against config chunk 9's windows (within 5 frames grade 3; within +0x198 / 0x19c / 0x1a0 = 0 / 8 / 10 by style level grade 4; else 0); table 0x5c9400[the blow's guard class +0x3e][grade]: class 0 `0 4 3 5`, 1 `2 2 3 5`, 2 `2 2 3 2`, 3 `6 6 3 5`, 4 `6 6 3 6` (2 a break, 6 not blocked) | all; the blows' class taken from their kind (`combat.c`) |
| V0x83 | Air Hike, at his feet, tinted the weapon's colour (`FUN_00222700`, `FUN_002fd1c0`) | yes |
| V0x14d (P247, 248) | Enemy Step (`FUN_00228950`) | yes |
| V0x8e (Dante's pack, P18), P44 (V0xf2 on floor material 0xb) | Helm Breaker's landing (`FUN_00264b90`) | yes, nfx bank 20 (GDATA 97) |
| V0x90 | kicks off a wall / an enemy (E235 is in the port already); the start of Cerberus's 14:3 (action 45, `FUN_00272210`) and Beowulf's 7:10 charge (action 115, `FUN_0027abf0`), on the weapon | the two charges; the kick: E235 alone |
| Cerberus V0x252 | 40 frames into 14:4 (action 45), on the weapon | nfx bank 21 (GDATA 98) |
| Agni & Rudra V0x288, V0x285 | the start of 5:25 (action 57, `FUN_00272c70`) and of 5:23 (actions 58-60, `FUN_00273150`), on Rudra | nfx bank 22 (GDATA 99) |
| Agni & Rudra V0x37e / V0x37d; G0x1cc / V0x302 | as each blade's attack window opens (+0x24ed 0x80 Agni, 0x40 Rudra; the weapon, `FUN_00296ae0`): in action 56 (5:9); in actions 64 (5:30 / 5:26 / 5:11) and 71 (15:8-10) | yes |
| Nevan V0x38e, V0x2c1 | as window 0 byte 0 bit 0 turns on (+0x24d1): action 81/82's finishers 6:33 / 6:9 / 6:10 (V0x38e) and its 6:14 / 6:40 swing (V0x2c1) (`FUN_00274c70`), action 88's 6:34 (`FUN_00276ac0`); action 92's 16:4 on its flag 1 (`FUN_00277350`) | nfx bank 23 (GDATA 100) |
| Beowulf V0x382 | 17 frames into 7:11 (action 115), where his hips are then (not following them) | nfx bank 24 (GDATA 101) |
| (where they hang) | the player's +0x1380[n] is his model's bone list (24 a form, the model's +0xb0 call in `FUN_0024fb90`; `FUN_002e2070` / `FUN_002e20c0` add the form's 0 / 24 / 48), +0x104 its matrix: +0x1380 the root, +0x1384 the hips, +0x1388 spine 0, +0x13b8 the pelvis. So V0x90, V0x288 / 0x285 and V0x382 hang on his hips, Cerberus's V0x252 on the root, Nevan's on spine 0. The A&R ones are the weapon object's own +0x108 / +0x104 frames | `update_arm_fx` in `main.c`: the bones; for A&R the weapon placement's origin; turned by Dante's yaw only |
| Devil Trigger V0xdc, E0x140, V0x3c (bank 0) | as it starts (`FUN_00244b20` -> `FUN_002298f0(1)` -> CEfcPub 0x20 + weapon): V0xdc, the red rune lattice, on his hips tinted the weapon's colour (`FUN_002fd1c0`); while it lasts E0x140 (tinted) and V0x3c looping there. A weapon change in it (`FUN_0021f500`: 4 then 3) ends those and starts them again in the new colour (CEfcPub 0x42 + weapon) without V0xdc; its end (2) ends them | `update_devil_fx`; the port's glow only if they can't start. The PS2 also shows a lattice burst as it ends whose source isn't found |
| Million Stab V0x90 | its start (action 14, `FUN_00266220`), on his hips | yes |
| (CEfcPub) | `FUN_002fbec0(id, frame, player)`: ids 0-9, 0x27, 0x28, 0x40, 0x41 are the gun flashes (`FUN_002fbf20`); the others make a CEfcPub object (`FUN_00331f90`, 0xb0 bytes, vtable 0x606cf0: id +0xa0, frame +0x80, owner +0x90) whose first update (`FUN_00332240`) is a 110-case switch on the id starting V / G / E effects (the Devil Trigger's, the Devil Arms' charges, ...); 71 callers in Dante's handlers | the Devil Trigger's and the ones below |
| Trickster's dash (id 0x1c), Sky Star (0x2e) | V0xc0 on his hips as the dash's 23:x starts (state 0x16, `FUN_00225c90`); Sky Star (state 0x17, `FUN_002261b0`) V0xc0 and V0x295. The +0x28b1 costume picks V0x160 / V0x3b9 / V0x3ba for others | yes; the port's streak and seal are gone |
| Air Trick V0x26a | as 23:15 / 23:16 start (state 0x18, `FUN_00226550`, through `FUN_002086e0`; +0x28b1 picks V0x26b / 0x8f / 0x215 for other costumes) | yes, the port's streak gone |
| Stinger (0xb), High Time (0xc), action 11 (0xd) | Stinger's dash 3:15 (`FUN_00264fe0`): G0x5f, V0x29, V0x7b (CEfcPub 0xb's case, as Beowulf's 7:31); High Time 3:11 (`FUN_002656a0`): V0xa1 + V0xf3; 3:13 (`FUN_00265950`): V0xb0 + V0xf5 | yes; 3:13 not seen (the port's action 11 didn't start from the ground in the test) |
| Dance Macabre (0x25), the Shotgun (0x3e, 0x1b), Beowulf 124 / 125 (0x12) | 13:17 and 19:3 start the dash set (G0x5f, V0x29, V0x7b); 19:0 / 19:1 G0xb2 (the Shotgun's pack); 17:1 / 17:2 V0x189 on the right hand 35 / 6 frames in, then every 7 / 6 frames | yes, the repeats aside |
| Royalguard's Release (0xf) | V0x29 as actions 190-197 start, whatever motion they open on | yes (`arm_fx` "#n" entries key on the action) |
| Artemis V0x2f4 (0x53) | 2 frames into 20:2 (action 154, `FUN_0026ebf0`), on the gun's frame | on his left hand, where the port's guns are |
| CEfcPub ids at a motion's start, on the bone the handler names | Million Stab 10 frames into 3:8 G0x44 + G0x7d (id 0xa, Dante's pack); 13:11 V0xc7 (action 18); Cerberus 14:5 V0x262 on spine 1 (action 46); A&R 5:26 V0x287, 5:18 V0x28a, 15:0 V0x284, 15:1 V0x27e, 15:2 V0x25c, 15:7 V0x286 (actions 64, 62, 65-67, 70); Nevan 6:16 V0x2b7, 6:19 V0x2a7, 16:5's flag 2 G0x179 on the root, 16:9 V0x260, 6:26 V0x187, 6:17 V0x2b8, 6:11 V0x90 + G0x234 on the root (actions 83-87, 93-95); A&R 15:8 V0x90 + V0x273 (action 71); Beowulf 7:16 / 7:58 V0x2ba (action 120), 7:31 G0x5f + V0x29 + V0x7b (actions 121/122, id 0x58: V0x29 / 0x161 / 0xdd / 0x3bb by +0x28b1, V0x7b / 0xd5 by +0x2928; the port takes the first of each); Beowulf 7:48 V0x2a8, 7:52 V0x299, 7:15 V0x29a, 17:0 V0x18a on the right hand, 17:4 V0x90 + V0x27d, 17:3 V0x27b (actions 113, 117-119, 123, 126, 128) | `arm_fx` in `main.c`, banks 20-24. 13:11 has no port move. The handlers' paths: action 86 starts V0x2a7 with 6:35 (87 with 6:19); 93 is one 16:5 (V0x90 at its start, V0x38e on flag 1, G0x179 on flag 2); 113 starts V0x2a8 76 frames into 7:7 (never, with 7:7's length) or with the charged release 7:7 / 7:47 / 7:48; 117 / 118 V0x299 23 frames into 7:14 or with the release 7:14 / 7:51 / 7:52 |
| CEfcPub ids not yet wired | 3:52 (action 207, after a timer), 13:4 / 7:12 id 0xe (no case: nothing), 24:x V0x2a6 (actions 190-197, when +0x3c86 is set), the Royalguard 24:x V0x266, 0:0 V0x31f, V0xdd + V0x328 (0:52 / 6:0) and V0x36e | their timers and conditions aren't traced yet; CEfcPub's update (`FUN_00335500`) isn't read either, so a looping one ends with its move in the port |
