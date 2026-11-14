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
rank-0 normal/DT move weights, guard probability and health. The follow-up fidelity pass replaces the DT activation/timers and form, adds native dive chains and
Round Trip release events, and uses the low-health move tables. Effect collision, cloth drivers and
some weapon-mode handling remain approximations described in the Vergil spec.

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

Final integration: merged master 98d22f2 (M19), rebuilt successfully, reran all Vergil variant host
checks, and ran `pM_m20_merged` for 900 frames. Inspected
`analysis/testroom/pM_m20_merged_600.png`: the final arena and variant remain functional after the
shared enemy/room additions. The full clear above precedes this unrelated M19 integration.

## Vergil fidelity follow-up

Against master c73efb6: native M20 health latch below 39%, maintained DT's 60-frame entry / 600-frame
active timer / 2700-frame human cooldown, 1.2x DT animation, native DT body and attachments, native
Force Edge attachment records, event-driven Round Trip, single/four-dive chains, three Judgment
Cuts and late random/ring area-cut schedules replace the first port's stand-ins. Late cut attacks
also use their native temporary DT form. See `VERGIL_SPEC.md` for source addresses and remaining limits.

`run_vergil_variants.py` passes M7 isolation and M13 regression, exact M20 threshold boundaries,
DT expiry at a state boundary, cooldown/re-entry, finite native DT poses, four completed storm
landings, Round Trip release/return, and area-cut recovery with paused temporary-DT cooldown.

PPSSPP `pM_m20_final_fidelity` (Normal) reached Mission Clear at frame **13366**, 293.8 seconds wall
time, on the final combat build. `M20_CLEAR.TXT` again records
`mission=20 room=12 boss_defeated=1 ending_frames=13920 variant=2 hp=0`.
The trace records first late-phase state at HP 3809, DT request at frame 3538, native DT expiry at
3948 with cooldown 2700, two random-cut patterns plus one ring pattern (51 cut emissions), and
boss HP 0 at frame 6405. Near-target shotgun automation did not select Round Trip in this run;
its native event release, flight and return are covered by the host fixture.

Inspected M20 frames 3700/3800 show the native DT form; tracked capture:
`docs/screenshots/mission20_native_dt.png`. The clear is retained as
`docs/screenshots/mission20_fidelity_clear.png`. Scripted positioning/minion-clear and the shared
Dante-health floor still apply, so this verifies progression and behavior rather than balance.

Remaining estimates include cloth drivers/rest attachments, healing rate, full native projectile/
cut-effect collision and deflection, conservative arena collision, weapon draw-mode selection,
and some Force Edge chain movement/recovery details. The exact native 3% DT visual scale is not
applied. These limits do not affect the M7/M13 variant branches.

Final boot regression, 600 frames each: `pM_m7_final_fidelity` (144) and
`pM_m13_final_fidelity` (234), both with `onvergil 30`; inspected their frame-300 captures. M13
reports variant 1, HP 8000 and DT off. After merging master **52aab4b** (M18), the combined build
passed, then `pM_m18_fidelity_regress` booted native M18 start 400 and `pM_m18_rerun_regress` booted
412's Cerberus rerun. Their frame-300 captures were inspected. These M18 checks are boot/encounter
smokes, not a new full M18 clear. The Vergil implementation is unchanged by that integration.
`pM_m20_merged_fidelity` also completed 600 frames in room 411 after that merge; its frame-300
capture was inspected. Build output: 24,795,303-byte EBOOT. No hardware performance claim is made.
