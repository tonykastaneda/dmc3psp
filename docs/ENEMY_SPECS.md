# Enemy specs: Enigma, Hell Sloth, Hell Gluttony, Gigapede

All addresses are the PS2 SE executable (`SLUS_213.61`) unless marked. Decompiled C is in
`analysis/ghidra/export/decompiled/<addr>_FUN_<addr>.c`. The gp register is `0x620170`, so `fGpffffXXXX` is
the word at `0x620170 + XXXX - 0x10000`.

**Decoded** means read directly from code or table values. **Inferred** means interpretation, a name for
something decoded, or a guess. Where a line doesn't say, it is decoded.

## 0. Shared: classes, factory, EST ids

- Factory `FUN_002acf40` switches on a class number, which is the em number. EST spawn id to class number is
  the byte table at `0x599690` (pairs of bytes: class, pack). Ids 0-3 → 0, 4-7 → 1, 8-11 → 2, 12-15 → 3,
  16-19 → 4, 20-23 → 5, 24 → 6, 25 → 7, 26 → 8, 27 → 9, 28 → 10, 29 → 11, 30 → 12, **31 → 13**, 32 → 14,
  33 → 15, 34 → 16, 35-38 → 17-20, 39-40 → 21-22, **41 → 23**, 42 → 24 ...
- So EST 31 = class 13 = `CEm013` (Enigma), **confirmed**. EST 41 = class 23 = `CEm023` (Gigapede), confirmed.
  Room 111, EST set 1, is `2[41,2] 3[4200,0,2000]`, the Gigapede spawn.
- Factory cases → "new" wrappers → constructors (constructors found by scanning for the vtable address):

| Class | new | Constructor | Typeinfo | Primary vtable | Set-up (vtable -160 subobject, slot 33) |
| --- | --- | --- | --- | --- | --- |
| CEm000 Pride | 2b5fc0 | 2b5eb0 | 5f4198 | 604210 | 2b6450 |
| CEm001 Gluttony | 2bd890 | 2bd780 | 5f42e8 | 604460 | 2bdd20 |
| CEm003 Sloth | 34c110 | 34c000 | 5f7828 | 607210 | 34c5a0 |
| CEm013 Enigma | 2cc8a0 | 2cc410 | 5f4580 | 6048e0 | **2cd200** |
| CEm023 Gigapede head | 386690 | 386330 | 5f8948 | 608c20 | 386da0 |
| CEm023Body | - | 38dc40 | 5f88e8 | 608900 | 38e3d0 |

  The factory calls the set-up through `vptr + 0x130`. Vtables are contiguous: `vptr + N` runs on through the
  secondary vtables, so `vptr + 0x1c8` on an Enigma is `0x604aa8`, which holds `0x257620`.
- A vtable dumper and the code scanner I used are in the scratchpad (`vts.py CEm013`, `xr.py <addr>`). Not in the repo.

---

## A. Enigma (CEm013)

### What it is

A ground-standing archer with **three pairs of arms, each holding a bow**. All of this is from the skeleton.
It never walks: it stands where it's spawned, on a ledge or a roof, turns in place, hops, and fires. Each arm
pair is its own "shooter" with its own timer, so up to three volleys can be in flight at once. It does not fly
or teleport. If its footing is gone it falls with gravity and lands.

### Pack: em013.pac, GDATA slot 124 (a PAC, not PNST)

The set-up reads chunks through the pack table at `this+0x610` (offset `+8+4k` = chunk k):

| Entry | Content | Used as |
| --- | --- | --- |
| [0] | Texture set, 1 TIM2, 256x256 | Every model (`+0x08`) |
| [1] | MOD, 42 bones, 3306 verts, about 282 cm across the arms and 323 tall | Body (`+0xc`, model object `+0x640`) |
| [2] | MOD, 2 bones, 42x21x35 cm | Second model `+0xc40`, only when spawn mode is 2 or 3 (`+0x1840 == 1`). Stays where the body landed. Inferred: the statue's pedestal or base |
| [3] | Motion scripts (format below) | Script players |
| [4] | PAC, 16 slots: motions 0-15 (group 0, body) | `+0x614` |
| [5] | PAC, 25 slots: motions 100-124 (group 1, reactions) | `+0x618` |
| [6] | PAC, 1 motion: 200 (219 frames, "appear") | `+0x61c` |
| [7] | PAC, 3 motions: 300, 301, 302 (bow arms) | `+0x620` |
| [8] | MOD, 2 bones, tiny (4 cm) | The arrow model, passed to the arrow objects (`+0x28` = chunk 8). Inferred: drawn scaled or mostly by effects |
| [9] | PNST: an effect id list (`G 138 ...`) and an effect pack | `FUN_00209160(...,2)` |
| [10] | Caps table, 8 entries of (kind, bone, shape u16) | |
| [11] | 6 shapes, 0x50 each | |

Motion headers are 0x80 bytes for 42 bones, so `tools/mot_ps2.py` needs `HEADER_SIZES` to include 0x80.
Frame counts:

- [4]: 0:1, 1:374 (idle, loops), 3:27, 4:27, 5:27, 6:44, 10:388, 11:44, 12:95, 13:90, 14:90, 15:44
- [5]: 100:61, 101:65, 102:89, 103:111, 104:61, 105:134, 106:90, 110:26, 111:26, 112:145, 113:166, 114:141,
  115:64, 116:24, 118:88, 120:58, 121:30, 122:6, 123:115, 124:115
- [6]: 200:219. [7]: 300:82 (loops), 301:20, 302:130 (loops)

**Skeleton** (parent → child, world rest positions in cm):

- 0 root; 1 hips (y 168); 2, 3 spine; 4 head (y 223)
- **Arm set A** (mesh flag 2) off bone 5: bow arm 6-9, bow hand **9** with bow tips 10 and 11, other arm 12-15
  (draw hand **15**)
- **Arm set B** (flag 3) off bone 16: 17-20 with bow hand **20** and tips 21-22, other arm 23-26 (draw hand **26**)
- **Arm set C** (flag 4) off bone 27: 28-31 with bow hand **31** and tips 32-33, other arm 34-37 (draw hand **37**)
- Legs 38-41 (short, down to y 69)

**Caps** ([10] and [11]): (kind 4, bone 0, sphere y60 r60), (0, 9, r30), (0, 20, r30), (0, 31, r30),
(4, 0, y100 r60), (2, 1, (0,-10,30) r80), (0, 0, r20 at y20), (4, 0, ...). Every script turns on caps 0, 4
and 5. Inferred: those are its hurt shapes. It has no melee attack cap.

### Script chunk [3] format

This is the same bytecode as the Pride's chunk 38, but with **groups**:

- `u16[0]` is the script-group table and `u16[1]` the motion-group table. Each table is u16 offsets ending in
  0xFFFF. Each group is a table of u16 offsets to its scripts or motion entries.
- A script player is started with (group, script): `FUN_00254df0(player, g, s)`.
- In a motion entry, the motion id is the u16 at +6 (hundreds pick the PAC: 100+ is [5], 200 is [6], 300+ is [7]).
- `op3 (01 vv ..)` sets an event value that code reads with `FUN_00255af0(player, 0, 1)`.
- A group-aware dumper is `scr2.py` in the scratchpad.

Group 0 (body): S0.0 statue pose (mot 0, then 1, 1, 3); S0.1 idle (mot 1 loop); S0.3, S0.4, S0.5 hops (mot 3,
4, 5); S0.6/7/8 jump (mot 6 split at 23/33); S0.10 awaken (mot 10, 388 frames); S0.11 to 14 jump down from a
ledge (mot 11 loop, then mot 12 split at 42/64); S0.15, S0.16 big turns (mot 13, 14); S0.17 to 20 fall
(mot 15 loop, then mot 12 split).
Group 1 (reactions and death): 1.0 to 1.31 (motions 100-124), listed with states below.
Group 2: 2.0 appear (mot 200).
Group 3 (arms): see "Shooting".

### Object layout

- Five script players at `+0x2450 + layer*0xd0`. Layer 0 drives the body, 1 a second body layer, and
  **2, 3 and 4 drive arm sets A, B and C**. The bone flags 2, 3 and 4 are what tie each arm set to its layer.
- Each layer has a state byte at `+0x2ce4+layer`, a done flag at `+0x2ce9+layer`, and a phase byte at
  `+0x5ad+layer*4`.
- Full-body states write all five layers. Arm states write one.
- Per-state flags are the u32 table at `0x5b5d60`:
  - bit 0: grounded. The body is pushed down 30 a frame and snapped to the floor.
  - bit 2: can act in mode 3.
  - bit 3: interruptible, so the AI may start a new move.
  - bit 4: can turn.
- Top-level mode `+0x2d7c`, run by `FUN_002cf5c0`:

| Mode | Function | What it does |
| --- | --- | --- |
| 0 | 2cf680 | Nothing (hidden spawn, waiting for its "appear") |
| 1 | 2cf690 | **Drop-in or statue**: falls (v_y starts at 0, gravity -1 a frame, terminal -30) until the floor probe hits. Then waits: spawn mode 3 waits `+0x600` × 60 frames; mode 2 waits until Dante is **under 200** (`0x619394`). Then plays the awaken (state 0x33, S0.10, 388 frames), then mode 2 |
| 2 | 2cf870 | **Idle**: idle motion on layers 0 and 1. Rolls a 60-180 frame random. When Dante is **under 9000** (`0x5b5b28`) it goes to mode 4. On big angle errors (more than 0x2000, 45 degrees) it turns (state 0x22) |
| 3 | 2d0000 | Lost footing or no line: turns toward Dante (state 0x30, S0.17) until `vfunc+0x1c8` (`0x257620`, inferred: "can see or reach Dante") is true, then mode 4 |
| 4 | 2d0100 | **Combat** (below) |
| 5 | 2d0b60 | Hit reaction running: arrows off |
| 6 | 2d0b70 | Nothing |
| 7 | 2d0b80 | **Death**: fades out over `0x618b2c` frames, drops orbs, and frees the slot |

### Spawn parameters (EST record → object)

`FUN_002cc8a0` copies the spawn record into the object:

| Record | Field | Meaning |
| --- | --- | --- |
| +0x10 | +0x5e0 | Position |
| +0x20 | +0x5f0 | Yaw (65536 = one turn) |
| +0x28 | +0x5f8 → `+0x2f2e` | **Type** 0, 1 or 2 |
| +0x2c | +0x5fc | **Spawn mode**: 0 normal; 1 "appear" (hidden, then S2.0, 219 frames, then idle); 2 statue (with [2] model), wakes when Dante is under 200; 3 statue that wakes after `+0x600` seconds |
| +0x30 | +0x600 | Seconds for mode 3 |

- **Inferred:** EST `op 8 [k, v]` sets these. Room 102 set 10 has `8[0,1] 8[1,3] 8[2,12]`, which reads as
  type 1, mode 3, wake after 12 s, and fits the walkthrough's "statues come to life".
- **Type** changes the AI:
  - Type 0 may switch to mode 3.
  - Type 1 never dodges.
  - Type 2 never does the facing-cone back-hop and **hops sideways on a timer** (below).
  - If type 1 or 2 came in by "appear" and has lived over 10800 frames (`0x619390`, 3 minutes), it becomes
    type 0 and its rank goes up by 1.

### Rank

`+0x5c2` = `FUN_002cee50(v)`, the number of thresholds in `{409, 549, 1200}` (`0x5b59d0`) that v exceeds,
capped at 2. v comes from the AI base's `vfunc+0x7c`. Inferred: the same rank or difficulty value the Pride
reads. Rank-indexed tables, giving r0 / r1 / r2:

| Table | r0 / r1 / r2 | Use |
| --- | --- | --- |
| 5b5b28 | 9000 / 9000 / 9000 | Wake-up and give-up distance |
| 5b5b18 | 1000 / 900 / 800 | "Near" range for direct shots |
| 5b5b08 | 300 / 400 / 500 | Dodge range |
| 5b5ad0 | 0.5 / 50 / 90 | Percent chance per frame to dodge when Dante is inside the dodge range |
| 5b5ae0 | 0 / 50 / 95 | Percent chance that a volley is **red** (stronger arrows) |
| 619370 | 256 / 512 / 768 | Turn per frame (65536 = one turn), times dt |
| 619380 | 120 / 90 / 30 degrees | Cone for the back-hop. Outside it, a 90-frame big turn |
| 619388 | 0 / 1 / 1 | 1 = the arms keep shooting while it hops |
| 619378 | 1 / 1 / 1 | Extra aim loops for a normal shot |
| 5b5af0 | (300,400) / (300,350) / (200,250) | Type 2's sidestep interval, random in [min, max] |
| 5b5ac0 | 1.0 / 1.2 / 1.4 | `+0x2d70`, set during hops. Inferred: a motion speed scale |

### Combat (mode 4, `FUN_002d0100`) each frame

1. If Dante is further than 9000 → mode 2. If type 0 and `vfunc+0x1c8` is false → mode 3.
2. **Dodge** (not type 1). If Dante is within the dodge range, roll the chance above, and only when not already
   hopping or turning (`FUN_002d3440`). Then:
   - if not type 2 and |angle to Dante| is inside the cone: **back hop**, state 4 (`2d3360`, S0.3, mot 3,
     27 frames, foot effect 0x8a, heading +0x8000).
   - otherwise a big turn: angle < 0 → state 0x2e (`2d2ad0`, S0.15, mot 13, 90 frames); angle ≥ 0 → state
     0x2f (`2d2bb0`, S0.16, mot 14, 90 frames).
3. **Type 2 sidestep**: timer `+0x2f30` runs down. At 0 it re-rolls (table above), then 50/50 a right hop,
   state 5 (`2d3480`, S0.4, mot 4, +0x4000), or a left hop, state 6 (`2d3560`, S0.5, mot 5, -0x4000).
   Inferred: the hops move it through root motion in mots 3, 4 and 5. The code adds no velocity.
4. **Shooters** (layers 2, 3, 4), phase `+0x5c1`:
   - Phase 0: give each arm set a cooldown. Phase 1: wait 32 frames (`+0x2b20`), then enable the three arm
     aim controllers.
   - Phase 2, per arm set: count its cooldown down 1 a frame. At 0, re-roll it. Then, if `vfunc+0x1c8` is true
     **and the attack-token manager** (`(*(0x1d0d70c)+0x20)+0x840`, slot 0x14) grants a token, take it for
     **3.0 s** (the same manager and 3.0 as the Pride). Roll red (table above). Pick the shot with weighted
     `FUN_00359f20(2, 0x5b5b40 + near*8 + rank*0x10)`: **single** state 2 (`2d2d00`), or **fan** state 7
     (`2d2e30`).
5. Turn toward Dante at the rank's turn rate (`FUN_002d3940`). If the error passes 45 degrees and it isn't
   type 2, it does the turn state 0x22 (S0.11).

**Cooldowns** in frames are `base[arm] + range × rand`, from `0x5b5a60 + near*0x10 + rank*0x20`. The first
volley uses the far base alone.

| | Arm A, B, C base | + range |
| --- | --- | --- |
| r0 near | 230, 200, 250 | 200 |
| r0 far | 80, 100, 120 | 100 |
| r1 near | 160, 150, 180 | 200 |
| r1 far | 50, 70, 80 | 70 |
| r2 near | 100, 120, 140 | 100 |
| r2 far | 35, 50, 45 | 50 |

**Shot weights**, single : fan: r0 near 80:20, far 60:40; r1 near 70:30, far 25:75; r2 20:80 both.

**"Near"** (`+0x2cef`, `FUN_002d1190`) is 0 when Dante is within the near range **and** an angle check passes
(inferred: inside a vertical cone). Otherwise it is 1.

- Each arm set's aim controller (`+0x2b30 + k*0x90`, on the bones at `+0x508`, `+0x534`, `+0x560`) targets
  Dante's position, plus a lift `+0x2d24`.
- While "far", the lift grows by 60 a frame up to **2000** (5000 in room id 0xd8). When near, it shrinks by 60
  a frame to 0.
- So when Dante is far, the bows point up, and the arrows are lobbed (below). Aim blend weights ease at 0.02
  a frame.

### Shooting (arm states 2 and 7: `FUN_002d15d0`, `FUN_002d1910`)

Arm set → bones (bow hand, draw hand): A (9, 15), B (20, 26), C (31, 37). The charge and release effects sit on
bones 8, 19 and 30 (`0x5a0bc0`).

**Single shot, state 2.** Script 3.0: mot 300 loops (82 frames) with blend 20. Events: value 0 at frame 0,
**value 1 at frame 16**. Loops allowed: `+0x2cfc = 1`.

1. Phase 0:
   - On event 1, it **spawns one arrow** `CEmShl000` (`FUN_003156e0`) with the arrow MOD [8], held between the
     two hands.
   - On event 2, charge effect 0x127 (red-tinted when red).
   - When mot 300's loop count passes `+0x2cfc`: release flash effect 0x131, and a **20-frame** timer.
2. Phase 1: when the timer ends, script 3.1 (mot 301, 20 frames, event **value 2 at frame 0**). **The arrow is
   released here.**
3. Phase 2: when the script ends, the layer is done and goes back to idle (`FUN_002d28a0`).

Inferred timing: frame 16 nock, about 2 loops of aim (about 150 frames), then 20 frames, then release. Not yet
measured on the HD Collection.

**Fan, state 7.** Script 3.2: mot 302 loops (130 frames). Events: value 1 at frame 26, 3 at 31, 4 at 50, 5 at
70, 5 at 120. Loops allowed: random 0-3.

- On event 1 it spawns `CEmShl01` (`FUN_00314720`), **three arrows in a fan**. Arm A's fan is mirrored.
  - The fan opens to ±30 degrees (`0x618fb8`) and closes by 30/40 degrees a frame after event 3.
  - The arrows grow out to 140 at 28 a frame (`0x5acc60/64`).
- On value 2 (script 3.1 again, after the same 20-frame wait), it fires **three** `CEmShl000` with spread ids
  1, 2 and 3 (`FUN_00315890`).

### Arrow: CEmShl000

Vtable `0x6061e0`, constructor `315aa0`, set-up `315c40`, update `315bb0` → `315f00`, flight `3167b0`, release
`316b20`.

Parameters (normal `0x5b5ce0`, red `0x5b5d20`):

| Field | Normal | Red | Meaning |
| --- | --- | --- | --- |
| +0x110 | 35 | 45 | Launch speed (cm/frame) for a direct shot |
| +0x128 | 65 | 65 | Top speed |
| +0x120 | 0.2 | 0.5 | Initial upward steer |
| +0x124 | -0.1 | -0.1 | Change per frame (gravity) |
| +0x138 | 512 | 512 | Inferred: homing turn rate |
| +0x13c | 300 | 300 | Unknown |
| +0x144 | **240** | **240** | Lifetime in frames |

Lobbed shots launch at 45 (`0x5b5d00`).

- **Held** (phase 0): it follows the bow. Its direction is the bow aim, from the two hand bones (`3165f0`). The
  parent cancelling deletes it.
- **Release** (`316b20`) happens on event value 2, or at once for fan arrows. Normalize the direction, then
  compare it with its horizontal projection:
  - **Dot > 0.85: direct shot** (`+0xba9 = 0`). Velocity = dir × 35 (45 red).
    - Fan arrows 1 and 3 get x ± (9 + rand), so they spread sideways.
    - Each frame, steer toward Dante with `FUN_0025a8e0(dt, this, Dante+0x30, 2)`. Inferred: homing, not
      decompiled yet.
    - Add the vertical term `+0x120`, which shrinks by 0.1 a frame. Clamp |v| to 65 and move.
  - **Otherwise: lob** (`+0xba9 = 1`), when the bows point up because Dante is far. Velocity = dir × 45, then
    solved ballistically (`FUN_0025ae40`) onto Dante's feet plus an offset along the arrow-to-Dante direction.
    - The offset comes from `0x5b59e0[idx]`. A normal arrow picks at random from 200, -150, 100 or 0. A red
      arrow uses 0. The fan's three use -200, 0 and 200.
    - Then straight flight with gravity -0.1 a frame (`+0xf4`), and no homing.
- Collision is set by `FUN_002942e0` with shapes `0x5b5580` (3 spheres; inferred radii 10, 25 and 30) and
  flags `0x30443`. In flight:
  - **Hits Dante** (flags 3): effect 0x52 and sound 0x23. Calls Dante's `vfunc+0x118` with hit record
    **`0x5b5640`**, or **`0x5b5600`** when flag bit 2 is set (inferred: guarded or Royalguard). Then it's gone.
  - **Hit by Dante's attack** (flags 0x30000): effect 0x37, sound 0x22. It fades over 25 frames (it is knocked
    away, not reflected).
  - **Hits the world** (flag 0x40): effect 5, then gone.
  - A sound 0x2c plays once when it comes within 500 of Dante.
- **Hit records**, raw: both start `0x402`.
  - `0x5b5640` (normal): +0x1c = **300**, +0x20 = 8, +0x24 = 500, +0x28 = 60, +0x2c = 7.
  - `0x5b5600`: +0x1c = **100**, +0x24 = 300.
  - Inferred: +0x1c is damage (300 against 100 when guarded) and +0x24 is knock-back. Check against the
    Pride's records when the damage format is decoded.
- **Number of arrows:** up to 3 arm sets, each with one volley at a time. A volley is 1 arrow (single) or 3
  (fan). One token per volley means only one volley starts per 3 seconds across all enemies holding tokens.

### Turning, falling and being hit

- It turns in place only (`FUN_002d3940`). Small errors are absorbed each frame at the rank's rate. Errors over
  45 degrees play the turn motion.
- **Floor** (`FUN_002d79d0`): a ray from y+130 down. With no floor, the floor is y-1000.
  - In a grounded state, if the floor is more than 30 below, it **falls**: state 0x20 (`2d3180`, S0.12 or 0.18,
    mot 12). From combat it drops to mode 2.
  - Fall (`2d2110`): gravity -1.5 a frame (`0xbfc00000`). On landing, effect 0x14f, then state 0x21 (`2d3290`),
    then idle.
- **Jump** (states 0x27 to 0x29, `2d2270`, `2d23d0`, `2d2470`, S0.6/0.7/0.8, mot 6): from frame 9 v_y = 13.33,
  gravity -0.889, and the fall is capped at -30.
- **Jump down a ledge** (state 0x1f, `2d1fb0`): from frame 15 v_y = 14.29, forward 5, gravity -0.51, then fall.
  What triggers 0x1f and 0x27 is outside this range. Inferred: knock-back off a ledge.
- **Hit reactions** (`FUN_002d3ac0(kind)` → pending state `+0x2cee`, applied by `FUN_002cea80`). Mode becomes 5.
  The reaction kinds are the shared hit system's. Inferred meanings are in brackets, from motion length.

| Kind | State | Script, motion |
| --- | --- | --- |
| 0 | 9 or 0x31, alternating | 1.0 mot 100 / 1.31 mot 104 (light flinch) |
| 1 | 8 | 1.1 mot 101 (no mode change: flinch while still shooting) |
| 2 | 10 | 1.2 to 1.4 mot 102 (heavier, 3 parts) |
| 3 | 0xd | 1.5 to 1.7 mot 103 / 106 |
| 4, 5 | 0x25 | 1.24 mot 122 |
| 7 | 0x11 | 1.9 mot 106/113 |
| 8, 9 | 0x14 or 0x15, alternating | 1.12 mot 110 / 1.13 mot 111 (26 frames, light air) |
| 10, 11 | 0x26 | 1.26 mot 120 |
| 12 | 0x24 | 1.23 mot 121 |
| 0x15 (killed) | 0x23 | 1.25 mot 123, then mode 7 |
| 0x14 (killed) | 0x23 if in 0x14/0x15; 0x2a (1.27 mot 124) if airborne (0x26, 0x25, 0x13, 0xf, 0x1c, 0x19, 0xc); otherwise 0x2b | 0x2b = 1.28 to 1.30 mot 112, a fall off the ledge (`FUN_002d40b0` tests 0/90/180/270 degrees for a drop) |

  States 0xb, 0xc, 0xe, 0xf, 0x10, 0x12, 0x13 and 0x17 to 0x1c use scripts 1.3, 1.4, 1.6, 1.7, 1.8, 1.10,
  1.11, 1.15 and 1.16 to 1.20. These are the knocked-down and launched chain (mot 105, 112, 113, 114, 118).
  Their handlers (`2d42b0` to `2d50d0`) weren't read line by line.
- The walkthrough says Kick Jump reaches it and knocks it "to ground level". In code terms, a launch reaction
  that leaves the floor probe more than 30 below makes it fall. Inferred.
- Health: the param block `0x5b56b0 + type*0xfc + rank*0x54` is passed to the damage component (`+0x2e3c`).
  It is all 100s, then 50, 150, 100, 0, **600**, 110, 60/80. Inferred: +0x2c = 600 is HP. The same field
  holds 250 for the Pride, 700 for the Gluttony and 1200 for the Sloth (B). Hit stop: `+0x2f28` comes from the hit record's +0x2c (`2cecb0`).

### Port recipe (simplified)

- Spawn it standing on its EST point. Mode 2 → 4 when Dante is under 9000. Use rank 0 tables.
- Keep 3 shooter timers using the far base for the first volley. On expiry, take the global 3-second token,
  then choose single or fan by the weights.
- Arrow: at release, if Dante is within 1000, fire a homing arrow at 35/frame (top 65, life 240). Otherwise lob
  at 45 with gravity -0.1 onto Dante's feet ± offset.
- Hops: back hop if Dante comes within 300 and inside a 120-degree cone (0.5% a frame at r0). That is rare at
  rank 0.

---

## B. Hell Sloth (CEm003) and Hell Gluttony (CEm001)

### Main finding: almost everything is shared with the Pride (decoded)

- Every Hell's constructor calls `FUN_003674a0`, so all of them have the same AI object at `this+0x2bc0` with
  vtable `0x607b20`.
  - Plans (`FUN_00367670`) and actions (`FUN_0036bff0` / `FUN_0036b940`) are shared.
  - The Sloth's warp and the Gluttony's spew are ordinary shared plans and actions that the Pride's table never
    picks.
- The four per-class overrides (`this+0x130` vtable slots 0x224 to 0x230) do nothing for gameplay:
  - 0x224 and 0x228 (Sloth `34dd90`, `34ebe0`; Gluttony `2bf480`, `2c01b0`) are colour fades for the hit flash
    and death.
  - 0x22c (Sloth `352c80`, Gluttony `2c4280`) is hide: `FUN_001a2020` clears model+0xa0 bit 0.
  - 0x230 (Sloth `352d00`, Gluttony `2c42f0`) is show.
- Every set-up reads the **same pack chunks**: scripts 38, caps 39, shapes 40 and motions 35 (`hl-NN`).
  - The hit-reaction tables are byte-identical: Pride `0x5a7a80`, Gluttony `0x5aae70`, Sloth `0x5b0750`.
  - The script maps (`5a79a0`, `5aad90`, `5b0670`) are the identity.
- **What changes per class:** the AI table, the attack-record table, a 0x54-byte parameter block, floats
  `[0x984..0x987]`, and the models. So `gen_enemy_ai.py` only needs the table addresses switched per class.

| | Pride | Gluttony (CEm001) | Sloth (CEm003) |
| --- | --- | --- | --- |
| AI table | 5a8300 | **5ab9d0** (`*0x618f20`) | **5b0e60** (`*0x619228`) |
| Attack records (0x40 each) | `*0x618c28` | `*0x618f0c` = 5aa9f0 | `*0x619218` = 5b0380 |
| EST ids, class byte | 0-3, 0 | 4-7, 1 | 12-15, 3 |
| Body | [1] | [5] | [13] |
| Weapon MOD | [26]/[29] | **[28]** (ids 4, 5) / **[31]** (ids 6, 7) | **[27]** (ids 12, 13) / **[30]** (ids 14, 15); weapon texture chunk 25 |
| Extra MODs | | ids 4, 5: [7] (attach text [6]) | ids 12, 13: [15] (text [14]) and [17] (text [16]) |
| Param +0x2c (inferred: max HP) | 250 | 700 (50 if spawn word `[0xf6]` is set) | 1200 |
| Param +0x30, +0x34, +0x44 | 30, 30, 42 | 60, 20, 112 | 60, 60, 168 |
| Floats `[0x984..7]` (`[0x985]` → attack +0xe4; inferred: damage scale) | 1, 1, 1, 1 | 1, 1.2, 1, 1 | 1, 1.2, 1.2, 1 |

The 0x54-byte parameter blocks are `5a78f0`/`5a7840` (Pride), `5aace0`/`5aac30` (Gluttony) and `5b05c0` (Sloth).
On the same reading the Enigma's 600 (A, health) is its HP.

### Hell Sloth

**AI table** (decoded):

- Bands at 330 and 700 cm. Zones (distance, angle, gauge per frame): (300, 150°, 5), (600, 90°, 5), (1500,
  10°, 5).
- Times and thresholds as the Pride. Table +0x1c holds 150, 300 (Pride 100, 200; use unknown).
- Rank 0:

| | Under 330 | 330-700 | Over 700 |
| --- | --- | --- | --- |
| With the token | 48 | 46 | 49 |
| Without | 48 | 47 50 / 48 50 | 47 50 / 48 50 |

Higher ranks mix 46, 47 and 48. **It never uses 27, 32, 51 or 52.**

**Plans to actions:** 46 → 57, then 47 → 41, 48 → 42, 49 → 43. Each goes back to plan 23 at the end.

**Warp, actions 41, 42 and 43** (enter `375a10`/`375fd0`/`376590`, update `375a60`/`376020`/`3765e0`). Script
68 = `hl-46`, 60 frames:

- f40: warp effect 0x2a (2× scale) and sound 0x19. Render mode switch (`2856e0`; inferred: warp shader).
- f44: **hidden** (event 0x386).
- f45: **teleport.** pos = Dante + (sin a·R, 0, cos a·R), with a a random u16 angle.
  - **R = 900 (41), 600 (42), 300 (43).**
  - Y comes from a ground ray cast down from Dante's y+400 (`FUN_00201c70`). If it misses, y = Dante's.
  - Then it snaps its yaw to face Dante (`32b290`).
- f50: effect again, sound 0x1a. f54: **shown** (0x387).
- End: render mode back, action 14.
- Invisible for 10 frames. Leaving any warp action for another (for example on being hit) forces show
  (`FUN_0036bff0`). Whether the hurtbox is off while invisible: inferred no.

**Warp strike, action 57** (enter `379d10`, update `379d60`). Script 66 = `hl-60`, 124 frames:

- f100: warp effect.
- f104: hide, and both motion players seek to script 87 (`hl-47`, the Pride's swing) at frame 25 (event 3000,
  handler `285820`). At once it teleports to **R = 300** facing Dante.
- f30: appear effect. f34: shown.
- **Sphere cap 11 live at frame 63 for 4 frames**, attack record 0. About 221 frames in all.
- Sloth record 0: [0, **700**, 0, 20, 0, 60, 0, 350, type 1, ..., 30, 1]. That is the Pride's record 0 with
  word 11 = 0. Inferred: word 1 = 700 is damage.

### Hell Gluttony

**AI table** (decoded):

- Bands at 350 and 500. Zones: (250, 45°, 5), (500, 15°, 2), (1500, 1°, 0.5). Table +0x1c holds 130, 300.
- Rank 0:

| | Under 350 | 350-500 | Over 500 |
| --- | --- | --- | --- |
| With the token | **45** | 32 50, 34 25, 33 25 | 32 70, 51 15, 52 15 |
| Without | 32 70, 51 15, 52 15 | the same | the same |

- From rank 3 the 350-500 token row adds **plan 37** (50 to 80%).
- Some rows sum to 90. `FUN_0032b7b0` rolls rand × 99, so the overflow reads into the next row's first entry.
  `read_rows` already reproduces this.

**Plans:**

- 45 runs action 56 (script 71 = `hl-10`, 41 frames; inferred: wind-up), then **action 31**, then plan 23.
- 37 runs action 31 alone.
- 32, 33, 34, 51 and 52 are the Pride's leap, steps and taunts.

**Spew, action 31** (enter `3736e0`, update `373750`). Script 52 = `hl-52`, 243 frames, with no hit ops:

- Enter: effect 0x4b attached to model frame 0x106.
- f15-35: turns toward Dante, at most 3° a frame.
- f80-105: scales model frame 0x117 by X, Z = 1 + (f-80)·0.8 and Y = 1 + (f-80)·0.5 (inferred: the belly or
  throat swelling). It resets at f121.
- f120: effect 0x69 at its position and yaw (inferred: the spew visual).
- **f129: `FUN_0037c250` spawns `CEm000Shl00`** (RTTI `0x5f8510`, vtable `0x607f80`, update `37c390`).
  - **It is not a moving projectile.** It is a stationary hit volume at the enemy's position and yaw: three
    spheres of radius **100** on the root, at local (0, 100, 100), (0, 100, 250) and (0, 100, 400).
  - It is live about 3 frames (life 3.0 × frame delta), so a burst reaching about 500 cm ahead at mouth
    height.
  - It uses record **6** of `0x5aa9f0`: [0x10000, **800**, 0, 60, 0, 600, 0, 400, type 8, 200, 120, 5, ..., 30,
    0x10000]. Inferred: 800 = damage, type 8 = heavy or knock-down.
- A sister shell `37c6b0`/`37c830` (one sphere of radius 450, record 7) is spawned by event 9. Neither class
  sends that event. Inferred: another Hell uses it.

### Port notes

- Run plans 45, 37, 46, 47, 48 and 49 through the existing machinery, with the class's own AI table, records
  and models. All scripts and motions come from chunk 38 and PAC 35.
- New per-class work:
  - The Sloth's 10-frame hide and teleport (R from the action, random angle, ground ray from Dante y+400).
  - The Gluttony's 3-frame three-sphere burst at f129 of `hl-52`.

---

## C. Gigapede (CEm023): Mission 4 boss

**Class and pack, decoded:** `CEm023` (head) plus `CEm023Body` (segments), spawned by EST id 41 in room 111 at
(4200, 0, 2000). Pack **GDATA slot 129, em023.pac** (PNST). This rules out em021, em025 and the rest.

### Pack

| Entry | Content |
| --- | --- |
| [0] | Textures |
| [1] | `em023_01.clt`, cloth per segment |
| [2] | MOD 83792, **head** (18 bones). Motions are [9], a PAC of 6 |
| [3] | MOD 33360, **body segment** (16 bones). Motion is [10], 1 motion of 179 frames |
| [4], [5] | SHW. Inferred: head and body shadows |
| [6] | SCM, a 3-bone "path carrier", instanced 20 times (object `+0xec0`, stride 0x440), one per path |
| [7] | Light text: Force 1400, Range 10000 (× 0.7 in room 0x19d), Rgb |
| [8] | PAC of 20 motions = **movement paths**. Bone 1 carries world-space translation. Paths 18 and 19 are empty |
| [11] | Head motion script: 0 idle, 1 death, 2 stun, 4 roar or charge, 5 after the head attack (`FUN_0038d400`) |
| [12] | Effects PNST |

### Body: a train of slots on authored paths

- Segment count is spawn param +0x2c minus 1. If the param is 0, it is `0x619780` = **7**, capped at 8. So
  1 head + 7 bodies.
- There are n+2 trail slots: 0 is the head, 1..n the bodies, n+1 a tail point. Each slot has a path id
  (`+0x65a0[i]`) and a path frame (`+0x65f0[i]`). Its position is path bone 1 sampled at that frame.
- **Spacing: 500 units of arc length** (`0x619784`). At set-up, frames per 500 units is measured per path
  (`+0x6410[path]`), and each slot sits that many frames behind the one in front.
- When a slot reaches the end of its path, it takes the path of the slot in front, at that slot's frame minus
  the spacing.
- **Catch-up:** over 500 units behind, its rate rises 0.1 a frame. Over 600, it snaps forward in 5 steps.
- **Orientation:** look at the slot in front, up = +Y. Then roll about forward by the path's angle (path bone 2
  +0x148) plus a per-slot roll (`+0x6668`). The roll is how it shakes Dante off.
- Body bones: 2→4 left emitter, 5→7 right emitter, 0 belly. Shell plates are mesh parts 1, 2, 4 and 5.

### Movement: routes of paths through the room (holes)

- Routes table `0x5ba9b0`. Each step is {path, speed, flag, 4 × (frame threshold → event id)}, read in pairs:
  the first is used if Dante's y ≥ 900, else the second (only routes 4, 6 and 9 differ). Path -1 ends the
  route.
- Path change happens at the path's last frame minus a margin. The new path starts at an enter frame (table
  `0x5b98a0`: enter 100, except 200 for path 1 and 300 for paths 3, 15, 16 and 17; margin 100, except 0, 250
  and 300 for paths 14, 15 and 17).
- **Start:** route 11 (path 14 entrance with a roar, sound 0x20 and script 4, then path 0), or route 12 when
  spawn short +0x22 is 2.
- **Next route**, random from `0x5b99e0` by phase: phase 0 picks from 1, 2, 3; phase 1 from 4, 5, 6; phase 2
  from 7, 8, 9. Rank 0 only gets 1, 4 and 7.
- **Rate** (path frames per frame):
  - Target = speed × 1.4 (× 1.8 in phase 2), minus 0.05 per broken shell (floor 0.5), doubled if no segment is
    within 1000 of Dante in XZ.
  - It eases toward the target by 0.01 a frame.
  - Path 13 runs at 4 (death). Path 14 targets × 2.8.
  - Paths are 7k to 44k units long, at about 4 to 10 units per path frame.
- Holes (inferred from path ends): A ≈ (-1400, 600, 8500), B ≈ (8400, 1850, 1900), C ≈ (-1500, 1000-1600,
  4300) for paths 13 and 14. Paths climb to y 2700 (walls and ceiling).
- Routes as path(speed): threshold → event:

| Route | Steps |
| --- | --- |
| 1 | 3(1): 2000→1; then 1(2): 1200→1 |
| 2 | 12(1): 1250→1; then 2(1): 1200→1 |
| 3 | 3: 1700→1, 2500→0; then 2: 750→1, 1250→1; then 15: 1650→2, 2200→0; then 1(1.5): 1200→2, 3100→0 |
| 4 | 15/11: 1550→2 or 1200→3, 1700→3; then 9: 1100→2, 2200→0 |
| 5 | 17(.8): 1200→3, 2800→3; then 3: 1500→1; then 6(.8): 800→10, 1500→9; then 2 or 16: 620/1140/1500/2000→3, or 600→7, 2000→1 |
| 6 | 15/12: 1550→2 or 1000→4; then 16/9: 1100→4 or 800→7, 1800→6 |
| 7 | 3: 1300→2; then 6(.8): 800→10, 1500→9, 1550→1; then 16: 1200→2 |
| 8 | 12: 1200→6; then 1(1.5): 1300→2; then 17(.8): 2800→7, 2850→2 |
| 9 | 11(1.1): 1100→7, 1800→1; then 7: 2450→2; then 6: 800→10, 1000→2, 1500→9; then 16 or 2: 1200→2, or 670→6 |
| 10 | 13(2), death |

### Events (attacks), from `FUN_0038bba0`

- 1: Shl00 volley, 2 per segment with an intact shell (and on screen, inferred). 2 repeats it whenever none are
  alive.
- 3 and 4: Shl01, the same way.
- 5 and 6: Shl02 on every other segment.
- 7: head attack (action 0x6b, fires Shl03).
- 8 and 9: Shl05, 3 belly beams.
- 10: roll (action 0x69). 11: long roll (0x6a). 0: stops repeats.
- Caps alive at once: Shl00 16, Shl01 8, Shl02 8, Shl05 16.

### Projectiles

Tables are [rank][phase].

- **Shl00, homing electric sphere** (`469d70`, set-up `469fe0`). Inferred: the purple homing sphere.
  - Grows between the emitter bones over 50 frames (r0). It leaves after 50 + segment index × 25-35 frames, so
    a volley ripples down the body.
  - Homes on Dante + 150 y, with accel 1.0 (r0) to 4.0 (r6) for 60 frames and top speed 30 or 40 (r0) up to
    100. Floor y 50.
  - Life 300. Radius 50 (hit) and 150.
  - **Hit by Dante:** v × -0.5 and it homes back onto its own segment (accel 1/1.2/1.5, top 40). Hit type 0x11
    on the body does ×5 damage (inferred: a batted-back sphere).
- **Shl01, drop-then-roll sphere** (`4faf90`). Inferred: the "lower" or ground orb.
  - Charges 60 frames, then falls 10 a frame to y < 500.
  - Then it runs straight at Dante's XZ position at launch, speed 30 (40 in phase 2, 50 at r6), with no homing.
  - Life 300. Dies after 60 frames against a wall.
- **Shl02, belly beam spanning a segment** (`5065f0`). Inferred: the side-to-side discharge.
  - The beam goes from the emitters extended × 2.3, grows over 30 frames and holds 30 + index × k.
  - Both ends drop to y 50 at 20 to 45 a frame. Then it sweeps toward Dante at 50 to 100 a frame for 180 frames.
- **Shl05, beam straight down** (`51b820`): a capsule from the belly (bone 0) of a random segment, re-picked
  each frame, down to y -6000. Radius 100, tilt ±0.1, re-hits every 10 frames, life 60. Three are spawned.
- **Shl03, head orb** (`51a210`): held at the mouth for 60 frames (head bone 2, tilted 45 degrees, offset y -100,
  z 200). Dropped with gravity 0.5, life 300. On landing (y < 50) it blasts and spawns **5 Shl04**, fanned at
  -2..2 × 0.15.
- **Shl04, ground wave** (`519860`): 50 a frame along its heading, radius 200, live after 10 frames, life 300.
  Inferred: the "blue orb leaving an ice trail" is Shl03 plus Shl04, or Shl01.
- Damage to Dante is not decoded: the colliders use table `0x5bacc0` through `FUN_002942e0`.

### Head states (action `+0x7610`, sub-state `+0x760c`)

- **100, normal** (`388690`): roars every 40 frames and unwinds any roll. All shells broken → 0x6a. Dante on it
  with his +0x28ac at 1 → 0x69.
- **0x68, shake-off** (`388e70`): when Dante stands on a segment (`+0x7805`, set in `38cd00`).
  - Sparks on every slot for 60 to 20 frames (by rank, `0x5baab0`).
  - Then it rolls 0x400 a frame for 64 frames, one full turn. Each slot starts 4 frames after the one in front.
- **0x69**: waits 20, then rolls 0x200 a frame for 64 frames.
- **0x6a**: waits 120, then rolls 0x200 a frame for 300 frames at path rate 5 (rushes off).
- **0x6b, head attack** (`388940` → `388de0`): sparks for 60 frames, Shl03, then 60 frames, script 5, back to 100.
- **0x65, stun** (`389900`, from `38d950`; inferred: a stagger callback): script 2, roar, then 0x6a after 100
  frames.
- **Death** (`389a10`): shells killed, route 10 (path 13), steps `389df0`, `38a0d0`, `38a430`.

### Health and shells

- One HP pool on the head (float `+0x7310`, shown on the boss bar). Start value: index 11 of block `0x5baf40` =
  **2800** (inferred but solid: the same index holds HP-like values elsewhere).
- Phase 1 below 2000 (`0x619788`), phase 2 below 500 (`0x61978c`).
- Body hits pass their damage to the head. The head block's indices 8-9 are 75/10 against the body's 30/3.
  Inferred: this is why the head takes more.
- Shell HP per segment (int `+0x144c`): 4, 4, 4, 4, 6, 6, 8 by rank (`0x5bba80`), 1 per hit.
  - At 0 it shatters (2 × effect 0x38d) and that segment stops firing.
  - Plates hide in stages (`390a50`) and the colour shifts (`391170`).
  - The shell refills to full when the segment moves onto a new path (through a hole).

### Port recipe (simplified)

- Bake the 18 used paths from [8] (bone 1 per frame) into polylines with arc-length tables.
- The head runs routes as above. Bodies follow 500 units behind along the same path chain.
- Use rank 0: routes 1, 4, 7 only, Shl00/Shl01/Shl05 and the head orb, and 4-hit shells.
- Shake-off: if Dante stands on a segment, 60 frames of sparks, then a 64-frame full roll that throws him.

Not decoded: damage numbers, the meaning of the head script entries, what triggers `38d950`, and how the holes
hide the body (the `0x78c4` "emerged" flag is set from slot 1's frame being in [0, end-100]).

### In the port (`psp/testroom/enemy.c`, rank 0)

- **Model:** drawn rigid in its rest pose. `tools/decimate_rigid.py` collapses edges across UV seams, which the
  skinned decimator can't, taking the head from 1,657 to 579 triangles and a segment from 629 to 219: about
  2,100 for the whole Gigapede against 6,060. Parts deep in a wall aren't drawn.
- **Routes 11, 1, 4, 7** with their events: 1-2 homing spheres from every segment with a shell, 3-4 the
  drop-then-roll orb from every other one, 9 three belly beams (Shl05, a segment re-picked each frame,
  hitting every 10 frames), 10 the half roll (0x69).
- **Shells:** 4 hits a segment. Broken, the segment's top carapace (body object 4, chosen from a render of each
  object) is left out, it stops firing, takes double damage and slows the Gigapede by 0.05. It grows back once
  that segment has followed the head onto a new path. All 7 broken: 120 frames of sparks, then the rush (0x6a).
- **Standing on it:** an upright segment's back is floor (`enemy_giga_floor`, through the mission's floor) and
  carries Dante (`enemy_giga_ride`). The first frame he's on it starts the shake-off (0x68): 60 frames of
  sparks, then a full roll running back 4 frames a segment. Past about 37 degrees he's thrown (a heavy hit) and
  can't land on it for 40 frames.
- **Damage:** a sword hit is 1 (2 heavy) of 67, the head takes 2.5 times that, a segment without its shell twice.
  Ebony & Ivory do 0.17 and don't crack shells, as with Cerberus.
- **In the walls:** a part with no collision floor cell under it is in a hole or behind a wall. It isn't a
  target for the lock-on, the sword or the guns, and doesn't fire; it still counts as alive.
- **Death:** route 10, path 13 at rate 4, flickering out after 90 frames.
- **Camera:** the room's fixed cameras aren't read. While it's in the room, the follow camera turns 30% of the
  way toward the middle of its visible parts (at most 350 cm across, 380 up) and pulls back and up.
- Tests: `python tools/ppsspp_scene.py <name> --mission 4 --room 111 --start 1 ...`; `onboss frame part` puts
  Dante on a segment, and the report (FOCUS_DONE.TXT) lists its states, hits, shells and throws.
