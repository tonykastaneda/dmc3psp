# Mission 3 Cerberus — 2026-11-06

Recovered OpenCode's interrupted implementation and original request from session
`ses_e4dc8d7d2ffe512D0dAO8DnD6B`. It had compiled before regenerating the room encounter
header and had not completed runtime validation.

## Implementation

- One class-43 enemy in room 006. Original GDATA 130 meshes, 118 motions, four script
  banks, attack windows, armour pieces, three heads, role rotation and three phases.
- Added direct linear/Hermite MOT support alongside existing quantized channels.
- Boss health bar, target points, body/head damage, story death and room progression.
- Corrected byte-to-int combo copying, combo lengths, the re-ice reset order,
  signed breath-vector stride/component selection, and turning across pi.
- Corrected paw damage distribution across live heads and preserved projectile hit
  parts instead of replacing them with the part nearest the shooter.
- Each chain now keeps a separate GPU vertex buffer. Added the Cerberus HUD name,
  game-written trace collection, room-generator freshness checks and longer scenes.
- Ordinary E&I damage uses base 10, gun factor 0.8 and Easy's factor 2. Other attacks
  still use the port's coarse HitKind damage categories.

## Validation

`python tools/tests/run_cerberus_logic.py` compiles the actual boss C implementation
and MOT reader on the host. GPU calls are stubs; this does not validate rendering.
Checks cover all 11 moves in each phase, all eight combos, shell creation, smooth
turning across pi, re-ice completion under damage, armour, projectile target
preservation, Easy/Normal pistol damage, and defeat through the normal hit API.

Vanguard logic/effect regressions and the three existing Python tests passed.

PPSSPP observation: `build/tests/cerberus_observe` completed 3,600 frames at fixed
60 Hz without attacking. Game framebuffers were inspected; punches, pounce, breath
and head rotation occurred. No desktop capture was used.

PPSSPP combat: `build/tests/cerberus_combat` uses regular E&I input, without kill-all
or minion-clearing hooks. The final run reached phase 1 at boss tick 1,358, phase 2
at 2,151, detached chains at 2,933 and exhausted every pool at 3,702. All three
head-death events were recorded. The same run exited through the rear door and
reached Mission Clear; the event log and complete 391,734-byte native framebuffer
passed `check_cerberus_run.py`, and the framebuffer was visually inspected.

Validated EBOOT SHA-256:
`9263275776401284F30ADA29404D7D33EFF6C43DF8B2240AFC0C4B3FC4080CE8`.
The identical build and room binaries were installed in PPSSPP's regular
`DMC3TESTROOM` game directory. The test harness stopped only its own emulator.

Reproduce with:

```powershell
pwsh -NoProfile -File tools/build_testroom.ps1
python tools/tests/run_cerberus_logic.py
pwsh -NoProfile -File tools/test_scene.ps1 -Script tools/tests/cerberus_observe.scene -Out build/tests/cerberus_observe -TimeoutSeconds 180
pwsh -NoProfile -File tools/test_scene.ps1 -Script tools/tests/cerberus_combat.scene -Out build/tests/cerberus_combat -TimeoutSeconds 180 -MissionClear
python tools/tests/check_cerberus_run.py build/tests/cerberus_combat
```

## Remaining fidelity limits

This is a playable implementation, not a completed PS2 presentation match. Ice
projectiles currently use simple geometry and shared spark/explosion effects;
the original em025 particle/effect pack is not reproduced. Chains use straight
kinematic attachments rather than the native rope simulation. Boss cinematic
cameras, sounds, head-death presentation and the full weapon/difficulty damage
record system remain incomplete. The game's existing player-death/Game Over
limitation still applies. Rank-zero AI is used; higher AI ranks are not ported.

The fixed-step captures establish animation/logic progression, not real-time
performance at 60 fps on PSP hardware. Validation was performed in PPSSPP only.
