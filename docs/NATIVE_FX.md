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
done, or again with the loop flag. One colour fills every corner; two put the second on corners 1 and 2.

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
| `003fb580`, `003ff050`, `003ffcd0`, `003ffec0` the Arachne's landings (states 0x23, 0x32, 0x28, 0x2b) | 44 | dust at its six legs' bones (+0x104 matrices); 0x32's events 1 / 2 one at its body | yes (any landing, legs 110 cm out round it) |
| `00264b90` | 44 | (Dante's code range; not traced) | no |

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
