# The Fallen (CEm011): Mission 15's new enemy

All addresses are in the PS2 SE executable (`SLUS_213.61`). Object offsets are byte offsets from the object base.
**Decoded** means read from code or tables; **inferred** means interpretation; **approximation** means the port
does something simpler. A line that doesn't say is decoded. Values are quoted at **AI rank 0**.

## 1. Class and pack

- EST spawn id **29** → class **11** (byte table `0x599690`) → pack 6 → **GDATA 122, em011.pac**. The factory
  `2acf40` case 0xb calls `341890`: a pool of 6 (`0x6a2530`), objects of 0x13c0 bytes built by `3414f0`
  (vtable `0x606db0`).
- Update `349670` (vtable +0xc): 0 set-up `342630`, 1 alive `342770`, 2 gone. The alive frame runs the AI
  (`vfunc+0x2ec` = `342ea0`) and the action (`vfunc+0x300` = `3449a0`).
- em011.pac: [0] 4 textures; [1] the body, 43 bones; [2] its cloth (`em011_000.clt`: the two wings, bones 24-26 and
  28-42); [3]-[6] motions (PACs; script ids 0xx-3xx); [7] the motion scripts (the player format: 4 banks);
  [8] effects. No weapon model: the spear is an effect.

## 2. The AI (state `+0x139a`, sub-step `+0x139b`; `342ea0`)

The mode byte `+0x4d4`: **0** in the air, **1** down on the ground (set by action 0x1a), **2** rising (action 0xe;
after 120 frames it's 0 again). `342da0` puts Dante's distance in a band `+0x134c`: 300, 500, 700, 800, 900.

| State | Action (`+0x1394`) | Script | What it is |
| --- | --- | --- | --- |
| 0xe | 0x1e (EST variant 0) | 2:0, motion 200 | Appearing (the set-up's first state) |
| 0x16 | | | Think: 0x17 in the air, 0x1a on the ground |
| 0x17 | 0 | 0:0, motion 1 | The hover |
| 0x1a | 3 | 0:0 | On the ground |
| 0x18, 0x19, 0x1b-0x1d | 0x17-0x1b | 1:1-1:4 | Reactions (101-104) |
| 0x1e | 0x1a | 1:5, motion 104 | Down to the ground: mode 1, effect 0x33f |
| 0x1f | 0xe | 0:5, motion 304 | Rising: mode 2, then 0 |
| 0x20 | 0xf | 3:1, motion 301 | Turns 0x71c, attack record 0 on frame 33 (caps 8 frames): the throw |
| 0x22 / 0x23 | 0x11 / 0x12 | 3:2, motion 303 | Records 2, 2, 5 on frames 25, 60, 90 (caps 10 frames each) |
| 0x24 | 8 → 0x13 | 3:2 | The same, after action 8 |
| 0x27 | 0x15 | 3:4, motion 308 | 13 frames turning 0x100 a frame, then a dash from 20 a frame slowing by 1; record 4 on frame 35 |
| 0x14, 0x15 | 0x1f | 2:1, motion 202 | Death |

**The hover** (action 0, `344cc0`): a point 320 from Dante, 0x3000 + rand(0x1000) (67.5-90°) off his facing,
alternating sides; he moves a 30th of the way a frame (capped at 10), rises a 30th and sinks a 1000th of the height
difference (the PS2's y points down), pauses 5 frames at the point, and turns 0x400 a frame toward Dante. A bob:
+0x1348 turns 256 units a frame and y moves sin × 2 a frame.

**The attack scheduler** (sub-object `+0x130`, the class Vergil's uses; config `0x5c3280`):
- Lists by mode and distance band (`0x5c31f0`: 300, 400, 600, 800, 900, 1200). Rank 0: in the air (list 0,
  `0x5c2f90`...): **0x27 80, 0x20 20** (75/25 from 800, 70/30 from 900, 60/40 from 1200); on the ground
  (list 1, `0x5c30b0`...): **0x22 57, 0x23 21, 0x24 22**. Rank 1 (AI value over 400, `0x619a38`) uses 0x21/0x20 and
  0x28/0x29/0x23/0x24.
- The gauge (`+0x18`, `0x5c3230`): {300, 15, 15}, {700, 30, 10}, {1500, 1, 0.5}; full at 100 (`0x5c32a0`).
- `3442c0`: when the gauge is full, the enemy manager (`+0x840` of the scene) has to grant the turn, and rests 3.0
  after.

## 3. In the port (`psp/testroom/fallen.c`, its glue at the end of `enemy.c`)

Sections 1-2 at rank 0, with the model and motions from em011 (`tools/prepare_skinned_model.py`, "faBNN" = script
id B*100+NN). Approximations:
- **The hover point** is placed off the line from Dante to the Fallen, not off Dante's facing.
- **The gauge** uses the distance columns only: 15 a frame within 300, 10 within 700, 0.5 within 1500, else 0.167.
  One Fallen attacks at a time, then they all rest 180 frames.
- **Damage taken** (the class's damage handler isn't decoded): in the air, hits from the front land on the closed
  wings and do nothing; 8 of them (a heavy counts 2) bring him down (state 0x1e). On the ground, or from behind, he
  takes the hit. After 600 frames on the ground he rises (0x1f). Health 1500 by the port's scale [estimate].
- **His attacks:** the lunge (0x27) and the slashes hit what's within 200-240 in front of him; the throw (0x20) is a
  straight shot at Dante's chest (a Blood-goyle's bullet in flight). Damage is the generic enemies' (the records
  aren't read).
- **Walls:** he doesn't pass through them; the appearing motion plays where the EST puts him.
- The cloth wings are rigid.

## 4. Checked in PPSSPP (2026-11-14)

Room 222, Mission 15: the two Fallen appear at 521 and 1794 frames, hover round Dante, lunge and throw (both land),
take turns; struck from the front they drop to the ground after 8 hits, take damage there and rise again. Not
compared with the PS2 yet.
