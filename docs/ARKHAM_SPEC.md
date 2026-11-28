# Arkham (class 35): the Mission 19 boss spec

All addresses are in the PS2 SE executable (`SLUS_213.61`). Object offsets are byte offsets from the object base.
**Decoded** means read from code or tables; **inferred** means interpretation; **approximation** means the port
does something simpler. A line that doesn't say is decoded. Values are quoted at **variant 0** (`+0x15d0`, the AI
value under 700).

## 1. Class and pack

- EST spawn id **53** → class **35** → pack 23 → **GDATA 137**. The factory `2acf40` case 0x23 calls `3d0b70`
  (0x3600 bytes), constructor `3d1030`, vtable `0x60a9a0`; update `3d1390` (0 set-up `3d1400`, 1 alive `3d2220`).
  His Legions are EST 54 → class 36 (factory case 0x24, `3e8f60`), pack 24, the same GDATA 137.
- The pack (a PAC): [0] 4 textures; [1] Arkham's body, 39 bones (y −730 to 663 about its origin); [2] his outer shell,
  12 bones; [3] the body's motions (51 slots), [4] the shell's (the same slots, and 36-38 of its own); [6] the motion
  scripts (state *n* plays script *n* on both: `FUN_003d3a50(this, 0, n, layer)`); [7] [8] caps; [11] the Legions'
  5 textures; [12] a Legion, 8 bones (cloth `em033_00.clt` in [13]); [15] a 7-bone piece (`em033_01.clt`).
- **Health 18000** (`DAT_005c1e6c`, kept at `+0x1c24`). The room's first group ends at 9000 (demo 53: Vergil joins);
  group 1 is Arkham again, set to **9000** (`0x460ca000`).
- **The second round** (EventTbl19 room 421, counter 1, after demo 53): spawn_group 1 (Arkham, his spawn record's op 8
  (0, 1): the round's 9000), then **op 0x7e [0]** (`FUN_0020be20` → `FUN_0025e540`): a second player object is made
  (`FUN_00204210(1, 1, 1)`, kept at Dante +0x3d44 and the game's +0x10), placed by Dante, both flagged partners
  (+0x3c7b), Dante's mode +0x28aa = 3: **Vergil joins as a player character** (the partner a second pad drives,
  or the game's partner AI), not as an enemy class. **op 0x78 [23]** (`FUN_00211150`) shows hint 23 once (bit 23
  of the save's +0x70). With no second pad, he **replays Dante's input** a frame late (`FUN_0021fe30` in mode 3: the input recorder
  `FUN_00489590` / `FUN_00489670`, the Doppelganger's, delayed `0x594bd8`[level]: 1, 30, 60 frames).
  **In the port** (`main.c` partner, `vergil_boss_draw_partner`): as the second round begins (9000) a second Player /
  Combat beside Dante takes Dante's input a frame late and hits Arkham as Dante's blows do. Stand-ins: Dante's moveset
  (Vergil's player moveset, `CPlVergil` / pl001, isn't ported) on Vergil's body (em035 [1], Dante's bone layout),
  untouchable, his shots not fired. He wears the boss's coat (its .clt chains, as VERGIL_SPEC) and scabbard, and
  Yamato is in his right hand while a move plays or the sword is out, else sheathed (the boss's attach records).
- The phase byte `+0x15d1` follows his health (`3d4540`): over 9000 → 0, over 3000 → 1, else 2.

## 2. The states (`+0x1424`, `3d2220`'s switch; `3d3760` sets one and starts its script)

| State | Script (body / shell motion) | What it is |
| --- | --- | --- |
| 0, 1 | 0:0, 0:1 (0, 1) | Idle, moving |
| 2 | 0:2 (1) | Turning (120 frames) |
| 3 → 4 → 5 | 0:3-0:5 (2, 3, 4) | |
| 6 → 7 | 0:6, 0:7 (20, 21) | Recovering (after the missiles) |
| 10 | 0:10 | Reaction |
| 0xb | 0:11 (30; waits 90, 165) | **Summoning the Legions** |
| 0xc | 0:12 (10) | Roar |
| 0xd, 0xe | 0:13 (34, record 0 at 105), 0:14 (35, record 1 at 40) | Tentacles on his right (0xe from phase 1) |
| 0xf | 0:15 (31, 45) | Tentacles in front |
| 0x10, 0x11 | 0:16 (32, 105), 0:17 (33, 45) | Tentacles on his left (0x11 from phase 1) |
| 0x12 → 0x13 → 0x14 | 0:18-0:20 (shell 36, 37, 38) | The body turns green, then the wide attack in front |
| 0x15 → 0x16 → 6 | 0:21, 0:22 (39, 40; record 5 at 25 and 95) | **The homing red missiles** |
| 0x17 → 0x18 | 0:23, 0:24 | (from 7) |

What follows each when its script ends: `3d7cc0` (0 → 1, 3 → 4 → 5, 6 → 7, 0x13 → 0x14, 0x15 → 0x16 → 6,
0x17 → 0x18, the rest → 0).

## 3. The choice (`3d3ae0`, raised as flags `+0x1ceb`-`+0x1cf5`, taken by `3d7930`)

Four timers fill while Dante is on that side of him, within **700** (`FUN_00359b40`), and empty while he isn't:
front (a 40° wedge, `+0x1cbc`), back (100°, `+0x1cc0`), left (110° about +75°, `+0x1cc4`), right (110° about −75°,
`+0x1cc8`); a fifth (`+0x1ccc`) fills while he's on none of them. Only checked in states 0-2. Limits
(`DAT_005c22d0`, by phase): front 40 / 30 / 20 → **0xf**; right 50 / 40 / 30 → **0xd** (0xe); left 50 / 40 / 30 →
**0x10** (0x11); back 50 / 40 / 30 → **2**, the turn. When one fires, a roll may change it: 0 / 30 / 50% → **0x12**
(the green wide attack, `0x5c25c0`), 0 / 5 / 8% → **0x15** (the missiles, `0x5c2620`). The away timer's limit
200 / 150 / 120: 20 / 30 / 40% → **0xc** (`0x5c25f0`), 0 / 15 / 20% → **0x15** (`0x5c2650`).

**Legions** (`3d4ca0`): flag `+0x1cee` → **0xb** when his clock `+0x15e4` runs out, or the damage taken since the last
summon passes 2000 / 1800 / 1600 (`0x5c2270`). At most 40 at a time (`0x28`).

## 4. In the port (`psp/testroom/arkham_boss.c`, `tools/prepare_arkham.py`)

Sections 1-3 at variant 0. Approximations:
- **He stays where the EST puts him** (2650, −10, 3200), his origin 730 over the floor; he only turns (state 2,
  0x200 a frame [estimate]).
- **Hits on Dante:** the side-zone reach and one contact check at the event frame still approximate the native
  moving collision caps. Damage/kind now come from their native records (below). Missiles still use three
  simplified homing shells, with native record 5 (1500). The former 700-damage wide burst is replaced by the
  decoded mouth capture, hold and drain; full cap-flag/held-handle release is approximated by a damaging rescue hit.
- **Legions:** four/five/five by phase at frames 90 and 165 (count table 5c23b8, 4e9b50), with native
  summon-clock reloads 3000/2700/2400 (5c2390, 3d7dc0). They still fly at an estimated 8 per frame and die to one
  blade hit. **Their pose:** CEm033 (ctor `3e9340`, vtable `0x60bad0`, alive update `3e9cd0` with its own states
  0-10) has no motions; its tail, bones 2-6, is a chain (+0x9e0, `FUN_0022f1d0`; em033_00.clt: gravity −0.08,
  spring 0.02, max speed 50, stiffness 0.45, axis Z) ridden with the root, so it only settles under gravity, the same
  in every Legion. The port settles it once (600 steps) and draws them all in that pose. Not shown: the settling as
  one appears, and `3ed840`'s pull on a bone (−70 y, −20 z) in its states 4 / 5. Their contact uses native radius 40 and record 0 (300); the combined
  horizontal/height collision test remains simpler than the native collision engine.
- **Damage taken:** as Vergil's (42 light, 84 heavy, × 1.1; Easy × 2).
- **The second round:** at 9000 he pauses 120 frames, demo 53 plays (`main.c`, queued as the partner joins), and he
  fights on at 9000; Vergil joins
  as above.
- **Death:** the room clears; the clear plays movie 328 (m19_c00, `main.c`), then Mission Clear.

## 5. Checked in PPSSPP (2026-11-14)

Room 421, Mission 19 (`tools/tests/mission19_arkham.scene`): he roars, summons Legions that reach Dante and burst,
120 heavy test hits bring the second round at 9000, 110 more and Mission Clear shows. Not compared with the PS2 yet.

## Attack damage follow-up

The absent attack-record opcodes in the front/left scripts are deliberate. Shell event slot 0 byte 0
has values **4 / 5 / 6** after waits **45 / 105 / 45** in scripts 15/16/17. Their C++ handlers select the
records themselves:

| State | Handler | Record at 5c1c80 + n*0x40 | Damage | Kind / reaction | Enabled caps / duration |
| --- | --- | --- | --- | --- | --- |
| 0f front | 3d6710 | 2 | 1200 | 2 / 600 | 7,8,9,13,14 / 10 frames |
| 10 left | 3d6830 | 3 | 800 | 2 / 300 | same / 30 frames |
| 11 left | 3d6950 | 4 | 800 | 2 / 300 | same / 25 frames |

`2949c0` puts table+n*0x40 into the cap set's current record (+c8), `2949f0` resets its hit id, and `294370`
enables those caps. The set is shell +1950, bound to 5c1c80 by `3d1400`; scripts alone cannot reveal these
record selections. The port exports the records/events and passes kind 2 through the enemy damage bridge,
replacing the estimated 500 and generic reaction. Right-side records 0/1 and missile record 5 use the same
exported table.

### Green capture and release

State **12** (`3d6a70`) checks full 3D distance (`132470`) to **shell bone 9**: pointer array +64c, slot +670.
It can take Dante through `21f2e0(player,3,held_bank)` before event 7 at shell motion frame **40**. Range table
**5c2560**, variant 0 by phase, is **150 / 150 / 200**. Missing the capture returns to idle, rather than
unconditionally entering a damaging wide burst.

State **13** (`3d6c70`) enables cap 15, keeps Dante attached, waits until its **60-frame** timer is below zero,
then calls **21ecc0(frames * rate, player)** every update. That helper subtracts HP at player +2ad4; table
**5c2590** gives **5 / 10 / 15 HP per native frame** for variant 0's phases. This damage is not an attack
record. State **14** (`3d7090`) only plays the release animation and an effect; it has no separate damage burst.

The attachment (`3d6f00`) is shell bone 9 plus rotated offset **(100,-80,-110)**, facing Arkham+180 degrees.
The player's held type 3 follows a supplied motion bank without an independent button-mash escape. Chunk
**[5] motion 30** is the native 24-bone held pose; it is now exported as `ah30` and used through the existing
combat hold interface. The native owner checks sixteen cap contact flags (mask 0x1) and whether its held handle
is still live to release; until those flags are wired, a damaging hit on Arkham supplies the rescue signal.
Capture eligibility and interruption use the port's existing combat plumbing; his actor's vertical origin
still has its documented 730-unit placement approximation.

### Legion contact

**3ed200** constructs each Legion's collision set at +bd0, binds **5db9d0**, explicitly selects **record 0**
with `2949c0` and resets the hit id with `2949f0`. It creates a type-2 sphere of **radius 40** at +e20 and
enables cap 0 indefinitely. Record 0 is **300 damage, kind 0, reaction 100**. Thus no Legion script opcode
is required either. The port now exports that record instead of relying on its formerly hard-coded value.

`python tools/tests/run_arkham_attacks.py` checks event boundaries, native front/left damage/kinds,
Legion radius/contact, capture ranges, all three drain rates after the grace period, native held motion,
one-shot hold notifications and the harmless release state.

PPSSPP verification (2026-11-27, Mission 19 room 421): forced front/left/left2 states
landed 1200/800/800 with kind 2; summoned Legions landed 300 with kind 0. A forced
capture held Dante in `ah30`, drained 690 HP after the grace period, and released on
the rescue hit (HUD 9310/10000). `arkham_camera_close_40.png` and
`arkham_checked_left_65.png` show the boss in the actual fight. Earlier `_view`
captures were occluded by room geometry and are not visual evidence. The test hook
forces the hold state; capture distance/phase ranges are additionally checked by the
host test, rather than claimed as an unforced gameplay capture.

After merging master (`26b767c`, through `e72ef80`), a full
`pwsh -File tools/build_testroom.ps1 -Regenerate` completed at 07:02 on Nov 27
(EBOOT 30,856,539 bytes). Host attacks and credit checks passed again. PPSSPP
Mission 19 room 421 rechecks `arkham_merged_front`, `arkham_merged_legions`, and
`arkham_merged_grab` retained front 1200/kind2, Legion 300/kind0, and the 690-HP
grab drain followed by rescue/release (9310/10000 HP). Inspected captures:
`analysis/testroom/arkham_merged_front_40.png`,
`analysis/testroom/arkham_merged_legions_130.png`,
`analysis/testroom/arkham_merged_grab_85.png` and `_150.png`.
The full build also reported unrelated cutscene room/demo memory-budget warnings
for rooms 237, 228, 111, 001 and DSTG_035 (128 bytes over); these were not changed
as part of this boss decoding task.

### Tentacle caps (decoded, not yet used)

em033 [7] / [8]: 16 caps of {kind, bone, shape}, type-2 spheres. 0 bone 3 (0,70,0) r232 and 1 bone 2 (0,-100,-130) r208
(kind 6, push); kind 1 (attack): 2-6, 10, 11 on bone 30 (y 0 / 60 / -60 / -130 / 126 / 4 / -102), 12 on bone 28, all
r58-60; 7, 8, 9, 13, 14 on bone 8 (y 0 / 60 / -60 / -120 / 120) r60; 15 bone 4, a type-4 capsule. Scripts 0:13 / 0:14
switch on 2-6, 10-12 for 25 / 30 frames from motion frames 105 / 40; the front / left handlers' caps are 7-9, 13, 14.
A port of them on the body's bones (2026-11-27) put cap 7 about 1000 above a Dante 450 in front during 0xf's window
(at 2813, 946, 3216), so the set's bone list isn't the body's as the port poses it [open: which model +1950 binds,
FUN_002942e0]; the zone test stays.
