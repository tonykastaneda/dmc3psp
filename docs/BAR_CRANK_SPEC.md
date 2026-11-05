# Mission 3, Bullseye: the crest switch ("crank") and the blue seal

This is a port-ready spec of the Mission 3 puzzle. In the bar, a crest switch sits behind the dart machines. Charging it with sword hits removes the blue seal, which blocks the Shotgun and the door to Love Planet.

## PSP implementation and verification (2026-11-05)

The runtime charges from sword hits, drains after the timeout, lights eight flames, fades the blue seal,
unlocks the Love Planet door, and retains the open state for the rest of the mission. Switch and seal
graphics are stand-ins; original gimmick models, the unlock camera event and Shotgun pickup are still pending.
Damage per hit remains the port's estimate described below.

PPSSPP checks passed for: breaking the dart machines and walking into the alcove to activate the switch;
the closed seal blocking walking and door use; walking through the opened corridor and pressing Circle to
enter room 004; returning to room 003 with the switch still lit; partial charge draining to zero; and gunfire
leaving it uncharged. These are isolated interactions, not certification of a full Mission 3 playthrough.
Evidence is under `build/tests/bar_crank_flow`, `build/tests/bar_crank_decay`, and
`build/hdref/pp/out_crank_play` (game framebuffer BMPs, position logs and switch telemetry).

The first run exposed a permanent safety wall generated from the edge of a lower floor underneath the bar's
walkway. The converter now caps those guards below upper floors and preserves floor-backed door approaches.
Room-entry camera setup also clears the previous room's leash and target state.

Reproduce (emulator only):

```powershell
python tools/test_bar_crank.py --build
python -m unittest discover -s tools/tests -p test_stage_edges.py
```

`--build` regenerates room 003 and its tables before compiling. Ordinary incremental builds reuse generated
room files, so rebuilding just the EBOOT does not update an older `ROOM_003.BIN`.

All addresses are in the PS2 SE executable `SLUS_213.61`. The gp register is `0x620170`. File offset to address: + `0xFFF80`.
Tags: **[D]** decoded from data or code. **[I]** inferred: an interpretation, a name for a decoded thing, or a guess.
Where a line has no tag, it is [D].

The user calls the dart machines "lottery (slot) machines" and the switch a "crank". In the game data the switch is
`紋章スイッチ` (crest switch, class `CStageSetSwitch`) and the seal is `封印扉` (sealed door, class `CStageSetSeal`).

---

## 1. Where: room 003, Bullseye

| What | Value |
|---|---|
| Stage | `st003.pac`, GDATA slot **284** (its `# GAME` text is PAC[4], its PNST models are PAC[5]). Room data is in slot **285** |
| Port room index | 3 (`tools/gen_rooms.py` `SLOTS["003"] = 284`). `MISSIONS[3]` gives 003 no fight, so today its doors open at once |
| Event script | EventTbl03 (GDATA 241), `ROOM [3]`. Room 3 appears in no other mission's script (checked EventTbl00 to 21) |
| Doors (slot 285 text) | **door 0** → 002 pos 1, BoxIn (2960, -10, 3048) size (50, 300, 100), BtnOn, Mission ≤ 13. **door 1** → **004 Love Planet** pos 0, BoxIn (2300, -10, 1290) size (100, 300, 50), BtnOn, Mission ≤ 13. Doors 2 and 3 reuse those boxes for Mission ≥ 14 (→ 007, 011) |
| Player starts (POS) | 0: (2884, 0, 3060) yaw 180, by door 0. 1: (2296, 0, 1380) yaw 0, by door 1. 2: (2764, 550, 1981) |

Layout [D positions, I reading]: you enter at z ≈ 3050 (door 0). The bar runs toward −z. The blue seal stands across a
corridor at **z = 1741**. Behind it are the Shotgun on its board (z 1624) and door 1 to Love Planet (z 1290). The crest
switch is on a wall in a side alcove at x ≈ 1680, z ≈ 1950. Three dart machines (`ダーツ`) stand across the alcove's mouth
at x ≈ 1935–1955, z 1850–2050, with the jukebox (`ジュークボックス`) at the corner (2044, 0, 2150). The walkthrough
puts it this way: smash the machines "at the corner near the blue seal to reveal a hidden room. You will see a device."

### Event areas used here (room data, slot 285 `EVE` chunk) [D values, I field names]
Each record is 0x80 bytes: u32 id, u32 kind, u32 value, then 4 corners (x, y, z, 1) and a height. In the list below,
"kind 3" records carry a message id and the script never tests them. They are probably automatic examine texts.

| id | x range | z range | height | used for |
|---|---|---|---|---|
| (kind 3, msg 10005) | 2447–2975 | 3174–3414 | 300 | by door 0 |
| (kind 3, msg 10006) | 2397–2478 | 2732–3127 | 300 | the bar counter |
| **0** | 2110–2440 | 1451–1551 | 500 | behind the seal: touching it gives the Shotgun |
| **1** | 2111–2441 | 1742–1810 | 500 | in front of the seal: examine gives message 10003 |
| **2** | quad (1569,1743) (1666,2080) (1832,2092) (1737,1751) | | 300 | in front of the switch: examine gives message 10004 |
| **3** | 2140–2253 | 1482–1757 | 500 | at the Shotgun board: examine gives the Shotgun |
| **6** | 1917–3117 | 2529–2629 | 1500 | a strip across the bar: tip 9 |

---

## 2. The stage sets (`# GAME`, slot 284 PAC[4]) [D]

```
# SET 100 SWITCH  ; 紋章スイッチ   model 20   pos 1680, 140, 1950   rot 0, 90, 0
# SET 200 SEAL    ; 封印扉         model 17 (板モデル board)  bmodel 16 (手モデル hand; "without it no hand comes out")
                                    pos 2300, 120, 1741
# SET 300 STAY    ; ショットガン   model 18   pos 2168, 183, 1624   eff G 100 (light effect)  epos 0, 5, -25
# SET 0 BREAK     ; ダーツ x3      model 10 / bmodel 11, vital 50, hit box 0,100,0 (50|30),200,50, alpha on
                                    pos (1910,0,2050) (1930,0,1940) (1910,0,1850); one rolls RED_ORB 100 at 50 %
# SET 0 BREAK     ; ジュークボックス model 14 / 15, vital 50, hit box 0,50,0 50,120,50, RED_ORB 500 100 %, pos 2050,0,2150
```
`tools/gimmicks.py` already lists the dart machines as breakables 17–19 and the jukebox as 20 (`breakables()`). Their
boxes are solid in the port (`collision_boxes`). Nothing has to be added for them.

### Models (PNST, slot 284 PAC[5]) [D]
- **Switch, model 20:** an SCM with 1402 triangles and texture 16, in 13 objects (nodes 1–13). Local bounds are
  x ±66.7, y −67.6..78.9, z ±22.5. The objects are:
  - obj 0: a hub cube (±22.5)
  - obj 1: a disc (r ≈ 51) → node 1
  - obj 2–9: **eight identical blades** (y 10..79 each) → nodes 3–10
  - obj 10: an inner hub
  - obj 11: the outer ring (r ≈ 67)
  - obj 12: a flat quad (r ≈ 46)

  `gimmicks.DRAWN` excludes SWITCH, so the port draws nothing for it today.
- **Seal board, model 17:** an SCM with 50 triangles, flat in local XY at z = 0. It spans x ±154.8 and
  y −218.1..203.7, with full 0..1 UVs on texture 14 and white vertices. In the world it covers x 2145..2455 and
  y −98..324 in the plane z = 1741.
- **Seal hand, model 16:** an EFM (effect model) with a motion PAC in slot 1. The port has no EFM support.
- **Shotgun, model 18:** an SCM, 282 triangles, about 10 × 22 × 73.

---

## 3. The switch (`CStageSetSwitch`)

### Code map [D]
| Address | What it is |
|---|---|
| typeinfo `0x5f5940` | name `0x5abd00` "CStageSetSwitch" |
| vtables | primary `0x605c60`, `+0x40` hit receiver `0x605c84`, `+0xa0` `0x605d04`, `+0xd0` event interface `0x605d98` |
| `FUN_00304af0` | creates it from a SET; reads set fields into +0x827/+0x82a/+0x82c/+0x830 |
| `FUN_00304de0` | constructor (defaults below) |
| `FUN_003050e0` | update. Does nothing while **+0x820 = 0** (disabled). State byte +4 is 0 → 1 → 2, then `FUN_003053b0` each frame |
| `FUN_003051e0` | start (state 1). If the bound flag is **already set**, it shows as lit at once (`FUN_00305530`) |
| `FUN_00305820` | the charge logic, every frame while not lit |
| `FUN_00306000` | **hit handler** (through the `+0x40` thunk `0x306fd0`) |
| `FUN_00305dd0` | spin and flame visuals from the charge values |
| `FUN_003055c0` | every frame while lit: the optional lit timer, and a reset if the flag is cleared |
| `FUN_00305530` | set lit: charge 300, wing 200, flames 560, angles 60/20, all 8 blades out |
| `FUN_00305fd0` | reset: charge, wing and flames to 0 |
| `FUN_003065a0` | lit effects: effect 0xf4 once at (0, −6, 0), and 0x10b, which stays attached |
| `FUN_00306870(this, lit, i)` | flame `i`: effect 0x181 |
| `FUN_00306160` | turns nodes 1 and 2 by the spin angles, and pushes blades out by +0x858[i] |
| `FUN_00306c90(this, n)` | picks motion or visual preset `n` from table `0x5abbc0` (0x50 bytes each). Hit = 3, lit = 0, then 2/1 in a loop |
| event `+0xd0` interface | vfunc +0x28 `0x307000` → `0x304fe0`: **bind flag** (stores op 32's flag at +0xdc). +0x0c `0x307020` → `0x306eb0`: enable (+0x820 = 1, op 37). +0x10 `0x307010` → `0x306ec0`: disable and kill effects (+0x820 = 0, op 36) |
| op `35 id,t` (FUN_0020be20) | writes **+0x838 = t**, the lit time limit. Room 3 never uses it; M6 room 124 and M17 room 130 do, for timed switches |

### State [D]
| Field | Meaning | Default |
|---|---|---|
| +0x820 | enabled | 1 |
| +0x821 | lit | 0 |
| +0xdc | bound event flag (op `32 100 32` → **flag 32**) | −1 |
| +0x834 | frames left in the "being hit" window | 0 |
| +0x838 / +0x83c | lit time limit and its countdown (−1 = stays lit for good) | −1 |
| +0x844 | **charge**, 0..301 | 0 |
| +0x848 | **wing**, 0..201 (second ring's speed) | 0 |
| +0x84c | **flames**, 0..561 | 0 |
| +0x850 / +0x854 | spin angles (degrees, wrapped at 180) | 0 |
| +0x858[8] | blade push-out per flame (0 or 23.0) | 0 |

### Hit volume [D values, I shape]
Collider at +0x7c0: kind 6, record +0x7d0 = 2, centre (0, 0, −20) local, w 1, **radius 60**. It is registered with
`FUN_002942e0`, the same call as the enemy colliders. So it is a **sphere of radius 60 at local (0, 0, −20)**. With the
set's yaw of 90° (the `gimmicks._rotation` convention, local +z → world +x) its world centre is **(1660, 140, 1950)**.
The disc faces +x, into the alcove. Dante hits it from x ≈ 1720–1830, which is inside area 2.

### How it is operated: attack hits, no button [D]
In `FUN_00306000(this, hit, attacker)`:
1. Ignore the hit if `hit` or `attacker` is null, if **`hit->byte[2] != 0`**, or if the switch is already lit. The red
   orb crystal's handler (`FUN_00366f10`) makes the same `byte[2] == 0` test [I: byte 2 is the attack's kind, so melee
   counts and gun shots don't. The walkthrough says the device "reacts to the sword's power"].
2. `amount = hit->float[+4] * attacker->vfunc+0x68()`. For Dante that is `CPlayer` +0x40 `0x206920` → `0x2e0d50`, which
   returns **player+0x2a18** [I: an attack power multiplier, 1.0 normally, higher in Devil Trigger]. A weapon's own
   +0x68 returns 1.0.
3. Then:
   - **If charge < 300:** `charge = min(charge + amount, 301)`. If the new charge is over 100, then also
     `wing = min(wing + amount, 201)`.
   - **Otherwise:** `flames = min(flames + amount, 561)`. Damage that overshoots 301 during the charge phase is lost.
4. Then:
   - **window = 120 frames** (2 s).
   - +0x823 = 0, which re-arms the "running down" sound.
   - visual preset 3 (a hit flash), effects `FUN_003064b0`/`FUN_00306520`.
   - **sound 0x14** (bank `0x1e58e80`, group 8).

So the switch is a **damage counter**: 300 points to spin it up, then 560 more to light the flames. There are no turns
and no button prompt. Each flame is 70 points.

### Every frame while not lit (`FUN_00305820`) [D]
`dt` is the object's +0x10 time step [I: 1.0 a frame at normal speed].
- **Window:** `window -= dt`. While it is above 0, nothing runs down. When it runs out, sound **0x1a** plays once.
  Then the values run down by **2·dt a frame** toward 0:
  - charge and wing always run down;
  - **flames** run down only while **charge ≤ 100**.

  If Dante stops for 2 s, the disc slows. Charge falls from 300 to 100 in about 100 frames. After that the flames go
  out, one every 35 frames, from the last lit one down.
- **Visuals** (`FUN_00305dd0`):
  - the disc (node 1) turns by `60·(charge/300)·dt` degrees a frame;
  - the second ring (node 2) turns by `20·(wing/200)·dt` degrees a frame;
  - **flame i (0..7) is lit when `flames ≥ 70·(i+1)`**. Its blade goes out by 23, and effect 0x181 is made at local
    `Rz(−45°·i) · (0, 85+23, 8)`. That puts the 8 flames on a circle of radius ≈ 108 round the disc, every 45° from the
    top;
  - the flames are red, RGB (0x80, 0x30, 0x30). They are blue (0x30, 0x30, 0x80) only in rooms 202, 212, 226 and 227;
  - the code meant to show a part-lit flame does integer `(flames % 70) / 70`, which is always 0. So a flame is either
    off or fully lit.
- **Hum:** the hum sound depends on the charge. It is silent at charge 0. Up to 100 it plays sound 0x17 every 25 frames,
  up to 200 sound 0x18 every 15 frames, and above that sound 0x19 every 5 frames. The timer is +0x840.
- **Lit test:** the switch lights when **charge ≥ 300 and flames ≥ 560**, which means all 8 flames. Then:
  - +0x821 = 1;
  - **bound flag 32 is set**;
  - lit effects (`FUN_003065a0`), preset 0, +0x824 = 1;
  - the lit countdown starts from +0x838.
- It also lights if flag 32 gets set from outside.

### While lit (`FUN_003055c0`) [D]
- It goes back to unlit (reset to 0, flag cleared) only if the bound flag is cleared from outside, or if a lit time limit
  (+0x838 > 0) runs out. Room 3 does neither, so **once lit it stays lit**.
- The disc keeps turning at full speed with all 8 flames, and loops presets 2 and 1.

### Hits needed [I]
Dante's per-hit damage (the hit record's +4) isn't decoded anywhere in the repo. The port's breakables assume about
**42 points a light hit** (`VITAL_PER_HIT`: a Pride's 250 ≈ 6 hits).

| Hit kind | Points (port count) | Hits to spin up (300) | Hits for 8 flames (560) | Total, with no 2 s pause |
|---|---|---|---|---|
| Light | 42 (1) | 8 | 14 | **≈ 22** |
| Heavy | 84 (2) | 4 | 7 | ≈ 11 |

This needs a measurement (open question 1).

---

## 4. The blue seal (`CStageSetSeal`) on door 1

### Code map [D]
| Address | What it is |
|---|---|
| typeinfo `0x5f5ef0`, vtable `0x6064e0` | "CStageSetSeal" |
| `FUN_0031ba60` | creates it from a SET: board model at +0x250, hand model at +0x6c0 (+0xd6f = 1 if `bmodel`) |
| `FUN_002103e0` → `FUN_00209620` | op **33** `door_attach_seal gim, door, flag, colour`: stores the seal, lock flag and colour on the door. If the flag is already set, the seal goes on at once |
| `FUN_00209730` | op 44 door_lock → `FUN_0031d270(seal, anim, colour)` |
| `FUN_00209830` | op 45 door_unlock: clears the door's flag → `FUN_0031d420(seal, anim)` |
| `FUN_0020a210` | door use: a door whose lock flag is set can't be used |
| `FUN_0031d270` | **seal on**: tint by colour, **register the collider** (`FUN_0031d110`), state 1 (instant, alpha 0x40) or 2 (fade in, sound 5) |
| `FUN_0031d420` | **seal off**: state 6 (instant) or 4 (fade out). **Collider removed at once** (`FUN_00294820(this+0xd80)`) [I: 294820 unregisters] |
| `FUN_0031c8b0` | fade states, by the jump table `0x5f5e80` |
| `FUN_0031c390` | random shimmer of the board (generator `0x1e560b0`) |
| `FUN_0031d4b0` | the hand comes out (hand motion 0, +0xd44 = 75 frames) |

### Room 3's seal [D]
- `door_attach_seal [200, 1, 33, 1]`: gimmick 200 on **door 1** (→ 004), lock flag **33**, colour **1**.
- Colour 1 is the board tint **0x40804030**: A 0x40, B 0x80, G 0x40, R 0x30, a **blue** seal. Colour 0 is
  0x40100280 (red; Love Planet's `door_attach_seal [200,0,-1,0]`). Colour 2 is 0x40800080 (purple). The hand gets the
  same tint (0x40100260 for red).
- **Look:** a flat, translucent board drawn with texture 14 (the seal pattern), with alpha up to 0x40 (about half on the
  PS2), plus a random shimmer.
  - Fade in (state 2): alpha +2·dt a frame to 0x40 (≈ 32 frames), sound 5.
  - Fade out (state 4): sound 6 once, then alpha −2·dt a frame to 0 (≈ 32 frames), then state 6 (gone).
- **Collision** (`FUN_0031d110`) [D values, I layout]: record +0xfa0 = 4, +0xfb0 = 3. The floats at +0xfc0.. are
  (0, 0, −50·sz, 0) and then (0, 0, 200·sx, 180·sy, 50·sz). I read that as a **box centred at local (0, 0, −50) with
  half sizes (200, 180, 50)**. With scale 1 and no turn that is world **x 2100..2500, y −60..300, z 1641..1741**: a wall
  whose bar-side face is at z = 1741, the board's plane. This fits the corridor: area 1 spans x 2111..2441, and the door
  boxes are 300 high.
- **The hand:** when the seal is on, the hand exists, and Dante is within **250** of the seal, the hand comes out (motion
  0). It is cosmetic [I]. A second collider record at +0x1000 (kind 2, radius 200) may be its push or touch volume
  (open question 4).

---

## 5. The event script, room 3 (EventTbl03 0x03f8..0x08d8) [D]

```
on enter (once per visit):
  bgm; set flag 1
  32 gimmick 100 ← flag 32                  (switch lights → flag 32)
  33 seal 200 on door 1, lock flag 33, blue
  if item 29 (Shotgun) count != 0, or the op-0x29 player check: gimmick 300 (Shotgun) off, areas 2 and 5 off
  if flag 4 (set in 13th Avenue): door 0 → room 11 pos 1
  counter 1:  1 → door_lock [1, 0] (seal on, instant)    2 → set flag 32 (switch starts lit)

block, per visit, counter 1 == 0 (the first visit only):
  counter 1 = 1; event (cam 1, skippable); cam cut 0; wait 30;
  door_lock [1, 1]  (seal fades in); wait cam; cam cut 4; wait cam; wait 90; end event; wait 5; tip 8 (seals)

block, once per mission, in area 6:  tip 9

block, per visit, counter 1 == 1 and flag 32 (switch lit):
  event (cam 0, skippable); cam flags 0x800; wait 30; cam flags 0x400; cam cut 3;
  door_unlock [1, 1]  (seal fades out, wall gone); wait cam; wait 60; end event; counter 1 = 2

block, once per mission, no item 29, in area 0:            give item 29 ×1 (Shotgun); gimmick 300 off; areas 2,5 off; tip 10
block, once per mission, no item 29, area 3 + examine:     the same
block, per visit, flag 33 set, area 1 + examine:           event cam 1, message 10003 (the seal)
block, per visit, flag 32 clear, area 2 + examine:         event cam 2, sound (1,22), message 10004
                                                           ("A device that seems to be reacting to the sword's power." [I: from the walkthrough])
```
A correction for `docs/EVENT_SCRIPTS.md` (not edited here). It says the 10004 text shows "with flag 32 set". The block
reads `sw_flag 32; case_eq 1 → end_block 0`, so the text shows **while the switch is not lit yet**.

### Flags and persistence
| State | Scope | Role |
|---|---|---|
| flag 32 | room-local (≥ 32, cleared on every room load) [D] | switch lit |
| flag 33 | room-local [D]. It is door 1's lock flag | seal up / door 1 shut |
| **counter 1** | mission-wide [I: counters keep across room loads; room 6's counter 0 survives its demo reloads] | 0 never seen, 1 sealed, **2 open for good** |
| flag 1 | mission [D] | Bullseye visited |
| item 29 | save | Shotgun owned |

On a revisit:
- with counter 1 = 2, the switch starts lit (flag 32 set before its start) and the seal is never put up;
- with counter 1 = 1, the seal comes back at once, and the switch starts from 0 again.

Nothing carries into other missions. Room 3 is in no other EventTbl, and doors 0/1 exist only up to Mission 13.

### Doors, camera, sounds
- **Doors:** only door 1's lock changes. Door 0 is retargeted by flag 4 (unrelated). Since door 1 is BtnOn, the player
  still presses O at it after the seal is gone.
- **Cameras:** cuts 0–4 come from the room's `CAM` chunk (slot 285 PAC[3], 7 entries). They aren't decoded, so their
  lengths aren't known. Both events can be skipped (`49 cam, 1`).
- **Sounds and effects (note only):**
  - switch sounds, all bank `0x1e58e80` group 8: 0x14 hit, 0x17/0x18/0x19 hum, 0x1a run-down;
  - switch effects: 0x181 flames, 0xf4 and 0x10b lit;
  - seal sounds: 5 on, 6 off;
  - message examine sound: `se [1,22,0,0]`;
  - the Shotgun's glow is `eff G 100`.

---

## 6. Port plan

### Data: `tools/gimmicks.py` and `tools/gen_rooms.py`
1. In **gimmicks.py**, add `switches(game_text)` → `[(number, pos, yaw)]` for `SWITCH` sets, and
   `seals(game_text)` → `[(number, pos, yaw, model, bmodel)]` for `SEAL` sets, from `parse_game`. Add the decoded
   constants as named values:
   ```python
   SWITCH_HIT = ((0, 0, -20), 60)                # sphere: local centre, radius (CStageSetSwitch +0x7e0)
   SEAL_BOX = ((0, 0, -50), (200, 180, 50))      # local centre, half sizes (CStageSetSeal +0xfc0)
   SEAL_BOARD = (154.8, -218.1, 203.7)           # model 17 half width, bottom, top
   ```
2. In **gen_rooms.py**, add a table in the style of `GATES`, cited to EventTbl03:
   ```python
   # Seals a room's switch opens (EventTbl03 room 3: switch 100 → flag 32 → door 1's seal 200, blue).
   # (mission, room, switch set, seal set, door's next room, colour 0 red / 1 blue / 2 purple)
   SWITCH_SEALS = [(3, "003", 100, 200, "004", 1)]
   ```
   Emit:
   ```c
   typedef struct { short mission, room, next_number, colour;
                    float hit[4];                 /* world centre x y z, radius */
                    float box[7];                 /* seal collider: centre xyz, half xyz, yaw */
                    float board[5];               /* centre x, z, half width, bottom y, top y (plane at box face) */
                    float disc[4]; } RoomSwitchSeal;   /* switch pos x y z, yaw (for the stand-in) */
   #define SWITCH_CHARGE 300.0f
   #define SWITCH_FLAMES 560.0f
   #define SWITCH_PER_FLAME 70.0f
   #define SWITCH_WINDOW 120.0f
   #define SWITCH_DRAIN 2.0f            /* per 60 Hz frame */
   ```
   The world values for room 003 are hit (1660, 140, 1950, 60), box centre (2300, 120, 1691) half (200, 180, 50),
   board x 2145..2455, y −98..324 at z 1741.
3. Leave the Shotgun as it is for now. Hiding set 300 after pickup needs a stage tag for it: `story_tag` covers numbers
   100..227 only, and 300 falls outside. That is a separate feature.

### Runtime: `psp/testroom/main.c` and `stage.c`
Model it on 103's lift (`lift_index`, `lift_on`, `mission_gimmicks_draw`, `mission_update`):

- **State:**
  ```c
  static int seal_index = -1;            /* this room's RoomSwitchSeal */
  static float sw_charge, sw_wing, sw_flames, sw_window, sw_spin;
  static float seal_alpha;               /* 0..1, fades over 32 frames */
  static unsigned seal_open_bits;        /* per mission, like the game's counter 1 = 2; cleared where rooms_cleared is */
  #define SEAL_TAG 255                   /* a stage_add_box tag that no stage triangle uses */
  ```
- **`enter_room`:** after `breakables_enter`, find the room's entry for this mission. Then:
  - if it is not open: zero the charge, `stage_add_box(box..., SEAL_TAG)`, `seal_alpha = 1`;
  - if it is open: charge 300, flames 560, no box, `seal_alpha = 0`.

  The game also fades the seal in on the first visit (a cutscene and tip 8). The port can simply start with it up, or
  fade it in over 32 frames.
- **Hits:** in `breakable_hit` (already `combat_hit_fn`, melee only, so guns stay out as in the game), add the switch
  test:
  - same reach and facing test, against the sphere: `d ≤ reach + 60`, `|dy| < 60 + 100`;
  - `amount = (HIT_LIGHT || HIT_AIR) ? 42 : 84`;
  - charge < 300 → `charge = min(charge + amount, 301)`, and if it is over 100, `wing = min(wing + amount, 201)`.
    Otherwise `flames = min(flames + amount, 561)`;
  - `sw_window = 120`, `fx_hit_spark`. No shot or Devil Trigger scaling (open questions 1 and 2).
- **`mission_update`**, with `f = dt * 60 * (game_turbo ? TURBO_SPEED : 1)`:
  - not lit:
    - `sw_window -= f`;
    - if it has run out, let charge and wing run down by `2f`, and let the flames run down too while charge ≤ 100;
    - `sw_spin += 60 * charge / 300 * f`;
    - if charge ≥ 300 and flames ≥ 560: lit, `seal_open_bits |= 1 << seal_index`, `stage_hide_group(SEAL_TAG)`
      (the collider goes at the start of the fade, as in `FUN_0031d420`), start the fade;
  - when it opens, optionally hold Dante's input for about 1.5 s with the camera on the seal, as the event does
    (30 + cam + 60 frames);
  - `seal_alpha -= f / 32` down to 0.
- **Door:** in `door_gated`, return 1 for a `RoomSwitchSeal` door (`next_number`) in this mission and room while its
  bit isn't set. The box already stops Dante, so this is a belt-and-braces check, the same as the door flag 33 in the
  game.
- **Draw** (stand-ins, in `mission_gimmicks_draw`, until SCM/EFM gimmick models are drawn):
  - switch: `draw_box` hub plus a thin disc turned by `sw_spin`, and 8 small cubes on a radius of 108 every 45° in the
    disc's plane (x = 1688). Red (0xff3030ff in ABGR) when `flames ≥ 70(i+1)`, dark otherwise;
  - seal: two textureless triangles over the board rectangle (z = 1741), colour RGB (0x30, 0x40, 0x80) with
    `alpha = 0.5 * seal_alpha`, blending on, depth write off;
  - later: draw model 20 with nodes 1 and 2 turned and blades 3–10 pushed out by 23, and model 17 with texture 14 tinted
    0x40804030. `prepare_stage.py` already converts PNST SCMs. It would just stop skipping SWITCH and SEAL, and tag them.
- **Room 103:** its lift switch is the same `紋章スイッチ` class (`CStageSetSwitch`, model 5, flag 32). Once this charge
  model exists, 103's "strike it or press O" switch should use it too. 103's script resets flag 32 if the lift didn't
  rise (flags 33/34), which this model handles.

### Test in PPSSPP (a scripted run, real clock)
Write a `CMP_SCRIPT.TXT` for the `cmp` focus scene (`tools/test_focus.ps1 -Name cmp`). Start from
`build/hdref/pp/bar_out.txt`, which already boots Mission 3 into room 3:
```
frames 1500
turbo 1
realdt 1
menu 1
movie 20
startroom 3 0
mkey cross 30
mkey right 60
mkey right 65
mkey cross 110
mkey cross 160
walkto 2300 1800 20 140        # up to the seal (it must stop him at z ≈ 1741 + his radius)
psp_shot 150                   # blue seal up, Dante held at its face
walkto 2000 1950 160 260       # to the dart machines
triangle 270 271               # break the three (vital 50: one hit each), turning between them
...
walkto 1790 1950 330 420       # into the alcove, facing the switch (x 1660)
triangle 430 431  ... every 12 frames to 700   # about 22 light hits
psp_shot 520                   # spinning, a few flames
psp_shot 710                   # all 8 flames, seal fading
walkto 2300 1500 760 900       # through where the seal was
walkto 2300 1300 900 960
circle 970 972                 # door 1 → Love Planet (room 004 loads)
psp_shot 1100
```
Add a one-line log each 10 frames (room, charge, flames, lit, seal box present, Dante x/z) to a `BAR_LOG.TXT`, and check:

1. Before it is lit, Dante's z never goes below about 1741 + capsule radius in the corridor x 2100..2500.
2. Flames light in order, every 70 points.
3. A second run that stops at 4 flames for 5 s: the charge drops to 100 after about 2 s + 100 frames, then one flame goes
   out every 35 frames.
4. Shooting the switch (square) changes nothing.
5. Leaving and re-entering 003 after it opens: the switch is lit and there is no wall.

Per the testing rules, this runs in PPSSPP only. `startroom` is an entry, not a mid-run teleport. For the strict run,
walk in from 002 instead.

---

## 7. Open questions
1. **Damage per hit.** The hit record's +4 for Rebellion, and player+0x2a18. They decide whether it takes ≈ 22 hits.
   To check in PCSX2 or the HD Collection: count Combo I hits from cold to the eighth flame, and watch the switch
   object's +0x844/+0x84c.
2. **`hit->byte[2]`.** Is it melee against guns, or something else (Devil Trigger, Quicksilver)? Shoot the switch in the
   reference to check.
3. **Seal collider layout.** The +0xfa0 record's reading (centre, then half sizes) is inferred. The 400 × 360 × 100 wall
   fits the corridor, but `FUN_002942e0`'s shape kinds 4 and 6 aren't decoded.
4. **The hand.** Does it push or hurt Dante (the +0x1000 radius-200 record, `FUN_0031cb50`/`FUN_0031cff0`)?
5. **Event cameras and timings.** CAM cuts 0–4 aren't decoded, so the lock and unlock cutscene lengths are unknown.
6. **Texts.** Messages 10003/10004/10005/10006 and tips 8/9/10 weren't looked up (10004 is from the walkthrough).
7. **The +0x10 time step under Turbo** (the port assumes the game step × 1.2).
8. **Op 0x29.** The on-enter block also hides the Shotgun when op 0x29's player check is true. The decoder's name for
   it ("playing as Vergil") is a guess.
