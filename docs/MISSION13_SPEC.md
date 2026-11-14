# Mission 13 evidence and port limits

EventTbl13 (GDATA 251) was decoded under the hdref lock to local `build/mission13_re`. GAME_FLOW.md records
its actual room/group list. Room 234's EST [0,1] selects CEm035 variant 1. No generated disc data or
Ghidra output is committed. Vergil's additive variant is detailed in VERGIL_SPEC.md; M7 remains variant 0.

## Native crystal-light puzzle

CStageSetCrystal typeinfo 5fb6c0, vtable 60d5a0. The `no 1` in room 233 selects the pointer at 5bef44,
layout 5be9d0. Each 8-byte record is {kind,direction,grid_x,grid_z}; kind 4 terminates. There are 16 nodes:
emitter, two receivers, one cover and twelve relays. 41f460 instantiates them; 4206c0 places them at
(1700 + grid_x*200, 164, 299 + grid_z*200), facing direction*45 degrees. 41fbf0 walks the eight grid
directions, skips broken nodes, stops at a cover/emitter/loop and succeeds at a receiver. 41fa80 calls
that walker; EventTbl13's flag 34 then opens STAY 101 and persists puzzle flag 1.

`prepare_mission13.py` exports the native four SCM parts from st233's PNST (relay 0, emitter 10,
receiver 20, cover 30), the crest switch 60, their used textures and the ELF grid. `mission13.c` independently
implements the grid walker and draws those native parts; swords break relays/cover and the beam reroutes.
The crest switch restores the grid. One valid solution breaks node indices 8,11,3,14 (zero-based).
The beam route becomes 0→13→9→7→15→2. The key item 51 only appears once a receiver is lit; use it in
232's native area 0 to open the door into the Vergil arena. The seal disappears rather than animating
its authored opening. Durability (one contact), switch charging (120 damage), circular body push-out,
beam glow and inactive model poses are approximations. The authored crystal hit callbacks/particles
and cutscene cameras are not reproduced.

241's return from 233 runs its native group 1 via a separate local script reader; initial entry from
230 runs group 0 through the regular room system. Return-wave waits keep the doors sealed, including
when no enemy is alive during a timed gap. Other side-route encounters are not invented from unrelated
EST groups. The 228→218 rear passage is disabled as the event script specifies.

## Damned Bishop

EST 37 is class 19; its native body is em017 (GDATA 127) chunk 12, an 8-bone chess model. Texture bank 0
is shared with the Pawn. Its native death clip is PAC 13 slot 1 (150 frames). Slot 0 has duplicate key
frames rejected by the existing MOT validator, so the living Bishop currently uses its rest pose.
`bishop.c` supplies a simple aimed spell with a 60-frame warning; moving outside 180 cm avoids it.
Spell timing, damage (700), health, rendering and AI are approximations; CEm019's original fireball,
healing and decision tables are not decoded. Model identity is native, not a substituted Seven Hell.

## Checks

Host checks: `run_vergil_variants.py`, `run_mission13_logic.py`, and `bishop_logic.c`. These cover M7
isolation, M13 native health/DT threshold and Beowulf chain, finite poses, the native beam solution,
switch reset, persistent gate, return script, aimed-spell avoidance and enemy death. PPSSPP route and
full-health boss validation are recorded after the end-to-end scene completes.

PPSSPP crystal check: `pM_m13_crystals` broke the native solution's four nodes through ordinary sword
contacts, lit the receiver and collected item 51. Its report: room=233, key=1, puzzle=1, broken=18696.
Frame 700 shows the beam reaching the opened gate (`docs/screenshots/mission13_crystals.png`).
