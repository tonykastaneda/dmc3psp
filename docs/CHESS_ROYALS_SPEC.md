# The Damned Queen and King (EST 39 / 40)

Mission 18's chess board (room 401, group 0: 8 Pawns, 2 each of EST 36, 37 and 38, and one Queen and one King).
Decoded from the PS2 executable (SLUS_213.61) and em021.pac. Not yet checked in PCSX2. The chess master that gives
the pieces their turns (commands at +0x488) is ported separately (`psp/testroom/chess.c`). This page is only what the
Queen and King do with those commands.

## Class and data

- The EST table at 0x599690 maps EST 39 to class 21 and EST 40 to class 22, both pack 13 = GDATA 128 (`em021.pac`).
  The pack table's only class for them is **CEm021** (vtable 0x60d850). It is CEm017 (the Pawn's class, vtable
  0x60c2b0) with three overrides:
  - the destructor 0x42a000;
  - the update 0x429a60;
  - one secondary slot, 0x42a1a0.
- The update runs on +4:
  - 0 is the set-up, 0x429b10 → 0x429b50;
  - 1 is CEm017's alive function, 0x406730;
  - 2 is CEm017's 0x406d50.
- The alive function runs the piece's own logic by EST (+0x46, jump table 0x5fab10):

  | EST | Logic | Piece |
  |---|---|---|
  | 35 | 0x4383e0 | Pawn |
  | 36 | 0x4349e0 | |
  | 37 | 0x435510 | |
  | 38 | 0x436220 | |
  | 39 | 0x437510 | Queen |
  | 40 | 0x4341a0 | King |

- **Set-up 0x429b50.** The kind comes from the spawn's EST (+0x4a0): 39 → kind 0 (the Queen), 40 → kind 1 (the King).
  - The pack entries come from the 6-byte rows at 0x5bfbe8: the Queen's are [2]–[6] and the King's [7]–[11].
  - Shared by both kinds: +0x4e4 = [0] the textures, +0x4f8 = [1] a PNST (the effects pack), +0x500 = [12] a `.TSC`
    texture scroll (texture 2 scrolling down, 300 / 90).
  - +0x46 / +0x47 = EST / class from gp-0x67bc / gp-0x67b8.
  - +0xf90 = the hit record (gp-0x67b0), +0x11c = the param block (gp-0x67a8).
  - It ends in CEm017's common set-up 0x405930.

| em021.pac | Queen (kind 0) | King (kind 1) |
|---|---|---|
| Model (MOD) | [2], 64544 bytes | [7], 61296 bytes |
| Motions (PAC) | [3], 3 motions | [8], 1 motion |
| Clip table (u16: count 192 / 222, 6, then frame marks 30, 40, 50, 60, 76 …) [inferred: motion n → frame span] | [4] | [9] |
| 16-byte header | [5] `06 00 00 00 06 02 01 00 …` | [10] `06 01 00 00 06 01 01 00 …` |
| Collision spheres [inferred, 0x40-byte rows] | [6]: 5 rows (radius 80, 70, 120, 230 / 30) | [11]: 3 rows (radius 100, 246 high) |
| Hit record (+0xf90) | 0x5bfff0: type 2, **damage 1500**, then 10, 300, 60, 4 / 5, 750 … hit stop (+0x2c) 7 | 0x5bfc50: type 2, **damage 2500**, then 10, 600, 60, 4, 1200 … hit stop 7 |
| Param block (+0x11c) | 0x5c0030: profile 100 / 100 / 95 / 100 / 105 / 95 / 100 / 4.5 / 100, +0x24 = 50, **max HP (+0x2c) 1800** | 0x5bfc90: the same profile, +0x24 = 20, **max HP 2300** |

## Common to both: actions, timers and rank

- Action +5, sub-state +6. Rank +0x3ab is 0–3 and indexes every per-rank table. The time step is +0x10.
- The shared states are:
  - 0 is the wait, `FUN_00408320`;
  - 10–13 are the reactions `00408be0`, `00408dd0`, `004091f0` and `00409450`, the Pawn's too.
- `FUN_0040a140(n)` starts action n: only while HP (+0xea4) > 0, and only if n is a new action.
- `FUN_0040a190(n)` plays motion n (−1 = the action's number).
- `FUN_00409690` sets the timer +0x3b8 to base[rank] + spread[rank] × a random 0–1. The table is per EST and action:
  - the Queen's at 0x5c00d0, the King's at 0x5bfd30;
  - rows are 0x34 bytes: base, spread, then turn speeds in degrees a frame at +0x20 (`FUN_00409ce0`), then a
    decision list at +0x24.

  | Table | Action 0 (the wait) | Action 2 (the move) | Action 3 (the attack) | Turn deg/frame (act 2 / 3) |
  |---|---|---|---|---|
  | Queen 0x5c00d0 | 120 / 80 / 70 / 60 + 60 | 100 + 50 | 50 + 50 | 2 3 3 4 / 2 3 3 4 |
  | King 0x5bfd30 | 320 / 280 / 240 / 200 + 60 | – | – | 2 / 2 |

- **The wait** (action 0, as the Pawn's).
  - Its timer counts down by 3 × 1000 / max(distance to Dante, 1000) a frame, so twice as fast with +0x3bc bit 0x800.
  - When it runs out, the Queen and King (EST 39 / 40) call `FUN_004095f0`, which picks the next action (Queen
    0x437330, King 0x434390).
  - The pick walks the decision list: {weights, angle offset, sector width, reach} rows. It takes the first row whose
    sector holds Dante (`FUN_00359b40`), then draws an action from the weights by rank.
  - Both pieces' lists are one row, all round and 99900 cm, with weight 999.9 on action 2: **after every wait they
    move.** They attack only on the master's command 0x14 (and the King on his own, below).
- Commands from the master: 0x0a (`FUN_004095f0`) and 0x0f (action 4, then 3–6 random beats in +0x491) are read in the
  wait's sub-state 2, as for every piece. 0x14 is read in the Queen's and King's own logic: idle (action 0) → action 3;
  once action 3 is running, the command is cleared.
- **Landing** (both pieces, after every move and the Queen's attack):
  - they fall at −3.2 × step a frame² until the ground (`FUN_0042eb00`);
  - `FUN_0040a2a0(5.0)` starts effect 0x2c on the piece (+0x360) with a shake strength of 5 (+0xf80, +0xf3c = 1);
  - sound 0x14 plays from the piece's bank (`FUN_00359da0` maps EST 39 / 40 to banks 0x15 / 0x16);
  - then back to the wait.

## The Queen (EST 39, logic 0x437510)

- **Moving (action 2, 0x437640).** She moves like a chess queen, onto a line with Dante:
  1. She rises for 0x5c2ce0[rank] = 20 / 15 / 15 / 10 frames, at 2 / 2.5 / 2.5 / 4 cm a frame (× step).
  2. Two pairs of points are built round Dante:
     - `FUN_0040a480` gives the two rook points (Dante's x with her z, and her x with Dante's z);
     - `FUN_0040a4c0` gives the two bishop points, where the diagonals through her meet the diagonals through Dante.
  3. She picks one of the four, 25 % each. The heading toward it is snapped to one of 8 directions (`FUN_0040a300`:
     (angle + 0x1000) >> 13).
  4. The velocity is 10 cm a frame along an axis or 7.07 / 7.07 on a diagonal (`FUN_0040a340`), × (1 + rank / 4).
  5. She glides for `FUN_00409690`'s 100 + 50 × rand frames. The master's command 0x14 cuts it short. Then she lands.
- **The charge (action 3, 0x437ab0)** comes on the master's 0x14. +0x110 bit 0 (value 1) is set throughout [inferred: her body hits].
  1. She rises for 0x5c2d00[rank] frames (as above). +0x490 = 3.
  2. She takes the four points again and picks the **nearest** one. She turns toward its 8-way heading (+0x480) at
     the action's turn speed.
  3. Once she faces it:
     - effect G0x31b starts on a bone of her model (+0x504's +0x104);
     - effect G0x242 starts at her feet: a lane along her heading, its matrix scaled (1, 1, 30);
     - motion 3 plays.
  4. Sound 0x37 plays. She holds for 60 − 3 × rank frames.
  5. Effect G0x390 starts on the same bone, then motion 7. The charge timer is `FUN_00409690`'s 50 + 50 × rand.
  6. She charges along +0x480:
     - the speed (+0x68) starts at 40;
     - each frame it gains +0x78 × step, capped at 50 and never under 35;
     - +0x78 starts at 5 + 2 × rank and loses 0.3 × step a frame, down to −1;
     - G0x242's lane is stretched to half the time left;
     - hitting a wall (the collision's +8 & 0x30000) ends the charge at once.
  7. When the timer runs out, the three effects stop and motion 8 plays. Then she lands, unless both of these hold:
     the roll is under 75 % and +0x490 was already 0. In that case she waits 60 frames and charges again.
     Since +0x490 is set to 3 at the start, one charge per command is the rule [as read; nothing else seen writes
     +0x490].

## The King (EST 40, logic 0x4341a0)

- **Moving (action 2, 0x4345b0).** One square toward Dante, like a chess king:
  - he rises for 20 / 15 / 15 / 10 frames (0x5c2c00, the same numbers as the Queen's);
  - the heading straight to Dante is snapped to 8 ways;
  - he glides at 10 cm a frame × (1 + rank / 4) for the same 20 / 15 / 15 / 10 frames, so 200 cm at rank 0, one board
    square;
  - he lands as above.
- **His own attack trigger** (besides the master's 0x14). Either case sets command 0x14 with +0x478 = 120:
  - in a move with Dante within 350 cm, +0x478 counts down, and he attacks when it runs out;
  - when he is hurt (HP +0xea4 below +0x3c4 [inferred: the HP before this frame's hit]) with Dante within 500 cm, he
    attacks at once.
- **The attack (action 3, 0x434890).** Motion 3 plays (his only motion, so it is a clip of the [8] PAC). Between
  frames 100 and 102 he spawns the attack object (`FUN_0043e880`, class vtable 0x60df60, 0x460 bytes, update
  0x43ebe0, start 0x43ea30). At the motion's end he goes back to the wait.
- **The attack object** stands on the King and lives 240 frames:
  - **Frames 0–60, the charge.**
    - Effect G0x36c follows it.
    - Sound 0x3d plays (bank 0x16).
    - Its hit volume (`FUN_002942e0` with the King's record 0x5bfc50) starts at radius 100, height 5, growing 1 a
      frame, and doesn't hit.
  - **Frame 60, the blast.**
    - Sound 0x3f plays.
    - G0x36c gives way to **G0x233**.
    - The volume becomes **radius 1500**, its height + 280.
  - **Frames 60–90:** it hits (`FUN_002946a0` only while 150 < time left < 180), for 2500 by the record.
  - It is deleted at 240.

## For the port

- The models, motions and clip tables are [2]–[4] and [7]–[9] of GDATA 128. The effects are G ids from em021's [1]
  PNST.
- The pieces' timing, moves and attacks above are exact. Still to check against PCSX2:
  - the clip table's reading (which frames motions 3, 7 and 8 are);
  - the collision rows;
  - which bone +0x504 +0x104 is.
- The decision tables mean their turns come from the master (`chess.c`): 0x14 starts their attacks; the King also
  starts his own attack by distance and by hurt.
