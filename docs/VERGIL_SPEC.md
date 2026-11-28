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
- **Hit reactions:** decoded below ("Hit reactions"); Dante's per-blow gauge values are estimates. Hits from the
  front during the guard are blocked.
- **Damage taken:** Cerberus's per-hit values (42 light, 84 heavy, × 1.1; Easy × 2), guns 20%.
- **Air movement:** vertical root motion and the fixed downward Helm Breaker vector now follow the decoded update below; floor collision still uses the port's room-floor query.
- **Cloth:** the coat (pl001_02.clt, [4]: two chains on its 39-bone rig hung from body bone 3) and the scabbard's
  cord (wp005.clt, [30]: bones 3-6, axis X) run as the game's chains through `demo_cloth.c` (the cutscene cloth,
  slots 60 / 61), baked by `prepare_vergil.py`. They run in his model space, as riding the owner's root each frame
  (FUN_00230150) amounts to, with demo.c's body capsules for Dante's 24-bone layout (FUN_00230130's own capsules for
  CEm035 aren't read [inferred the same]). The M13 / M20 forms' cloth isn't run. Cost in PPSSPP (room 144, Vergil
  fighting): about 0.2 ms a frame.
- **Effects:** em035.pac entry 57 is his effect pack, exported as nfx bank 27 (`tools/native_fx.py`). V0x338 plays
  on the body's second bone as he vanishes and, turned half round, as he appears (`4dac60`, `4ddbf0`); the slashes
  `4e0fc0` starts with an action ride his root bone until the next action: 0x1a V0x2fc, 0x1b V0x2fd, 0x1c V0x2fe,
  0x47 V0x2ff, 0x2a V0x300 + V0x32e, 0x27 V0x34f (Mission 7 only). Not played: the effects' time scale (+0x14 from
  +0xb534), the turned-aside blow's V0x32d, and the other variants' effects. The effects aren't compared with a PS2
  capture yet.
- **Death:** he plays reaction 1:11 and the mission clears; the clear plays 322 (a movie, m07's clear, `main.c`).

## Hit reactions (decoded 2026-11-27)

Decompiled from a scratch copy of the Ghidra project. The chain, for one of Dante's blows:
- **The hit handler** `4e6d20` (vtable 0x613580 +0xc4) switches on the action's hit mode `+0xb9a3` (vfunc +0x30c `4e77e0`),
  which `4e0fc0` sets per action: **2** for 0x56 / 0x57 / 0x61 / 0x62 (the Devil Trigger change: the blow is turned
  aside, effect 0x32d, or 0x377 in state 0x41), **1** for 0x75 (nothing taken), **0** for the rest. Mode 0 hands the
  blow to the damage component and the hit-effect component (`+0xb87c`, vtable 0x606650, initialised with
  **`0x5dcc90`**: five 8-byte rows of u16 pairs {3, 0x15f} {3, 0x1c} / {3, 0x4}, effect-like ids [inferred]; it is
  not a reaction table).
- **The damage component** `+0xb864` (vtable 0x605730, the same class as Dante's), `2f17a0`: HP −= record +4 ×
  weapon share; the **stun gauge** (+0xb818, full 60, param block +0x30) −= record +0xc; the **flinch gauge**
  (+0xb81c, full 60, +0x34) −= record +0x14 (each × the weapon's share from the param block, × 1.1 on record +8's %
  roll). HP gone → vfunc +0x24; else the stun gauge empty → +0x28 (both refill); else the flinch gauge empty →
  +0x2c (it refills); else +0x30. `2f1750` refills both once 180 frames pass without a blow.
- **+0x28 / +0x2c** go to the AI sub-object +0x130 (`0032a540` / `0032a800`), which send a **reaction code** to
  setState (vfunc +0x2f8, `4dee00`): by the blow's kind (5 → 4/5, 3 → 7, 6 → 0xc) or, for the rest, by where it came
  from: **0 / 2 from the front, 1 / 3 from behind** (the flinch / the stun gauge). In the air (+0xb9a0) the flinch
  gauge sends 7 / 8 / 9. **+0x30** (`4e6b20`) only sends 8 for a blow in the air during a reaction: never for him.
- **The gate** `4e51e0` (in setState, codes 0-0xc, 0x12, 0x13): no reaction for a shot (+0xbb52, the blow's byte 2),
  in actions 0x27 / 0x5f / 0x6f (+0xb9a0), in Devil Trigger's form, in states 0x44 / 0x32. Then `4e52f0`: frames
  flinching (+0xbb0c) past their limit (+0xbb10) → **the brace**; already flinching (0x36 / 0x37) → flinch again;
  the body script's **window 1 byte 4 = 1** (the finishers' tails, 0x47, 0x52, the dives' recoveries) → a new budget
  of **600 frames and 12 points** (gp-0x576c / -0x5764; 900 / 15 from states 0x34 / 0x35, `4e77f0`, unreached in
  Mission 7) and flinch; anywhere else (standing, strafing, the start of an attack) → the brace.
- **States:** 0x36 (`4db5d0`) plays reaction **0x11 or 2** (50/50), 0x37 (`4db750`) reaction **3** (codes 1 / 3,
  unless already in 0x37); both run the budget's clock and brace once its points pass 12 (`4e5540`; a blow while
  flinching adds 1, 2 if its +0xc is 60 or more). **The brace, 0x32** (`4daf90`): action **0x3d** (vr3-40), facing
  Dante, both gauges held full; after 60 frames a lunge is answered (guard / away); its end → think.

In the port: all of the above, except that Dante's attack records aren't decoded (pl chunk 7 holds his collision
shapes, not records), so a light blow takes **20 / 30** from the gauges and a heavy one (finishers, launch, slam)
**60 / 60** [estimates], guns 20% of that; the record +8 roll and the blow kinds 3 / 5 / 6 aren't read. Checked in
PPSSPP (`vr_react`, Mission 7, `onvergil 30`, Rebellion every 10 frames): flinches in 0x1a / 0x1c / 0x47's tails,
re-flinches inside the budget, the brace on blows at 0x18's start.

## 6. Checked on the PS2 (2026-11-14)

`tools/ps2_vergil.py` (harness PCSX2, the 100% save, Normal): Mission 7 started, 127's parsed door record (its next
room at the u16 after the four corners and the height) pointed at 144, and the fight logged (`+0xb9d4` state,
`+0xb9d7` action, every 0.1 s) with Dante standing still: `analysis/ref/vergil_ps2.txt` and `.mp4` (local).
Variant 0, rank 0 on Normal. What it showed, and what changed in the port:
- The states and their order: wander (0x1d, 0x1f, 0x20, 0x19) until the gauge fills (it rose 1 per 0.1 s beyond
  500 cm, the 0.167 a frame), the teleport behind (0x31) from far, then 0x21 / 0x22 / 0x25 up close.
- **0x31 goes back to 0x17**, not into 0x29 (fixed). **0x19 does come up** (the no-repeat rule above; fixed).
- The stand state lets action 0 run out (0x77) and holds (fixed).
- Durations (PS2 / port, seconds): taunt 5.6 / 5.5; 0x47 0.9 / 0.92; 0x1a 1.7 / 1.43; 0x1c 1.85 / 1.65 (the port
  as it was then).
- Re-measured in the port on 2026-11-14 (VERGIL_LOG, Mission 7, Dante standing still, eight combos): 0x18 9 ticks,
  0x19 11, 0x1a 47 (1.57 s), 0x1b 17, 0x1c 53 (1.77 s), 0x47 30 (1.0 s); 30 ticks a second. Each finisher plays
  one motion to its end (its script's last wait is "finish, frame -1"): 0x1a is vr3-05 from frame 39 (its loop
  start, where a play with frame -1 begins) to 131, 0x1c vr3-07 from 72 to 174, 0x47 vr3-50 from 132 to 189. Against
  the PS2's 0.1 s samples the finishers are now 0.08-0.13 s short, within that log's resolution. A frame-exact PS2
  log wasn't taken: with this build's harness, `tools/ps2_vergil.py`'s door redirect (the text of 127's door to 114)
  no longer takes Dante to 144, and the parsed record wasn't found [open].
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

The port's Force Edge chain 23 follows native `4d8830` actions 18→19→65; the follow-up condition
is variant 2 (`4e5590`), now established rather than assumed. Chain 24 follows 18→19→1b, a single
native dive sequence, then the 66→67→68 retreat. Its intermediate turn/translation details remain
simplified. State 30 Judgment Cut uses 2b→2c, rank-0 hold **8** (`5df9a0`), **three** releases
(`61a948`), then 2d. The old single 60-frame burst is replaced. Spatial cut radii and 1000-damage
bursts remain approximations to the native effect actor.

State **3a is Round Trip**, not summoned swords. `4df700` releases the thrown Force Edge from action
69 when motion-script event slot 1 byte 4 equals **3**. That previously discarded byte is now exported
as an additional event opcode; its original byte-0 consumers remain unchanged. The sword uses its
native model, 20 cm/frame outgoing/return speed (`61a9bc`, GP-derived `61a9bc`), 5461 turn units/frame
spin (`4fa510`), 100-frame caught hold and 10-frame re-hit timer (`61a9c4`, `61a9c0`), attack record 10
and a 100-cm return/catch distance (`4fa9e0`). The 2000-cm no-hit travel cap, chest launch point,
simplified spherical collision and absent deflection/wall response remain approximations.

States 3b/3c (`4dcdb0` / `4dd290`) now use native actions **54→55→6f→70→71**, rather than one 5f/60 leap.
Single dive height is 600 cm; the four-dive storm starts at 1000 cm and reduces evenly to 350 cm
(`61a914`, `61a930`, `61a938/93c`). Teleport travel is 50 cm/frame. Fall starts at 2, accelerates by
0.5 to 40; motion event byte 4 == **2** switches to 40, accelerating by 4 to 100 (`61a8fc..90c`).
The native collision solver is approximated by the arena floor height; camera shake/trails are omitted.

The latched low-health human scheduler now uses `5dde40`, `5ddfc0`, `5de120`, including area-cut states
3f/40. `4ddff0` actions 6a→6b→6c→6e and 15-frame intervals (`61a970`) drive three random-cut waves
(rank-0 counters 7 and 3), or one ring followed by targeted cuts (counters 12 and 5). The decrement-
before-spawn convention gives 6 cuts/wave or 11 ring + 4 targeted cuts. Target placement and instantaneous
bursts approximate the native moving effect actors; the final 60-frame recovery is retained.

M20 phase follow-up: `4e6d20` latches `+bb58` after a damaging contact takes health strictly below
`5dfae8[2]` = **39%**. This replaces the reused 75% approximation; M13 retains its own 75% rule.
`4e6310` requests DT from Think once that latch is set, human form is active and cooldown is zero.
DT uses native actions 56→57 and a 60-frame entry hold (`4db950`), then a 600-frame active timer
(`5df950[0]`). `4dee00` clears DT at the next state boundary after expiry, sets a **2700-frame**
cooldown (`5df930[0]`), and `4e22a0` counts that cooldown down only in human form. M20 animation/root
motion advances at **1.2x** in DT (`4e22a0`); AI/phase timers retain native-frame units. These replace
the previous 1200-frame cooldown and unscaled DT animation. The DT parameter block `5dcbf4` has 50
rather than 100 at +20/+24, supporting the existing half-damage resistance. Healing remains an estimate. The conservative 1650-cm arena radius, fixed three-hit escape
counter, weapon attachment selection remain port estimates; the DT form update below replaces body reuse.

`run_vergil_variants.py` adds M20 HP/DT/Force Edge/Judgment Cut checks to the M7/M13 regression checks.

M20 DT form follow-up: the earlier chunk-51 stand-in was a Beowulf body. Variant 2's Yamato DT
setup (`4d0d60`, `4d3130`, `4e5ea0`) uses body **15**, with attachments **16/17/18**, and texture
bank **14**. These are now exported separately, appended after the existing parts and rendered
only for M20 DT. The 24-bone body follows the native motion sample; the 15/4/31-bone attachments
use authored rest poses, so their native cloth drivers remain an approximation. M7/M13 mesh choices
are unchanged, and invisible new parts are not skinned for those fights. Force Edge now has its
own pose, with native carry record `5dc160` (bone 14) and draw record `5dc130` (bone 13), replacing
the shared Yamato pose. Draw-mode selection by action range is still an approximation. Active Round Trip replaces that carry pose
with the independently moving/spinning sword pose.

Area-cut form detail (`4ddff0`): entering 3f/40 from human form sets the form-toggle latch before
6a, then toggles back before 6e. These attacks now temporarily display the native DT form and use
its animation rate/resistance without granting a fresh maintained-DT duration. Human cooldown is
paused during this temporary form and resumes on recovery. Entering from maintained DT leaves its
existing duration intact. A host regression covers the temporary form's entry, recovery and paused
cooldown. This replaces another form transition omitted by the first M20 port.

## Air movement follow-up

The earlier gravity=1 and destination-fitted arc were wrong. `4e4570` adds the complete model root delta
(X/Y/Z) to the actor. `4e22a0` at `4e27a0` then subtracts **10 from Y per native update**, except hidden
travel action 55 or flag +b9a0. `4e0fc0` sets that flag for **27 / 5f / 6f** and clears it for other actions.
This is a constant floor pull, not acceleration. The port now consumes vertical root motion and removes
it from the rendered root, preserving the takeoff height in the actor/collision position.

State 29 (`4d9680`) plays 27 until window 1 byte 0 is 1. That motion rises about 209 cm by its chain point.
State 3d (`4dd840`) then sets action 5f and rotates (0,0,45) using the absolute vertical target separation
(`131b50`) and a pitch clamped to 25..75 degrees. The rotation helper `131760` has Z basis (0,-sin,cos):
**the resulting Y velocity is negative**, not an upward launch. This vector is added while window 1 byte 0
is 1; it is never accelerated or fitted to Dante's destination. `3599f0` runs outside that window too,
lands within floor+10 and changes to 60. The room-floor approximation is retained for the collision query.

`run_vergil_variants.py` checks takeoff height, unchanged negative dive velocity, landing/recovery and the
existing M7/M13/M20 regressions. The existing `arforce` test command now routes to Vergil's native state
when he is active, allowing the uppercut-to-dive chain to be checked in his actual fight.
