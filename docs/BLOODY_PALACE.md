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
at (3500,768,3520), types 12 and 15 (`0xc8`, `0xec`); +100 gives neither. Type 12 is a large green orb;
type 15 is a large white orb. `0x2f3750` applies **10000 HP** (`0x5aa560`), or **300 red orbs** when HP is full
(`0x5aa570`), and **5000 raw DT / five runes** (`0x5aa580`), respectively. Each boss room has its own authored
arrival pickup position. The generator takes the first type 12/15 spawn in each ROOM block; Lady's later defeat
pickups are separate and remain omitted.
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
| 7 | 440 | 0 | Hell Envies (25), the Leviathan-room wave |
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
Cerberus's room moves Dante to (2500,0,1700), keeping his yaw (op 0x5a at `0x481c`); this is applied on entry.
Nevan's room hides STAY 150 (op 0x36 at `0x53a0`), as the story's room 210 does; otherwise its white sheet covers
the floor. `gen_rooms.py` emits the matching Mission 21 visibility rule for room 436.
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
An additional reference pass cleared the initial encounter and captured the water portal
(`build/bloody_palace/ps2_portals_compare3.png`), then took the fire portal: PINE's event+0x79c changed **0 → 100**,
and the screen showed **Level 100** (`ps2_fire_warp.png`). The full save maps melee to Square. Its Vergil attack
multiplier was temporarily set from 1 to 1000 in runtime memory to speed clearing; no reference data was saved.
The PSP's recovered water effect is brighter/more opaque than this PS2 capture; original assets alone do not
recover the GS blend and every particle/rendering behavior. That remaining runtime approximation is documented.

## Port implementation and checks

`bloody_palace.c/.h` owns floor/group selection, portal readiness and routing. Mission 21 reuses the mission
wave runner, collision, stage loading and enemy classes; generated `bloody_palace_runtime.h` contains EST
groups from the owner's disc and stays local. It uses Normal EST columns; rank-based scaling remains a stand-in.
The Mission Select row launches directly. Portal positions and boss exits come from each room's authored doors.
Readiness follows the three 60-frame ordinary portal reveals; boss exit readiness is 180 frames after clear
[approximation of the original event camera sequence]. The final clear waits 180 frames.

`camera.c` starts ordinary Palace arena sweeps at Dante's head because the default (2500,200,2500) lies below
the arena floor at approximately y=514. This is an explicit fallback, not a recovered PS2 camera initialization.
The level label and portal legend use the menu font. Arena STAY 100/101/102 name **V965/V967/V964** at y=517:
water/lightning/fire for +1/+10/+100. Their effect bank is **GDATA 830**, shared by st423–432. Bank 19 in `nfx.c`
plays these through the native effect runtime; the generated closure has 23 V/G, 27 E, seven P, 17 textures,
and ten models, with no missing G children. `bp_native_portals_280.png` is the first PPSSPP visual check.
Arrival rewards use the event's positions and decoded recovery amounts; their mote visuals, simplified falling
and 120 cm collection radius are stand-ins. Taking a boss exit does not grant an additional arrival reward.

Tests: `python tools/tests/run_bloody_palace_logic.py` uses the actual generated encounter table and checks portal
delays, repeated-room encounter reset, the thousand boundary, all seventeen boss routes/returns, and the 9999 cap.
PPSSPP test controls: `bpstart floor visits boss group` (-1 chooses boss/group randomly); `bpwarp frame portal`
places Dante at the authored portal and presses O, without bypassing fight/readiness checks. Combine with
`killevery 30` and `fastwaves 1`. These controls are used only in scripted test mode.

Floor-flow scene: `bp_flow_camera`, screenshots `analysis/testroom/bp_flow_camera_30.png`, `_250.png`,
`_430.png`, `_740.png`; +1, +10, +100 takes Level 0 to 111 with the room encounter restarting each time.
`bp_boundary_verified` repeats the 990 +10 crossing with the corrected camera and collects its white reward:
`analysis/testroom/bp_boundary_verified_330.png`, `_400.png`; final state Level 1000, room 424, pickup consumed.

All seventeen boss rooms were run with `killevery`, then their authored exit was taken; each returned to room
423 at Level 500. Local logs: `build/bloody_palace/audit_batch1.txt`, `audit_batch2.txt`, `audit_batch3.txt` and
the per-room `audit_NNN.txt` files. Images: `analysis/testroom/bp_boss_NNN_60.png`, `_450.png`, `_550.png`;
`build/bloody_palace/boss_rooms_qa.png` is the contact sheet reviewed for room/exit visuals. Nevan's STAY fix
is checked separately in `bp_nevan_verified_150.png` with a live encounter and the white sheet absent.

`bp_final` starts at 9990, takes +100 (clamped to 9999), fights room 444's final Vergil and reaches Mission Clear:
`analysis/testroom/bp_final_315.png`, `bp_final_menu_clear.png`. Final telemetry is Level 9999, room 444,
clear=180, finished=1. `bp_rewards` takes +1 then +10 and collects each reward once; its green pickup at full HP
changes the visible orb count from 27 to 327 (`bp_rewards_350.png`, `_380.png`); final Level 11, pickup=0.
`bp_cerberus_verified` checks the decoded entry warp with a live Cerberus and return to Level 500
(`bp_cerberus_verified_60.png`, `_550.png`). `bp_story_regression` checks Mission 20 room 411 after integration
(`bp_story_regression_60.png`), with Palace inactive. Final build and the protected-credit check pass.

The effects merge preserves cl3's banks 7–13 and reserves 14–18 for its in-flight gun effects; Palace uses 19.
A full `-Regenerate` rebuild is checked in `bp_merge_verified2_280.png` and Mission 19's mirror shatter in
`bp_merge_m19_mirror_20.png` / `_35.png` (bank 11, V490/P113). Startup now reads the requested mode before
allocating its menu or test street: holding the unused 5.4 MiB street reserve caused the 10.5 MiB menu
allocation to fail after the larger merged effect banks. Test capture buffers are allocated afterward.

## Portal polish

The oversized white sheet remains with `nopfx 1` (`bp_polish_no_p_280.png`), identifying an E-layer problem.
E1515 is the water portal's 46-frame startup smoke; E +0x84 is zero there, while the cyclic column records
set it. A persistent STAY previously restarted every finite E. Palace now restarts only its cyclic records,
alpha-blends E1515, and clamps linear sampling inside each sprite tile with a half-texel inset. The authored
size is preserved (the native sprite /16 initial-size and /2048 growth units already matched).
The shared P first-draw colour expansion is cl3's identical fix from `2e3e16d`; a host regression checks a
transparent one-colour burst whose unused corners are bright. `bp_polish_smoke_fixed_280.png` shows the
columns without the repeated white sheet, at 58 FPS; the P pool drops no instances. Other banks keep their
existing E lifetime/blend policy. PS2 references: `polish_ps2_portals_later.png`, `_final.png` and the earlier
`ps2_portals_compare3.png`, all in local `build/bloody_palace`.
