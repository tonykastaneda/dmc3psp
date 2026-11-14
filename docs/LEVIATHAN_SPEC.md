# Leviathan / Mission 8 implementation evidence

## Confirmed native data

Local source: the user's SLUS_213.61 executable and GDATA archive. Ghidra exports
are research inputs only and are not committed. Additional read-only exports are
kept under `build/leviathan_re`, with the shared `ghidra.lock` held during export.

- Heart actor: **CStageSetHeart**, RTTI `0x5fc7b0`, vtable `0x60f2f0`.
  Constructor/factory `45f840`/`45f150`, setup `45fb20`, dispatch `45fe70`.
  It is a stage actor, not an enemy-pack class. The port uses one enemy-manager
  slot to expose its three targets to existing combat and boss HUD code.
- Stage asset pack: GDATA 688, st302. Its stage-prop PNST contains left lung
  model/motions 0/1, right lung 10/11, Heart 20/21. These are EFM models with
  the MOD skeletal layout: 6, 6, and 9 bones respectively. Stage textures are
  shared. `prepare_leviathan.py` exports all three with 20 native MOT clips.
- Heart health: **6500**, `DAT_005ccf9c`, copied at setup to actor +0x830.
- Lung actor: **CStageSetLungs**, RTTI `0x5fc3f0`, vtable `0x60eb10`.
  Setup `44ffd0`; HP reset `450200`: type 0 **1500** at `0x5c986c`, type 1
  **2000** at `0x5c98cc`. Damage `451b50` excludes its down/recovery states.
- Closed Heart deflects hits; damage `461630` requires +0x80f or +0x810.
  The attack-vulnerable flag multiplies incoming damage by **1.5**. Difficulty
  factors are Easy 2, Normal 1, Hard .9, Very Hard .7, DMD .5.
- Heart exposed handler `460520`: vulnerability delay **55 frames**; exposure
  tables `5cccf0` / `5ccd20`, beginning **600 / 900 frames**, indexed by native
  rank and secondary flag. Lung stun `450d10` uses table `5c96a0`, beginning
  **600 frames**, and signals +0x80a/+0x80b on its owning Heart.
- Attack handler `4607c0`: 70-frame lead-in, 55-frame vulnerability delay,
  laser and projectile modes. Native shell classes are CHeartShl00/01/02.

## Hell Envy

**CEm007**, RTTI `0x5fb8f8`, vtable `0x60dbb0`; setup `42a7c0`.
GDATA 119 (em007): body 1, scythe 26, motions 35. Sparse PNST entries must
be read by their next *present* offset; the older adjacent-offset reader
returns an empty body for this pack.

- Native HP **400**, parameter block `0x5ceba0`, field +0x2c.
- Own AI decision table `0x5cf5b0`, referenced via `0x61a6c0`.
- Own motion scripts/collision from pack chunks 38/39/40.
- Own hit reaction map `0x5ced30`, referenced via `0x61a6bc`.
- Shared Seven Hells animation bank and runtime locomotion.
- EST class 25 maps to the appended `ENEMY_ENVY` model kind.

## Current coverage and approximations

This is a boss/enemy foundation, not a claim that Mission 8 is complete.
The native Heart and lungs render, the side organs can be damaged and recover,
and the core has a protected/exposed/death cycle. Native lung HP and damage
factors are implemented. Lung transitions currently use the exposed timing
with a simplified opening delay; the native hidden rank is temporarily mapped
from difficulty. Target points are selected from native model bones rather
than the complete posed hurt capsules. Render state uses a preliminary motion
selection and does not yet reproduce native per-object visibility switches.

Still required: native shell attacks, charge/orb routing and lung drain,
full state-to-motion mapping, boss encounter entry and death event integration,
acid floors, eye/tusk seals, goo doors, and complete M8 progression. Mission
room routing waits for worker m6's decoded list and the orchestrator's merge
notice. Do not infer route membership from the stage asset used to test the boss.

## Verification

- `pwsh -File tools/build_testroom.ps1 -Regenerate`: passed with Envy.
- `python tools/tests/run_leviathan_logic.py`: passed; checks native MOT parsing,
  closed-core rejection, lung down/recovery, delayed exposure, immortal training
  mode, all five difficulty factors, death exactly once, and finite skinned poses.
- PPSSPP `pM_envy_visible`, shots at 80 and 200: inspected the native Envy and
  scythe in the street. `analysis/testroom/pM_envy_visible_80.png`.
- PPSSPP `pM_heart_foundation`, shots at 60 and 180: inspected the three native
  organs in the native stage arena. `analysis/testroom/pM_heart_foundation_60.png`.
  For this isolated test only, `prepare_stage.py --slot 688` produced STAGE.BIN;
  the ordinary street STAGE.BIN was restored after capture.
- PCSX2 under pM's lock: launched the full100 card save, selected Normal M8,
  inspected the mission opening and acid/eye layout; local capture
  `build/leviathan_reference_start.png`. No claim of boss behavior measurement
  is based on that opening capture. Lock released after the session.

REA's Windows provider rejected this MIPS ELF / installed Ghidra version;
research continued through the repository's Ghidra 12.2 headless workflow in
read-only project mode, with local build output and no tool installation.
