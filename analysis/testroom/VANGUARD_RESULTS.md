# Hell Vanguard verification — 2026-11-06

Combat completion is verified below; complete PS2 visual parity is not established. Earlier clips exposed actual porting defects and are not fidelity references.

Corrections include the original rush entry frame, distinct uppercut placement/timing, corrected robe anchors, advancing death pose, separate model visibility and hit immunity, native teleport light/smoke sequences, and a solo boss transition.

Two pacing defects were corrected: ordinary E&I shots used the sword-flinch route, and defensive decisions incorrectly renewed the three-second attack token. Pistol shots now use the native base damage 10 (20 on Easy), 5 strain per hit and the decoded 300-point tertiary pool. Only a primary attack grant renews the token.

| PPSSPP scene | Roster at boss start | Pistol hits | Flinches | Primary attacks | Warps | Result |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| [observe_easy](../../build/tests/vanguard_observe_easy/VANGUARD_LOG.TXT) | 1 | 0 | 0 | 6 | 3 | combat observed |
| [melee_easy](../../build/tests/vanguard_melee_easy/VANGUARD_LOG.TXT) | 1 | 0 | 0 | 2 | 4 | combat observed |
| [clear_easy](../../build/tests/vanguard_clear_easy/VANGUARD_LOG.TXT) | 1 | 150 | 2 | 10 | 7 | death → removal → Mission Clear |
| [clear_normal](../../build/tests/vanguard_clear_normal/VANGUARD_LOG.TXT) | 1 | 300 | 0 | 22 | 13 | death → removal → Mission Clear |

Easy required 150 actual pistol hits; Normal required 300. Both dealt exactly 3000 HP damage and passed the flinch bound and primary-attack availability assertions. Sword damage and a distant teleport strike were verified separately. Preliminary ordinary waves are accelerated by the harness; the boss takes ordinary combat damage. The transition removes defeated wave actors and refuses to start while earlier living enemies remain.

Host regressions passed against the actual Vanguard logic, original MOTs and effect data: pistol damage/strain versus sword flinch, attack-token starvation, rush seek, uppercut timing/landing, visibility versus hit immunity, collision fixtures, death pose, native V child timing, atlas UVs, and scale/alpha curves. GPU calls are no-ops in these host tests. Three Python room/stage regressions passed. The effect exporter reproduced the compiled data and accounted for all 186 nested PNST children.

The recording exposed a recorder race: file existence was mistaken for a completed Mission Clear BMP. The runner now waits for the complete 391734-byte framebuffer, and the victory checker verifies that size. Both subsequent victory runs produced complete end-screen images.

[60 fps comparison clip](vanguard_native_pace60.mp4): 2564 consecutive game frames, 42.733 seconds, H.264. It includes the fight through defeat. Its incomplete end-screen image was omitted, not replaced from another run. The later successful victory runs verify Mission Clear independently. Playback uses 60 simulation frames per second and does not measure emulator wall-clock throughput.

Final EBOOT SHA-256: `f139da723a18b569dd85da8c55ebc80c5adb945b5ddd27d0102fde191efd9a48`

```powershell
python tools/tests/run_vanguard_logic.py
python tools/test_vanguard.py --mode all
python tools/test_vanguard.py --mode clear --normal
python -m unittest discover -s tools/tests
```

Remaining limitations: P312 VU particle disintegration and E797 model effects, full cloth simulation, full damage modifiers/other weapons, dynamic AI ranks, camera/cutscene fidelity, and player death/Game Over. Port-specific body collision does not prevent every weapon/robe intersection. Details and source addresses: [VANGUARD_SPEC.md](../../docs/VANGUARD_SPEC.md), [VANGUARD_EFFECTS.md](../../docs/VANGUARD_EFFECTS.md).
