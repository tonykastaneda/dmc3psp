# Hell Vanguard: Mission 2 implementation

Spawn class 26 now runs Vanguard in `psp/testroom/vanguard.c`. The Mission 2 fight includes his original
body/robe/scythe meshes, low-rank decisions and motion scripts, teleport attacks, health bar, damage,
death animation and Mission Clear. Combat completion tests do not establish animation or PS2 fidelity.
The first implementation had incorrect rush entry frames, shared teleport timing, invented actor spin,
and a frozen death pose; these defects were visible even though the fight could be completed.

## Encounter and AI

`EventTbl02` (GDATA slot 240) starts EST group 0, waits for it to clear, then starts group 1. After that
clears, it runs the boss entrance and starts **group 3** at `(3436,0,2384)`. Group 2 is the rooftop cinematic
actor at `(4951,1360,2457)`; group 4 is the cutscene-skip alternative. The port runs `[0,1,3]`, with clear
gates and the 40/30-frame inter-group delays. The previous generated script contained only group 0.

The decision table at `0x5cdd00` has distance bands 350 and 600. At rank 0 its primary rows pick plan 27
nearby and plan 56 farther away. The alternate rows pick plan 57 nearby, and 57/40 at equal weights farther
away. Zones at `0x5cdcb0` add gauge at 10/1/0.5 per frame inside 350/600/3000 cm and 45/25/10 degrees.
The rank-0 gauge threshold is 100. The port shares the existing enemy Mersenne Twister and attack-token
convention; it does not reproduce dynamic AI rank or all manager arbitration.

| Plan / action | Translation and code evidence |
| --- | --- |
| Watch, action 15 | Turns at 256 angle units/frame; authored root motion. `00474be0`, `00474d20`, `0046d680` |
| 27 / 16 | Script 47 from frame 20; tracks during the wind-up; script 101 follows at frame 77. Live hit shapes come from the scripts. `0046dab0`, `00474e00`, `00474e50` |
| 56 / 72 → 69 or 70 → 73 | Wind-up to frame 70, one random rush/uppercut at rank 0, then recovery. Rush enters script 102 at frame **202**; uppercut relocates to Dante rather than the random 400 cm circle. `0046f1a0`, `0047fbb0`, `0047e840`, `0047ef00`, `0047fd10` |
| 57 / 71 | Hidden 30 frames, relocate 400 cm around Dante, face him and return after 30 more. `0046f400`, `0047f5b0` |
| 40 / 35 | Scripts 75 and 7. `0046e6a0`, `00476b40`, `00476bb0` |
| Hit reaction | The grounded flinch ends at frame 30 and rank 0 selects plan 57. `00471ea0`, jump-table target `0046cca8` |

Rush/uppercut first spend 30 frames in phase 10, then use the rank-0 relocation delay of 25 at `005cdd50`.
Rush waits another 15 (`005cdd70`) before revealing, then 30 before striking. Uppercut reveals on the next
step, plays script 105, waits 50, plays 107, then waits 3 before starting vertical velocity 80 with
acceleration -8. At the apex acceleration changes to -0.5. `00285820` command **3000** passes a script ID
and a start-frame override into `00254e80`/`00255420`: `(102,202)` is not a `[102,202]` interval.
The extra actor rotation from the first implementation has been removed.
`002b52f0` attaches the two torso robe pieces to body table `+410` (bone 3), not bone 4.
Their attachment indices have been corrected; the remaining static robe rig is still not PS2 cloth simulation.

Port-specific placement safeguards try the original random direction first, then other directions at
the same radius if scenery blocks it. A candidate needs floor on the same storey, circle clearance,
and an unobstructed chest-height segment to Dante. These are adaptations to the port's stage collision,
not translations of the PS2 collision engine. Ground movement is subdivided spatially into steps no
larger than one quarter of the decoded 50 cm root sphere, rejecting unsupported floor transitions.
Hidden phases cannot be targeted, hit, or push Dante. Weapon/robe geometry can extend beyond the body
collision sphere; full mesh collision is not implemented.

Health uses the decoded base value **3000** at parameter block `0x5cd590 + 0x2c`. Incoming damage still uses
the port's 42/84 light/heavy convention; outgoing damage uses the existing heavy-hit route (800), not a
fully translated Vanguard attack-damage table. Protected attacks retain their motion; grounded flinches
lead into teleport recovery. Full launch/juggle reactions are not implemented. Death plays script 29 and
removes the boss before the existing two-second mission-clear delay finishes.

## Emulator verification

`python tools/test_vanguard.py --build` regenerates encounter tables, builds, and tests observation,
melee, and a real-gunfire victory. `--mode clear --normal` selects Normal. The harness accelerates waits
and clears the earlier ordinary enemies only; it never kills or directly damages the boss. Logs and game
framebuffer captures go under `build/tests/vanguard_*`. Victory requires both a game-written death → removal
→ mission-clear trace and a captured Mission Clear screen. Observation checks teleports, teleport strikes,
live melee hit shapes and health loss to Dante; melee testing checks health loss to Vanguard from sword input.

Trace `t` counts simulation substeps, not authored motion frames; the `frame` field is the animation timeline.
Test scenes reposition Dante for isolated interactions and do not certify a complete unaccelerated playthrough.

`python tools/tests/run_vanguard_logic.py` compiles the actual Vanguard logic and motion sampler against
the extracted original MOTs on the host. It checks the rush start frame, uppercut placement/timing,
landing, death pose advancement, a thin-wall movement fixture, and a floor edge. GPU functions are no-ops:
this test cannot certify visual quality. `tools/record_vanguard.py` captures each simulation frame at
60 Hz, independently of emulator wall-clock throughput; the resulting MP4 is not an emulator performance test.

Remaining limitations: real-time intro/Vergil cutscenes, sound effects, EFM fades, full cloth simulation,
dynamic AI ranks, complete hit reactions and original damage scaling. Robes use bind-corrected attachments;
the rush and recovery play the separate scythe motions. The existing Dante damage path still clamps his
health at 1; player death/Game Over remains a separate unimplemented system.

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
| Additional motions | 20 | PAC with 9 mixed body/scythe motions, exported as `vx-NN.mot`; entries 5 and 7 animate the two-bone scythe |
| Grouped scripts | 21 | Retained as `scripts.bin`; requires its own script decoder |
| Collider map | 22 | Retained as `caps.bin`; kind/bone/shape records |
| Collision shapes | 23 | Retained as `shapes.bin`; sphere and capsule data |

Generate with:

```powershell
python tools/prepare_vanguard.py "path\to\game.iso" --output build/vanguard
```

All nine meshes pass skeleton, skin-weight, strip and geometry validation. Both motion PACs parse without
skipped entries. The generated header passes PSP GCC's C syntax check alongside `model_runtime.h`.
The build also generates `vanguard_ai.h` and `vanguard_motions.h`; the latter appends motions without
changing any existing Dante or Seven Hells motion index. Unused reserved scripts 90/91 have missing
motion entries and remain disabled.

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

Standing scope recovered from the prior session: test in PPSSPP, keep changes local, and do not modify the
original PS2 executable or the user's memory cards.
