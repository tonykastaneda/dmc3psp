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

- **The attack gate** (the member at +0x130, vtable `0x60e874` for Lady; set up by `0032a290(gate, owner, block, ...)`):
  a gauge (+0x38). `0032b450` (+0x84) ticks it: Dante's bearing and ground distance from the owner, then the first row
  of block +0x18 ({range, half-angle in degrees, rate}, ending at 0) that holds him adds rate × the frame (per
  frame, `0x1e53730`). `0032b5a0` (+0x88) says it's open: past block +0x14 [k], k the bucket of the difficulty value
  (+0x14, 0-1199) in block +0x10. `0032b610` (+0x8c) empties it. Blocks: the Pride `0x5a8300` (open past 100 below
  difficulty 250; 10 a frame within 250 and 45°, 0.5 within 1500 and 1°), Beowulf `0x5b8900` (300; 20 within 500, 5
  within 3000 and 30°), Lady `0x5cc100` (100; 1 within 1500 and 30°, 2 within 300), and `0x5ab9d0`, `0x5acc10`,
  `0x5b0330`, `0x5b0e60`, `0x5b1460`, `0x5cdd00`, `0x5ce910`, `0x5cf5b0`, `0x5bf290`, `0x5bc010`, `0x5c1370`,
  `0x5e4160` (others, by their set-ups). Each class ticks it only in some states (Lady: walk, guns, run; Beowulf: idle
  and action 1). The port uses it for Beowulf's choice and Lady's specials.
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
- **Batting spheres back:** a sword hit's reach plus 200 in front of Dante (a swing's hit is one instant, so
  the window is wider than the move's) turns a homing sphere around (v × -0.5); it homes on the nearest part
  in the room (accel 1.2, top 40) and hits it as 5 light hits (the spec's ×5 for hit type 0x11). Test: `gigaorb
  frame` fires one at Dante.
- **Camera:** the room's fixed cameras aren't read. While it's in the room, the follow camera turns 30% of the
  way toward the middle of its visible parts (at most 200 cm across and up) and pulls back and up, short of
  any wall between it and Dante.
- Tests: `python tools/ppsspp_scene.py <name> --mission 4 --room 111 --start 1 ...`; `onboss frame part` puts
  Dante on a segment, and the report (FOCUS_DONE.TXT) lists its states, hits, shells and throws.

---

## D. Blood-goyle (CEm014): Mission 5

**Class and pack:** EST 32 → class 14 → `CEm014` (factory case 0xe, `FUN_0039b900`; constructor `0039bc20`,
vtable `0x609600`, object 0x1200 bytes). Pack **GDATA slot 125, em014.pac** (PNST):

| Entry | Content |
| --- | --- |
| [0] | 4 textures |
| [1] | The body, 21 bones, 2,384 triangles. Tagged `EFM ` but laid out as a MOD. Its objects are layers over the same surfaces (flesh, then scrolling blood: [2] is the UV-scroll script): only 1,226 distinct triangles |
| [2] | `.TSC` UV scroll script |
| [3] | `em014_00.clt` |
| [6] | Motion scripts (not decoded: their layout isn't Dante's) |
| [7] | Effects PNST |
| [8] | PAC of 28 motions. Motion N goes with state N (by length: 2/3, 4/5 and 6/7 are the left/right, toward/away and light/heavy pairs) [inferred] |

**Defaults** (`FUN_0039bef0`): health 300, split cooldown 300, splits left 5, at most 5 out at once (+0x461; the
spawn record can raise it to 6). Hover offset 50 above Dante, re-rolled to 50-120 every 200-400 frames.

**States** (+5, dispatched by `FUN_0039cb30` to `0042f090`…`004324e0`; the next one picked by `FUN_0039e660`
from table `0x5bf814`):

| State | What it does |
| --- | --- |
| 0 | Hover: holds Dante's height + the offset, easing at most 3 down / 1 up a frame. 30% use motion 0x1c |
| 1 | Settle to the floor (effect 0x198) |
| 2, 3 | Strafe (sideways 6, or 20-32 when dodging). When Dante aims (his state 0x11) within 500, 50/50 into one |
| 4 | Approach: 12 a frame over 2500, 6 over 1000, else 3 |
| 5 | Back off at 2 |
| 6, 7 | Flinch light / heavy |
| 8 | **Split**: cooldown 300, effect 0x194, `FUN_0039e370` spawns a CEm014 at its place with its health, the splits left (after one's taken) and the spawn flag; thrown apart on frames 11-40 |
| 9 | The new one, emerging (thrown back 10) |
| 11 → 12 → 14 | Attack: wind-up; aim (yaw ± 50° at random, pitch -0.61); dive at 20, accelerating 0.5 to 30, steering 3° a frame (546 × dt of 0x10000) while Dante's ahead. Hits a wall or the floor: effect 0x1be, back to 0 |
| 13 | Shoots: Shl00 (`FUN_004404a0`) is an emitter that, 27-32 frames in, sends 2 / 3 / 4 Shl01 bullets by band (`0x5bf242`) at Dante's place ± 100 cm a side; they fly straight and burst on walls (`FUN_00459eb0`) |
| 15 | Turned to stone: falls 40 frames (gravity), lands (effect 0x2c), then 16 or 17 |
| 17 | Statue (its own textured body surface, MOD object 3, switched by `0039cb30`) until the stone gauge is back to 0 → 18 |
| 18 | Back to life (effect 0x36d) |
| 19, 21 | Stone struck: knocked up at 23.1, falling 0.7 a frame |
| 27 | Splits of its own accord (`FUN_004324e0`): chosen before the tables when fewer than +0x461 are out, splits are left (+0x45c) and none is splitting (flag 0x80000000). Effect 0x194 at frame 149, then state 8 |

**Hits** (`FUN_0039d9e0`):
- The table at `0x5bf240` is indexed by +0x41b, a **distance band** (`FUN_0039e660`: under 350, under 700,
  beyond; the u16 at the start of each row), not the difficulty rank as first read.
- **Guns** (hit record byte 2 = 1): stone gauge +0x448 += 2 × power, shots +0x44c += power; over 60 shots: gauge
  +120, a flinch (7). Over `0x5bf244[band]` (**150** near and mid, 200 far) it turns to stone (0xf). Capped
  at 1000.
- The gauge drains by distance band: 1 / 1.2 / 1.5 per frame as stone and 0.2 / 0.25 / 0.25 as flesh (
  `0x5bf248` and `0x5bf24c` in `0039cb30`).
- **Sword** (byte 2 = 0): the split check `FUN_0039e460`: not stone, the cooldown +0x454 run out (it drains
  1e6 / max(d, 1000)² a frame, d = distance to Dante), fewer of its class than +0x461, a 75% roll, splits left.
  Else a flinch by hit type. On stone: states 0x13 / 0x15.

**In the port** (`psp/testroom/enemy.c`, "Blood-goyle"): all of the above. The blood bullets fly 30 cm per frame for 30 frames (`00459ce0`, `00459eb0`); it shoots
instead of diving half the time from beyond 500 cm. The dodge from aimed guns is taken from Dante's shots (within 500 hovering, 300
approaching, 50/50 left or right). The decision tables are the game's (below).

**Decision tables** (`0x5bf814`, 11 words a state): a list of {row table, s16 yaw offset 0, s16 -18, float
distance}; the first whose distance Dante's within (400, 1000, 100000) picks the rows. States 6 and 7 use the list at
`0x5bf7c0`, the rest `0x5bf790`. A row is 4 float weights (one per band, the 4th unused) and the u16 next state;
a 0-100 roll walks the cumulative weights of Dante's band (999.9 = the rest). The same state as now: it carries on.
An attack (11) needs fewer than 2 attacking and the attack token (taking it for 3 s), else the next row; under 350
it also needs a check (vfunc 0x1c8) that isn't decoded, so the port skips it. Weights, under 350 / under 700 / beyond:

| Table | Rows |
| --- | --- |
| under 400 | attack 10/10/15, strafe L 25/20/20, strafe R 25/20/20, back 25/30/30, else hover |
| under 1000 | attack 40/42/45, approach 10/30/30, strafe L 13/13/10, strafe R 13/13/10, else back |
| beyond | attack 10/15/20, approach 60/60/rest, else hover |
| flinched, under 400 | attack 10/10/10, strafe L 30/30/40, strafe R 30/30/40, else back |
| flinched, under 1000 | attack 10/15/20, strafe L 25/20/20, strafe R 25/20/20, approach 20, back 5/10/10, else hover |
| flinched, beyond | attack 10/15/20, else approach |

Shooting (13) isn't in these tables; where state 11 goes to 13 isn't read, so the port still shoots instead of
diving half the time from beyond 500 [port's choice]. It re-decides after 70 frames hovering, 90 approaching and
45 otherwise [estimate]. The body is drawn once per surface and cut to 490
triangles with `tools/decimate_rigid.py` (skin-aware), as up to a dozen can be on screen. A pistol shot's power
is taken as 5 [inferred]; stone takes triple sword damage [inferred]. Stone is drawn untextured grey.

---

## E. The Jester (CEm044, SE): Mission 5, room 422

**Finding the pack.** EST 62 → class 44 (`0x599690`: class 44, pack 27). The room loader (`FUN_0020afc0`)
collects each spawn's pack byte and calls `FUN_0028f050`, which asks for list 5 (the enemy) and list 6 (its
effects) with that number. List 5 is `FUN_0028eac0` case 5: the pointer table at **`0x58e740`**, one word per
pack whose top 16 bits are the GDATA slot (pack 9 → 0x7d = 125, the Blood-goyle; 14 → 129, the Gigapede; 16 →
130, Cerberus; 18 → 131, Agni & Rudra). Pack 27 → **0x8c = slot 140, em037.pac**.

| Entry | Content |
| --- | --- |
| [0] | 6 textures |
| [1] | Body, 36 bones, 5,158 vertices: frilled collar, sleeves and skirt |
| [7] | His hat (17 bones), modelled in place on his head (bone 5) |
| [3] [4] [5] [6] [19] [28] [29] | Motions: 3, 18, 9, 4, 7, 1, 2 |
| [8] [9] | `em037_00.clt`, `em037_01.clt` |

**Code.** `new` `FUN_00532880` (object 0x4e00 bytes), constructor `00532d50`, vtable `0x616010`, set-up
`FUN_005331d0` (arena centre (3500, 492, 3500) at +0x680). He's built like a player character: actions 0-0x15
are moves picked by `FUN_00536b10`/`00536c70`/`00536dd0` (a random one of three) and 0x64-0x80 reactions
(dispatch `FUN_0053bfc0`). The class is 383 functions, 0x532000-0x556000: not ported.

**In the port** (`psp/testroom/enemy.c`, "The Jester"): a stand-in fight on his model (cut to 1,656 triangles)
and motions. He walks in and swings (one or two swings), lobs three bombs that roll at Dante (the Gigapede's
rolling orb), vanishes in a puff and reappears behind Dante, and dances. He stays on his square platform (±1550 cm about (3500, 3500); held to ±1400), teleports
included. Hits fill a stun gauge (12, draining
slowly); stunned for 200 frames he takes double damage. Which motion plays for each was chosen from their
shapes and lengths [inferred]: idle js21, walk js03, swings js08/js09, bombs js10, vanish js26, dance js31,
flinch js05, stun js14, launched js04, death js16.

**Decoded** (`psp/testroom/jester.c`; decompiled 0x532800-0x556000):
- **Health** by encounter (+0x481e, from the spawn record; `0x5e2f60`, with the second set's threshold at +4):
  **5000** (room 422, Mission 5: group 0), **6500** below 2500 (room 448, through 218's door 4 in Mission 12: group 1),
  **10000** below 4500 (room 449, through 129's door in Mission 17: group 2). The rooms' groups 0-2 are the three, each
  a spawn record with op 8 (0, n) after it setting the encounter; st447 isn't on the disc (448, 449 are GDATA 924, 928).
  The parameter block is `0x5e3a60` (its own +0x2c, 7000, is overwritten by that). In the port Dante's hits take 42 /
  84 as against the Vanguard's 3000.
- **Where he reappears** (`FUN_00536230(row)`): table `0x5e2e40`, 12 bytes a row {about Dante?, radius, spread}: row 0
  about himself 0-50, row 1 about Dante 50-550, 2: 200-1200, 3: 50-300, 4: 100-1000, 5: 150-750, 6: 100-500, 7:
  150-750. A random bearing, turned 10° at a time up to 36 times until the spot is within 1000 cm of (3500, 3500).
  The port's vanish uses row 1.
- **Actions** are requested through vfunc 0x368 (`0053bf40`) and run by `0053bfc0`; when a request for actions
  0-0x13 meets flag +0x4cd0 he does one of `00536b10` / `00536c70` / `00536dd0` instead (a third each), or with
  +0x481d set and +0x4818 clear `00536f30` / `00536fb0` (half each). Hits (`0053d0d0`, vfunc 0xc0) answer with
  reactions 0x67-0x6a (a coin flip, by the hit's type), 0x6d, 0x7b or 0x7d.
- **Attack choice** (`FUN_00534490`, his per-frame AI): while idling or flinching (actions 0x64, 0x67-0x6a) his idle
  time (+0x168) runs; at `0x5e2e30[rank]` (500 frames on Easy and Normal, 450, 400) he requests one of three attacks,
  0x75 / 0x76 / 0x77, picked a third each but never the last again (`0053e960`). With +0x481d set (later
  encounters, below `0x5e2f64[encounter]` health) `0053ea40` adds 0x7f / 0x80 and 0x7b; not in room 422.
- **Each attack** (actions 21-28, 29-36, 37-44; dispatch `0053bfc0`: 0x75 `00537750`, 0x76 `00538110`, 0x77
  `00538ae0`): he vanishes to a spot 100-1000 cm from Dante (row 4, motion js32), then throws: js33, js30 or js31.
  What he throws is picked the same no-repeat way (`0053e720`, +0x4d6c) from three shell classes (`0055c380`,
  `0055bf40`, `0054d3a0`), not decoded. Motion ids map to jsNN directly (table `0x5e2db0`: pack, motion).
- **What he throws** (`0053e800`, by +0x4d6c): not shells but emitters that live until the attack ends (the Jester
  destroys them through +0x4cec / +0x4cf0 / +0x4cf4), each made with a life of 10000 frames:
  - kind 0, `0055c380` (vtable `0x617f90`, update `0055c5c0`): after a delay from `0x5e2fd0[encounter]` (140 for the
    first), a cycle of five phases (`0055c690`, `0055c6e0`, `0055c820` 69 frames, `0055c880` 99, `0055c950` 60):
    it plays the prop's motions 5, 6 and 0 and spawns a projectile `0054de00` (0xcc0 bytes, vtable `0x616f70`,
    update `0054e240`) from the prop he holds (+0x4cd8, at +0xe90), launched at 1000 (`00550f10`) and set off at
    99 frames (`0054edc0`).
  - kind 1, `0055bf40` (vtable `0x617e50`, update `0055c130`): every `0x5e3000[encounter]` frames (180 for the
    first) spawns `0054c660` (0xd40 bytes, vtable `0x616cf0`, set-up `0054caa0`, update `0054ce50`): placed on the
    floor (y 500) turned at random, it lives 180 frames and at 55 arms its hit (`00294480`, 150 cm up), a trap
    springing where it was put [where that is, is not read].
  - kind 2, `0054d3a0` (0x540 bytes, vtable `0x616e30`, update `0054d590`, then `0054da60` / `0054d850`), its count
    `0x5e3010[encounter]`: not read.
  Ported in `jester.c` (the projectile follow-up below) and thrown in the fight (enemy.c).
- **His defence and stun** (`FUN_0053d0d0` his hits, `FUN_0053eb20` the parameter row a hit meets, `FUN_0053e6a0` the
  meter): the rows at `0x5e3a60` (0x54 bytes) are picked by the hit (blade or shot) and his state (+0x1e2: 3 stunned,
  4 another); their +0x20 scales the damage, **1 as he is, 100 stunned, 35 in state 4** (hundredths). The stun meter
  +0x4cc8 takes **50** a blade hit (`0x5e2f14`, rank 0) and **1.75 ×** the shot's own value a bullet (`0x5e2f10`;
  raw packet +4 is copied to +0x4d54 by `0053d0d0`). Ordinary E&I records 0/1 at `0x5a5a30` both
  contain **10** at +4 (`gp=0x620170`, table pointer `0x618bf0`, constructor `002efaa0`), giving
  **17.5 stun per ordinary shot** at rank 0: hit 15 crosses the threshold. Difficulty damage scaling in
  `002f17a0` does not rewrite this packet value. Upgraded/charged E&I variants carry different packet values;
  the port's `HIT_PISTOL` represents the ordinary unupgraded shot. Past **249** (`0x5e2f30`) his next reaction is the stun (0x6b, `FUN_00535e60`:
  motion js10, the meter emptied, **500** frames from `0x5e2ed0`). Otherwise a shot makes him flinch where he is
  (0x67-0x6a, `FUN_00535470`, js06, row 0) and a blade sends him off (0x6d, `FUN_00536110`, js12, row 1) unless his
  attack is due (idle time ≥ 500). Hits during his attacks (actions 0x15-0x2c, 0x3e-0x43) don't stop him. With
  +0x4d30 set (a health threshold) a hit sends him to the arena's middle (3500, 500, 3500) and into his second set
  of moves (+0x481d, `FUN_00539470`, action 0x79), below.
- **The second set** (encounters 1 and 2, below 2500 / 4500, `0x5e2f64`): 0x79 (state 0x32 `00539540`, 0x33
  `005397c0`): out of sight at the middle 60 frames, then from y -220 facing Dante (js25) up 6 a frame to the floor.
  His attacks then (`00534490`): on the floor (+0x4818 clear: landings clear it, `005373c0` / `0053a160`) 0x7b, else
  `0053ea40`'s no-repeat pick of five, 0x75 / 0x76 / 0x77 / 0x7f / 0x80. **0x7b** (`00539a40`, state 0x35 `00539c80`):
  up at y 2492 for 60 frames (`0x5e2fc0`), then falling (gravity 1.11, +0x4810) onto a spot from row 1 (or the middle,
  by +0x4dc4) in 60 frames. **0x7f** (`0053a550`, 0x3e-0x40 `0053a670` / `0053a9d0` / `0053ae90`): 120 frames
  turning 5° a frame, 300 charging at 45 along his facing (`0x5e3020` row: 120, 45, 35, 300), back toward the middle
  ±35° at random at the edge (+0x4d1d), 60 to recover (js03). **0x80** (`0053afb0`, 0x41-0x43): 120 frames spinning 10°
  a frame (js29, `0x5e30c0`), then 5 hops (`0x5e3060`: count 5, 45 frames, 500 high, at most 500 toward him), 60 to
  recover. In each, a sphere rides him (`00548fb0`, 150 up, 160 cm `0x5e3238`): kind 1 in the charge (record 6,
  1000, live, resting 30 frames after a hit), kind 2 falling in 0x7b / 0x80 (record 7, 1000), both knock-downs.
  **In the port**: all of it (`enemy.c` J_RISE / J_FALL / J_CHARGE / J_HOPS); stand-ins: the charge's edge is the
  port's arena clamp, his attacks clear +0x4818 as they end [inferred from `005373c0`], the effects 0x3cb / 0x3cc / 0x3f3 play (enemy.c
  jester_fx; the wind-up's and charge's trail interval 10 frames [estimate]) but the room flag 0x79 sets isn't shown, the 0x7b / 0x80 hop motions are js01 / js29 as `0053bd90` names them.
- **In the port**: the attack choice, its 500-frame wait, the teleport and the three throw motions are the game's;
  each throw starts the emitter its no-repeat pick names (`jester_projectiles_begin`, AI rank 0), the attack holds
  its last pose until the emitter's cycle is through (`jester_projectiles_minimum`) and its end stops the emitter;
  what lands (the ball 1200 heavy, a trap 650, the cross 800) hurts Dante with that damage. The bomb (em037 [25], textures [24]: the
  ball attack's projectile, `0054de00` entry 0x19 + 2 x variant) and the trap's hand ([27], textures [26], motions [28]:
  `0054caa0`'s +0x70 / +0x74 / +0x78) are drawn as his own models (`tools/prepare_jester_props.py`,
  `jester_projectiles_draw`): the bomb tumbles in flight [estimate]; the hand plays PAC [28]:0,
  a nonlooping 120-frame motion on its 18-bone rig with its original three-weight skin. Its transform stays at
  floor y=500, pitched -90 degrees and randomly yawed (`0054caa0`); it does not rise/sink by a guessed offset.
  Its opacity follows `0054d080` / `0054d0e0` / `0054d120`: +12 per native tick to 128, hold for >50 ticks,
  then -12 to zero; its animation continues independently until the 180-frame actor lifetime expires.
  The throw hold includes its first 30 body frames before the emitter starts, so the trap actually reaches
  its native >180-frame spawn wait instead of being cancelled early. The cross and the bomb play the game's effects from em037 [10] (NFX_JESTER): the cross V0x3f9 once and V0x3f8
  looping on its frame (`0054d610`: at him, facing Dante turned 45 degrees), V0x3fa as it ends (`0054dc70`); the bomb
  V0x417 in flight (`0054e710`) and V0x41c as it goes off (`0054edc0`). Their P particles aren't ported: the bomb's burst
  and the trap's warning ring are motes. **G0x1c8 is not this trap's effect**: `0054c1e0` initializes a
  separate 0x470-byte actor (`0054bf90`, vtable `0x616bb0`), whose sole caller in the exported call graph is
  player weapon routine `0052c4c0`. The Jester's hand is the 0xd40-byte actor `0054c660`
  (vtable `0x616cf0`), initialized by `0054caa0` and updated by `0054ce50`, spawned by emitter `0055c200`.
  Neither hand initializer nor update spawns G0x1c8; em037 [10] contains G IDs 0x253, 0x25a, 0x277, 0x276,
  0x25d, 0x267, 0x25b and 0x25c, with no 0x1c8. No unrelated generator is attached to the hand. Between attacks he idles as the game: 0x64
  (`00534d10`) goes at once to 0x66 (`00535140`), one of `0x5e2e10`'s five motions (js21, js24, js23, js27, js28 by
  `0x5e2db0`) at random but never the last again (the next instead); for its first 60 frames he turns to Dante
  (`0053ed60(10)`, the port's rate [inferred]), js27 and js28 walk him by their root (`00535310`), and at its end 0x64
  again (0x7b in his second set off the floor). His defence, stun meter, flinch and vanish are the game's (above; an ordinary E&I shot
  contributes 10 x 1.75 stun); the arena clamp stays the stand-in's. The old cane swings and taunt are no longer
  chosen: the game's first encounter has no such attacks in its picker.
  Verified in PPSSPP Mission 5 room 422: `run3_jester_hand_live` frames 1815/1830/1840 show the
  native hand motion and its 650-damage hit; `run3_jester_stun_native` frame 240 shows js10 stunned,
  with the meter log advancing 17.5 each ordinary hit, 245 at hit 14 and 262.5 at hit 15.

**Projectile follow-up (pM).** The emitters' tables use **AI rank `+481c`**, not encounter `+481e`:
ball wait/speed `5e2fd0` = (140,70), (80,70), (40,70), (20,70); trap interval `5e3000` =
180/120/90/60; cross angular speed `5e3010` = 0/0/−0.13/−0.25 degrees per frame. Thus the first
rank's four-arm cross is stationary. The native health-by-encounter table remains separate.
`55c5a0`/`55c110` initialize their initial delay from the constructor's zero argument.

The ball (`54e710`, `54edc0`, `54ebc0`) charges attached to the prop, then flies at 70 cm/frame,
aimed on the prop's horizontal axis with a 15-degree cone. Its life is 600 frames, arena bound
2500 cm from (3500,3500), direct radius 50, burst radius 200, burst life 20. Attack record 3 at
`5e3f00` supplies 1200 damage. The trap (`54caa0`, `54ce50`, `54d230`) is placed at Dante's sampled
XZ, y=500, random yaw; after 55 frames it arms a radius-75 sphere at y=650 for 10 frames, and
expires at 180. Record 4 supplies 650 damage. The cross (`54d610/850`, `54db00`) has four 90-degree
transforms, initially 45 degrees from Dante, with a 5-degree lean; each box spans 0..1200 along
its arm, 0..2000 upward, half-width 70. Record 0 supplies 800 damage. Visuals and integration are
recorded with the completed projectile port below.

---

## F. Agni & Rudra (class 27): Mission 5, room 121

EST 45 → class 27 → `FUN_00480bb0`, an object of only 0x290 bytes: a controller for the two brothers. Pack 18
(`0x58e740`) → slot 0x83 = **GDATA 131, em026.pac** (PNST): [0] 2 textures, [1] the body both brothers use (19
bones, 3,148 vertices, 3 m tall), [2] and [3] their swords, [6] [7] `em026_03.clt` / `em026_04.clt` (one each),
[11] a PAC of 100 motion slots (54 used), [12] [13] a motion script each. Hands: bones 7 and 11. Classes 28-30
also use pack 18 but are made by `FUN_0039ee10`, in the Blood-goyle's code. Slot 132 (em027) is a lighter copy.

**In the port** (`psp/testroom/agni_rudra.c`: the brain and the state table; `enemy.c`, "Agni & Rudra": the
bodies). One spawn brings both: Agni (tinted red, sword in bone 7) and Rudra (blue, bone 11), each 956 triangles.
Each runs the game's states, playing their motion scripts (em026 [12] Agni's, [13] Rudra's, exported by
`tools/prepare_agni_rudra.py` with the caps [14]/[15] and the attack records at `0x5cf6f0`) through the Seven Hells'
script runner, so motions, hit windows, caps and damage are the game's. Health 7000 each; Dante's hits take 42 /
84 as against the Vanguard's 3000. Pairing (the class-27 controller, `004810f0` / `004811b0`): at rank 0 the formations alternate, 6 then 5, every
1800 frames (lists `0x5cf6d0`, records `0x5cf600`); the healthier brother takes flags 0x12 / 0x22 (role 1, he
attacks) and the other 0x213 / 0x223 (role 2, no attack permission: he works round Dante to the far side by the
flank, `003ad5d0`). Ported from the decoding below: the attack timer, the zones, the attack choice
and the lone brother's moves. The brain's order is the game's: the formation's 0x8000 (the climb, below), then Dante
beyond his side line turns him (state 0, `FUN_003af2c0`), then the reaction to Dante's move (`FUN_003aeec0`, below):
Agni takes his guard stance (21), Rudra his (23), the survivor never. A blow that throws (`FUN_003a2ab0` /
`FUN_003a2c00`, vtable +0xe4 / +0xe0) knocks him back, 37 struck from in front, 38 from behind (which of Dante's
hits are those is inferred: the port's heavy, slam and launch). The pair's opening (64, below) is the game's
(`ar_opening`; Rudra going 60 frames after Agni reads +0x2ee0 as their place in the pair, an inference): in the
arena (rooms 121, 414, 435; `enemy_set_room`) they start where `FUN_0039fa30` puts them, (2900, 2125) and (2120,
2125), and the first leap and turn aim at `FUN_003af970`'s points, not Dante.
**The survivor** (`ar_climb`, `ar_pedestal`, `ar_pounce`): his brother down, the controller's formation 9 sends him
up his statue's head (63), where he takes up both swords (59 / 60, the second drawn in his other hand), and leaps
down at Dante (56); from then on he's alone (+0x2ee7 = 0). **The guard** (`ar_hit`): the stances (21 / 23 / 25) and
his swings' own guard turn blows from in front as `FUN_003a1a50` does (below). **The hops** (`ar_hop_run`) and the
leap (11, `ar_leap`), the stun (36, `ar_stun`) and the leaps 56 / 65 move by the game's speeds and accelerations
(`FUN_003b0090` / `FUN_003b02b0`), off the floor by their own fall. Not yet: the survivor's moves below each health
mark (57 / 58, `FUN_003aeca0`: they fire shots through `FUN_004ea5f0`, not decoded), Dante's recoil from a guarded
blow (the hit's +0x34), the game's own effects (the charged blades are the game's V588 / V589 from em026 [18], with their generators, looping while the charge lasts but riding his hand, not the sword's matrix [estimate]; the slam's fire and wind bursts 0x311 / 0x312 are a stand-in: their E part is a 6-frame flash and the rest P particles, so the port's burst, hurting within 300 cm as attack record 12 [estimate]; the pedestal's sword flash 0x3b3 / 0x3b4 and the landings' 0x2c / 0x2e5 are dust), and the script events. The slam (52-54, `003a8200`) plays its scripts by swords (61-64, or 65-68 with both; gp tables `0x6195a0`) and bursts at frame 165.
Dashes use their records' speeds; other states move by root motion. A looping script gives way after 150 frames.
They start as statues (ar71/ar72) and wake within 900 cm, when struck, or after 600 frames (the game plays a
cutscene). One boss bar for both.

**Decoded** (the brothers' class: constructor `0039f790`, vtable `0x609960`, init `0039fd70`, per-frame `003a0d50`):
- **One object per brother**; +0x2dd5 says which (1 Agni, 2 Rudra), +0x2f40 which swords he holds (0, 1, or 2 =
  both: the survivor picks up his brother's). Parameter blocks (0x54 bytes, same layout as the Vanguard's):
  `0x61a754` (base), `0x61a758` (Agni), `0x61a75c` (Rudra), `0x61a760` (the survivor with both swords). **Health
  +0x2c = 7000** each (the Vanguard's is 3000).
- **States** (+0xac0, 66 of them, dispatched by `003a0d50`); the next one is written to +0x2dda by the brain and
  taken when the current one allows (`003af300`). Motion slot per state (the em026 PAC; `ar` + slot): 0 → 26 (wake),
  2 idle → 0, 3 walk → 1, 4 → 94, 5-7 → 96, 8-10 → 95, 11 leap → 2, 3, 4, 5 (take-off, rise at 3 with 16 more, air,
  land), 12-15 a move from table (`003b0450(n)`), 16-20 another (`003b09c0(n)`), 21/23/25 the reactions to Dante's
  attacks by sword set (gp table `0x6195e0`: 21, 23, 25), 22-26 → 28-35, 27-29 → 36-38, 30/31 → 39-41,
  32/35 → 45, 33/34 → 42-44, 37 → 50, 38 → 51, 39 death → 52, 40/41 → 53, 42-45 → 55-58, 46-49 → 87-90,
  50 → 59, 51 → 60, 55 → 69, 56 → 74-77, 65 → 75-77, 57/58 → 84-86, 59 → 78/80/82, 60 → 79/81/83.
- **The brain** (`003acfb0` with his brother up, `003ace70` alone), in order: the pair's opening (`003aedd0`, state
  64); Dante in an odd sector (+0x2df5 bit 0, below) → **state 0, a turn** (ar26 at 0x888 a frame); a reaction to
  Dante's move (`003aeec0`); an attack when allowed (`003ade30`); else a move (`003ad5d0` paired, `003ad180` alone).
- **Sector** (+0x2df5, `003afd30` on the angle to Dante, 65536 a circle): 0 within ±0x1000 in front, 2 / 4 to
  ±0x4000, 3 / 5 to ±0x7000, 1 behind. **+0x2df4 is the zone**, not a count.
- **Reaction** (`003aeec0`): never with both swords (+0x2f40 = 2); only for a move id (`FUN_0021e920`, the command
  table's action) that differs from the last (+0x2ec3), Dante in an even sector, and, for Dante (character byte
  `0x1d0d741` = 0): Stinger **7 / 8** in zones 0-2 and facing him (Dante's yaw − his − 0x4000 under 0x8001, +0x2dee);
  **38 / 39** in zones 0-2; **92 and 4** in zones 0-1. Other moves get none: the last branch tests +0x2ec2, which
  stays 0xff. The state is the gp table's (21, 23) by sword set.
- **Zones** (`003afc60`, +0x164 = distance to Dante): ≤210, ≤350, ≤650, ≤1000, beyond (0-4). **Role** +0x2ee2 =
  `[2,0,1,2,3,4][flags & 15]` (gp table `0x6195d8`), 1 when alone. **Flank** +0x2ee3 (`003acae0`): the angle at
  Dante between the brothers, under 0x1400 → 0, under 0x4800 → 1 / 3 (by side), else 2 / 4.
- **Attack permission** (`003af5c0`): a timer from `0x5b7440[rank]` (300, 240, 180, 120, 60 frames; rank 0 for
  Easy and Normal) runs down at (2 − health fraction) a frame, × 2.5 while his brother is in state 36, × 2 for
  the survivor, × 0.75 within 650 and × 0.5 beyond. Paired, only one holds it at a time (flags 0x100).
- **Attack choice** (`003ade30`, with permission 1), by sword set and zone (≤350 / ≤650 / ≤1000), and whether the
  sword is charged (+0x2ee4, 1800 frames from the charge states 50 / 51, `003ac280`): Agni's 42 charged 43, then
  50 (charge) or 47 / 1-in-4 52, then 50 or 52; Rudra's 44 or 45 / 1-in-4 53, then 51 or 49 / 1-in-4 53, then 51 or
  56; both swords 45 / 43 with 1-in-8 54, then 49 / 47 with 1-in-4 54, then 56 / 54. Beyond 1000 none. Dante 300
  above them for 88 frames (`003afbe0`) → 55. After a guard (permission 2 or 4) a second table, not ported.
- **The opening** (`003aedd0`: with +0x2de7 set, two brothers, and neither in state 0x24 nor flagged, state 64).
  State 64 (`003ab250`, steps +5): 0 beyond 100 of Dante script 26 (else straight to 0x14); 1 five frames; 2 turning
  0x888 until both layers' scripts end; 0xa script 2 (take-off); 0xb its end, script 3 (rise), velocity (0, 3, 0) and
  +0x2e08 = 16; 0xc turning 0x800, then script 4 (air) with a 420-frame clock; 0xd in the air (turning 0x800,
  `003b0050`) until the clock or within 100; 0x14 script 26; 0x15 five frames; 0x16 turning until done; 0x1e script 0
  and 300 frames; 0x1f waiting (turning 0x100) for his brother to be in state 64 past step 0x1d (or to have no opening
  flag), or the 300 to run out (then idle); 0x28 a wait of 60 × the brothers' count (+0x2ee0); 0x32 script 2; 0x33
  script 3 at (0, 3, 0) and 16; 0x34 script 4 with 300 frames; 0x35 at him (turning 0x800), landing or zone under 2
  ends it (zone under 4 by its clock: state 0x41); 0x36 twenty frames still; 0x37 back to idle (state 2).
  "Within 100" and the turns aren't to Dante before step 0x1e: `003af970` sets the target (+0x150, distance +0x164) in
  64 to the floor before his pedestal (2860 Agni's / 2170 Rudra's, 2200) with +0x2de7 = 1 (Dante nearer z 4000 than
  2200, `00481350`) or (2862 / 2133, 4000) without, then (2500, 3000) to step 0x1d, then Dante.
- **The survivor** (formation 9, `004810f0`: the pair's count falling to one, of two). Record 9 gives his row 0x8000
  for 999999 frames; `003ac9c0` copies it to +0x2ede, and the pair's brain (`003acfb0`, run while +0x2ee7 = 1) takes
  **state 63** first (`003af2f0`). 63 (`003aad50`, steps +5; target the floor before his pedestal as in 64): 0 beyond
  100 script 26 (else 0x14); 1 five frames; 2 turning 0x888 until done; 0xa script 2; 0xb script 3, speeding 3 a frame
  to 16; 0xc turning 0x800, then script 4; 0xd in the air until within 150; 0x14 still; 0x15 **hop 5** (target
  (2500, 3000)): past y 960 he's set at (3065, 960, 1780) (Agni, +0x2dd4 = 0) or (1930, 960, 1778) (Rudra) in state
  59 / 60, setting the room's flag bit 1 / 2 (+0x18 +0x5c); landing short, 63 starts over. The pedestal is the top of
  his giant statue's head. **59 / 60** (`003aa180` 0 / 1): 0 script 78 / 79, facing +z, clocks 173 and 110; 0xa at 110
  the sword's flash (`003ac670`: effect 0x3b3 / 0x3b4, sound 0x59); 0xb at 173 clock 215; 0x14 then script 80 / 81
  and 95; 0x1e then 0x28: from the script's frame 112 **both swords** (+0x2f40 = 2, block `0x61a760`, +0x2ee7 = 0 so
  `003ace70` from then on, +0x2ee5 = 0, `003af8a0` / `003af8e0`); 0x29 script 82 / 83; 0x2a at its frame 300 set at
  (3061 / 1958, 939, 1816) with both blades charged (`003ac280` 0 and 1); its end → **state 56** with gravity on.
  **56 / 65** (`003a94c0` 0 / 1): 0 script 74, turning 0x800; 0xa (56) its end, or (65) at once: script 75, up at 45
  slowing 2; 0x14 its end, 76; 0x15 past the top falling 3 a frame to 30, turning 0x5b74b0[rank] + 0x100 (0xc0);
  0x1e landed: 77, effect 0x2e5; then idle. Steps 0x14-0x1e go ahead at (distance − 350) / 10 + 10, 10-30 (10 with
  Dante beyond the side line). **57 / 58** (`003a9980`, chosen by `003aeca0` for the survivor once his health
  fraction falls to the next of 0.8, 0.65, 0.5, 0.35, 0.2 (`0x5b7480`); 57 Agni, 58 Rudra): scripts 84, 85 (looped
  `0x5b7460[rank]` = 1), 86, firing shots through `003a9e90` / `004ea5f0` (not decoded).
- **Hops** (`003b09c0(n)`, records `0x5b76d0` + 0x40 n, phase at +6): +0 the turn (`0x5b7410`: 0, 0x100, 0x300,
  0x800, 0x1000), +4 four scripts (22-25: take-off, rise, fall, landing), +0xc the speed up, then the rise's y, x, z
  accelerations and x, z caps, the fall's y acceleration and y cap, x, z accelerations and caps (local x right, z
  ahead; `003b0090` runs x and z to their caps, `003b02b0` y). 0: up 50, −3 a frame, falling to 25; 1 / 2 the same,
  12 ahead / back; 3 / 4 up 40, 10 right / left; 5 (the climb) up 40, −0.5, falling to 20, turning 0x1000. Phases: 0
  take-off; 1 its end, the rise; 0xa / 0xb past the top, the fall; 0x14 / 0x15 landed (vtable +0x1bc), the landing
  (effect 0x2c, `003ac750`); 0x1e still; 0x1f from its frame 85 `003acd50` may end it; 0x28 done. States 16-20 are
  hops 0-4, then idle. **The leap** (11, `003a3860`): 2; 3 speeding 3 a frame to 16, turning 0x800; 4 for 5 frames,
  or until a wall pushes him (vtable +0x1c0, `002575a0`: the push past 1 cm); then 5 slowing 1.5 a frame (from frame
  25 `003acd50` may end it), and after a wall's stop with Dante beyond 350 in front, hop 1 (state 17). The survivor
  with both swords flies on to within 350 and lands slamming (91 + swords, `003a3e30`).
- **Guard** (`003a1a50`, vtable +0xc4, a hit while the guard for its kind is up: +0x1e4 blades, +0x1e5 bullets). The
  stances 21 / 23 / 25 (`003a4270` / `4770` / `4c70`, by swords: gp `0x6195e0`) raise both to 2 and play 27 then 29
  (30, 32; 33, 35), turning 0x800 for 20 frames. The attacks 42-49 and the parries 30, 31, 33, 34 raise +0x1e4 by the
  script's flag 0 (op 3, over the swings; `003af800`): 4 while +0x2ee5 (blows met in a swing) < +0x2ee6 − 1, else 3;
  +0x2ee6 = `0x619580[rank]` (1 at rank 0), doubled with both swords. A blow from Dante's odd sector (behind) is
  taken. From in front: mode 2 counts +0x2ec4 (a blade 2, when Dante's +5 is 0-2; a bullet 1), and `003a27f0` (vtable
  +0xf8) sends a blade past +0x2ec5 (`0x619588[rank]`: 11) to 27-29 (`0x619598`, the guard broken: script 36-38,
  counter permission), else 22 / 24 / 26 (`0x619590`: the hold 28 / 31 / 34 then 29 / 32 / 35 if he's attacked since
  (+0x2de5), else only the lowering; counter permission 2); a bullet past 11 clears +0x2de5. Modes 3 / 4 send a blade
  to `003a24d0` (vtable +0xe8): +0x2ee5 up by 1 (2 when Dante's +5 is 3 and +6 his id), under +0x2ee6 the parry by the
  swing he's in (34 from 34 / 45 / 49, 33 from 33 / 44 / 48, 31 from 31 / 43 / 47, 30 from 30 / 42 / 46, else 32 or 35
  by swords), at it **36**; mode 4 also turns Dante's blow back (+0x34 on the hit). **36** (`003a5ee0`): 46, +0x2ee5 = 0,
  charge off, counter permission 4; 47 for `0x5b7420[rank]` = 150; 48: from frame 66 up at 24.4, from 75 falling 2 a
  frame to 30, spinning 14° a frame over frames 60-85 (`003aff80`); landed 49 (the survivor's blades recharged), from
  frame 102 `003acd50` may end it.
- **Moves** (`003ad180` alone): beyond 1000 leap (11) unless role ≥ 3, then on a 12-frame wait 12; 650-1000 → 12, or
  3, or 14/15 at random; 350-650 → 12, 4, 14/15, or 13 when role ≥ 4; nearer → 4 or 13. Paired (`003ad5d0`) adds the
  flank: 14 / 6-7 / 15 / 9-10 / 3-4 for flank 4 / 3 / 2 / 1 / 0.

---

## G. Nevan (class 31, CEm031; EST 49): Mission 9, room 210

Pack 19 → GDATA 133 (em028.pac): [1] her body, [2] a PAC of 60 motions (the scripts' motion ids are its slots), [3]
22 more, [10] her action scripts (39, one per action, the Seven Hells' script format), [11] / [12] caps and shapes
(all hurt caps: her blows come from what her events make). Code 0x3b1b40-0x3c6000: new `003b1b40`, constructor
`003b2240`, vtable `0x609cc0`, per-frame `003b38e0` (dispatch on the action, +0x4be4, 39 of them), set-up `003b26a0`.
Health **7000** (`0x5bc32c`). Rank +0x4e17 from the game's difficulty value (under 250 → 0); phase +0x4e18 by health
(`003baf10`): over 5000, over 2000, the rest.

- **Patterns** (`003bba20`): a weighted pick from `0x5c1970` + rank·0xf0 + phase·0x50, the first row with Dante
  within 600 cm, the second (+0x28) beyond; each row is 8 pattern ids then 8 weights. Rank 0: near {0: 80, 1: 20},
  {0: 20, 3: 60, 4: 20}, {0: 10, 3: 20, 6: 70}; far {8: 50, 9: 50}, {8: 50, 10: 50}, {8: 50, 14: 50} by phase.
- **Moves** (`0x5c1930`: 16 pattern pointers; each a list ending in 23); `003bbc50` takes the next, `003b9a00` makes
  it an action through `0x5bc250` (moves 0-9 → actions 0-9, 10-13 → 22-25, 15-21 → 28, 29, 30, 32, 33, 34, 35; 14
  and 22 → 9).
- **Movement** (rank 0; frames by phase / distance kept / speed rate / top): 0 idles 30, 20, 10 frames (`0x5bca20`);
  1 closes to 300 (200/150/120 frames, 0.1, 2.5), 2 to 300 (0.5, 6), 3 to 900 (0.5, 6); 4-6 circle him (120/100/80
  frames, 0.2 or 0.1, 2.5), 5 and 6 also closing to 300 and 800. The speed rises by the rate a frame to the top
  (`FUN_0035a4a0`) and she moves its **square** in cm a frame (`003b4500`, `003b4e30`). 7-9 (`003b5870`) fade her out
  over 30 frames, place her 400 (move 7) or 1000 (move 8) cm beyond Dante on the line from the room's middle (2500,
  2500), a floor ray from 400 up (`003ba790`), and fade her back over 30.
- **Scripts** (action: motion, event frame): 22 m40 @82, 23 m41 @67, 24 m42 @165, 25 m46 @126, 29 m25 @90, 30 m44
  @77 (looped to 134), 32 m45 @24, 33-35 m49 @107 and @240 (the kiss), 38 m25 @90; 10-14 the reels (m10-m14), 21 the
  fall (m24), 28 m30.
- **The swarm** (action 23's event, `003ba470`): of her 16 swarm bats (+0x4db0, a flocking class: per-mode handlers
  `003c7fd0` / `003c80e0` / `003c8310`, step `003c8580`), `0x5bc8e0`[rank × 3 + phase] (3 / 5 / 8) are let go
  (`003c9080`: mode 1), `0x5bc930` (20 / 15 / 12) frames apart, each with a random vector from `0x5bc7d0` (16 of
  them, (100, 0, 20) round to (-100, 0, 20) through (0, 100, 20)). Mode 1 (`003c80e0`): it keeps to her formation
  point (`003b9c40`) until its wait's out, then aims 300 beyond Dante + 150 (`003c9140`) and steers (`00512f20`) by
  `0x5bc360`: 25 a frame, 180 frames at most, done within 50, its first heading its vector, turning up to 0x800
  (11.25°) a frame (+0x14 is a short, 0x800; it read as 0 printed as a float); then back to her (mode 3, `0x5bc380`: 60
  a frame, 0x600 every 3 frames, 240 at most, within 100). Hit: a 50 cm sphere, `0x5bc3d0` record 0 (300). Speeds in the formation: `0x5b7d40` + mode × 0x28 (the speed rising by [rank] to its
  top, squared over 1). In the port (`nevan_boss.c` swarm_update): all of it but her formation before launch (they leave from her chest) and
  the vectors' frame (taken as hers [inferred]).
- **Bats** (action 22, `003b6c90`): 2 / 3 / 4 by phase (`0x5bce38`), 8 frames apart, made 150 before her and 100
  up (`004187f0` → class `00418970`, vtable `0x60d210`). Each (`00418a50`, `00418bf0`) aims once at Dante + 150 and
  flies straight: speed +0.2 a frame to 18 (`0x5be780`, rank 0), moving its square, for 360 frames; an 80 cm
  sphere with record `0x5bc3d0` (**300** damage, kind 8).
- **Hits** (`003bb150` → flags +0x5188-0x518f, `003bbcf0`): a reaction id 0/1/4/7/8/10-12 asks a light reel, 2/3 a
  heavy one, 0x14/0x15 the fall (21). None while she teleports (7-9) or in 33 / 34. Light: from 10 → 11, 11 → 10,
  13-15 → 13, 12 none, else 10 or 11; heavy: 13-15 → 14, 10 → 11, 11 → 10, 12 none, else 12.
- **Movement, exactly** (rank 0 tables `0x5bcab0` + 0x90 per action, [time, distance, rate, top] by phase): 1-3
  (`003b4330` & co. → `003b4500`) head at him, 4 (`003b4c20` → `003b4e30`) 90 degrees off his bearing, 5 (`003b5020` →
  `003b51f0`) 75, 6 (`003b5450` → `003b5620`) -105 (backing out to 800); the side is `003b4d80`'s random bit
  (+0x4e1f). At the distance she moves on at once; when the time's up (or 1-3 and 5 are within it, 6 beyond it) she
  slows 0.98 a frame for 40 (state 2) first. 4 and 5 also slow within 200 of him, and slowing they move the speed
  itself, not its square.
- **Teleport** (`003b5870`, steps +6): 0-1 30 frames on her models' materials (+0x9c / +0xa0, 0 to 128, effect 0xe
  at her chest), 2 her alpha +0x5161 128 to 0 over 30, 3 placed (`003ba790`) and turned to him, 4-5 15 frames (effect
  0x93, sound 0x17), 6 alpha back over 30, 7 the materials back over 30, then her next move; moves 14 and 22 go on to
  actions 27 and 38. Moves 9, 14, 22: (2500, 300, 1200) when Dante's z is over 2500, else (2500, 178, 3800).
- **Shells** (hit records `0x5bc3d0`, 0x40 apart, damage at +4: 300, 1500, 1000, 800, 500, 800, 1500, 500):
  - 24 (`003b7300`, on script event 3): 1 / 1 / 2 by phase (`0x5bc920`) of `0045d720`'s class from her right hand
    (bone 9), 30 frames apart. Each (`0045d950`, `0045da20`): its delay, then his spot at y 100 is marked (a beam
    effect 0x1c1 from her hand scaled to the distance / 500, the mark 0x1c7, sound 0x19); 40 frames later it strikes
    (`0045dd70`: shape 2, (0, 100, 0) radius 200, record 6; effect 0x1ce, sound 0x1a), live 20.
  - 25 (`003b7460`): one of three layouts at random, `00468580`'s class (`00468860` places them from `0x5cd210`-
    `0x5cd290`): two at a time (`0x5bc910`: 1 / 2 / 3), two and three by turns (`0x5bc900`), or 3 / 3 / 5 together
    (`0x5bc8f0`), each set `0x5bc960`'s 50 / 45 / 40 frames after the last. Each ball (`00468860`): on his bearing
    from her, at a side offset (bar halves -400 / +400 (`0x5cd280`); pillars `0x5cd210` 0, ±250, ±500, `0x5cd240`
    0, ±250, `0x5cd260` ±125), first to 100 ahead at her height plus its own (bars `0x5cd290` 50 / 300 / 100 / 200
    by turns from a random one, pillars 400), then to 2600 ahead at his floor plus the same. State 0 (`00469050`)
    gathers to the first point (`00469340`: speed +2 a frame to 10, moving its square past 1, at most the top) until
    her script's event 4 (`0x61acf8` = 3) or she leaves 25; state 1 (`004691d0`) waits its delay, then flies to the
    second (+1 to 20) for at most 300 frames. Live all along (`00469790`, record 7, 500): a pillar (shape 6, (0,
    -300, 0), 600 high, radius 30) or, on the first half of a bar, shape 4 (800, radius 30) [read as the bar
    between the halves].
  - Hit shapes, by their use [inferred]: 2 a capsule up from a point, 3 a box (centre, half sizes), 4 a bar
    (length, radius), 6 a vertical pillar (centre, height, radius; Cerberus's landed icicle is one).
  - 29 / 38 (`003b7bd0` / `003b8d70`, as the action starts): `0043df80`'s class at her chest, effect 0x175 at his
    feet. `0043e250`, only while she's in 29 or 38: 50 frames charging (effect 0x16c), then (`0043e5d0`, record 1)
    shape 6 about her chest ((0, -200, 0), 800 high, radius 100: a pillar through her) for 50, then that and a world box at (2300, 50, 2300) of half
    sizes (1700, 50, 2200), the room's whole floor up to 100, for `0x5c9370`'s 30 / 30 / 30 frames, with a camera
    shake (0x38, 0x39); each stage hurts once. Event 5 calls `003ba660` (not read).
- **Hair, dress, sleeves** (`003b26a0`; model objects +0x880 body [1], +0xe80 [4], +0x1480 [5], +0x1a80 [6], their bone
  arrays +0x718, +0x778, +0x7d8, +0x814). A rig bone's +0xfc link record (+0x18 = a body bone's world matrix) ties it
  to the body: **[4] her hair** (texture 0), rig bones 0/1/2 on body bones 3/4/5, four strands of five from bone 2;
  **[5] her dress** (textures 2 and 3), 0/1/2/3 on body 1/14/2/3, one chain of ten hanging 268 cm from the pelvis;
  **[6] her sleeves and back pieces** (texture 2), 0/1/2/6/7 on body 14/7/8/11/12, no chain. Chains (CCnsChain,
  `FUN_0022fbd0` reading the .clt, `FUN_0022f270` a bone a frame): [4] by em028_01.clt ([7]: bones 4-7, 9-12, 14-17,
  19-22, gravity -0.02, spring 0.02, top speed 50, stiffness 0.3) against three capsules (`FUN_00230130`: list
  `0x5bc030`, shapes `0x5bc040` stride 0x30): body bone 5 ±4.65 r 9.3, bone 4 ±6 r 12, bone 3 ±9.5 r 19; [5] by
  em028_02.clt ([8]: bones 5-13, gravity -0.07, the rest the same), no capsules. No floor: only a script's key at
  `0x591280` sets +0x50, and hers don't. Each frame (`003b38e0`) `FUN_00230150` moves a chain's bones by her root's
  change (her root's new matrix after the old one's inverse), so the chains swing only with her body's motion and
  gravity, never with her travel or her turns.
- **Texture scrolls** ([13], a .TSC: `FUN_0019d4a0` / `FUN_0019d620` parse it, `FUN_0019daf0` → `FUN_0019ddb0` run
  it a frame; set up on [5]'s and [6]'s scrollers, +0x2080 and +0x209c). Keywords and fields (0x74-byte records): TexNo
  +8, JntNo +7, DirUV +0x58/+0x5a (left and up +1, right and down -1), RateUV [8]/[10], TimeUV [0xc]/[0xe], InterUV
  [0x10]/[0x12], TurnTimeUV [0x11]/[0x13], MinimumUV [9]/[0xb] (flag 4), RndUV [0x18-0x1b] (flag 2); the step is the
  owner's speed (+0x1b0). Type 1 (`FUN_0019e740` → `FUN_0019e360`): the phase moves step / TimeUV along DirUV,
  wrapping, and is the offset. Type 3 (`FUN_0019df50` → `FUN_0019e840`): the same phase, drawn eased as (cos((1 -
  phase)·π) + 1) / 2, plus MinimumUV's drift. Hers: scroll 0, type 1, texture 3 (the lightning) up a texture every
  90 frames; scroll 1, type 3, texture 2 (the dark cloth) left over 400 frames, drifting 0.0001.

**In the port** (`psp/testroom/nevan_boss.c`): the patterns, moves, phases, movement, teleports, scripts' motions
and event frames, bats and reels above, at rank 0, with the movement, teleports, 24's strikes and 29 / 38's floor
lightning and 25's balls as above (her fades blend). The kiss's hold is the game's (36, `003b8810`: Dante held, his
action 0x2e; 360 / 420 / 480 frames by phase, `0x5bce50`; 10 a frame from him, `0x5bce80`, half back to her,
`0x5bceb0`; 80 struggle points free him, `0x5bcf10`, counted as `00254a20` does: 2 a button pressed, 1 a change of the
stick; free, she reels heavy, 12; timed out, she throws him, 37). Dante's motion while held is hers: `FUN_0021f2e0`
(him, mode 2, her +0x2158 = her pack's [3]) gives him that bank and `FUN_0021f3a0` an index in it, 0x14 held (slot 20,
270 frames), 0x15 broken free (21), 0x16 timed out (not in the bank); his action 0x2e (`FUN_0022b9b0`) plays it as
group 0x20 (`FUN_002500d0`). The Soul Eater does the same (mode 1, its [9]: 0xf, 10 and 11 lifting him, 12 held,
14 let go; `0049dac0`), the Arachne too (mode 0, its [15], `003f8080`), and an enemy at `004f4a60`. In the port:
"nvh-20" and "seh-12" (`tools/prepare_skinned_model.py`). Stand-ins: 23's circling bats
(let out as the bats are; `003ba470` keeps the bats' count), the shapes of Shl03-05 beyond their numbers, the
teleport's material steps (not shown), 29 / 38's event, a wall ray on the 7 / 8 teleports (she keeps to
the stage instead); the bats are drawn as glows. Her hair, dress and sleeves are the
game's (above): their rigs are appended to her skeleton (`tools/prepare_skinned_model.py`, EXTRA_CHAINS), linked
bones follow her body, the chains step in her model space (`psp/testroom/cloth.c`, coat.c's FUN_0022f270 port), and
the scrolls run (`enemy.c`, scrolls_update). Not ported: RateUV, InterUV, TurnTimeUV and RndUV (her script doesn't
use them), and the lightning texture's blend (drawn as the others).

---

## H. Arachne (class 10, CEm010; EST 28), Soul Eater (class 16, CEm016; EST 34), Abyss (class 6, CEm006; EST 24): where they are

From the EST table `0x599690` (pairs class, pack) and the pack table `0x58e740` (top 16 bits = GDATA slot):

| | class / pack | GDATA | new / constructor / vtable | pack layout |
|---|---|---|---|---|
| Arachne | 10 / 5 | 121 (em010) | `003f1e00` / `003f1770` / `0x60bf30` (per-frame `003f3e40`, actions `003f6eb0`, set-up `003f2380`) | [16] scripts, [17] caps, [18] shapes, PACs [7]-[15] motions |
| Soul Eater | 16 / 11 | 126 (em016) | `0049a560` | [1] an **EFM** (effect model) body, [3] text, [4] caps, [5] shapes, [6] scripts, [8] / [9] motions |
| Abyss | 6 / 1 | 118 (em006) | `0041bdb0` / `0041bc70` / `0x60d360` (its own class on the base `00284da0`, not the Hells' shared AI `003674a0`) | the Hells' layout: [1] body, [3]-[5] extra body parts, [25] scythe texture, [26] scythe, [35] motions, [38] scripts, [39] caps, [40] shapes |

- **Arachne**: health **1400** from block `0x5c3870` +0x2c (a second block, `0x5c396c`, has 2000; `003f2380` picks by
  spawn), already the port's. Its spiderlings are a separate 0x21b0-byte object (`004f0f60` → `004f0df0`), made by
  action handler `003fd720` (action 0x76: once per Arachne, +0x36e0, and not while another class-0x1b object is up;
  motion 0x24, effect 0x307 at its body). The object (vtable `0x614090`, update `004f1a80` → `004f1af0`, set-up
  `004f12a0`, 47 functions at 0x4f0000-0x4f6000) is a **swarm**: one small spider per part of its model (count at
  model +0x11), each with health (+0x1638) and a state (+0x5ec: 0, 2, 3, 4 hatching between the Arachne's bones
  +0x820 / +0x8d0, 6 riding bone +0x850 until it drops, 8 crawling (re-aimed every 30-60 frames; Dante's feet,
  bones 0x11 / 0x15, within 30 take 10 off it), 9 leaping (bounces off walls by a stage ray, lands by a floor ray),
  10), its hit record `0x5c37b0`. Not ported.
  The web shot (CEm010Shl00 / Shl01) isn't made by any action handler: inferred from its
  script events, not traced.
- **The Arachne in the port** (`enemy.c`, "The Arachne"; `tools/prepare_arachne.py`): its scripts (em010 [16], groups
  0-3 on motion PACs [7]-[10], motion id g*100+n), caps ([17] / [18]) and records (`0x5c3570`: 600, 100, 100, 600, 1000,
  600, 1000, 800, 400, ...) on the shared runner, its motion table (`0x5bd0d0`: index -> group, script) and its class
  (actions `003f6eb0`, states `003f7710`): the decision `003f9650` (gate `0x5c6bb0`, zones under 400 / 700, the
  move tables `0x5c3c90` / `0x5c3dd0` / `0x5c3f20`, attacks `0x5c4f10` with the shared token or `0x5c5cc0`, the
  wall's `0x5c6200` / `0x5c6740`, health bands at 20 / 50%), idle 0x65 (45 ± 10 frames), walk 0x66 / run 0x6b /
  strafes 0x67 / 0x68 (15 a frame) / turn 0x6c with their turn rates (0.04 / 0.03 / 0.15 / 0.1 rad a frame) and 20-frame
  pause, 0x6d's crouch (5 / 10 / 15 frames) and pounce, 0x6e / 0x6f, 0x70 (a ray for a wall 300-800 up and 1000 ahead,
  three tries, then 0x77), the leaps 0x71 / 0x72 (up 25-32, 1.6 a frame), 0x77 (record 0 on its script's flag),
  0x7a (by distance, 0-12.5 a frame; landing records 5 / 8), the wall's 0x73, 0x79 (four web shots ringing Dante at
  150, `004f46d0` / `004f4a60`: falling 0.8, 100 cm, holding him 300 frames) and 0x7b (the drop, 2 a frame), 0x88,
  its reaction (40% of hits, `0x5c6aa0`: motion 10 / 11 and back 23, or 6 and back 7) and death (0x14, motion 0x1e).
  Attack 0x77 reads both flag-4 events for cap 9 on bone 9. Attack 0x7a reads flag 4 for cap 9 on bone 9 and
  cap 10 on bone 2. The port mirrors bone 9 for the second rig. Root translation ×1.5 is the default spawn's
  multiplier in `00402ba0`. Stand-ins: the wall normal (the ray's way back, flattened) and its pose on the wall
  (turned 90 degrees, facing it), 0x6d's web-line swing (a leap), 0x6f's sideways speed (8), the struggle against a web
  (35 [inferred]), and its hit-on-a-wall fall (0x7d unread); 0x76's spiderlings and 0x78's charge have weight 0 at rank 0 (not ported).
- **Abyss in the port** (`ENEMY_ABYSS`; EST 24): its own body ([1], 23 bones, the Hells' skeleton) and scythe ([26], texture [25]), its own scripts [38] and caps [39] / [40] (`tools/gen_enemy_ai.py`: `abyss_ai_ops`), the Hells'
  motions (its [35] is theirs byte for byte), health **1500** (block `0x5ce060` +0x2c, from its pointer block
  `0x61a678`, laid out as the Pride's `0x618c28`: records `0x5cdde0`, blocks `0x5ce060` / `0x5ce110`, script map
  `0x5ce1f0`). Its AI object (`004a8e30`, vtable `0x612220`, RTTI CComEm006) is a clone of the Hells' (`0x607b20`):
  the same slots, the same function spacing. **Its decision table is its block's +0x10, `0x5ce910`**: its set-up
  (`0041c240`) hands it to the AI's init (`004a8f00`, the AI's +8, where the Hells' `FUN_0032b7b0` picks from), laid
  out as the Hells' `0x5a8300`: bands 300 and 600, zones (300 cm / 60 degrees / 10 a frame, 600 / 30 / 2, 3000 / 5 /
  0.5), thresholds and times as theirs, and rows of its own. Rank 0: with the token, under 300 swing 25, wide swing
  25, plan 59 50; 300-600 plan 58 30, 59 70; beyond, 58; without it, leap 60 and the steps 20 each under 300, else
  leap 60, plan 40 40. Its own plans (its AI vtable `0x612220`): 40 → action 35 (`004abfc0` → `004b5470`: script 38
  or 39, turning 0x100 a frame), 58 → 74 (`004ac980` → `004bafe0` / `004bb030`: script 102, turning 2 degrees a frame
  over its motion's frames 0-30), 59 → 75 (`004aca20` → `004bb2d0` / `004bb3a0`: script 101 sinks it, then 300 cm
  from Dante at a random angle facing him, 50 frames later script 100 brings it up, +0xcd set from its frame 45 to
  95). Its scripts 100+ are its bank's group 1, playing its own motions (ids 100+: PAC [36]; the hundreds pick the
  PAC). The hit-script pairs come from +0xc (0x5ce1f0), installed at enemy+0x2304.
- **Soul Eater in the port** (`ENEMY_SOUL`, enemy.c's soul_*; `tools/prepare_soul_eater.py`): its body, motions (seNN,
  PAC [8]), six wing chains (em016_00.clt: X / NX axes, no gravity, stiffness 0.1, no length limit; cloth.c), scripts
  ([6], one per action; a wait to the motion's end ends one), caps, records (500, 3000), action table (`0x5c14f0`,
  0x2c each: time and random by rank, turn in degrees a frame, the decision list `0x5c14d0` → `0x5c14a0`: rank 0
  always 1, in Dante's view 0), its sight and showing (+0x4bc / +0x4c4), hover (+0x4b4 / +0x4b8), its actions 0-15
  and death (`0049c2a0`). The hold (7) drains his Devil Trigger (+0x28c4, FUN_0021eae0 20 a frame) and, its time up,
  bursts (9: record 1, 3000). Its bite holds Dante (`combat_hold`) and 60 struggle points free him (`004331b0`: `00254a10` / `00254a20`). Stand-ins: he isn't lifted or turned with it (`0049dac0`), the rank-0
  teleport's camera angle (his facing), 16's trigger, the cone's width (±60 degrees).
- **Soul Eater** (as decoded): new `0049a560`, constructor `0049a880` (on the
  base `00257340`, as Nevan and Agni & Rudra, not the Hells'), vtable `0x611760`; per-frame `0049aa40` → `0049b7b0`
  (state +4: 0 `0049ab30`, 1 `0049b7b0`, 2 `0049c2a0` the death), its action handlers at `0x4325d0`-`0x433fd0`;
  set-up `0049ad90` reads `0x5c1340` (300, then turn rates), the attack records at **`0x5c13c0`** (record 0: **500**
  damage, record 1: **3000**) and the defence block **`0x5c1440`** (+0x2c: health **200**, +0x20 damage scale 100).
  Its body ([1]) is tagged EFM but should convert as the Blood-goyle's does (an EFM laid out as a MOD).

---

## I. Beowulf (class 33, CEm033; EST 51): Mission 11, room 217

Pack 21 → GDATA 135 (em030.pac): [1] body (36 bones), [2] 43 motions (the scripts' ids are its slots), [8] his script
bank (groups 0, 1, 3 used), [11] / [12] caps and shapes. Code: new `002d7e60`, constructor `002d8300`, vtable
`0x604c60`, set-up `002d8680`, per-frame `002d85f0` → `002d99f0` (the action +0x37da, 28 of them). Health **10000**
(`0x5b84e0` +0x2c). Attack records at **`0x5b7fa0`** (0x40 each; damage 300-1500).

- **Actions → scripts** (`002da3d0` → `002da800(group, script)`): 0-4 → group 0 scripts 0, 1, 7, 8, 9; 5-10 → group 1
  scripts 0, 1, 2, 3, 6, 9; 11 → group 0 (the walk); 12-21 → group 3 scripts 0-9; 0x16 (the phase roar) → group 1
  script 0; 0x17 → group 0 script 7; 0x18-0x1b → group 3 scripts 11-14. (Without the room flags `0x1d0d738` = 0xd9 /
  0x1a0, a request for 0x17 becomes 2.)
- **Choice** (`002d99f0` from idle, 0 or 1): the phase's sector list (`0x5b8c50`[phase]: {action list, angle offset,
  half-angle, reach}, `FUN_00359b40`), the first sector Dante stands in names a list of {action, weight} ended by
  0x1c; a weighted pick (`FUN_00359f20`); 0x15 / 0x10 swap by Dante's height (150, `0x61967c`); the action just taken
  isn't taken again (`002da7b0`, with +0x3908); an action may follow another only if mask `0x5b8df0`[current] &
  `0x5a2100`[new] (from idle, all may). Rank 0:
  phase 0: in front ±120° within 300 → 16 (70), 11 (30); behind ±120° within 400 → 17 (70), 11 (30); else 11.
  phase 1: within 550 → 19 (70), 2 (30); else 2. phase 2: within 400 → 11 (35), 19 (30), 27 (35); within 600 → 2
  (15), 19 (20), 27 (25), 18 (20), 15 (20); else 2 (20), 15 (50), 27 (30).
- **Phase** (+0x38d0) steps up when his health fraction is under `0x619718`[phase]: 0.73, 0.47; action 0x16 plays.

- **Movement** (gp 0x620170): 1 (`002daa00`) turns 1274 a frame (`0x619680`) and lasts at least 300 (`0x619674`);
  the walk (11, `002dab50`) turns 1274 and, with Dante within 550 and 100 degrees (`0x5b8c64`), goes to 0x18, else
  stops after 300; the run (2, `002dade0`) turns 637 (`0x619688`) for 30 frames (`0x619690`), then 218 (`0x619698`),
  and after 180 (`0x619694`) or with Dante beyond 3000 (`0x5b8cb4`) stops (3) at its next stride's event (or 4, his
  charge, with Dante behind and under 3 strides, `0x5b8cc4`). Idle (0) fills +0x168 at 2 / 3 / 4 a frame by phase
  (`0x5b88c8`) while the gate at +0x130 is shut.
- **Stagger** (8, `002dbb00`): when done, `0x619670`[phase]: 0, 1, 1.
- **His hits** (`002df8e0`, vtable +0xc4): none while in 5-7. A blade from within ±40° in front (0x1c72) staggers him
  (8) `0x5b8ce0`[phase] % of the time (30, 20, 10); a knock-down record (kind 4) over 600 sets his blade tally (+0x38d8)
  to its limit (100000, `0x61966c`): action 5; shots add their damage to +0x38d4 and past 1500 (`0x619668`) give 6 unless
  in phase 2; his first shot hit gives 10 (+0x3903), later ones 50% (`0x619724`). Each hit adds 0.3 × its damage to
  +0x168 (`0x619720`). In the port: all of it, the port's heavy / slam / launch taken as the kind-4 record [inferred].
- **Volley** (15, `002dcf00`, his body script 3/3): it reads the body player's flag set 0 (+0x5d, five bytes that
  each op3 copies whole, `FUN_00255640`; `FUN_00255af0(player, set, n)` reads flag n). Flag 1 = 1 (frames 41 and 188)
  lets `0x5b8650`[rank] + 1 feathers go from his bone list's slots 6 on (7), then `0x5b8660`[rank] + 1 from 14 (3);
  flag 4 a wind pushing Dante away 4.7 a frame for 190 frames (gp-0x6a48, gp-0x6a40); effect 0xc1 on flag 3. It also
  starts a second script player (+0x31e0, group 0 script 1).
- **Feathers** (`0040cef0`, vtable `0x60c750`; set up by `0040d160`, run by `0040d380`): each starts at its wing bone,
  heading out from his body, waits index x 3 frames (`0040d130`), then moves by `0x5b8610` (speed 2, steering 0xb4 a
  frame toward Dante's chest for 300 frames or until within 300, life 1000: `FUN_0025a780` copies {speed, , , homing
  frames, retarget, turn (short), stop distance, life}; `FUN_0025a7d0` steps it, `FUN_0025ad70` ends the homing).
  It waits on his flags: flag 1 = 1, or flag 0 no longer 10 (the volley over), then index x 10 frames (gp-0x6860),
  and flies by `0x5b8630` (speed 40, turn 0x258 for 80 frames or until within 300, life 120); a touch (flags
  0x30400) bursts it (state 3, 0x142 and sound 0x19). Its record is 8 (360).

**In the port** (`enemy.c`, "Beowulf"): his scripts, caps and records run on the shared script runner
(`tools/prepare_beowulf.py`; group g script n is entry 16 g + n), so his motions, hit windows and damage are the
game's; the choice, phases, roar, the movement rules, stagger's end, the volley's flags, counts and wind, and the
feathers' flight above (the runner now keeps each enemy's flag set 0: `tools/prepare_cerberus.py` packs an op3's
flags 2-4 into its c). Stand-ins: the run's stop at once rather than at a stride's event, his charge (4) from the run
isn't taken, a feather's start heading from his body's centre 200 up [inferred], their burst effect.

---

## J. Lady (class 37, CEm037; EST 55): Mission 16, room 115

Pack 25 → GDATA 138 (em034.pac): [1] body, motion PACs [2]-[6] (60, 30, 4, 40, 70), script banks [12] / [13], caps
[14] / [15]; shot classes CEm037Shl00-Shl07, Ball, and their controllers. Code 0x443140-0x450000: new `00442610`,
constructor `00443140`, vtable `0x60e670`, set-up `00443520` / `00443830`, per-frame `00443490` → `0044b9f0`, mode
dispatch `00444c90` (+0x47c0: modes 0-0x17 her walk `00445210`, 0x18 `004455d0`, 0x19 `00445a20`, 0x1a `00445e30`,
... 0x31 `00448650`), action set `0044afc0` (+0x47c2), mode set vtable +0x2f4 (`00449560`). Health **4000** (`0x5cbe40`
+0x2c). Attack records `0x5cbc40`: 80, 500, 1500, 1000, 500, 30, 500, 50.

- **Action → motion** (`0044afc0` → `0044bda0(group, slot)`): 0-0x0e PAC [2] slot a; 0x0f-0x29 [3] a-0x0f; 0x2a-0x2d
  [4] a-0x2a; 0x2e-0x52 [5] a-0x2e (0x37 on add an upper-body layer: [2] 1, [5] 0x10 / 0x17 / 0x1e); 0x53 on [6]
  a-0x53. Shot actions 0x31, 0x34, 0x37 ... 0x58 take +1 / +2 with Dante above / below her by 15° (`0044ad40`).
- **Her walk** (`00445210`): with him in sight (`00449450`, a ray), to mode 0x18 while her bursts (+0x47cc) are under
  3; then to 0x28 if the magazine (+0x47e0) is empty or her last mode (+0x47c1) was 0x1a, else 0x1a. Out of sight:
  her waypoints (`00448df0` / `00448920`, tables `0x5cc1d0` / `0x5cc290`), or 0x2d (empty), or 0x2c (3 bursts).
- **The gun** (mode 0x18, `004455d0`, entered with action 0x30): Dante under 300 → 0x1d (her escape); under 500 →
  0x28; under 800 → 0x19 while bursts < 3, else 0x28 / 0x1a as above; beyond: in sight, within ±45° (`0x61a500`
  offset 0, half 0x2000, reach 10000) she shoots (0x34 +variant), else turns (0x31); she turns 1820 a frame
  (`0x61a4f4`, 10°). Mode 0x19 (`00445a20`) is the same with actions 0x3e, 0x3f, 0x42.
- **Magazine** 50 (`0x61a514`, refilled by mode 0x2d, action 0x2b); a shot takes one (`0044d3e0`). Mode 0x2c
  (action 0x2a) waits 32 (`0x61a510`) and resets the bursts.

- **Her waypoints**: two rings of 16 about the middle (2500, 2400) (`0x5cc350`): the floor's `0x5cc1d0` (radius about
  1270, y 1) and the raised middle's `0x5cc290` (about 680, y 101). `00448df0` takes the nearer ring's nearest point,
  `00445590` the next one on round the way she's going (`004494b0`, the cross product of her last spot and now about
  the middle), `00448920` jitters it ±150 (`0x61a4ec`, 300) or ±100 (`0x61a4f0`, 200). Within 300 of it, the next.
- **Her walk out of sight** (`00445210`): reload when empty (0x2d), pause after 3 bursts (0x2c), else on round her ring
  (actions 0x45 walk / 0x4c run), turning 182 a frame. **Mode 0x28** (`00447850`): running for her waypoints while he's
  in sight and she's had 3 bursts or he's within 800 (`0x5cc128`); within 200 of her spot (`004491b0`) the escape.
- **Mode 0x1a** (`00445e30`): action 0x85 ([6] 50), turning to him at `0x61a4f8` (1820), then her walk.
- **Specials** (`004492a0`, from the walk when the gate at +0x130 / +0x88 opens): by her health's thirds (`00449230`:
  over 0.66, 0.33-0.66, under): 0x1b if he's in sight, 0x1d, 0x1c. **0x1b** (`00445f00`): 0x5c, then 0x56 (0x57 /
  0x58 by his height, `0044ad40`), its event 1, then 0x59. **0x1c** (`004460c0`): 0x71, 0x72 `0x5cc140`'s times (2),
  0x73, turning toward her next waypoint at 1820. **0x1d** (`00446330`): her grapple: 0x5d, then turned to it 0x5e,
  a wire (`0049e4f0`) up at `0x61a520`, rising to `0x61a530` while dropping `0x5cc150` grenades, onto the nearest of
  five ledges at y 501 (`0x5cc360`) or a waypoint, 0x62, 0x61 landing, then mode 0x1e or her walk. In full: the rise
  is 20 a frame, 1 more each frame (`0x61a524` / `0x61a528`), for up to 120 frames (`0x61a534`); at y 1200
  (`0x61a530`) she's put over a ledge in her health's middle third (`00449230` = 1: the neighbour of Dante's nearest
  ledge, `00449120`, that's nearer him), else over her next waypoint, and drops 20 a frame (`0x61a52c`). +0x484c is
  the rank: 0x60 for 0x5f and `0x5cc150` (1) grenades as she lets go only above rank 1.
- **Mode 0x1e** (`00446f60`, on a ledge): action 0x8f `0x5cc160`[rank] times (3, 2, 2, 1), turning to him at 1820;
  then 0x0c, the hop down, facing the middle (`00359c80(middle, her)`, the way she turns to a target) if he's further
  from it than she is, else away; falling 28 a frame (+0x4824 = -28) to the floor, 0x0e, then her walk (0x17).
- **Line of sight** (`00449450`): a ray from her to him (`FUN_00201da0`).
- **Ledge collision**: st115's GAME STAY sets 47/51/55/59/63 are the five bookcase crosspieces, not HITS
  floors. Their authored boxes are at local (0,10,10), half sizes (250,34,70), placed at y468 and yaw
  36/108/180/-108/-36 degrees. The transformed centres have y478, hence top y512. `gen_rooms.py` enables
  these exact five boxes, using a negative tag for permanent structural surfaces; `stage_floor` queries their
  oriented tops as well as HITS, and `stage_collide` uses their sides. Other room boxes keep their prior
  behaviour. Lady drops onto the queried collision height and then takes 0x1e by the native y491 test;
  the old hand-placed y501 perch is gone. Dante can land on the same crosspieces.
  PPSSPP checks: `lady_ledge_fight_codex` shows the rise, collision landing at y512, ledge shots and hop
  down to y1; `lady_ledge_floors_codex` places Dante over each of the five tops (grounded y512 on all five)
  and outside the last footprint (grounded y102). `lady_ledge_dante_codex` records the separate departure:
  y512 on the top, y386 airborne beyond its footprint, then grounded at y1.

- **Grapple wire** (`0049e4f0`, set up by `0049e7e0`): em034 [26] hook with [19] textures, [30] five-bone cable
  with [29] textures. The held Kalina Ann ([20]) is attached to body bone 9 with `0x5c9520`'s offset
  (-8.4, -1, -1.3), rotated pi about Z. `0044b780` transforms its local +X direction; `00446330` fires the hook
  50 units/frame (`0x61a520`) from `0x5c9610` (87.8, 0, 4.28) in that frame. The cable follows Kalina Ann's
  bone 2 (65.2, 0, 4.3); `0049f020` positions five cable bones at quarters of the span, scaling their X axes by
  segment length / 70, preserving the original skin weights. The hook stops at a collision; Lady rises along
  the captured direction, not world up. Her preparation is 0x5d (ld610), followed by 0x5e/0x5f. On the y1200
  relocation `0049f500` collapses the cable to the relocated hook, and landing 0x61 retires the actor.


**In the port** (`psp/testroom/lady_boss.c`): the modes above at rank 0, each action's own motion, her turn rate,
magazine and burst count, and her records' damage; her waypoints, walk, 0x28, 0x1a, the special picker, 0x1b and
0x1c, her line of sight (a stage ray), her grapple (0x1d) and her ledge mode (0x1e), as decoded above. Stand-ins:
`00449120` taken as the ledge nearest Dante; 0x0e's slot ([2] 14) is empty, 0x61's landing plays; what 0x8f fires (one pistol shot
[inferred]); what 0x1b and 0x1c fire (a
rocket on 0x56, a grenade each 0x72 [inferred]), 0x5c's motion (its slot, [6] 9, is empty: the gun's draw), her run's
speed [estimate], shots per burst (3; the second gun 1), which record each weapon uses (pistols 80, second gun 500,
grenade 1000, rocket 1500 [inferred]), her flinches and knock-back gauge.

## Damned Chessmen: shared controller

See [CHESS.md](CHESS.md) for CEm017Master43f160, the six EST dispatches, native
wait tables, concurrent-turn capacity, Queen response, Rook/King castling commands
and King-death cascade. `chess.h` documents the shared actor interface.

Knight36: model127[7], motions[8], native leap/plunge/recovery and bone-sphere
strike, HP1000, hits1000/700. Bishop37: model127[12], motions[13], diagonal slide,
four orbiting then diagonal orbs (800 each), healing within800 and shatter death,
HP1000. Detailed handlers, script/cap tables and PPSSPP evidence: CHESS.md.
