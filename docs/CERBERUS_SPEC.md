# Cerberus (CEm025): Mission 3 boss spec

All addresses are in the PS2 SE executable (`SLUS_213.61`). The gp register is `0x620170`. Object offsets are
byte offsets from the object base (Ghidra often prints them as `param_1[n]`, which is byte offset `4n`).

**Decoded** means read directly from code or table values. **Inferred** means interpretation, a name for
something decoded, or a guess. A line that doesn't say is decoded.

"Rank" means `FUN_002acb40`: the number of thresholds in {130, 500, 700} (`0x5b3904..0c`) that the AI-base value
`vfunc+0x7c` (`FUN_0032b370`, clamped 0..1199) reaches. Every table below is quoted at **rank 0**. Inferred: the
rank is 0 on Easy and Normal, as for the other enemies (docs/ENEMY_AI.md). "Phase" means `+0x8928`, which runs
0, 1, 2 (section 5). Tables are `[rank][phase]`. Times are frames at 60 fps, scaled by dt (`+0x10`) unless
noted.

The repo's Ghidra export has almost none of these functions. I decompiled them from a copy of the project into
the scratchpad (`run_ghidra.ps1 -Addresses ...` against `analysis/ghidra` does the same). The addresses are
listed at the end.

---

## 1. Class, objects, entry points

- **EST spawn id 43 → class 25 = `CEm025`**, pack byte 16 (table `0x599690`).
  - Factory `FUN_002acf40` case 0x19 calls `FUN_002995f0` (the "new").
  - Typeinfo name `CEm025` is at `0x5f3db8`.
  - There are no CEm025Body or CEm025Head classes. **The body, three heads, ice armour and chains are all
    sub-objects inside one 0xabc0-byte object.**
- **Separate objects, the projectiles only:** `CEm025Shl00` (ice sphere), `CEm025Shl01` (ceiling icicle),
  `CEm025Shl02` (sphere shard).

| | new / spawn | Constructor | Primary vtable | Update | Set-up |
| --- | --- | --- | --- | --- | --- |
| CEm025 | `2995f0` (factory) | `298aa0` (vtables written by `299280`) | `603a40` | `29a460` | `29a4f0` (vptr+0x130) |
| CEm025Shl00 sphere | `32bd30(boss, pattern)` | `32bea0` | `606b80` | `32c2b0` | `32c020` |
| CEm025Shl01 icicle | `37edb0(boss, ?, entry, i)` | `37ef20` | `608420` | `37f2a0` | `37f080` |
| CEm025Shl02 shard | `40d970(sphere, entry)` | inline | `60c890` | `40daf0`→`40dd40` | `40db60` |

- Secondary vtables of CEm025: `603a64` (-64), `603ae4` (-160), `603b78` (-208), `603c44` (-304), `603cfc`
  (-464). They are contiguous, as with the other enemies.
- Other virtual entry points:
  - `vptr+0x18` → `2a84b0`: **the hit handler**, with args (hit record, attacker, hit position, part 0-3).
  - `vptr+0x20` → `2abac0`: a second damage path with no armour check. Damage is split by 3 over the heads.
    Inferred: environmental or special damage.
  - `vptr+0xd8` → `2ac090`: start death.
  - `2a7590`: destructor and free. `2a8240`: draw (body, heads, ices, chains, collar, small head models).
  - `2a83f0`: remove.
- **Update** `29a460` switches on the state byte `+4`:
  - 1 = alive: `29d0a0`.
  - 2 = dying: `29e240`.
  - 3 = dead: `29e500`.
- `29d0a0` runs, every frame:
  - Timers and gauges.
  - `2a7790`, the zone.
  - Lock-on targets (`2a7da0`).
  - **Dante's z is clamped to [1400, 3000]** (Dante `+0x58`).
  - The action switch on `+5`.
  - The per-head flinch blends.
  - The bone and model attach (`2a61d0`) and chains (`2a64d0`).
  - Collider updates for the body and each live head.

### Parts inside the object

| Sub-object | Offset | Pack chunk | Notes |
| --- | --- | --- | --- |
| Body model | `+0x1d00` | [1] | 31 bones; matrix `+0x9bb0`, position `+0x9be0` |
| Head model 0, 1, 2 | `+0x2900`, `+0x2f00`, `+0x3500` | [2], [3], [4] | 5 bones each. Head i attaches to body bone **6 + role[i]** |
| Body ice | `+0x2300` | [12] | 13 mesh parts; parts 4-7 and 9-12 are the paw armour |
| Head ice 0, 1, 2 | `+0x3b00`, `+0x4100`, `+0x4700` | [13], [14], [15] | 13 mesh parts each = 13 armour pieces |
| Chain ×5 | `+0x4d00` … `+0x6500` | [6] | 10-link rope sims at `+0x9930 + k*0x70` (`FUN_002eb440(…,10,150)`) |
| Collar | `+0x6b00` | [5] | 4 bones, on body bone 22. Chains 3 and 4 hang from its bones 2 and 3 |
| Small head models ×3 | `+0x7100`, `+0x7700`, `+0x7d00` | [16], [17], [18] | On head bone 3, hidden when that head dies. Inferred: eyes or crest glow |
| Script players | body `+0x8980`; heads `+0x8a50/+0x8b20/+0x8bf0`; head flinch layer `+0x8cc0/+0x8d90/+0x8e60` | [25]; [26+role] | |
| Colliders | body `+0x9ca0`; heads `+0x9ec0 + i*0x220` | caps [29], shapes [30], records `0x5b3350` | |
| Attack sphere sets | `+0x9200` (10 spheres), `+0x9420` (1 box) | | Used by the punch icicle and the breath |
| Push capsules ×4 | `+0x99a0`… (`FUN_002ec660`) | [31], [32] | Inferred: solid body against Dante |
| Lock-on targets | paws `+0x8f80`, `+0x8fd0`; heads `+0x9020 + i*0x50` | | **5 targets: 2 front paws and 3 heads** |

**Head roles** (`+0xa8f8..fa`, initially 0, 1, 2):

- Role r picks the head's script chunk [26+r] and its neck bone 6+r.
- `FUN_002a1ee0(dir)` cycles the roles: dir 0 rotates one way, dir 1 the other. It then re-binds the scripts.
  It runs after body scripts 0x1e and 0x1f (S30 and S31, 90 frames), so the heads physically move round the
  necks. Inferred: the "heads rotate" animation.
- **A head's special attack needs that head in role 0.** If it isn't there, the body first plays a rotation.

### Spawn and placement

- The set-up ignores the EST position. Cerberus is placed at **(2500, 0, 3250), yaw π**, facing −z (table
  `0x5b3920`, the same for all ranks and phases).
- Its "home" `+0x9be0` is reset to that point whenever it returns to idle (`29e6e0`).
- The EST record's shorts +0x20..24 go to `+0x8950..54`. The short at `+0x8952` (record +0x22) selects the death
  mode (section 5). Room 006 gives 0, the story mode (inferred from the EST record).
- **Facing clamp** (`29f470`): the body's forward is kept within **±25°** of −z (sin 0.422).

---

## 2. Pack: GDATA slot 130 (PNST, 39 entries)

This was confirmed by the set-up's chunk reads, matching the entry types and sizes. Slot 129 is em023 and slot
131 em026, so pack byte 16 maps to slot 130.

| Chunk | Content | Used by |
| --- | --- | --- |
| [0] | Textures, 5 TIM2 (33, 17, 17, 3, 17 sectors) | every model |
| [1] | MOD body: 31 bones, 2 objects, 604 wide × 762 tall × 567 long | `+0x1d00` |
| [2], [3], [4] | MOD heads 0, 1, 2: 5 bones each, 250 long | `+0x2900/2f00/3500` |
| [5] | MOD collar: 4 bones, 212 wide | `+0x6b00` |
| [6] | MOD chain: 11 bones at 150 spacing (1350) | ×5 |
| [7]-[10] | empty | |
| [11] | A PNST byte-identical to this pack's own first entries (an overlapping view). Not read | |
| [12] | MOD body ice: 31 bones, **13 objects** | `+0x2300` |
| [13], [14], [15] | MOD head ice 0, 1, 2: 5 bones, **13 objects** each | |
| [16], [17], [18] | MOD small: 4 bones, about 90×30×80 | on head bone 3 |
| [19] | PAC, body motions (60 slots, 25 present) | body player |
| [20], [21], [22] | PAC, head 0/1/2 motions (59 slots) | head players |
| [23] | PNST effects: an id list (`G 21, G 23, G 44 … V 73 …`) plus the effect pack | `FUN_00209160(…,2)` |
| [24] | MOT, 2 bones (`FUN_001fa410` ×2). Inferred: camera or shake curve | |
| [25] | Body motion script (grouped bytecode, as the Enigma's) | `+0x8980` |
| [26], [27], [28] | Head motion scripts, per **role** | head players |
| [29] | Caps, 20 × (kind, bone, shape u16) | colliders |
| [30] | Shapes, 17 × 0x50 | |
| [31], [32] | Push caps (kind 8 on bones 13, 14, 18, 19 and 3), 2 capsule shapes | |
| [33]-[38] | SHW shadows. The set-up reads 34, 35 and 36 | |

**Body motion lengths** (end frame; head motions have the same lengths):

- 0: 540, 2: 46, 3: 46, 12: 142, 14: 240, 15: 254
- 30: 90, 31: 90, 32: 210, 33: 182, 35: 159, 36: 80
- 40: 270, 42: 440, 46: 525, 47: 244, 48: 240, 49: 130
- 50: 140, 51: 140, 52: 125, 53: 50, 55: 146, 56: 149, 57: 290

The body motions use channel interpolation **type 1**, which `tools/mot_ps2.py` rejects. It needs that type
added. The head PACs parse.

**Body skeleton** (world rest position, cm):

- 0 root; 1, 2 hips (0, 474, −95); 3, 4 chest (0, 488, 202); 5 neck base (0, 460, 420)
- **6, 7, 8: the three neck sockets** (all at 0, 460, 420; the head for role r sits on 6+r)
- 9 and 10: collar sides (∓302, 564, 414), where chains 1 and 2 attach
- 11: back top (0, 726, 332), where chain 0 attaches
- Left front leg 12-16: 15 is the lower leg, 16 the paw at y −36, x −171
- Right front leg 17-21: 20 is the lower leg, 21 the paw, x +171
- 22: hips (where the collar sits). Left hind leg 23-26, right hind leg 27-30

**Head skeleton:** 0 root, 1 base, 2 (z 119), 3 (z 223), **4 tip / mouth (z 250)**.

- Effects and projectiles leave from bone 4 (`+0x1be0 + i*0x14`).
- The small models sit on bone 3.
- Inferred: bone 3 is the jaw and bone 4 the snout tip.

**Caps** ([29] → [30]; spheres given as centre and radius; kind 6 and 4 are hurt shapes, kind 1 attack):

| Cap | Kind | Bone | Shape |
| --- | --- | --- | --- |
| 0, 1, 2 | 6 | 14, 15, 16 (left leg) | r100, r100, r70 |
| 3, 4, 5 | 6 | 19, 20, 21 (right leg) | r100, r100, r70 |
| 6, 7, 8 | 4 | 0 | (0, 300, 0) r500, (±300, 300, −100) r500 |
| 9 | 4 | 1 | (0, −150, 300) r450 |
| **10** | 1 | 8 | (−100, −100, 150) r260 |
| **11** | 1 | 15 (left paw) | r160 |
| **12** | 1 | 20 (right paw) | r160 |
| 13 | 6 | 4 | (0, 0, 80) r100. On a head model: head hurt |
| **14** | 1 | 4 | (0, 0, 80) r150. On a head model: **bite** |
| 15 | 6 | 2 | (0, 0, 80) r100. On a head model: neck hurt |
| **16**, **17** | 1 | 16, 21 (paw tips) | r80 |
| **18** | 1 | 0 | (0, 0, 300) **r700**: the chain-break burst |
| **19** | 1 | 8 | (−100, −100, 150) r230 |

- Body scripts keep caps 0-9 on.
- Head scripts keep 13 and 15 on.

**Attack records** `0x5b3350`, 0x40 each. Word 1 is inferred as damage and word 6 as the reaction type, by
analogy with the Pride's records (docs/ENEMY_SPECS.md):

| Record | Damage | Type | Word 7 | Used by |
| --- | --- | --- | --- | --- |
| 1 | 300 | 1 | 150 | neck sweep (cap 19) |
| 2, 3 | **1200** | 4 | 600 | punch-icicle paw (caps 11, 12) |
| 4 | 600 | 1 | 300 | left scratch (cap 11) |
| 5 | 600 | 0 | 300 | right swipe (cap 12) |
| 6 | 400 | 7 | 200 | breath ground spheres |
| 9 | **1440** | 2 | 700 | dash / pounce (caps 10, 16, 17) |
| 12 | 240 | 0 | 120 | punch-icicle ground spikes |
| 14 | 600 | 2 | 300 | head bite (cap 14) |
| 15 | **1560** | 5 | 800 | chain-break burst (cap 18) |
| 17 | 800 | 7 | 400 | breath sweep box (phase 1 and up) |

- The projectile records:
  - Icicle `0x5b3510` rec 0: 240, type 1.
  - Sphere `0x5b3550` rec 0: **1800**, type 2.
  - Shard `0x5b3750` rec 0: 720, type 7.
- Inferred: type 7 is "freeze" or ice.

---

## 3. Decision logic

### Idle: action 0, `29f1a0`

1. **Timer** `+0xaa2c` starts at `0x5b19b0`: **1000 / 700 / 300** for phases 0 / 1 / 2.
   - Each frame it loses rate × dt. The rate depends on Dante's distance from home (`0x5b19e0`): **6** under
     1000, 8 from 1000, 10 from 2000, 12 from 3000.
   - So, close up: about **167 / 117 / 50 frames**.
2. **Extra wait**, picked by weight from `0x5b1a70` (0, 4, 8, 12 or 16 frames):

   | Phase | 0 | 4 | 8 | 12 | 16 |
   | --- | --- | --- | --- | --- | --- |
   | 0 | 10 | 10 | 20 | 20 | 40 |
   | 1 | 10 | 20 | 20 | 40 | 10 |
   | 2 | 20 | 20 | 40 | 10 | 10 |

3. **Decision** `2a7a60`.
- While a combo is running (`+0xaa92`), the timer is 0, so its steps follow back to back.

### Back-to-idle: `29e6e0`, run at the end of every move

1. If the phase just changed (`+0xa989`), it does the **chain break** (section 4).
2. If the "turn gauge" `+0xaa80` is over 500 and Dante is more than about 18° off its forward (sin 0.31), it
   turns.
   - Script 2 or 3 (46 frames). It rotates **0.0273 rad a frame** between frames 6 and 22, so about 25° per turn.
   - The facing clamp above applies.
3. Otherwise it picks from `0x5b3b00`: 0 = idle, 1 = **chain struggle**, 2 = **re-ice**.

   | Phase | Idle | Chain struggle | Re-ice |
   | --- | --- | --- | --- |
   | 0 | 50 | 50 | 0 |
   | 1 | 100 | 0 | 0 |
   | 2 | 100 | 0 | 0 |

4. **Re-ice is forced** when the flag `+0xa990` is set (see `+0xaaa4` in section 5). It is cancelled if no part's
   armour index has reached its limit (`0x5b3a10`):
   - Phases 0 and 1: heads 13, 13, 13 and paws 4, 4.
   - Phase 2: 10, 10, 10 and 3, 3.

### Zones: `2a7790`, Dante in body space

| Zone | When |
| --- | --- |
| 1 | Under 1200 and within the ±15.5° cone (`0x619350` = 31° full) |
| 2 | Under 1200, to the side of the right paw (+x) |
| 3 | Under 1200, to the side of the left paw (−x) |
| 4 | Over 1200 and local z under 1400 |
| 5 | Over 1200 and local z of 1400 or more (far in front) |

The 900 band (`0x5b39e0`) maps to the same zones.

### Decision table `0x5b1bb0`

Entries are {id, weight for zones 1-5}. Ids 0-11 are moves, 12-19 are combos (`0x5b3230`):

- 12: [0, 9]
- 13: [2, 3, 8]
- 14: [2, 3, 9]
- 15: [5, 5, 4]
- 16: [7, 7, 7]
- 17: [7, 6, 7]
- 18: [0, 0]
- 19: [8, 8]

Weights for zones 1 / 2 / 3 / 4 / 5 at rank 0 (empty = 0 in every zone):

| Id | Move | Phase 0 | Phase 1 | Phase 2 |
| --- | --- | --- | --- | --- |
| 0 | Bite (all heads) | - | - | - |
| 1 | Neck sweep, then rotate | 15/10/10/0/0 | - | - |
| 2 | Punch icicle, left paw | 10/0/60/15/5 | 0/0/40/5/5 | 5/0/10/10/0 |
| 3 | Punch icicle, right paw | 10/60/0/15/5 | 0/40/0/5/5 | 5/10/0/10/0 |
| 4 | Left scratch ×2 | - | 5/0/40/5/0 | 5/0/10/0/0 |
| 5 | Right swipe | - | 5/40/0/5/0 | 5/10/0/0/0 |
| 6 | Head 0: ice breath | 40/10/10/0/0 | 50/5/5/10/10 | 5/5/5/0/0 |
| 7 | Head 1: ceiling icicles | 5/10/10/10/35 | 10/5/5/10/30 | 0/5/5/10/30 |
| 8 | Head 2: ice sphere | 0/0/0/40/25 | 0/0/0/40/15 | 0/0/0/10/20 |
| 9 | Bend low and dash | 10/10/10/20/30 | 30/10/10/20/35 | 10/15/15/20/30 |
| 12 | [bite, dash] | - | - | 10/10/10/0/0 |
| 14 | [punch L, punch R, dash] | - | - | 10/10/10/10/20 |
| 15 | [swipe R, swipe R, scratch L] | - | - | 25/35/35/30/0 |
| 18 | [bite, bite] | - | - | 20/0/0/0/0 |

Forcing rules:

- **The bite is forced** (`+0xa991`, outside a combo) after more than **30 / 28 / 26** melee hits on heads
  (`0x5b54f0`).
- Specials 6, 7 and 8 fall through to the next head if their head is dead: 6 → 7 → 8 → 6.

---

## 4. Moves

Body script n is S0.n of chunk [25]. The `ev` values are the script's op3 event values, read by code.
Durations are the motion lengths.

### Bite: id 0, action 0x13 (`2a5220`, `2a52e0`)

- Body script **0x37** (146 frames), or **0x34** (125 frames) when Dante's `+0x2898 & 2`. Inferred: Dante is in
  the air.
- The heads' scripts carry the bites:
  - In S55, cap 14 (record 14, **600**) is live at **frame 34 for 7 frames**.
  - In S52, at frame 32 for 10 frames (head 0) and frame 35 for 13 frames (head 1).
- S52 events 1, 2 and 3 (frames 44, 48, 51) put effect 0xb5 on the role 0, 1 and 2 heads.

### Neck sweep: id 1, action 0x11 (`2a1c70`, `2a1d00`)

- Body S49 (130 frames): cap 19 (record 1, 300) live from **frame 40 for 30 frames**.
- At the end the heads rotate (`2a1ee0(0)`). If the new role-0 head is dead it rotates again (S30, action 5).
- Inferred: a sweeping head swing.

### Punch icicle: ids 2 and 3, actions 0xb and 0xc (`2a0350`/`2a0510`, `2a0f10`/`2a10d0`)

- Body **S50 (left paw, bone 16) or S51 (right paw, bone 21)**, 140 frames, at anim speed `0x5b3cd0` (1.0, 1.0,
  1.5 by phase).
- **Paw cap 11 or 12: record 2 or 3, 1200 damage, live at frame 86 for 5 frames.**
- Events:
  - ev1 @86: effect 0x73 at the paw.
  - ev2 @87: effect 0x3a.
  - **ev3 @88: 8 ground spikes** at the paw's xz, y 100. Each is a sphere of **r50**, record 12 (**240**), live
    **60 frames**, moving each frame in body space (rank 0, `0x5b3c10`):
    - Spikes 0-5: forward × 2.8·i (0, 2.8 … 14 a frame), so a line reaching about 840 forward.
    - Spike 6: forward 3, side −5. Spike 7: forward 3, side +5. Two diagonals.
  - ev4 @92: effect and camera shake.
  - ev5 @94: effect.

### Left scratch: id 4, action 0xd

- Body S48 (240 frames): cap 11, record 4 (**600**), live at **frame 34 for 10 frames** and **frame 126 for 11
  frames**.
- Effect 0xad.

### Right swipe: id 5, action 0xe

- Body S53 (50 frames): cap 12, record 5 (**600**), live at **frame 16 for 10 frames**.
- Effect 0xae.

### Head 0, ice breath: id 6, actions 0x15 → 0x16 → 0x17 (`2a1f80`, `2a2070`, `2a2320`, `2a2530`)

1. Rotate if needed (S30 or S31, 90 frames).
2. Blend to S40 (270 frames). Head attack effects: 0xb1 on the role-0 head, 0x140 at the mouth, and 0x141 in
   phase 1 and up.
3. Events:
   - ev1 @12, ev2 @60: mist effects 0x2c and 0x17. ev4 @102: the mist is killed.
   - **ev5 @136: the breath is live for 100 frames** (`0x619358`):
     - **10 ground spheres, r100, record 6 (400)**. Each sphere can hit again every 50 frames (`0x619368`).
     - Sphere i sits at the mouth's xz plus t·(0.08·dx_i, 0.002·dz_i) in **world** axes, at y −50.
     - The (dx, dz) values (`0x5b3d60`) are (40, −1050), (5, −1080), (−5, −1050), (30, −1050), (−7, −1050),
       (30, −1250), (5, −1400), (−5, −1250), (25, −1350), (−10, −1250).
     - So after 100 frames they spread up to ±320 in x and 210-280 toward −z.
     - Phase 0 adds effect 0x2d on the head.
   - **Phase 1 and up, the sweep:** a box with half sizes **400 × 50 × 800** (`0x5b3e00`), record 17 (**800**).
     - It starts at body-root local (−1700, 0, 1200) (`0x5b3e10`, `0x5b3e0c`).
     - It moves **23 a frame** along the body's x axis for the same 100 frames.
   - ev6 @140: effect 0x4e. ev7 @152: effect off. ev8 @230: effect 0x17.

### Head 1, ceiling icicles: id 7, actions 0x18 → 0x19 → 0x1a → 0x1b (`2a3350`, `2a3450`, `2a36b0`, `2a3710`)

1. Rotate if needed, then S35 (159 frames), then S36 (80 frames).
2. Events 1-9 (frames 9 to 53) are rising-ice effects (99, 0x97, 0x96, 0xfb) at the paws and the chest.
3. **ev10 @70:** effect 0x76 at bone 4. If head 1 is alive it spawns the **icicles**: 8 in phase 0 (table
   `0x5b3e40`), 15 in phase 1 and up (`0x5b4098`). Entries are {delay, scatter, gravity, stay, scale}:
   - Phase 0: delays 0, 2, 4 … 14; scatter 0, 50, 50, 100, 100, 300, 300, 300; stay 200 for the even entries,
     0 for the odd ones.
   - Phase 1 and up: delays 0 … 22, 32, 42, 52; scatter up to 800; stay 70-120 or 0.

Each `CEm025Shl01` icicle runs:

1. Wait `delay` frames (counted 1 a frame, not scaled by dt), then **40 more frames**.
2. Pick its x and z: Dante's x and z, each plus a random offset in [−scatter/2, +scatter/2].
   - Start at **y 1000**.
   - Effect 0x123, inferred the falling shadow.
3. **Fall:** v_y −= gravity (1) × dt each frame, so it lands after about **45 frames**.
   - Two spheres of **r50** at y −80 and y +150 from its point, record 0 (**240**).
   - Hitting the world (flag 0x40) shatters it (effect 0xab, sound 0x1d).
4. **On the ground** (y < 0): a camera shake, then the collider changes shape (type 6).
   - It stays up as a **pillar for `stay` frames**. It shatters early if it touches Dante (flags & 3).
5. Shatter: 50 frames, then it's freed.

### Head 2, ice sphere: id 8, actions 0x1c → 0x1d → 0x1e (`2a44b0`, `2a45b0`, `2a4860`)

1. Rotate if needed, then S42 (440 frames) at anim speed `0x5b53c0` (**1.0 / 1.8 / 2.5**).
2. `2a4860` spawns one **`CEm025Shl00`**: pattern 0 in phase 0, pattern 1 in phase 1 and up. It holds at the body
   root with effect 0x65.
   - **Charge:** effect 0x61 after 160/speed frames. **Launch** after **270/speed** frames, from the role-0
     head's mouth (head bone 4).
   - **Flight:**
     - It starts along the body's forward.
     - Its speed s ramps from 0.7 toward 6.0 by 0.7 a frame (`0x5b5100`). It moves dir · s² a frame, so up to
       **36 a frame**.
     - For its first 20 frames it **homes** on Dante + (0, 150, 0), blending 10% a frame, with its direction
       biased to −z.
     - **r150, record 0 (1800).**
     - It dies on a world hit or at **z < 1300** (sound 0x37), or if its head is dead.
   - **Life:**
     - **Phase 0: 300 frames**, then it vanishes (0 shards).
     - **Phase 1 and up: 10 frames**, then it **bursts into 4 `CEm025Shl02` shards**. One fan is picked by
       weight (`0x5b53a0`):
       - 10%: horizontal (±15, 0, 20) and (±3, 0, 30).
       - 90%: vertical (0, ±15, 20) and (0, ±3, 30).
     - The shard velocities are per frame, in the sphere's frame.
     - **Shards: r100, record 0 (720).** They die on a world hit or at z < 1300. A melee hit destroys them
       (`40ded0`).

### Bend low and dash: id 9, actions 8 → 7 → 9 → 10 (`29f8b0`, `29f980`, `29fbb0`, `2a0160`)

1. **Wind-up:** S56 (149 frames, no attack) at anim speed `0x5b3d00` (1.0, 1.0, 1.5). Effect 0xac.
2. **Pounce:** S46 (phase 0, 525 frames) or S47 (phases 1 and 2, 244 frames).
   - Home moves toward Dante over **17 frames**:
     - Phase 0 reaches at most **200**, because it's chained.
     - Phase 1 and up: the whole distance if Dante is within 500, otherwise 200.
   - **Caps 10, 16 and 17 are live at frame 7 or 8 for 30 frames, record 9 (1440).**
   - Two events give camera shakes.
3. **Return:** walks back to (2500, 0, 3250).
   - It takes 455/2 frames in phase 0 and 129/2 frames after (`0x5b3d30` divides them: 2, 2, 1). Footstep sound
     every 84 frames.
   - The chains go taut while it moves.

### Chain struggle: idle pick 1 in phase 0, action 4 (`29f6e0`, `29f7b0`)

- S32 (210 frames), no attack. The chains are pulled taut until ev @96.

### Chain break: the phase 0 → 1 transition, action 0x24 → 0x25 (`2a4c50`, `2a4d30`, `2a4e10`)

- S32, then **S33 (182 frames)**.
- On S32's event, **chains 1-4 come off** (`2a7360`). Chain 0, on the back bone, stays.
- **S33: cap 18 (r700 round the root, record 15, 1560) is live at frame 117 for 4 frames.**
  - ev2: effect 0xb4, a big camera shake, a screen flash, and the ice-glow colour cycling turns on (`+0xa562`,
    `2ac390`, visual only).

### Re-ice ("Ice-coat body"): id 11 or forced, action 0x14 (`2a5580`, `2a56b0`)

- S57 (290 frames). Events at 71, 96, 143, 153 and 163 re-grow, in order: the left paw, the right paw, and heads
  0, 1 and 2. Dead heads are skipped.
- **Each part shows one ice piece every 2 frames** until it is whole: 4 for a paw, 13 for a head. Then its armour
  index goes back to 0 and its armour flag back on.
- Effect 0x125 on each part, and a camera shake.

### Turning: actions 1-3

- Covered under back-to-idle above. The idle timer keeps running while it turns.

---

## 5. Damage model (`2a84b0`)

### Health (`0x5b3330`)

- **Body pool 2400.** **Heads 1650, 1600 and 1550.** The boss bar total is **7200**.
- Inferred colours, from the walkthrough's attack-to-eye list:
  - Head 0 (breath) is red-eyed.
  - Head 1 (icicles) is green.
  - Head 2 (sphere) is blue.

### Damage per hit

**damage = record +4 × weapon% × kind% × `FUN_002ab770`:**

- Weapon% is indexed by hit byte +1 (`0x5b3850`): 100, 120, 120, 80, 100, 120, 100, 25. Not decoded: which
  weapons these are.
- Kind% is indexed by hit byte +2 (`0x5b3870`): 110, 80, 0, 6000, 60, 1, 0, 1. Inferred: 0 = melee, 1 = gun.
- `FUN_002ab770`:
  - ×1.1 on a random roll under hit +8.
  - × the attacker's `vfunc+0x68`.
  - A difficulty factor by `0x1d0d73c`. This applies only when the attacker's byte +5 is 2 or less and the boss's
    AI sub-object is in action 3 or 4:
    - Easy ×2, Hard ×0.9, Very Hard ×0.7, DMD ×0.5.
    - Inferred: `0x1d0d73c` is the difficulty (docs/SHOP.md).
  - ×0.3 more on a further condition (`FUN_0032b240`).
- If hit byte +0x30 is set, the hit can't take a pool below 1.
- **Only one head takes damage per frame** (`+0xa674`).

### Ice armour (the decoded core of the fight)

**Five armoured parts.** Each has an armour index and a piece table:

| Part | Index | Piece table (ice mesh parts) |
| --- | --- | --- |
| Head i | `+0xa53c + 4i` | `0x599168`: 0, 1, … 12, then −1. 13 pieces on head ice [13+i] |
| Left paw (hit x < 0 in body space) | `+0xa534` | `0x618ac0`: 7, 6, 5, 4, then −1 |
| Right paw | `+0xa538` | `0x618ac8`: 12, 11, 10, 9, then −1. On the body ice [12] |

- **Each hit on an armoured part breaks exactly one piece**:
  - The part's armour HP (99, `0x5b39f0`/`0x5b39f4`) minus 100 goes below 0 on every hit.
  - The piece's mesh is hidden. Effect 0x7e and sound 9.
  - The index moves on.
- So **13 hits uncover a head and 4 hits uncover a paw.**
- **While the part's next entry isn't −1, the hit does no damage.** It makes a spark (effect 6) instead.
- Gun hits on the body pick the paw by which paw lock-on target is locked.

### Phases

- **Phase 0:**
  - Hits on uncovered heads or paws drain the **body pool** (2400).
  - When it reaches 0, phase = 1 and `+0xa989` is set. That triggers the **chain break** at the next idle.
- **Phase 1:**
  - Uncovered head hits drain that head.
  - **Uncovered paw hits deal 10% to every live head** (`0x5b3344`).
  - When the heads' total is **≤ 3200** (`0x5b334c`), phase = 2.
- **Phase 2:** the same, with 10% (`0x5b3348`).

### Head death

When a head's HP goes below 0:

- Its ice and small model are hidden. Effect sound 10.
- The live-head counter `+0xa994` (from 3) drops by 1.
- If heads are still left, that head is switched off (`+0x8920+i` = 0): no colliders, no specials, and its
  lock-on target is gone.
- When the last head's HP hits 0, or all three are off, `+0xa98a` (death) is set.
- Inferred: a dead head stays drawn, limp or frozen. The `+0xa9dc` state machine plays its own `+0x8300` object,
  then hides the head model's part 1.

### Stagger and other gauges

- **Down (`+0xa98c`, from 1000):**
  - Damage × 0.7 on heads and × 1.2 on the body (`0x5b3bc0`/`c4`) takes it down. It refills at 0.5, 1.0 or 1.4 a
    frame by phase (`0x5b3b90`).
  - **Below 0, if the current action is interruptible** (one of 0-6, 0xa, 0xd, 0xe, 0x11, 0x12 or 0x13): it
    falls. Body S12 (142 frames, action 0xf), then S15 (254 frames, action 0x10), with a camera shake. The
    gauges reset.
  - So about **830 uncovered paw damage** puts it **face-down for about 400 frames**, low enough for ground
    attacks on the heads. This matches the walkthrough.
- **Re-ice gauge (`+0xaaa4`, from 4000 / 3600 / 3000, `0x5b5520`):**
  - Taken down by the same scaled damage, and by time at 0, 0 or 0.001 a frame (`0x5b5550`).
  - At 0 it forces a re-ice at the next idle, if some part's armour is fully broken.
- **Turn gauge (`+0xaa80`, 1000):** raw damage takes it down. It refills at 0.6, 0.8 or 1.0 a frame. Under 500
  it doesn't turn toward Dante.
- **Head flinch:**
  - A melee hit on a head, outside committed actions (`+0xa993`), plays an additive flinch on that head's second
    player (`+0x8cc0+…`).
  - The script is picked by role and hit direction byte +0x20:

    | Role | dir 0 | dir 1-3 | dir 4 | dir 5-7 | Other |
    | --- | --- | --- | --- | --- | --- |
    | 0 | 0x17 | 0x0a | 0x0d | 0x0b | 0x1a |
    | 1 | 0x16 | 0x10 | 0x14 | 0x11 | 0x19 |
    | 2 | 0x18 | 0x12 | 0x15 | 0x13 | 0x1b |

  - A repeat of 0x19-0x1b plays 0x1c.
  - It blends in at 0.25 a frame and out at 0.05 a frame. The body isn't interrupted.

### Death (`2ac090`, `29e170`, `29e240`, `29e500`)

**`+0x8952` == 0** (the story):

- State 2:
  - Awards `vfunc+0x1e0(7200)` into global `+0x100`, with 7000 (`0x5b3894`) into `+0x104`. Inferred: score and
    orb bonus.
  - **Removes itself from the enemy manager**, which counts as "encounter cleared".
  - Stores the surviving head's index in `0x1d0cab4`.
  - Goes back to body script 0.
- State 3 then idles. The event script's demo takes over.

**`+0x8952` == 1:**

- Death script 0xe (S14, 240 frames) with effect 0x2b3.
- Then a 60-frame fade, the award, and the object is freed.
- Inferred: a boss-rush or replay mode.

---

## 6. Boss bar, fight start and end

- **Bar** (the set-up, `FUN_00325390`):
  - Maximum = 2400 + 1650 + 1600 + 1550 = **7200**. It reads **`+0xa530`**: the body pool plus each live head's
    HP, updated on every hit.
  - Icon or texture from `FUN_001aebf0(0x6c3810, 6, 3)`.
  - Inferred: the bar empties a third in phase 0, then the head pools.
  - The port has no boss bar yet.
- **Room 006** (from docs/EVENT_SCRIPTS.md, EventTbl03):
  1. Counter 0 is 0, in area 0: **demo 6** (Cerberus appears). Then back to room 6 at pos 2, counter 1.
  2. Counter 1: **start EST group 0** (class 43 at (2500, −100, 3300); the set-up moves it to (2500, 0, 3250)),
     and disable door 0.
  3. When the encounter is cleared: **demo 7**, counter 2, room 6 pos 2.
  4. Counter 2: **give item 23** (the Cerberus nunchaku) and the orb reward at (2500, 200, 2500).
  5. Area 1 (the tower door): demo 8, then **mission clear**.
- **Arena:**
  - Dante is held to **z 1400-3000** every frame of the fight. x is not clamped; the room walls do that.
  - Cerberus's home is z 3250. Its body never leaves home by more than the pounce reach (200, or up to 500).
  - Projectiles die at **z < 1300**.
  - Chain anchors (`0x5994e0`):
    - Chain 0: (2500, 1035, 3420) on bone 11.
    - Chains 1 and 2: (3200, 900, 2900) and (1800, 900, 2900) on bones 9 and 10.
    - Chains 3 and 4: (3000, 900, 3250) and (2000, 900, 3250) on the collar.
    - Each chain has 10 links of 150. Taut chains add or drop links (7-9) when stretched past 1.2 or under 1.0.

---

## Port plan

**Reuses enemy.c:**

- Script player and op format (`script_play`, `script_tick`, the caps and records path from
  `tools/gen_enemy_ai.py`). This one is grouped like the Enigma's, with one body player plus three head players
  on the same script ids.
- Turning helpers (`turn`, `wrap_angle`).
- The Enigma's arrow pool pattern for projectiles.
- The Gigapede's multi-part draw and hit routing (`giga_*`).
- `land_attack` for Dante damage.
- `enemy_died_fn` for the "encounter cleared" event.

**New:**

1. A pack tool `tools/prepare_cerberus.py`, like `prepare_gigapede.py`:
   - Body plus 3 heads plus body and head ice (with per-mesh-part hide flags).
   - Collar and chain models, motions [19]-[22] (needs type-1 channels in `mot_ps2.py`), scripts [25]-[28],
     caps [29] and [30].
2. A one-object, five-part boss:
   - Heads attached to neck bones 6+role.
   - Role rotation.
   - Five hurt-part ids routed to the damage model above.
3. The armour model (piece per hit, −1 = uncovered), the three pools, phases, down, re-ice.
4. Moves at rank 0, in this order:
   1. Bite, swipes and punch icicle (body and head caps only).
   2. Dash.
   3. Breath (spheres plus box).
   4. Icicles.
   5. Sphere and shards.
   6. Chain break.
5. A boss bar in hud.c, and the room-006 flow (start group 0 after demo 6, cleared → demo 7, item 23).
   Optional: chain rope sims; before that, draw the chains straight from bone to anchor.

**Suggested order:**

1. Pack and draw (pose S0 idle in room 006).
2. Damage model, armour and bar.
3. Idle and decisions, then the moves as listed.
4. Phase transitions and death or clear.
5. Chains.

## Open questions

- Which weapons and attack kinds hit bytes +1 and +2 index. Weapon% has 120 for indices 1, 2 and 5. The
  walkthrough says Agni & Rudra is its weakness.
- What decides the AI rank value (`vfunc+0x7c`). This spec assumes 0 on Easy and Normal.
- The meaning of records' word 6 and 7 (reaction type and knock-back), shared with the other enemies.
- Whether a dead head is still drawn and how. The `+0x8300` object and `+0xa9dc` states.
- The phase-0 sphere: with 0 shards it just disappears after 300 frames unless it hits. Check against the
  reference.
- The breath's world-axis spread (x up to ±320 against z of only about 280). Check it in PCSX2.

## Functions decompiled for this spec

- Cerberus: `298aa0 2995f0 299280 29a190 29a460 29a4f0 29d0a0 29e170 29e240 29e500 29e6e0 29f1a0-29fbb0
  2a0160-2a5f70 2a61d0 2a64d0 2a7360 2a7590 2a7790 2a7a60 2a7da0 2a7e40 2a80f0 2a8190 2a8200 2a83f0 2a84b0
  2ab770 2abac0 2ac090 2ac130 2ac170 2ac2b0 2ac390 2acb40`.
- Shells: `32bd30-32cdb0 37edb0-37fe00 40d970-40e1c0`.
