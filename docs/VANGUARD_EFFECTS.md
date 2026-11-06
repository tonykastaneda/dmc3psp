# Vanguard teleport presentation

The visibility flag alone does not reproduce the PS2 teleport. The native model visibility functions
`001a2020`/`001a1ea0` only change draw flags. `002b4280`/`002b4350` apply those flags to the body,
robes and scythe; separate event calls create the accompanying effects.

## Visibility and timing

- Action entry (`0047e730`, `0047edf0`, `0047f4e0`, `0047fc40`) requests event `1f`, which dispatches
  V702 (`00285820`). Vanguard is untargetable but remains drawn during the departure presentation.
- After 30 frames the model is hidden. The relocation occurs while it is hidden.
- Event `15` dispatches V499 for a normal/rush arrival; `16` dispatches V516 for the uppercut arrival.
  These effects begin while the model is still hidden. The model returns only at the native reveal
  call (`387`), after the corresponding 30/50-frame buildup.
- Untargetability (`hidden` in this port) and model visibility (`visible`) are now separate states.
  The first implementation hid immediately on departure and showed the uppercut actor 50 frames early.

## Original assets and supported subset

GDATA slot 120, PNST child 24 contains an ID list and a nested effect bundle. The loader at `002091e0`
consumes one child per ID, except M entries, which consume two. `tools/vanguard_fx.py` exports directly
from that bundle, without fabricated replacement textures.

| Timeline | Native children (effect type, ID, delay in 60 Hz frames) |
| --- | --- |
| V702 departure | P312 at 1; E796 at 10 and 20 |
| V499 arrival | P312 at 15; E1193 at 25; E796 at 35; E1311 at 1; E1312 at 2 |
| V516 uppercut arrival | P312 at 45; E1193 at 50; E796 at 55 and 47; E1311 at 31; E1312 at 32; E797 at 0 and 20 |

The port now renders **E796, E1193, E1311 and E1312** with their authored V delays, translations and
scales, 40-frame lifetimes, scale curves (`001f1230`, `001f12e0`), color curves (`001f8ce0`, `001f8e40`),
and atlas rectangles (`001ec360`). T25 is the original 256×256 sheet with A117 atlas selection; T48 is
the original 128×128 light-burst sheet. Visual randomness uses the existing FX random stream and does
not perturb AI decisions. Effects remain drawn independently of the hidden boss model.

This is a **subset of the original effect runtime**, not complete visual parity. P312's VU particle
emitter (`ee008-50p0`) and the E797 model children are preserved as raw assets but are not rendered yet.
The sprite renderer currently uses the first native corner color uniformly; complete native vertex
gradients, particle programs and model effects remain unfinished. Those children are omitted rather
than replaced with invented effects. Cloth simulation and sound remain separate unfinished work.

`tools/tests/vanguard_effects.c` tests native V scheduling, E scale/alpha curves and valid original
atlas coordinates. `tools/tests/vanguard_logic.c` checks visibility separately from hit immunity.
Run both with `python tools/tests/run_vanguard_logic.py`; GPU calls are no-ops in the host tests.
PPSSPP frame capture is still required to review rendering.

## Dissolve and P312 stand-in

Two defects made the teleports "blink":

- **Evicted sprites.** All effects shared one 56-sprite round-robin pool. Each E&I shot spawns three flash
  sprites and each hit spark four, so gunfire overwrote the teleport's 40-frame native sprites mid-effect.
  The pool is now 128, and a live native sprite is never the one recycled.
- **No body presentation.** P312 (`ee008-50p0`) is a VU particle program. The 50 points come from em008's own
  mesh, and the record gives a 40-frame life and colour 80 a0 c0. With nothing in its place the model simply
  popped off at the hide call and on at the reveal.

The port now does the following. It is presentation only and does not change visibility, hit immunity or timing:

- **Departure.** The model fades out over V702's 30 visible frames. Points on his posed skeleton drift off the
  body, starting from V702's P312 delay (frame 1).
- **Arrival.** Points gather into the body from V499's P312 delay (frame 15), or V516's (frame 45). The model
  fades in over 12 frames from the native reveal.

Visual randomness uses `fx_random_u32`, not the AI's stream. This is a stand-in for P312 from its recorded
numbers, not a port of the VU program.

## Solo encounter

The preceding Mission 2 waves are gated by the living count. Dead actors can still draw during their
death motions, particularly when the recording harness accelerates waits. Before spawning class 26,
the transition now refuses any living earlier enemies, retires defeated actors, and clears their
projectiles. The encounter trace records `encounter_enemies value=1`; the emulator regression requires
exactly one actor at this transition. No ordinary spawn follows the boss in the generated script.
