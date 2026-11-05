# Hell Vanguard verification — 2026-11-05 (corrected implementation)

The original combat-completion checks did not establish PS2 animation fidelity. User review exposed incorrect motion entry, teleport behavior, robe attachments and geometry interactions. The first clips are not fidelity references.

Corrected against recovered PS2 code: rush script 102 starts at frame 202; no additional actor spin; distinct uppercut relocation and phase timing; apex acceleration switch; torso robes attach to bone 3. Death now samples its advancing pose. Port-specific safeguards subdivide ground movement and reject teleport destinations behind scenery, inside collision or on another storey.

| PPSSPP test | Warps | Hits on boss | Hits on Dante | Result |
| --- | ---: | ---: | ---: | --- |
| [observe_easy](../../build/tests/vanguard_observe_easy/VANGUARD_LOG.TXT) | 3 | 0 | 7 | combat observed |
| [melee_easy](../../build/tests/vanguard_melee_easy/VANGUARD_LOG.TXT) | 3 | 6 | 2 | combat observed |
| [clear_easy](../../build/tests/vanguard_clear_easy/VANGUARD_LOG.TXT) | 7 | 72 | 1 | death → removal → Mission Clear |
| [clear_normal](../../build/tests/vanguard_clear_normal/VANGUARD_LOG.TXT) | 7 | 72 | 1 | death → removal → Mission Clear |

The observation test uses real stick movement to cover both close-range melee and a distant teleport attack. The harness accelerates preliminary minion waves only; ordinary gun/sword input damages the boss.

Host regression using the actual Vanguard implementation and original MOT data passed: rush entry/seek, uppercut placement and timing, landing, death pose advancement, thin-wall movement, floor-edge rejection and teleport clearance. GPU calls are no-ops in this test; it does not verify rendering. Three existing Python regressions also passed.

Final EBOOT SHA-256: `6f48d8de781981b168473c0fd6e34bdec55be35e681a1a089e29ab3b0b01e0a5`

```powershell
python tools/tests/run_vanguard_logic.py
python tools/test_vanguard.py --mode all
python tools/test_vanguard.py --mode clear --normal
python -m unittest discover -s tools/tests
```

**Not established:** PS2 visual or runtime parity. Static robe rigs still replace cloth simulation; teleport effects/fades and cutscenes remain incomplete. Damage, AI rank and collision are partial ports. Body collision does not prevent every robe/scythe intersection with scenery. Player death/Game Over remains unfinished. Source addresses and distinctions: [VANGUARD_SPEC.md](../../docs/VANGUARD_SPEC.md).
