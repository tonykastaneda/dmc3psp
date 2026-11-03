# Dante moveset reverse engineering (Phase 0)

Goal: an authoritative motion -> move map for Dante's whole arsenal, taken from
the game's own code and data instead of reading moves off root motion and poses
by eye. This document records what is known so far and what is next.

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

## Open questions (next steps, in order)

1. **Input bytes** `[8..0xa]` of a command (`FUN_0024b2d0`,
   `FUN_0024b1d0`): direction (forward/back on lock-on) and button.
2. **Action id -> script.** Find where the action handlers dispatch on
   `player+0x2998` and which `(bank, index)` each requests
   (`analysis/moveset/motion_calls.csv`), then emit `docs/MOVESET.md`:
   move, weapon, style, input, posture, conditions, script, motions,
   active frames and gate timings.
3. Chunk 6 (252 x 4 bytes) and chunk 7 (251 x 80-byte floats, likely hit
   spheres): index and meaning.
4. Identify motion files 27-33 (likely Devil Trigger / special states).

## Corrections already known

- Aerial Rave is Swordmaster (circle in the air, up to four hits); plain
  triangle in the air is Helm Breaker. The test room currently has these
  wrong (see psp/testroom/combat.c) and will be rebuilt from the map above.
- The test room's "Trickster (slot 33)" moves are from GDATA 33 =
  Swordmaster-Rebellion, and "Royalguard (slot 34)" from GDATA 34 =
  Swordmaster-Cerberus. Real Trickster / Royalguard motions are GDATA 43 /
  44. `STUDY_MOTION_SLOTS` in tools/prepare_skinned_model.py is mislabeled
  the same way.
