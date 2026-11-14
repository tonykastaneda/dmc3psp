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
`tools/tests/mission20_clear.scene`; its result is recorded below when completed.
