# The Doppelganger (CEm034): the Mission 17 boss spec

All addresses are in the PS2 SE executable (`SLUS_213.61`). Object offsets are byte offsets from the object base.
**Decoded** means read from code or tables; **inferred** means interpretation; **approximation** means the port
does something simpler. A line that doesn't say is decoded. Values are quoted at **AI rank 0**.

## 1. Class and pack

- EST spawn id **52** → class **34** (byte table `0x599690`) → pack 22 → **GDATA 136, em034.pac**. The factory
  `2acf40` case 0x22 calls `3bc990` (0x2240 bytes, pool `0x6a2530`), constructor `3bcf80`, vtable `0x60a040`.
- Update `3bd190`: set-up (`vfunc+0x130` = `3bd1f0`), then alive `3be4f0` every frame.
- em034.pac (a PAC): [0] 4 textures; [1] the body, 24 bones in Dante's layout (6,028 triangles; tagged "EFM " but laid
  out as a MOD); [2] the coat, 9 bones (cloth `em031_01.clt` in [4]); [3] the sword, 1 bone; [5] 34 motions;
  [7] the motion scripts (the player format, one bank of 54); [8] [9] collision caps and shapes.
- **Health 10000**: the parameter block `0x5c2ab0` +0x2c (the reading that gives Vergil 8000 and Cerberus 7200).

## 2. The light (room 139's # GAME)

- Six crests, **BROCKEN 0-5**, on the wall at y 225 round the arena's middle (2500, 2500): (2500, 1350), (3496, 1925),
  (3496, 3075), (2500, 3650), (1504, 3075), (1504, 1925). Hit sphere 160. Struck, the shutter opens and its LIGHT set
  (10-15: range 1250, force 750) shines. **Crest 2 starts open** (`lightflg 1`). BROCKEN 6 (model 8) lights the
  middle when all are open.
- The AI keeps seven flags at `+0x221a` (`3c4bf0`): six sixths of the arena and the middle. `DAT_005b7a30` pairs each
  crest with the one opposite (3, 4, 5, 0, 1, 2).
- `3c37b0`: all seven lit → state **0x23**, which goes to the body blast (0x10-0x12) that puts every crest out.
  `3c3850`: the crest he's heading for (`+0x21fc`) lit and within 400 → **0x24** → the meteor (0xd / 0xe) onto it.

## 3. The states (`+0x20d0`, `3be4f0`'s switch; `3c3d50` sets one)

| State | Handler | Scripts (motions) | What it is |
| --- | --- | --- | --- |
| 1 | `3c07f0` | 9, 10 (motions 7, 8) | Rising out of the floor |
| 2 | `3becf0` | 0 (0) | Think: turns 0x100 a frame, then `3c3180` picks |
| 3, 4 | `3beea0`, `3bef50` | 1, 2 (walk 1, run 2) | Moving |
| 5-7 → 8, 9 → 10 | `3bf080`, `3bf2a0`, `3bf4e0` | 3 / 0x29, 4 / 0x2a, 5 (motions 3, 4, 5) | Rebellion's combo; attack records 0, 1, 2 (7, 8 in the other set) |
| 0xd, 0xe | `3bf740`, `3bf750` | 0xf-0x12 (motion 9) | The meteor onto a lit crest: it shuts |
| 0x10-0x12 | `3bfe90` | 0x13-0x16, 6-8, 0xb, 0xc (motions 10, 11) | The body blast: every crest shuts |
| 0x17 | `3c0b80` | 0x1d-0x1f (motions 17-19; effect 0x9f) | The approach |
| 0x1b, 0x1c | `3c1370` | 0x23, 0x24 (motion 23; record 4; effect 0x293) | Stinger |
| 0x1d | `3c16d0` | 0x2b-0x2f, 0x33 | The blast at the crest he's standing by |
| 0x1e-0x22 | `3c1fc0`, `3c2190`... | 0x25-0x28 (motions 25-28) | Sinking into his shadow and moving in the dark |
| 0x25, 0x26 | `3c2a80`, `3c2d70` | 0x2e, 0x34, 0x35 (motion 33, record 0xb) | Devil Trigger answer, taunt |

**The pick** (`3c3180`), in order:
1. `3c4dd0`: his own sixth is lit → the nearest dark one, states 0x1e/0x20/0x21/0x22 by the direction bits.
2. `3c4ec0`: Dante's sixth is dark and not his → **0x1f**: up out of Dante's shadow (0x25 if Dante's in Devil Trigger).
3. `3c35c0`: his sixth dark, Dante's lit → a move by distance (70 / 170 / 130-180 bands).
4. `3c3240`: a lit crest next to a dark one → go and shut it (0xd/0xe, or 0x1d if he's there); else by distance
   (bands `0x5c2b80`+0x24: 300, 470, 700, 879): **within 470** a combo from list `0x5b7b50` (5 ×6, 6 ×5, 7 ×1);
   **to 700** Stinger (0x1b/0x1c); **beyond** the approach (0x17).

## 4. In the port (`psp/testroom/doppelganger_boss.c`, `tools/prepare_doppelganger.py`)

The pick of section 3 and the light of section 2, at rank 0. Approximations:
- **Which sixth:** the crest nearest by angle round the middle; within 550 of the middle is the middle [estimate].
- **Damage** only lands while his sixth is lit or he's reeling; otherwise the blade goes through him (the
  walkthrough). A crest opening on him stuns him for 150 frames (reaction motions 26-28) [the stun's form and length
  are the port's]. Damage taken as Vergil's (42 light, 84 heavy, × 1.1; Easy × 2).
- **His attacks:** the combo's records on frames 13, 37, 63; Stinger's on 30 with a 28-a-frame lunge; the blast hurts
  within 400. The damage values are estimates (the records aren't read).
- **The meteor:** a 40-frame arc onto the crest, which shuts as he lands. The pick to go shut a crest is taken a third
  of the time a lit crest has a dark opposite [the walkthrough's frequency, estimated].
- **Sinking and rising:** hidden for 40-50 frames; up in the middle of the nearest dark sixth, or 150 behind Dante.
- **Drawing:** dark (the texture modulated to a quarter) unless lit; the coat rigid on the hips, the sword in the
  right hand (bone 9). The lights themselves aren't drawn: an open crest sparkles.
- **Death:** motion 32 and the room clears; demo 48 and the Doppelganger style (item 35) aren't given.

## 5. Checked in PPSSPP (2026-11-14)

Room 139, Mission 17 (`tools/tests/mission17_end.scene`): he rises, comes up out of Dante's shadow and lands his combo
and Stinger, flies onto the crest Dante opens and shuts it, sinks away when lit; hits pass through him in the dark and
land in the light. Not compared with the PS2 yet.
