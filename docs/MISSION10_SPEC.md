# Mission 10 implementation and evidence

EventTbl10 is GDATA 248. Decode and census: local `build/mission10_re`; numeric route/group facts are in
GAME_FLOW.md. No decompiled code or generated game data is checked in. Start 209/2; clear at 211 area 0,
via demo 26. There is no boss encounter.

Dullahan: EST 30 maps through the byte pairs at 599690 to class 12, pack 7. Pack-list 58e740 selects
GDATA 123, em012.pac. Its main MOD is chunk 0 (28 bones), textures chunk 1; the exporter normalizes that
order. Native motions in PACs 6-9 are exported as duCNN. The main shield/body is rendered; the separate
EFM core layer and scrolling textures are not yet reproduced. CEm012 RTTI is 5f9a98, vtable 60a640.
Ghidra evidence: 3ccbc0 plan dispatch, 3cd9a0 action dispatch (1-11/13), 3cfea0 hurt-state selector,
3ce320/3ce5d0 slow tracking and committed approach, and 3cb770 setup. This was read in the existing
Ghidra 12.2 project under its exclusive lock, with exports confined to local build output.

`dullahan.c` follows the script dispatch at `003cd860`: tracking uses 0:0, charging 0:1, striking 3:0, and death
2:1. The charge winds up for 25 frames, starts at 13 cm per frame, then loses 0.17 per frame at rank 0
(`003ceb50`, `0x5b8e80` / `0x5b8ea0`). Strike 3:0 uses record 1 for five frames from frame 15
(600 damage, `0x5cc7f0`). The decision tree and shield volume remain approximations. `tools/tests/dullahan_logic.c` checks frontal rejection, rear damage,
immortality and death. PPSSPP `pM_m10_dullahan_close`, frame 150/300, shows the native animated shield.

Mission registration includes the exact M10 groups, first room/start, clear room, native key-item
positions and use areas. A return from 206 to 209 runs group 2; the initial visit holds that script. The less common 210-return
group-1 variant is not selected yet.

Lake/bridge gimmicks live in `mission10.c`. Using the Stone Mask removes native collision set 101 and
raises the lake's SCM bridge by 829.36 cm over 600 frames (the endpoint delta in native st206's motion).
The Neo-Generator automatically reacts in 201 EVE area 1, returns Dante to native start 0, and rotates
the two major native bridge arms about their authored pivots (1250,3750) / (3750,1250). Control area 0
then toggles the bridge. Native arms are tagged 240/241, lake bridge 242; the stage's small generic
rigid-group API leaves other missions' geometry unchanged. Bridge rotation is a 45-degree rigid
approximation over 600 frames; gears, articulated arm details and authored easing are not sampled.
Their deck collision uses an approximate diagonal strip / lake ramp, rather than animated HITS.
Optional 206 item 31, the story cameras and the bridge's long helper-thread choreography are omitted.

Clear requires entering the actual 211 EVE area 0, not just entering the room. The first combined
PPSSPP `pM_m10_route` run recorded `M10_CLEAR.TXT`: mission=10 mask=2 generator=2 room=211. It scripts
placements and minion clears to test item gates and event connectivity, then walks the powered bridge
using normal movement (frame 2500 shows Dante at 211's door). Host tests in `mission10_logic.c` check
conditional cave entry, powered deck collision, toggle/reset, and the native clear-area polygon.

Final regression: `pM_m10_verified` repeats the complete route after adding the Neo-Generator arrival
warp and the repeatable bridge control. A newly created `M10_CLEAR.TXT` confirms mask=2, generator=2,
room=211. Host shield/death and bridge-toggle/clear-polygon tests pass. Captures are checked into
`docs/screenshots/mission10_clear.png` and `mission10_dullahan.png`. Native group data and key-item
placements were decoded under the requested hdref lock.
