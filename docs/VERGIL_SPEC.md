# Vergil (CEm035): the Mission 7 boss spec

All addresses are in the PS2 SE executable (`SLUS_213.61`). Object offsets are byte offsets from the object base.
**Decoded** means read from code or tables; **inferred** means interpretation; **approximation** means the port
does something simpler. A line that doesn't say is decoded. Tables are quoted at **rank 0** and **variant 0**.

The repo's Ghidra export has none of these functions; they were decompiled from a scratch copy of the project
(`run_ghidra.ps1 -Addresses ...` against `analysis/ghidra` does the same).

---

## 1. Which class is boss Vergil

- `CPlVergil` (vtable `0x6066b0`, constructor `2058c0`, made by the player factory `204210` with kind 1) is a
  player-class Vergil, not the boss. `CPlNewVergil` (kind 3, `0x615e70`) is the SE's playable Vergil.
- **The boss is `CEm035`.** EST spawn id **58** → class **40** (byte table `0x599690`) → pack **26** → the pack
  pointer table `0x58e740` → **GDATA 139, em035.pac**. The factory `2acf40` case 0x28 calls `4d0d60`, which
  allocates 0xbbc0 bytes and constructs with `4d1ee0` (vtable `0x613580`, typeinfo name `CEm035`). Its shells are
  `CEm035Shl00..02`.
- Rooms: **144** (EST op 8 `[0, 0]`) and **234** (`[0, 1]`). The op 8 word becomes `+0xbb48`, the **variant**:
  0 = Mission 7 (Yamato only), 1 = Mission 13 (Beowulf too), 2 = Mission 20 (spawned elsewhere).
- Update `4d2740` switches on `+4`: 0 set-up (`vptr+0x130` → `4d27d0`), 1 alive (`4e22a0`), 2 dying (`4e2cc0`).

## 2. Pack (em035.pac, a PAC of 64 entries)

| Entry | Content | Used (variant 0) |
| --- | --- | --- |
| [0] | 4 textures | all models |
| [1] | Body, 24 bones (Dante's layout), 3,209 triangles; byte-identical to pl001.pac [1] | `+0x1240` |
| [3] | Coat, its own 39-bone rig; cloth `pl001_02.clt` in [4] | `+0x1840`, cloth bound to body bone 3 |
| [25] | Yamato, 2 bones, the blade along +z | `+0x3640` |
| [28] | Scabbard, 7 bones: 0-1 the scabbard, 2-6 its cord (cloth `wp005.clt`, [30]) | `+0x3c40` |
| [7] [8] [9] [10] | Motion files 0-3 (PACs of 50, 50, 50, 95 slots; 56 motions) | list `+0xb570` |
| [11] | The body's motion scripts (the player format, tables read directly: `FUN_00255830` mode 1) | `+0xaea0`/`+0xaf70` |
| [12] [13] | Collision caps (36 × kind, bone, shape) and shapes (0x50 each, type 2 spheres) | `FUN_002942e0` |
| [38] [43] | Sword-layer scripts; [37] [42] their motions | `+0xb390`/`+0xb460` |
| [15]-[18], [45]-[63] | The other variants' bodies, Devil Trigger forms and weapons | not variant 0 |

- **Parameter block** `0x5dca50 + variant * 0x54`: +0x2c is **8000** for variant 0 (10000 for 2, 7000 for 3),
  the HP by the same reading as Cerberus's 7200. The first ten floats (100 × 6, 80, 20, 100, 100) are inferred
  per-weapon damage percentages; the port takes the 20 as Ebony & Ivory's.
- **Attack records** `0x5dc4d0`, 0x40 each (as Cerberus's): damage at +4, kind +0x18, reaction +0x1c. 0: 200,
  1: 300, 2: 700, 3: 500 (kind 6), 4: 800, 5: 1000 (kind 2), 6: 500, 7: 1000, 8: 700, 9: 1000 ...
- **Sword places** (`FUN_004e4770`, records of {position, rotation, bone byte at +0x20}): sheathed `0x5dbfe0`
  (Yamato) and `0x5dc010` (scabbard) on **bone 13** at (19, −0.5, 11), rotation y 3.84; drawn `0x5dc0a0` on
  **bone 9** at (−7.5, −2, 0). Window 0 byte 0 of the body script moves Yamato: 1 to the hand, 2 back.
  The EE's matrices act on row vectors, so the rotation is applied transposed.

## 3. Actions and scripts

`vfunc+0x300` (`4e0450`) sets the action byte `+0xb9d7` and `4e0fc0` starts its script:

| Actions | Script bank, index |
| --- | --- |
| 0, 1 | bank 0, the action |
| 2-5, 0x11-0x13 | bank 1, action − 2 (reactions) |
| 0x14 | bank 2, 0 (taunt, 340 frames) |
| 0x15 and up | bank 3, action − 0x15 |

`vfunc+0x2ec` reads **0x77** once an action's script has finished. Script op 6 *n* selects attack record *n*;
ops 0x10-0x1f switch caps on for *n* frames (caps of kind 1 are attack spheres, mostly on bone 0, e.g. cap 13
(0, 80, 70) r 80). Window 1 byte 0 (op 4) is the **chain point** the attack states wait for.

Root motion (`FUN_004e4570`): the model's root delta, turned by his facing, is added to his position every frame.
There is no homing during attacks.

## 4. The AI (states `+0xb9d4`, `FUN_004d6770`)

**Think, 0x17** (`4d7050`), in order: Dante dead → 0x38; DT (variants 1 and 2 only) → 0x39; Dante lunging →
the reaction; the attack gauge full → an attack; else a **wander** pick from `0x5df610[rank*3+variant]`
(variant 0: **strafe 0x1d 40, stand 0x1f 35, taunt 0x20 25**). A pick that repeats the last one becomes the list's first other entry: **0x19, the walk in** (`4d7530`: action 1 toward Dante, 360 frames), whose own weight is 0.

**The attack scheduler** (sub-object `+0x130`, class methods `0x32b270-0x32bad0`, config `0x5df3b0`):
- Gauge `+0x38`: each frame in the wander states, + 20 within 350 cm, + 4 within 400, + 1 within 500
  (`0x5df360`, any angle), else + 0.167 (`DAT_0061a8c0`); nearer than 300 (`DAT_0061a8c4`) it's full at once.
  Full means over `0x5df340[rank]`: 100, 80, 60, 40.
- The pick (`32b7b0`) by distance band (`0x5df308`: 350, 500): **≤ 350: 0x21 50, 0x22 50**; **≤ 500: 0x25 50,
  0x2d 50** (0x2b, Beowulf, 0); **beyond: 0x31 100**. The lists are the same for every rank.
- Rank: thresholds 350, 750, 900, 1200 (`0x5df320`) on the manager's AI value. Inferred 0 on Easy and Normal.

**Reaction** (`4e5c60`, `4deca0`): Dante's current action is a lunge (7/8 Stinger, 17, 38/39, 58-60, 116),
within 500 cm (`DAT_0061aa24`), within 45° of Vergil's facing. Then `0x5df8f0[rank*3+variant]` % → guard
0x33, else teleport-away 0x3e. **Rank 0, variant 0: 100% guard.**

| State | Handler | What it does |
| --- | --- | --- |
| 0x1d strafe | `4d7b60` | Action 0x59 or 0x5a (50/50), up to 360 frames; 1 cm a frame forward beyond 500 cm |
| 0x1f stand | `4d81c0` | Action 0, 240 frames |
| 0x20 taunt | `4d82f0` | Action 0x14 to its end |
| 0x21 | `4d8410` | Actions 0x18 → 0x19 → 0x1a at the chain points, then 0x47 |
| 0x22 | `4d85f0` | 0x18 → 0x19 → 0x1b → 0x1c, then 0x47 |
| 0x25 | `4d9220` | 0x1b → 0x1c, then 0x47 |
| 0x2d | `4d99c0` | 0x28, then 0x29 unless within 300 (`DAT_0061aa3c`), then 0x2a (record 5, 1000: the long reach) |
| 0x31 | `4dac60` | 0x54 facing Dante; hidden, 0x55; travels at 50 a frame to 100 behind Dante; appears facing his way (0x54); 5 frames; → 0x17 (its test of `+0xb9d4` is the current state, 0x31, so never 0x29) |
| 0x29 | `4d9680` | 0x27 (upper slash, records 11 and 12), at its chain point → 0x3d |
| 0x3d | `4dd840` | 0x5f: flies at 45 a frame toward Dante, pitch held to 25-75°, while the chain flag's up; lands → 0x60, a shake |
| 0x33 guard | `4db1d0` | 0x51; once a hit's blocked (`+0xbb51`), after 15 frames (`0x5dfad0[rank]`) counters with 0x25 or 0x2d (50/50, `0x5dcde0`); else at its end → 0x17 |
| 0x3e away | `4ddbf0` | 0x54; hidden; travels to 1000 ahead of Dante, or to the arena centre (3500, 4300, 3500; `0x5df970`) when that's within 600 of him; appears facing him |
| 0x18 death | `4d72d0` | Awards the HP (as Cerberus's), leaves the enemy manager; the room's script plays demo 322 |

Turning (`4e5140`, table `0x5df650`): toward Dante at 10° a frame over the first 30 frames of a wander action.

## 5. In the port (`psp/testroom/vergil_boss.c`, `tools/prepare_vergil.py`)

All of section 4 at rank 0, variant 0. Approximations:
- **Hit reactions:** CEm035's own (reaction table `0x5dcc90`, the hit handler) aren't decoded. Between attacks a
  light hit plays reaction 2 or 3 and a heavy one 0x12; during attacks he doesn't flinch. Hits from the front
  during the guard are blocked.
- **Damage taken:** Cerberus's per-hit values (42 light, 84 heavy, × 1.1; Easy × 2), guns 20%.
- **The leap's fall:** `FUN_003599f0` only snaps to the floor; the port solves the arc to come down where Dante
  is.
- **Cloth:** the coat is rigid on body bone 3; the scabbard's cord hangs straight down.
- **Effects:** the teleport's 0x338 and the sword trails aren't drawn (a hit spark marks the teleport).
- **Death:** he plays reaction 1:11 and the mission clears; demo 322 isn't played.

## 6. Checked on the PS2 (2026-11-14)

`tools/ps2_vergil.py` (harness PCSX2, the 100% save, Normal): Mission 7 started, 127's parsed door record (its next
room at the u16 after the four corners and the height) pointed at 144, and the fight logged (`+0xb9d4` state,
`+0xb9d7` action, every 0.1 s) with Dante standing still: `analysis/ref/vergil_ps2.txt` and `.mp4` (local).
Variant 0, rank 0 on Normal. What it showed, and what changed in the port:
- The states and their order: wander (0x1d, 0x1f, 0x20, 0x19) until the gauge fills (it rose 1 per 0.1 s beyond
  500 cm, the 0.167 a frame), the teleport behind (0x31) from far, then 0x21 / 0x22 / 0x25 up close.
- **0x31 goes back to 0x17**, not into 0x29 (fixed). **0x19 does come up** (the no-repeat rule above; fixed).
- The stand state lets action 0 run out (0x77) and holds (fixed).
- Durations (PS2 / port, seconds): taunt 5.6 / 5.5; 0x47 0.9 / 0.92; 0x1a 1.7 / 1.43; 0x1c 1.85 / 1.65. The
  combo's chained actions run 0.1-0.2 s short in the port [not resolved].
- **The sheathed scabbard rises behind his shoulder** (attach applied untransposed; fixed).
- After his finishers the PS2's Dante is thrown about 5 m; the port's Dante less (Dante's reactions, not Vergil's).

## Mission 13 additive variant

`vergil_boss_set_variant(1)` selects M13 before spawn; 0 retains the M7 scheduler and renderer. Native
variant-1 HP is 8000 (`5dca50 + 54 + 2c`); the room's EST [0,1] and EventTbl13 select this variant.
Rank-0 wander weights (`5df400`) are approach/strafe/stand/taunt = 20/40/20/20. Close attacks (`5dcd00`)
are states 21/22/2b = 35/35/30; middle attacks (`5dcd70`) 25/2b/2d/31 = 20/20/30/30. Guard chance is
90% (`5df8f0` variant 1). State 2b shares the away teleport, then enters 2f (`4ddbf0`); 2f (`4d9ef0`)
plays native actions 2b → 2c → 2d. All underlying motion scripts were already in em035 chunk 11.
Attack record 20 is now exported too. Beowulf model 55 and its texture bank 54 are appended to the
existing four parts; its 11 weapon bones follow their nearest matching body rest bones. The M13 DT
body (51, textures 50) is also appended and uses the body skeleton. This attachment mapping and the
missing DT coat/secondary layers are rendering approximations.

`4e6310` requests the first DT transition below 75% health. `4db950` uses actions 56 → 57 and a
240-frame transition hold for variant 1. The port follows that entry. Subsequent 600-frame active DT,
1200-frame cooldown, 0.5 HP/frame healing and half incoming damage are explicit approximations; the
original's gauge/drain and cancel rules are not yet fully decoded. DT body rendering makes this state
visible. Normal M7 calls do not enter these branches.

`run_vergil_variants.py` checks M7 isolation, native M13 HP/threshold, Beowulf action progression and
finite vertices for every part. PPSSPP `pM_m13_vergil_close` shows the second arena and native boss.

M13 reaction follow-up: a contact crossing the DT health threshold requests the transition even from
a flinch, so sustained shotgun fire cannot suppress the phase. Three interrupting heavy/light
contacts cause the Beowulf warp escape; the native escape counter is not decoded, so this count is a
port approximation. Both branches require variant 1 and leave M7 reactions unchanged.

M13 arena safety: native centre `5df970[1]` is (2500,100,2500); the authored near-level combat floor
bounds are x=1246..3751, z=1229..3750. Escape destinations and root-driven movement use a conservative
1100-cm radial limit about that centre. This approximates native room collision and prevents repeated
escapes from leaving the deck. The M7 branch retains its existing movement handling.

## Mission 20 additive variant (2)

Room 411 EST op 8 [0,2] selects variant 2, with 10000 HP (`5dca50 + 2*54 + 2c`). Its native centre
`5df970[2]` is (2500,-200,3750). The port selects it before spawn and keeps M7/M13 branches isolated.
Rank-0 wander list `5df430` is approach 65, strafe 20, idle 15. Force Edge normal lists selected by
`4e55e0` are `5ddd80` (23:40,24:40,2d:20), `5ddf00` (25:20,2d:30,30:20,31:30) and
`5de080` (30:40,31:40,3a:20). DT lists `5de260/280/2a0` choose 24/3c near (65/35), and
3c/31 beyond (35/65). Guard chance is 80% (`5df8f0[2]`).

The port's Force Edge chain 23 follows native `4d8830` actions 18→19→65; the optional follow-up is
assumed. Chain 24 follows 18→19→1b then a simplified teleport/dive; the original `4d8a20` has more
movement phases. Judgment Cut state 30 uses native 2b→2c→2d (`4da300`), with one stationary 60-frame
warning at Dante's sampled position and a 180-cm, 1000-damage burst. Summoned sword state 3a plays
native action 69, then uses that warned burst in place of the native sword projectiles. DT storm 3c
is one teleport/leap using the existing native leap cap path, rather than the original repeated
54/55/6f/70/71 dive cycle. Those effect shapes, counts and timing are approximations.

M20 phase follow-up: `4e6d20` latches `+bb58` after a damaging contact takes health strictly below
`5dfae8[2]` = **39%**. This replaces the reused 75% approximation; M13 retains its own 75% rule.
`4e6310` requests DT from Think once that latch is set, human form is active and cooldown is zero.
DT uses native actions 56→57 and a 60-frame entry hold (`4db950`), then a 600-frame active timer
(`5df950[0]`). `4dee00` clears DT at the next state boundary after expiry, sets a **2700-frame**
cooldown (`5df930[0]`), and `4e22a0` counts that cooldown down only in human form. M20 animation/root
motion advances at **1.2x** in DT (`4e22a0`); AI/phase timers retain native-frame units. These replace
the previous 1200-frame cooldown and unscaled DT animation. Healing and incoming-damage resistance
still use the documented estimates. The conservative 1650-cm arena radius, fixed three-hit escape
counter, current DT body reuse and weapon attachment remain port estimates pending the form update.

`run_vergil_variants.py` adds M20 HP/DT/Force Edge/Judgment Cut checks to the M7/M13 regression checks.
