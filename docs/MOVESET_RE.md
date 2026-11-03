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

`FUN_00254e80` (start motion on a layer) stores bank at +0x30, index at
+0x34, then `FUN_00254f10` runs a **bytecode interpreter** over the byte
stream at `*(slot + 0x14)`: opcodes 1-8, 0x10-0x1f (`FUN_00256410(slot,
op - 0x0f)`), 0x20-0x24, terminated by 0. Motion data comes from the source
object at `slot + 0x44` (vtable +0x28 / +0x38 with `slot + 0x38`).

## Open questions (next steps, in order)

1. **Bank -> file mapping.** Banks are not the motion-only GDATA slots in
   order: bank 0 requests indices 30-42, which are empty in Dante's base
   PAC (slot 0 chunk 2 / GDATA 20). Find the loader that fills the banks at
   `slot + 0x44` (search callers of the AFS/PAC load path with GDATA ids
   20-60 and the SE remap table at 0x5e5400, which maps original file ids to
   SE replacements 318-329).
2. **Action scripts.** Dante's archive chunk 5 (73 KB, starts
   `06 00 40 d7 ff ff 04 00 ...`, offset table of u16) is the most likely
   script data for the interpreter above; chunk 6 (252 x 4-byte entries
   ending in a sequential motion index) and chunk 7 (251 x 80-byte float
   records -- probably per-motion collision/hit spheres) are per-motion
   tables. Decode the opcodes (`FUN_00255420`, `00255620`, `00255640`,
   `00255670`, `00255690`, `002556f0`, `00255740`, `00256410`, `00256590`,
   `00256730`, `00256760`, `002567f0`, `00256670`) to get hit windows,
   cancel points and effect/sound cues.
3. **Move semantics.** For each constant motion request in Dante's region,
   recover the input/state condition around it (button bits, style id,
   weapon id at `this+0x28a0`-style fields) and emit
   `docs/MOVESET.md`: move, weapon, style, input, bank/index, source.

## Corrections already known

- Aerial Rave is Swordmaster (circle in the air, up to four hits); plain
  triangle in the air is Helm Breaker. The test room currently has these
  wrong (see psp/testroom/combat.c) and will be rebuilt from the map above.
