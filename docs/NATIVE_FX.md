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
| P (particle program), G (generator) | no | VU programs. They're counted by the exporter and skipped. |

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

Stand-ins for unported particle programs. These use em025's own elements, but where and how often they're
placed is the port's choice:

- **The punch's travelling ice line** (P58/P59): small E199 spikes (model M55) along the spike shells' path.
- **The breath mist** (G44/G23/G45): E284 clouds along the breath spheres and the phase-1 sweep box.

## Not done

- P and G programs, so the particles and mist.
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
