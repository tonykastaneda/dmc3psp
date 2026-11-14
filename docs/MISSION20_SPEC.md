# Mission 20 implementation

Source: the player's SLUS_213.61 disc. Local event listing and room census are under
`build/mission20_re`; no disc data or decompiled code is committed. See `GAME_FLOW.md` for the
EventTbl20 route and `VERGIL_SPEC.md` for the additive final Vergil variant and fidelity limits.

The mission starts in room 411 at native start 0, runs group 0 (one 10000-HP Vergil, variant 2),
and automatically enters room 012 start 0 after his defeat. This replaces demos 57/58. The ending
street runs its native Easy/Normal group 0, omitting EST 55, variant 1: that record is Lady as an
ally, and spawning the existing hostile Lady boss would prevent the ending. Lady's friendly AI,
ending credits, scoring, dialogue and cinematic cameras are not implemented.

The street lasts 13920 native frames (232 seconds): the port treats EventTbl20's undecoded op 8c
argument 13620 as a delay, followed by the explicit 300-frame wait. This interpretation and the
omitted op 7e effects are approximations. Enemy waves continue under the ordinary script runner;
Mission Clear occurs when the timer expires, independently of how many enemies Dante defeats.
The dummy exits in both rooms are disabled. Mission 20 and its native banner are available in the
mission menu; sparse mission indexing keeps unported mission slots disabled.

The final fight reuses `vergil_boss.c`: native Force Edge model/texture, native motion scripts,
rank-0 normal/DT move weights, guard probability and health. The additional attack effects,
DT activation rule and weapon-mode handling remain approximations described in the Vergil spec.

Verification: `tools/tests/run_vergil_variants.py` covers M7 isolation, M13 regression and M20 HP,
DT, Force Edge follow-up and spatial Judgment Cut hit. The end-to-end PPSSPP scene is
`tools/tests/mission20_clear.scene`; its result is recorded below.

Test scope: automatic positioning keeps Dante near Vergil, ordinary shotgun hits remove the boss's
full native health, and scripted minion clears isolate the ending timer. The shared testroom engine
floors Dante's health at 1; this run does not establish survival difficulty or combat balance.

PPSSPP `pM_m20_clear`, Normal, completed at frame 15425 (354 seconds wall time). The boss started
with native 10000 HP; ordinary shotgun traces defeated him at frame 8464. The log includes DT
entry and native Force Edge follow-up action 65. Room 012 followed automatically; no scripted
room transition or direct boss-health modification is used. The timer reached exactly 13920
native frames, then the normal Mission Clear menu appeared.

`M20_CLEAR.TXT`: `mission=20 room=12 boss_defeated=1 ending_frames=13920 variant=2 hp=0`.
Inspected captures: `analysis/testroom/pM_m20_clear_1000.png`, `_3000.png` (arena/combat),
`_9000.png`, `_15000.png` (ending street), and `docs/screenshots/mission20_clear.png`.
`docs/screenshots/mission20_vergil.png` retains the arena capture. Build and the combined Vergil
host checks pass against master f26ebc4. The Easy ending group was decoded but this complete
PPSSPP route check used Normal.
