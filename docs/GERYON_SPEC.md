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
| [12] [16] | Their scripts (mode-0 tables), bank 0 by action: action *n* plays motion *n* for 1-18; 0 plays 0 then 1, 0x13 motion 9 for a frame, 0x14 motion 15 (both bodies) |
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
- **Motions:** actions play `gh`*n* and `gc`*n* by index, as their scripts do. Think is action 3 (`4bfb20` /
  `4c0c50`): motion 3 to its end (144 frames), then the pick; it doesn't turn (`42e580` is root motion); on the
  ground the spheres' check (beyond 800 from the centre, facing it within 45°) comes as it starts. The port's death
  plays gh19 under its own action 0x13 [the dying update isn't read]. Horse gallops 10/11 now use decoded
  script window timing (below), not the earlier twelve-motion-loop substitute. The full remaining horse/cart
  scripts are not yet executed.
- **Phase 0:** think → the charge (every third) or the cast (0x12 → 8: the spheres) → the charge. The charge runs
  the whole bridge line (400 to 5600) or to 1000 past Dante, then 7 turns him round. The shockwave is his answer to
  being hurt (`385130`, the reaction-code handler): his damage component's gauges (param block `0x5d6bf0`: stun
  800 at +0x30, flinch 500 at +0x34; shares Rebellion 100, guns 20 / 35; refilled after 180 frames without a blow)
  send codes; a stun code (2-5, 7) starts 0xd → 0xe at once, a flinch code plays 0x14 (motion 15) the first time and
  the shockwave the second (+0x978); none while +0x8fc has 0x4000000 [not read]. `384d20` also starts 0xd on a
  blow of kind 3 / 4 / 5 / 7 with its argument 6 [not read]. Dante's blows take 20 / 30 (light) or 60 / 60 (heavy)
  from the gauges [estimates: his records aren't decoded].
- **Phase 1:** as decoded: gallop 0x10 / 0x11 → turn 5 / 6 (`4c1250` / `4c1380`: 60 frames turning 382 units a
  frame each way at 25 a frame; +0x97a 2 / 1, flag 0x800) → the straight run 4 (`4c0fd0`: 65 frames, 30 with 0x800,
  10 with 0x100; +0x97a cleared) → with 0x800 action 10 (`4c1e10`: its script, then half round) → the missiles 0xb →
  think; a plain run → the spheres 8; a run with 0x100 → a gallop by a coin. Think (as it starts, beyond 800 and
  facing the centre: the spheres; else the gallop +0x97a names, a coin when 0). The spheres → hidden 120 frames →
  back on the ring behind Dante → 9 (`4c1af0`, 0x100 and 0x200 set: half the time 0x12, the spheres again, else
  the 10-frame run). Not read: flag 0x80 (think → the run), 9's count +0x979 without 0x200. The missiles are two
  volleys of 4 [the volleys are the port's]. The missiles rise 30 frames and home at 18 a
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

## Gallop window follow-up

Handlers **4c2d90 / 4c2fe0** query `255af0(horse+2b20,0,0)` and immediately clear it with `255b20`.
On value 1 they decrement signed byte **+973**; at zero they clear flag 400 and select **5 / 6**, respectively.
The counter initializes to **40** (`4c0b70`, and the charge entry). It does not count completed arena laps.

Horse chunk [12] is a mode-0 grouped script: bank table at +610, motion groups at +6. Actions **10 / 11**
select motions **16 / 17**. Each starts the clip at its MOT loop start, blends eight frames, waits for motion
frame **29**, and raises **window 0 byte 0 = 1**. Then it restarts the same clip without blending, waits
for 29 and raises that window again; its branch repeats this second play. Both loop starts are 0. Thus normal
counter expiry takes **40 events / 1160 native frames**, not twelve motion loops. The exporter validates this
program and writes the initial/repeating intervals to local `boss_geryon_windows.h`; the runtime consumes
those intervals and aligns the displayed loop frame with the script clock.

There is also a decoded acceleration condition: **383b70 at 384160** shifts an eight-update contact history
at **+970**, setting bit 0 if collision record **+35fc** has flag **1000** and Dante is more than **300** above
the horse. Each window subtracts an extra **2** while that history is nonzero and the counter is **>5**.
Continuous high contact therefore expires in 16 events (464 frames). The port retains the exact history
and decrement rules, using its existing horse/cart push footprint for the contact input; that footprint
remains an approximation of the native cap record, rather than a newly invented detection radius.

`python tools/tests/run_geryon_windows.py` checks both directions, the first event boundary, all forty events,
the conditional decrement and both native turn actions. Existing movement, shockwave and missile stand-ins
remain separate from this change.

PPSSPP Mission 12 room 228: boss_geryon_windows_live (nodemo, twenty heavy hits at frame 30)
checks the bridge-to-ground fight and consumes exactly forty logged events, first at frame126 and
last at691, then native turn6. Readable visual checks use an inner-arena camera:
analysis/testroom/boss_geryon_inner_camera_430.png, _450.png and _470.png show the horse and cart
following the ring in successive gallop poses. Local trace: build/boss_decode/geryon_live_log.txt.
