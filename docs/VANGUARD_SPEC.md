# Hell Vanguard: recovered work and asset preparation

This is a partial handoff, not a completed AI specification. Mission 2 still substitutes a Pride for
spawn class 26. Do not enable Vanguard in the encounter until his AI, damage and death have been ported.

## Original assets

GDATA slot 120 is a PNST pack. `tools/prepare_vanguard.py` reads it directly from the user's ISO and exports
all nine meshes, their original skeletons and weights, two indexed textures, and 80 MOTs under a selected
build directory. The generated manifest records chunk IDs, mesh counts, motion durations and the pack hash.
These generated assets stay under ignored `build/`; they are not committed.

| Part | Chunk | Notes |
| --- | --- | --- |
| Body | 1 | `EFM ` signature, 23 bones. Geometry and skin streams use the MOD layout; EFM effects aren't ported |
| Robe pieces 1–7 | 3, 5, 7, 9, 11, 13, 15 | Separate rigs; following even chunks hold cloth text |
| Scythe | 18 | Two bones |
| Body motions | 19 | PAC with 71 motions, exported as `vg-NN.mot` |
| Weapon motions | 20 | PAC with 9 motions, exported as `vw-NN.mot` |
| Grouped scripts | 21 | Retained as `scripts.bin`; requires its own script decoder |
| ID map | 22 | Retained as `id_map.bin`; do not assume it is the Seven Hells mapping |
| Collision records | 23 | Retained as `colliders.bin`; semantics still need verification |

Generate with:

```powershell
python tools/prepare_vanguard.py "path\to\game.iso" --output build/vanguard
```

All nine meshes pass skeleton, skin-weight, strip and geometry validation. Both motion PACs parse without
skipped entries. The generated header passes PSP GCC's C syntax check alongside `model_runtime.h`.
No Vanguard screenshot or playable boss is claimed by this preparation step.

## Recovered PS2 code

Addresses below refer to `SLUS_213.61`; this is saved Ghidra decompilation, not new REA Evidence. REA's
Windows Ghidra provider rejected the MIPS ELF, so use the repo's existing R5900 Ghidra pipeline.

- Spawn class 26 maps to CEm008. Factory wrapper: `002b08b0`; constructor: `002b0210`;
  destructor: `002b0570`.
- Setup: `002b1100`; renderer: `002b52f0`; attachment update: `002b5a40`.
- The scythe's draw matrix is based on the body matrix pointer at object `+0x428`, corresponding to body
  bone 9 (`002b4d40`, `002b52f0`). It also has a local transform, so simply translating to the hand is insufficient.
- `002b2b20` contains visibility/effect timers. These alone do not establish the teleport decision logic.
- `002b2cc0` contains staged effect/death sequences. Translate the matching alive/death entry path before reuse.

The interrupted research had exported the common AI functions in ranges `0046xxxx` and `0047xxxx`.
The recovered exports and script listing are preserved locally under `build/research/vanguard/`.
Do not substitute the Pride decision table or infer Vanguard attacks from motion numbers alone.

## Remaining work

1. Decode the decision table, action-to-script mapping, teleport placement and timings, hurt/attack volumes,
   health and hit reactions. Verify the lowest-rank Mission 2 values.
2. Attach and animate all robe pieces and the scythe; implement the EFM visibility/effect path.
3. Add boss health UI and connect spawn class 26 to the completed implementation.
4. Exercise the actual Mission 2 encounter and death/mission-clear sequence in PPSSPP; record game frames.
5. Continue with Mission 3 Cerberus using `docs/CERBERUS_SPEC.md`.

Standing scope recovered from the prior session: test in PPSSPP, keep changes local, and do not modify the
original PS2 executable or the user's memory cards.
