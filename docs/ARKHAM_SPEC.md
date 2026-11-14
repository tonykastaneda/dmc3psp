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
- **Hits on Dante:** each tentacle state hurts Dante if he's on that side at the script's attack frame (500, heavy);
  the wide attack at frame 20 within 900 and 60° (700); each missile volley is three homing shells (300). The
  records' damage isn't read.
- **Legions:** four at frames 90 and 165 of the summon [the count is an estimate]; they fly at Dante at 8 a frame
  and burst on touching him (400); one blade hit cuts one down. Drawn rigid. The summon clock's reload is 900 frames
  [estimate].
- **Damage taken:** as Vergil's (42 light, 84 heavy, × 1.1; Easy × 2).
- **The second round:** at 9000 he pauses 120 frames (demo 53 isn't played) and fights on at 9000; Vergil doesn't
  join, and Dante keeps his whole moveset.
- **Death:** the room clears and Mission Clear follows (demo 328 isn't played).

## 5. Checked in PPSSPP (2026-11-14)

Room 421, Mission 19 (`tools/tests/mission19_arkham.scene`): he roars, summons Legions that reach Dante and burst,
120 heavy test hits bring the second round at 9000, 110 more and Mission Clear shows. Not compared with the PS2 yet.
