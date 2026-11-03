# Dante moveset reverse engineering (Phase 0)

Goal: an authoritative motion -> move map for Dante's whole arsenal, taken from
the game's own code and data instead of reading moves off root motion and poses
by eye. This document records what is known so far and what is next.

## Ground movement

Implemented in `psp/testroom/player.c` (`locomotion_step`).

- **Stick reference** (`FUN_00254670`): `player+0x28e4` is recomputed from the
  camera (target - eye) every frame, so the stick always means its on-screen
  direction. The latched case (`player+0x28e8`, set by `FUN_00254990`) is only
  armed from `FUN_0021f500` on area entry; it releases at neutral or when the
  stick moves more than 0xaab (15 degrees).
- **Tilt** (`FUN_002e2f20`, `player+0x4a3a`): under 53 neutral, 53-112 walk,
  113 and up run. The test room assumes a 0-127 tilt scale; the scale itself
  wasn't confirmed from the pad code.
- **Table** (`player+0x2830`, Dante at `0x5a85c0`, per 60 fps frame): walk 3.5,
  run 8.5, turn 0x800 angle units, start turn 8 frames, run acceleration 6
  frames, full tilt held 4 frames before running. The second row (`player+0x28a7`,
  set from `FUN_0021f500`) raises run to 11.0755. The test room uses row 0 only.
- **States**: `FUN_00221ba0` start (walk toward the direction held on entry,
  turning evenly over 8 frames, motion 2-01), `FUN_00221d20` move (motion 2-01,
  then 2-02 or 2-51 if `player+0x28a0`), `FUN_002217a0` idle (2-00, long idle 2-03).
- **Lock-on**: none of these handlers read R1 or the target, so lock-on doesn't
  change walking or running. There is no strafe animation.
- **Not ported**: the body lean in `player+0x28ec`, row 1, 2-51, long idle. The
  stop transition and its blend length weren't traced; the test room stops at
  once and blends over 10 frames. Its camera follow (rate and angle limit) is
  its own tuning, not taken from the game's camera code.

## Executable (SLUS_213.61, DMC3 Special Edition)

- Analysed with the repo-local Ghidra project (`tools/run_ghidra.ps1
  -Addresses 'hex',...` decompiles functions into
  `analysis/ghidra/export/decompiled/`). Quote addresses in PowerShell:
  `2e3150` unquoted is parsed as a number (Infinity).
- `tools/se_vtables.py` finds RTTI type_info and vtables. GCC 2.9x layout:
  type_info = {name ptr, base type_info}; vtable = [type_info][this-offset]
  [methods...]; three vtables per class (offsets 0, -64, -160).

### Classes found (SE addresses)

| Class | Weapon | type_info | first vtable |
|---|---|---|---|
| CPlDante | Dante's action logic | 0x5f34f8 | 0x602650 |
| CPlWpSword | Rebellion | 0x5f3910 | 0x603460 |
| CPlWpNunchaku | Cerberus | 0x5f5e70 | 0x606380 |
| CPlWp2Sword | Agni & Rudra | 0x5f39f0 | 0x603720 |
| CPlWpGuitar | Nevan | 0x5f5b10 | 0x605e80 |
| CPlWpFight | Beowulf | 0x5f89f0 | 0x608fa0 |
| CPlWpGun | Ebony & Ivory | 0x5f3950 | 0x6035c0 |
| CPlWpShotGun | Shotgun | 0x5f3ef0 | 0x603d80 |
| CPlWpLaser | Artemis | 0x5f9fe0 | 0x60afe0 |
| CPlWpRifle | Spiral | 0x5f9f80 | 0x60ae80 |
| CPlWpLadyGun | Kalina Ann | 0x5f3a30 | 0x603880 |

`CPl000Shl00..10` (decomp names) are *shells* (projectiles), not move handlers.

### Motion request

`FUN_002500d0(float blend, this, bank, index, p5, p6, p7)`:
stores the motion id as `bank << 8 | index` at `this+0x24c4/0x24c6`, starts
it on both motion layers (`FUN_00254e80` on `this + 0x4170/0x4240 +
layer*0x1a0`), and copies a per-motion property word from
`0x593af0[character(this+0x46)][bank][index]` to `this+0x48b4`.
`FUN_002506b0(this)` is its stop/blend-out counterpart.

`tools/extract_motion_calls.py` lists every call:
`analysis/moveset/motion_calls.csv` -- 614 requests in 223 functions, 586 with
constant (bank, index). Callers by region: 0x21d000-0x22c000 generic player
(94), 0x246000-0x2ea700 Dante (282), 0x323000-0x325000 (16),
0x52a000-0x532000 SE-added code, likely Vergil (67).

### Motion slots are script driven

`FUN_0024fb90` sets up the two motion layers per weapon slot with
`FUN_00254d60(slot, script_file, layer, mode 0, motion_list, ...)`, where
`script_file = FUN_001aef10(0x6c3810, 0, character, 5)` -- **chunk 5 of the
character archive** -- and layer is 1 for `+0x4240`, 0 for `+0x4170`.

Script file (`tools/motion_scripts.py` disassembles it;
output in `analysis/moveset/motion_scripts.{csv,json}` and
`motion_files.csv`):

- Header `u16[3]` = offsets of the *script* part (0x6), the *motion
  definition* part (0xd740) and an unused third (0xffff). Each part starts
  `u16 4, u16 0xffff`, then a table of 34 u16 bank offsets (ends at 0xffff,
  counted by `FUN_002557f0`).
- `FUN_002557b0`: script = `t + t[bank]` then `+ sub[index]` -- the
  `(bank, index)` passed to `FUN_002500d0` addresses a script, not a motion.
  Dante has 769 scripts in 34 banks; unused entries share one dummy script.
- Interpreter `FUN_00254f10` runs a *segment* up to op 0, which is a
  **wait**: `00 xx u16 frame u8 flags xx` (`FUN_00255790`). The per-frame
  update `FUN_00255160` resumes the script once the motion passes `frame`
  (0x7fff = motion end); flags bit 0 = hold. So a script is a frame
  timeline (Combo I-1 has 12 segments). Op sizes:
  `01` play motion def, 8 bytes `01 loop b2 b3 grp sub s16 start_frame`
  (0x7fff = take from motion); `02 xx s16` jump back (loop);
  `03/04` +5 bytes copied to slot+0x5d / +0x62 (window 0 / window 1, see
  below); `05 xx` clears them; `06/07 n`,
  `08 xx` effect-manager calls; `10..1f layer xx u16 id + n*u16` attach
  `n = op-0xf` effects; `20 layer mask xx`, `24 layer s16` kill effects;
  `21 xx`, `22` (6 bytes), `23 xx`. All 769 scripts decode with no unknown
  opcode.
- Motion definition (`FUN_002558a0`, table part 2 indexed by `grp, sub`):
  `u8 n, pad, n x {u8 layer, u8 loop, u8 rootflag, u8 ?, u16 motion_id}`
  then `u8 n_events, pad` and timed events (`30-33`: 6 bytes, `34-39`:
  8 bytes, u16 frame at +4) -- sounds (`FUN_0033e820/ed90/ee10/ef00`) and
  effects, fired by `FUN_00255d30` when the frame is reached.
- `motion_id = file * 100 + index` into the 34-entry motion PAC table at
  `player+0x243c` (bounds checked; out of range plays nothing).

### Bank -> motion file -> GDATA (solved)

Filled by `FUN_00280110` / `FUN_0025bb00` with
`FUN_001aefc0(0x6c3810, 1, character * 0x22 + file)`; for Dante file *f*
is **GDATA slot 20 + f** (motion counts match for every file). Script bank
*b* plays motions from file *b* (bank 32 also uses file 1):

| file / bank | GDATA | contents |
|---|---|---|
| 0-2 | 20-22 (= archive chunks 2-4) | base movement |
| 3+w | 23-32 | weapon *w* moveset: 0 Rebellion, 1 Cerberus, 2 Agni & Rudra, 3 Nevan, 4 Beowulf, 5 Ebony & Ivory, 6 Shotgun, 7 Artemis, 8 Spiral, 9 Kalina Ann |
| 13-17 | 33-37 | Swordmaster extras, melee weapons 0-4 (loaded when style 0) |
| 18-22 | 38-42 | Gunslinger extras, guns 5-9 (style 1) |
| 23-26 | 43-46 | Trickster, Royalguard, Quicksilver, Doppelganger |
| 27-33 | 47-53 | other (27-31 single motions; 32/33 not yet identified) |

Weapon ids come from the five equipped bytes at `player+0x3d4f`; style is
`player+0x3c6c`. Bank 0 indices 30-42 (the earlier "empty" mystery) are
scripts that reuse base motions (e.g. 0:30 plays base motion 12).
Shotgun scripts reference file 9 indices 40-47, but GDATA 29 holds only 40
motions -- dead references in the shipped data.

### Script windows -> attack state and cancel gates (solved)

`FUN_00250a00` copies the layer-1 windows into the player every time the
script changes them (read with `FUN_00255af0(slot, window, i)`):

- Window 0 byte 0 -> `+0x24d1` (bit 0 read by `FUN_00274c70`; 0x80 / 0x40
  toggle through each attack). Byte 1 -> `+0x24d2` (low 6 bits: weapon
  placement state, read by the weapon objects, e.g. `FUN_002954b0`) and
  `+0x24ed` (bit 0x80: **attack active** -- Combo I-1 sets it frames 10-17,
  matching the hand-tuned hit at 12). Byte 2 -> `+0x24d3` (0x20: call
  `FUN_002e21a0(n)`, 0x10: `FUN_001acdf0(n)`, 0x40 -> `+0x24f1`; layer 0's
  0x80 -> `+0x24ce`). Byte 3 -> `+0x24d4`, byte 4 -> `+0x24d5` (one-shot;
  read by ~94 action handlers, values 1/2 select move variants).
- Window 1 = five commands for the 16 **cancel gates** at `+0x24dd[n]`:
  `0x3n` -> 1 (buffer), `0x2n` -> 0 (closed), `0x1n` -> 2 (open).

### Command table (solved: format; input bytes still open)

`FUN_002458f0` walks `player+0x2838`, which `FUN_0025bb00` sets to
**0x5a8e90** for Dante in game (142 commands; 0x5aa4f0 is a one-entry stub
set by `FUN_00280110`). `tools/command_table.py` dumps it to
`analysis/moveset/command_table.csv`. A command fires when its posture
(`[0x10]`: ground / air / both) matches, the weapon (`[0xb]`) and style
(`[0xd]`, for type 3) are current, and its conditions pass
(`FUN_0024bc40`: `0x80000000|n` = gate *n* open, bit 0x800000 = optional;
`0x40000000|id` = current action is *id*, i.e. a follow-up). On success
`player+0x2998` = action id `[2]` and `FUN_0021e620(player, 0x11, ...)`
enters the attack state. Types: 0 melee (gate 0), 1 gun (gate 10),
2 directional melee (gate 4), 3 style move (gate 4 / 7). Flags 0x100 =
usable from neutral, 0x200 = usable as a cancel.

So Dante's combo/cancel logic is data: scripts open gates on frames, and
commands name the gate and preceding action they need.

### Inputs (solved)

`FUN_002532d0` runs every frame: it tests the 58 input patterns at
**0x5950e0** (`{s16 steps, s16 precondition, s16 reset, s16 0, u32
steps}`) against per-pattern history at `player+0x3e9c` and sets bit *i*
of `player+0x4a40` on a match. Preconditions (`FUN_00254080`): 1 = R1
(lock-on) held, 2 = not in state 0x18, 3 = Artemis equipped. Steps are 10
bytes `{u16 kind, u16 button, u16 a, u16 b, u16 timeout}`
(`FUN_00253650`): kind 1 = button pressed / released / held (0x8000 /
0x4000 / 0x2000); kind 2 = stick sector `a & 0xf0` relative to facing
(0x10 forward, 0x40 back) or to the target (0x80 / 0x20, 0x90 / 0x30
need a target); kind 4 = hold *b* frames (charge, 0x4000 = on release);
kind 8 = more than *b* presses with gaps of at most *a* frames (mash).
Buttons are PS2 pad bits (0x10 TRI, 0x20 CIR, 0x40 X, 0x80 SQ, 0x08 R1)
remapped through the button config (`FUN_001ae900(0x6c37d0, bit)`).
The forward/back orientation is fixed by data: the Stinger scripts
(three phases) belong to the 0x10 command.

A command's `[8]` picks its input (`FUN_0024b1d0`): bit 0 = pattern
`[9]`, bit 1 = pattern from `0x594bc8[character][9][player+0x3d90]`
(mash variants). With no pattern, types 0 / 1 / 3 fire on a plain TRI /
SQ / CIR press (`FUN_00245f10`). `tools/command_table.py` writes the
decoded text to `command_table.csv` and `input_patterns.csv`.

### Action -> handler -> scripts (solved for 135 of 142 commands)

`FUN_002625d0` is a compare chain on `player+0x2998` that `jal`s one
handler per action; Ghidra does not decompile it, so
`tools/action_dispatch.py` emulates it (146 actions ->
`analysis/moveset/action_dispatch.json`). Each handler requests its
scripts with constant `(bank, index)` (`motion_calls.csv`).
`tools/moveset.py` joins everything into **docs/MOVESET.md**. Trickster
actions 180-183 are not dispatched there: `FUN_00245f10` sends them to
dedicated player states (0x17 / 0x18 / ...). Gun actions 134, 160, 170
request their motions indirectly.

## Trickster (actions 180-183)

`FUN_00245f10` turns the style rows (game style 2) into player states, not
actions; each state clears the current action (player+0x2998 = 0), so the
next command needs only its cancel flag (0x200) and gates. Dashes in a row
are counted at player+0x3c75 (< 1 / 2 / 3 by style level, cleared by
`FUN_0021f500` in neutral).

| action | state | handler | scripts |
|---|---|---|---|
| 180 dash (CIR, ground) | 0x16 | `FUN_00225c90` | table 0x5911d0 `{0,1,2,3, 24,25,26,27}`: fwd / back / left / right; the second dash on uses 24-27, and a fwd dash right after a fwd dash uses 23:17 |
| 181 Sky Star (CIR, air) | 0x17 | `FUN_002261b0` | 23:11 (580 cm); aims at the target within 10 deg |
| 182 Air Trick (R1 + fwd(target) + CIR) | 0x18 | `FUN_00226550` | 23:15 ground / 23:16 air, then a 10-frame warp to the lock-on point (stops 120 cm short, at most 500 cm across / up / down; config +0x290..0x29c), then a fall (0:4 / 0:5 / 0:9) |
| 183 Wall Hike (CIR into a wall; extra check 2) | via `FUN_0021e360` | `FUN_00227fb0` | wall ahead (player+0x3c6d = 2): 23:5 (462 cm up) -> 23:6; wall at the side (0 / 1): 23:7 / 23:9 -> 23:8 / 23:10 |

Dash direction (`FUN_00254180`): neutral or R1 released -> fwd, facing the
stick; with R1 held the stick picks a 45 deg sector against the facing.
Root motion: 23:0 +Z, 23:1 -Z, 23:2 +X, 23:3 -X, all 400 cm.

## Royalguard

Release is in the command table (190-197: R1 + fwd + CIR, scripts 24:15-17
ground, 24:18-20 air). The block itself is a player state: handlers
`FUN_00224850` (24:0, 24:1, 24:10, 24:11, 24:30, 24:31), `FUN_00224db0`
(24:2, 24:3, 24:6-8, 24:11, 24:12, 24:31-35) and `FUN_00225a00` (24:4,
24:9, 24:14). Not decoded yet.

## Open questions (next steps, in order)

1. **Gate meanings.** Gates 0 (melee), 1 (Combo II pause: opens at frame
   29 of Combo I-1), 4 (directional / style), 7 (mash follow-ups), 8
   (Trickster), 10 (guns) are clear from the commands that need them;
   5, 6, 9, 15 still need naming (15 looks like "free to move").
2. Royalguard block states (above), Air Hike, and the indirect gun handlers.
3. Chunk 6 (252 x 4 bytes) and chunk 7 (251 x 80-byte floats, likely hit
   spheres): index and meaning.
4. Identify motion files 27-33 (likely Devil Trigger / special states).
5. The Air Trick lock-on point (player+0x3c40) and side Wall Hike (needs
   the wall's side, player+0x3c6d).

## Corrections already known

- Rebellion, Swordmaster-Rebellion and Trickster in the test room now come
  from the data above (psp/testroom/combat.c, generated
  psp/testroom/combat_data.h). Royalguard block and Air Hike still play
  stand-in motions.
- Rebellion assignments the old test room had wrong: rb-10 is Helm Breaker
  (air TRI, action 6), not High Time; High Time (R1 + back + TRI, action
  10) is rb-11; rb-17/18/19 are Round Trip (charge TRI, action 13), not
  Prop / Dance Macabre; rb-29 is action 12. Swordmaster Rebellion moves
  live in GDATA 33 (s33-): Prop 13:19 -> Shredder 13:12/13, Aerial Rave
  13:7-10. Real Trickster / Royalguard motions are GDATA 43 / 44.
