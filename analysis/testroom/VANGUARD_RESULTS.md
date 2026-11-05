# Hell Vanguard verification — 2026-11-05

PSP build and PPSSPP scene checks passed. The preliminary waves were accelerated and cleared by the harness; Vanguard took only ordinary combat damage.

| Test | Warps | Hits on boss | Hits on Dante | Death / Mission Clear |
| --- | ---: | ---: | ---: | --- |
| [vanguard_observe_easy](../../build/tests/vanguard_observe_easy/VANGUARD_LOG.TXT) | 3 | 0 | 11 | observation only |
| [vanguard_melee_easy](../../build/tests/vanguard_melee_easy/VANGUARD_LOG.TXT) | 8 | 2 | 1 | observation only |
| [vanguard_clear_easy](../../build/tests/vanguard_clear_easy/VANGUARD_LOG.TXT) | 7 | 72 | 0 | yes |
| [vanguard_clear_normal](../../build/tests/vanguard_clear_normal/VANGUARD_LOG.TXT) | 19 | 72 | 1 | yes |

Both victory cases started at 3000 HP and reached zero through 72 gun hits. The melee scene dealt 168 damage and took one 800-damage scythe hit. Three Python regressions passed (stage-edge collision and Mission 2 boss gating). The final airborne-collision correction was rechecked with observation and a Normal victory.

Final EBOOT SHA-256: `bcae6df2b930618e91b3697762c767eec7368081f27171ccb2660d56d11ed4af`

Reproduce:

```powershell
python tools/test_vanguard.py --build
python tools/test_vanguard.py --mode clear --normal
python -m unittest discover -s tools/tests
```

Limitations and source addresses: [VANGUARD_SPEC.md](../../docs/VANGUARD_SPEC.md). Player death/Game Over, full cloth/effects, cutscenes and exact damage scaling remain unfinished.
