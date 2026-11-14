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

The independently written `dullahan.c` uses the native model and clips, frontal shield/rear weakness,
slow approach and a committed charge. Speeds, damage, health, reaction timing and attack volume are
port estimates, and its decision tree is simplified. It shares the existing enemy body's motion and
pose renderer through thin hooks. `tools/tests/dullahan_logic.c` checks frontal rejection, rear damage,
immortality and death. PPSSPP `pM_m10_dullahan_close`, frame 150/300, shows the native animated shield.

Mission registration includes the exact M10 groups, first room/start, clear room, native key-item
positions and use areas. Returning-to-209 conditional waves and moving bridge implementation are
still in progress. Full Mission Clear verification follows once these gates are complete.
