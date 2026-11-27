# Damned Chessmen

EST35..40 share CEm017 (class17, GDATA127). Their dispatch in406730 is Pawn4383e0,
Knight4349e0, Bishop435510, Rook436220, Queen437510, King4341a0.
The separate CEm017Master has vtable60e0a8, update43f030, initializer43f090,
and live scheduler43f160. This is a continuous arena fight, not a legal-move chess
engine: the scheduler has no square occupancy table. Each piece owns movement,
turning, floor/wall probes and its action scripts. Board encounters are room401
and Bloody Palace441; the same actor logic runs in other rooms.

## Shared controller and interface

`psp/testroom/chess.h` documents snapshots and commands. `enemy_chess_actor()`
returns the stable record for the current enemy, including during its spawn,
update or hit callback. Set `est` to35..40 at spawn; update `waiting`, `busy`,
`waited`, `alternate_ready`, `swap_ready`, `swap_blocked` from the actor brain.
Consume `command` by clearing it; command30 supplies a destination, and `kill`
requests the native King-death cascade. enemy.c samples live position, HP loss
and actor lifetime and runs the controller once before actor updates. Room entry
resets it. Queen/King implementations should use this interface, without adding
another scheduler or reordering enemy kinds.

43f160 scans live pieces and counts those marked busy (+110 bit40000000). The
capacity is1 below dynamic difficulty700,2 below1000,3 above. Waiters (+110
bit20000000) compete by elapsed ready time (+470). The four-entry insertion
repeats the displaced entry into later slots (verified native instructions
43f5e8..43f60c); it is preserved, rather than silently replaced by a sort.
Granted pieces receive command10 at +488. After a recent HP decrease, at most
two grants become command15 using40ab60's per-piece/rank probability and clearing
+3bc bit40000000. HP-loss severity weights are1,8,9,10,11,12 by piece; severity
above7 commands Queen20. If the highest-priority injured non-Rook is the King,
an eligible idle Rook exchanges destinations with him and both receive30.
Once a King has existed and disappears, the first remaining actor gets the death
flag, then another every30 updates. Counters here advance per update, as native.

409690 initializes action waits from per-piece rows (52-byte stride), base plus
random spread for each of four ranks. 408320 counts the wait down by
`3000/max(distance,1000)` per native frame, doubled by +3bc bit800. The port exports
all six tables through tools/prepare_chess.py into ignored chess_runtime.h.
Pawns now wait for this scheduler instead of a fitted90-frame turn. The port's
existing ordinary AI uses rank0, so enemy.c currently supplies dynamic difficulty0
(one concurrent action); higher-capacity boundaries are implemented/tested but
not connected to a dynamic difficulty service. Pawn promotion/summoning and
alternate-readiness flags still require their piece code.

Host validation: `python tools/tests/run_chess_logic.py` checks capacity boundaries,
busy slots, waiting priority, Queen response, paired castle destinations and the
King death cascade.

PPSSPP master smoke check: Mission18 room401, `chess_master_board`,300frames,
inspected `analysis/testroom/chess_master_board_150.png` and `_270.png`. The room
loads its16 actors and Pawn turns run through the shared scheduler. These distant
views verify integration/room loading; they do not validate the unported pieces.

## Knight (EST36)

Native dispatch4349e0 uses hop434de0 (states2/4), close strike434fc0 (3),
plunge435170 (8) and recovery435370 (9). Pack127 model7 has9 bones; bank8
has four authored clips (120,45,1,70 frames), bank9 scripts, bank10 caps,
bank11 sphere shapes. `knight.inc` uses the existing script player and posed
attack-sphere test rather than timing a fitted hit. Normal granted choice is
state2 (434bb0's idle row5c0870); alternate15 uses4 with3..6 repetitions.

Hop parameters5c2c30 at rank0: vertical60, forward10, vertical acceleration-3,
forward acceleration0.1 per native frame; turn rates are degrees/frame from the
52-byte action rows. At the apex it waits30 frames, then plunges from vertical-5
with gravity-3.2. Landing starts recovery9; that clip turns only within(28,58),
and applies authored root movement. Within300 and80 degrees either side, the
close-strike trigger has a120-frame cooldown, followed by a30-frame windup.
The script selects attack1 at motion55, cap3 for12 frames: bone4 sphere centred
(0,-10,100),radius60. Attack0 is the plunge's indefinite root sphere,radius60.
Table5c0770 records are1000/reaction500/kind0 and700/reaction350/kind0.
Health is1000 (damage-profile5c07f0+2c), converted to the port's ordinary hit units.

The common death406d50 starts shatter V1fb and ground effect20c and removes the
actor after60 frames. Shared `NFX_CHESS=25` exports all30 V timelines from pack127
chunk1 for this and the other pieces. Knight plunge usesV396, strikeV241.
Hit reception still uses the ordinary port's light/heavy/pistol units and does
not implement the native full damage/reaction component; dust/landing shake and
shatter attachment matrices remain approximate.

PPSSPP final checks: Mission18 room401, `chess_knight_checked_jump` and
`chess_knight_checked_strike`,400frames each. Inspected jump100/115 and
strike110/160 PNGs under analysis/testroom. The trace confirms motion741..744
(native cn00..03), states2->8->9->0, and the close attack's700-damage hit.
Forced heavy hits at300 exercise the death path; final live count is0. Existing
Bishop spell stand-ins and Queen/King fallbacks also participate in these scenes,
so total HUD damage is not attributed entirely to the Knight.
