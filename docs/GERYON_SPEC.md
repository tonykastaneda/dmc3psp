# Geryon (CEm029): the Mission 12 boss spec

All addresses are in the PS2 SE executable (`SLUS_213.61`). Object offsets are byte offsets from the object base.
**Decoded** means read from code or tables; **inferred** means interpretation; **approximation** means the port
does something simpler. A line that doesn't say is decoded. Values are quoted at **AI rank 0**.

---

## 1. Which class is Geryon

- EST spawn id **50** → class **32** (byte table `0x599690`) → the factory `2acf40` calls `381710`, which allocates
  0x3c80 bytes and constructs **CEm029** (`381e10`, vtable `0x608560`): the horse. Its carriage is a second object,
  **CEm029Cart** (built through `4a3430`), kept at `+0x910`.
- Pack: **GDATA 134, em029.pac** (a PNST).
- Room **228** (EST group 0, at (3450, 0, 2900)); the event script runs it twice, phase 0 on the bridge and phase 1
  on the ground below.
- Update `3820f0`: alive `383b70`. Phase byte `+0x972`: 0 the bridge, 1 the ground, 2 other uses. Action `+5`,
  its sub-step `+6`; `3856b0` sets the action, `3856f0` starts its script (bank 0, the action).

## 2. Pack (em029.pac)

| Entry | Content |
| --- | --- |
| [0] | 6 textures |
| [3] [5] [7] | Shells: the missile (2 bones), the red sphere, a small one |
| [9] | The horse: 33 bones, 2,165 triangles; faces +z, 452 tall |
| [11] | The horse's 21 motions (`gh00`-`gh20`; ends 249, 249, 249, 144, 29, 29, 29, 83, 47, 55, 292, 343, 430, 249, 80, 135, 29, 29, 29, 249, 249) |
| [12] [16] | Their scripts (mode-0 tables); one per action [inferred: action *n* plays motion *n*] |
| [13] | The carriage: 19 bones, 3,737 triangles (z −1015 to 114: it trails the hitch at the horse's origin) |
| [15] | The carriage's 21 motions (`gc00`-`gc20`) |

The carriage's bones 1 and 2 carry both channel triples, with **3-5 holding the bone's place** (bone 1 about
(0, 0, −612), with x swinging to ±52 on the gallops) and 6-8 its turn [inferred from the values; every other bone
has 3-5 rotation, 6-8 translation].

## 3. Health and phases

- Max health **9000** (`DAT_005d6c1c`). Phase 0 leaves the bridge at **80%** (7200); the value is carried to phase 1
  through `DAT_006193ac` (phase 1's default is 7000).
- Phase 0 is placed at **(2950, 7016, 2950)**, turned along the bridge line (400, 7016, 400) → (5600, 7016, 5600).
- Phase 1 is placed at **(2500, 0, 2500)**; the gallops ring the centre `0x5b60c0` (2500, 0, 2500).
- AI rank `+0x8eb` from the thresholds 300 / 500 / 800.

## 4. Phase 0, the bridge (`003853c0`)

| Action | Handler | What it does |
| --- | --- | --- |
| 0 | `4bf9e0` | Idle → 3 |
| 1 | `4bfa90` | → 3 |
| 3 think | `4bfb20` | At rank 0 every third pick (counter `+0x975` % 3 == 0) → 4, else 0x12 |
| 4 | `4bfc40` | The charge along the bridge (→ 7 or 8; 1000 past Dante) |
| 7 | `4bfd70` | Turning round (→ 0xf / 0) |
| 8 | `4bfee0` | Effect 0x26d, → 9 |
| 9 | `4c01c0` | → 4 or 0x12 |
| 0xb | `4c0410` | Shells 0x215 (the missiles), → 3 |
| 0xd, 0xe | `4c0740` | 0xd → 0xe: effect 0x322, the shockwave, → 3 |
| 0x12 | `4c0960` | → 8 |
| 0x14 | | → 3 |

## 5. Phase 1, the ground (`00385530`)

| Action | Handler | What it does |
| --- | --- | --- |
| 3 think | `4c0c50` | Beyond 800 from the centre and facing it → 8; below 40% health, flag 0x400 half the time; with flag 0x80 → 4, else a gallop, 0x10 and 0x11 alternating (`+0x97a`) |
| 4 | `4c0fd0` | Straight run at 25 a frame for 65 frames (30 with flag 0x800, 10 with 0x100) → 8, 10 or a gallop |
| 5, 6 | | Turns, → 4 |
| 8 | `4c1560` | Red spheres through `4cbdb0` (3 at rank 0, 5 above); then hidden 120 frames and back → 9 |
| 9 | | → 0x12, 7 or 4 |
| 10 | | → 0xb |
| 0xb | | Missiles (shell 0x215) → 3 |
| 0xe | | Shockwave, effect 0x322 at 600 → 0x13 / 0xc |
| 0x10, 0x11 | | The gallop: 25 a frame round the centre. Beyond 1800 it steers for a heading 0xbd29 (−94°) off the outward direction, inside 1700 0xc16d (−88°); it turns 728 units a frame beyond 1900, else 182. The lap counter `+0x973` starts at 40 and counts the script's windows down, then → 5 / 6 |

## 6. In the port (`psp/testroom/geryon_boss.c`, `tools/prepare_geryon.py`)

Sections 3-5 at rank 0. Approximations:
- **Motions:** action *n* plays `gh`*n* and `gc`*n*; the scripts ([12], [16]) aren't run, so no windows: the gallop
  runs **12 motion loops** before turning, not 40 windows.
- **Phase 0:** think → the charge (every third) or the cast (0x12 → 8: the spheres) → the charge. The charge runs
  the whole bridge line (400 to 5600) or to 1000 past Dante, then 7 turns him round. The shockwave comes when Dante
  has stayed within 500 for 90 frames [inferred trigger].
- **Phase 1:** gallop laps → turn (5 / 6) → alternately the straight run or the spheres (then hidden 120 frames,
  back on the ring behind Dante, then two volleys of 4 missiles). The missiles rise 30 frames and home at 18 a
  frame; the spheres drift at Dante at 7 a frame and hurt (they don't stop time).
- **Damage he deals** (the attack records aren't decoded): running into Dante 600, heavy; a missile 300, heavy; a
  sphere 150; the shockwave 500, heavy.
- **Damage he takes:** as Vergil's (42 light, 84 heavy, × 1.1; Easy × 2; guns 20%). Swords hit the point of his body
  (the horse's spine, then the carriage) nearest Dante.
- **The fall:** at 7200 he runs off the bridge's far end, vanishes, and phase 1 starts with Dante at 228's start 2
  (1438, 0, 2004). Demo 33 and the bridge falling aren't shown.
- **Death:** reaction 0x13 plays and the room's clear leads to Mission Clear; demo 34, the Quicksilver (item 34),
  door 1 and demo 35 aren't played.
- **Effects:** 0x26d and 0x322 are a hit spark; the horse's flames aren't drawn.

## 7. Checked in PPSSPP (2026-11-14)

`tools/tests/mission12_geryon.scene` (room 228, Normal): phase 0 charges down the bridge and turns, shockwaves when
Dante stays close; 20 heavy test hits take him under 80%, he leaves, Dante lands at start 2 and phase 1 gallops,
turns and charges; 80 more and Mission Clear shows. Dante's own sword hits register (`swing`). Not compared with
the PS2 yet.
