# Bloody Palace (PS2 Special Edition)

Source: the owner's US SE disc, `EventTbl21.bin` (GDATA.AFS 259), its stage/EST data, the local Ghidra exports,
and PCSX2. Addresses below are PS2 virtual addresses; event offsets are relative to EventTbl21.
No disc data or decompiled source belongs in this repository.

## Entry, floors and portals

The Mission Select row starts the mode directly through its own briefing. The event starts in room **423**, start 0
(`0x30`). **Level 0** is the first floor, observed in PCSX2. The event object's signed halfword **+0x79c** is the
floor, **+0x79e** the previous floor; initialization clears both (`0x1ee730`). Opcode **0x8a** adds its argument,
copies the old floor and clamps to **9999** (`0x20be20`, branch at `0x20e168`). Opcode **0x2a** switches on
`min(floor + argument, 9999)` (`0x20e510`, `0x20f1ec`).

Ordinary rooms 423–432 have three portals (door indices 0, 1, 2): **+1 / +10 / +100**, respectively
(`0xc0`, `0xe4`, `0x108` in room 423). The first entry skips the increment. The arrival door/start selector
chooses the increment on subsequent entries. The +1 and +10 choices also spawn pickups using opcode 0x8b,
at (3500,768,3520), types 12 and 15 (`0xc8`, `0xec`); +100 gives neither. Pickup meanings need a separate check.
All three exits stay disabled until enemies are cleared, then the script enables them at 60-frame intervals
after an initial 60-frame wait (`0x6c4`–`0x760`).

Ordinary arena room = **423 + floor / 1000** (0–999 => 423, 1000–1999 => 424, through 9000–9998 => 432).
The script retargets each portal when its increment crosses that room's thousand boundary (`0x5c0`–`0x634`).
At a prospective floor >=9999, every ordinary portal instead targets **444**, Vergil's final arena (`0x63c`–`0x6b0`).
The room's on-enter counter increments; at counter >=10, the next portals all target a chosen boss room
(`0x118`–`0x5b8`). A boss resets counter 0 to zero. Thus boss visits are based on rooms played, not merely
multiples of 100 in the displayed floor. A boss exit returns to the ordinary arena for the current thousand
without another floor increment. Boss selection uses opcode 0x2b: random modulo 17, scanning past previously
used flags; without game/save bit 0x800 it chooses index 0 (`0x20e510`, case 0x2b).

## Encounters and boss rooms

Rooms 423–432 each contain **20 EST groups**. Ordinary entry uses spawn_group **-1** (`0x6b4` etc.).
`0x2113e0` chooses random modulo 20, scanning past bits in event **+0x798**, resets that mask when exhausted,
and stores the group at **+0x7a0** (`0x211508`). Spawn-group -1 marks the group used, resets the mask across a
thousand boundary, and spawns the selected group (`0x20be20`, spawn branch). EST table selection uses the
game difficulty byte at **0x1d0d73c** (`0x2fb820`). EST opcode 12 normally sets rank; in Palace it instead sets
rank to **floor / 1000 + 1** (`0x2fb060`, `0x2fb530`). Port rank/difficulty scaling needs an explicit stand-in
unless implemented separately from the story difficulty.

| Boss index | Room | EST group | Encounter (EST class) |
| --- | --- | --- | --- |
| 0 | 433 | 0 | Cerberus (43) |
| 1 | 434 | 0 | Gigapede (41) |
| 2 | 435 | 0 | Agni & Rudra (45) |
| 3 | 436 | 0 | Nevan (49) |
| 4 | 437 | 0 | Beowulf (51) |
| 5 | 438 | 0 | Geryon (50), enter at start 2 |
| 6 | 439 | 0 | Doppelganger (52) |
| 7 | 440 | 0 | Blood-goyles (25) |
| 8 | 441 | 0 | Chess pieces (35–40) |
| 9 | 442 | 0 | Vergil first encounter (58) |
| 10 | 443 | 0 | Vergil second encounter (58) |
| 11 | 444 | 0 | Vergil final encounter (58) |
| 12 | 445 | 0 | Lady (55) |
| 13 | 446 | 1 | Arkham (53) |
| 14 | 422 | 0 | Jester first encounter (62) |
| 15 | 448 | 1 | Jester second encounter (62) |
| 16 | 449 | 2 | Jester third encounter (62) |

The table is encoded by the three door_retarget records in each case (`0x134`–`0x5b8`). Room 447 is absent
from the disc. Stage slot = **736 + 4*(room-400)** through 446; 448 uses **924**, 449 **928**; data is slot+1.
Boss EST group numbers come from their EventTbl21 ROOM blocks (`0x4848` through `0x78b4`).
At floor 9999, clearing room 444 waits 180 frames then executes mission_clear (`0x6b68`–`0x6b80`).

## Timer and reference checks

**No survival countdown or kill-time bonus is evidenced in this PS2 mode.** EventTbl21 contains no counter/timer
HUD setup of the kind used by the timed story and secret missions. PCSX2 shows Level 0 on entry and the regular
HP/DT/style HUD during the fight, without a countdown. Implementing the task's proposed kill-time mechanic
would be an unsupported change to the PS2 rules. An elapsed-time statistic, if added later, must not become a
survival limit without evidence.

Local reference captures: `build/bloody_palace/ps2_select.png` (unlocked row), `ps2_bp_start.png` (briefing),
`ps2_floor.png` (Level 0), `ps2_floor_wait.png` (first combat, three Pride), `ps2_clear.png` (combat HUD).
These are local reference images, not committed. PCSX2 was closed and its shared lock released after capture.

## Port implementation and checks

`bloody_palace.c/.h` owns floor/group selection, portal readiness and routing. Mission 21 reuses the mission
wave runner, collision, stage loading and enemy classes; generated `bloody_palace_runtime.h` contains EST
groups from the owner's disc and stays local. It uses Normal EST columns; rank-based scaling remains a stand-in.
The Mission Select row launches directly. Portal positions and boss exits come from each room's authored doors.
Readiness follows the three 60-frame ordinary portal reveals; boss exit readiness is 180 frames after clear
[approximation of the original event camera sequence]. The final clear waits 180 frames.

`camera.c` starts ordinary Palace arena sweeps at Dante's head because the default (2500,200,2500) lies below
the arena floor at approximately y=514. This is an explicit fallback, not a recovered PS2 camera initialization.
The level label and portal legend use the menu font; portal glows and missing arrival pickups are in StandIns.md.

Tests: `python tools/tests/run_bloody_palace_logic.py` uses the actual generated encounter table and checks portal
delays, repeated-room encounter reset, the thousand boundary, all seventeen boss routes/returns, and the 9999 cap.
PPSSPP test controls: `bpstart floor visits boss group` (-1 chooses boss/group randomly); `bpwarp frame portal`
places Dante at the authored portal and presses O, without bypassing fight/readiness checks. Combine with
`killevery 30` and `fastwaves 1`. These controls are used only in scripted test mode.

Floor-flow scene: `bp_flow_camera`, screenshots `analysis/testroom/bp_flow_camera_30.png`, `_250.png`,
`_430.png`, `_740.png`; +1, +10, +100 takes Level 0 to 111 with the room encounter restarting each time.
The initial `bp_boundary` check confirmed 990 +10 routes from room 423 to 424; repeat after camera correction.
