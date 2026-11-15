# Enemy AI: the Seven Hells (the Hell Pride, CEm000 / CComEm000)

## Collision audit (2026-11-14)

`enemyaudit 1` in a scene writes `ENEMY_AUDIT.CSV` beside its EBOOT. Each
violation records mission, room, scene frame, enemy slot/kind, other slot,
category, depth in centimetres and feet position. The footer records checked
frames, active bodies and violations. Sampling runs after Dante's existing body
and wall pass. It is observational: it never corrects an actor. The 0.5 cm
tolerance excludes floating-point contact noise.

Run from a built worktree with its slot set:

```
$env:DMC3_SLOT='pM'
python tools/enemy_audit.py --label baseline
python tools/enemy_audit.py --label stress --stress
```

The runner discovers Normal encounter rooms from the generated mission scripts,
including secret-room encounters once, and preserves CSVs, scene logs, screenshots
and `results.json` under `analysis/testroom/enemy_audit_<label>/`. `--mission`,
`--room`, `--frames` and `--resume` narrow or resume a run. Missing files, truncated
captures and mismatched row counts fail. `EMPTY` explicitly means no enemy was
observed; it does not certify a fight. Scenes accelerate timed spawn waits, but
retain live-count gates. `enemystress 45` cycles heavy knockback, launch, juggle
and slam hits through every active enemy every 45 frames, using normal hit handling.

Wall checks query the loaded stage on a copy of the actor. Floor checks use the
mission floor callback (including platforms). Bounds are the stage's XZ bounds;
missing floor is checked separately. Airborne reactions, authored leaps and
flying kinds may be above the floor. Ordinary body checks retain the port's
measured 48 cm radius and 170 cm vertical overlap interval; these are not newly
decoded per-kind native shapes. Special bosses use their own existing push
queries against Dante and ordinary enemies. The Gigapede intentionally crosses
arena walls on its authored path; offstage segments and hidden teleport phases
are excluded. This checks the port's collision proxies, not rendered mesh overlap.

Initial measurements before changing movement:

| Mission / room | Frames | Violations | Maximum depth / finding |
| --- | ---: | ---: | --- |
| 1 / 000 | 600 + initial frame | 16 | Enemy 62.035 cm; Dante 16.413 cm |
| 3 / 002 | 600 + initial frame | 0 | 2,995 body samples |
| 3 / 004 | 600 + initial frame | 1,707 | Enemy 77.649 cm; outside bounds 513.081 cm; 866 missing-floor samples |

These are baseline results, not a claim of zero clipping or complete mission
coverage. Full regression results follow as fixes are verified.

Reference check, 16:29-16:30: loaded the shared PCSX2 save in Mission 2 and
sampled the five `CEm000` actor roots through PINE 120 times at roughly 10 Hz.
The smallest grounded enemy-enemy distance was **95.807 cm**; a preceding
object census showed a Pride **88 cm** from Dante. Local evidence is
`analysis/testroom/enemy_ps2_spacing.json`, `enemy_ps2_loaded.png` and
`enemy_ps2_spacing.png`. The exclusive PCSX2 lock was released immediately.
The disc's Seven Hells pack also gives its root kind-6 sphere as radius 50 at
local y=50 (descriptor 0 in chunks 39/40). Kind-6 descriptors are hurt shapes;
they do not establish the native terrain or push radius. This audit retains
the measured 48 cm body separation radius rather than treating hurt data as
a newly decoded contact shape.

First correction: Sloth warps retain their original radius and first random
heading, then try 31 other headings with floor, storey and wall clearance;
failure keeps the previous position. Ordinary enemies now sweep both horizontal
travel and height in steps of at most 15 cm, retain support at open arena edges,
and update floor height after wall correction. Invalid spawns search locally for
supported placement instead of inventing floor at y=0. A final symmetric body
solve runs after Dante's existing body/wall pass and handles airborne and exactly
coincident enemies. Every correction to an enemy also meets the stage walls.
These constraints adapt the port's collision engine; they are not a translation
of the PS2's complete contact solver. No Dante movement code was changed.

| Mission / room | Before | After | Samples | Evidence (`analysis/testroom/`) |
| --- | ---: | ---: | ---: | --- |
| 1 / 000 | 16 | 0 | 601 | `enemy_collision_v1_m01_r000_599.png` |
| 3 / 002 | 0 | 0 | 601 | `enemy_collision_v1_m03_r002_599.png` |
| 3 / 004 | 1,707 | 0 | 601 | `enemy_collision_v1_m03_r004_599.png` |
| 3 / 006 | 0 | 0 | 601 | `enemy_collision_v1_m03_r006_599.png` |

Pawn hops use `PW_RISE`/`PW_FALL`, not the common airborne flag; the audit now
recognizes those native hop states. Gigapede visibility likewise comes from
its segment renderer rather than the skinned actor's hidden flag. Direct-room
tests can use `enemyaudit 2` / `--force-encounters` to bypass item/puzzle holds;
normal play and `enemyaudit 1` keep those gates. `--eboot` pins a scene or batch
to a saved executable for reproducible before/after captures.

The corner correction also tries nearby supported points around a pinned body,
accepting a reduction in total penetration and sweeping every candidate against
the room. Airborne enemies retain their ground-level wall footprint, preventing
a launch from clearing a doorway barrier and landing in a sealed pocket. This
is a conservative port constraint, not a decoded native pathfinding rule.
With the corrected grids, M6 / 126 changed from 58 contacts to **zero** over
601 samples (`enemy_footprint_v4_batch_002_599.png`); M1 remained zero.
M3 / 009, M7 / 136 and M18 / 401 also passed the corner suite. The audit now
uses native collision bounds rather than visible-scenery bounds, and never
checks a Gigapede segment against its own rideable back as floor. Clearing a
room removes the old Gigapede's dynamic floor as well as its actor slots.

`tools/enemy_audit_sweep.py --label <name>` batches direct-room captures using
fixed simulation steps. It records each room's frame/body/violation counters,
CSV, screenshot, command and executable hash, including zero-contact rooms;
incomplete batches fail. `--stress` adds reactions, and `--kill-every` can retire
waves after they have been exercised. Lift cases activate and ride the platform;
M8's Heart test seeds its normal Ignis-Fatuus-use prerequisite. Audit-only
`auditfast 1` omits the vblank wait; it is not a performance benchmark.

### Native collision grid coverage

The stage grids formerly used only rendered floor bounds. In room 304 this
made 752 of 905 native collision triangles unreachable by the runtime grids:
the grid was 7 x 9 cells around (7600,4400), while the EST enemy spawns lie
around x=3359..6325, z=1700..2600. Native floor exists at those spawn points
(for example 291.379 at 4142,1739), but the lookup returned no floor.

`prepare_stage.py` now sizes all three grids from rendered floor **and native
walls/floors**, with wall-query clearance. If rendered floor is absent, native
floor is retained before considering a synthetic deck. The build runs
`tools/audit_stage_grids.py --repair` to regenerate truncated cached stages
from the player's ISO. This repaired **74 / 143** local stages; all **143 / 143**
now contain every native collision vertex. The generated assets remain local.

PPSSPP room 304 changed from **1,520 missing-floor reports to zero** across
600-frame captures; the new run checked 2,990 active-body samples. M1 stayed at
zero (1,800 samples). Evidence: `enemy_grids_v3_batch_000_599.png`,
`enemy_grids_v3_batch_000_1199.png`, and `enemy_stage_grids_before/after.json`
under `analysis/testroom/`. Four stage-grid/edge tests pass. This also restores
floor lookup for Dante in the tunnel; no `player.c` change was needed.

### Posed bodies and independent boss actors

Root distance alone missed the visible M1 overlap: a Pride's torso could lean
into Dante while its feet remained 88 cm away. The posed bone-3 body centre
now supplements the root cylinder, using the measured 48 cm body radius;
the solver retains the 88 cm minimum root spacing. Both torso/Dante and
torso/enemy intersections are logged and constrained. The M1 probe recorded
108 torso reports (maximum **36.435 cm**) before this correction and **zero**
after it, across 601 frames / 1,803 active-body samples. Inspected captures:
`enemy_m1_torso_after_500.png` and `enemy_m1_torso_after_599.png`.

- **Vanguards:** each enemy slot now owns its position, health, animation clock
  and posed vertex storage. The shared global state had placed both Secret
  Mission 8 Vanguards at exactly the same point and advanced one clock twice.
  Their pair had **825 full-depth overlap reports** in the before fixture;
  the after fixture has **zero** over 901 frames. Static textures and mesh
  indices remain shared. The existing Vanguard body policy uses radius 50.
- **Bishop:** a motionless idle skins the bind pose even in a reused slot.
  Previously its missing `bishop-rest` motion could sample a null pointer
  when that slot retained a different pose frame. The Goyle-to-Bishop batch
  now completes; a new actor also receives a valid first pose.
- **Geryon:** ground movement sweeps the existing horse/cart footprint against
  the room, including neighbouring grid cells for its larger radii. A charge
  or turn retains its previous clear pose if it would crush Dante against a
  bridge end; the hurt reaction still runs. M18 / 417 changed from 28 wall
  reports to zero; M12 / 228 changed from 15 Dante reports to zero. These
  adapt the port's existing footprint, not the full native collision solver.
- **Pride attack records:** OP6's value is an index, not a heavy-hit flag.
  `*0x618c28 = 0x5a7600`: records 0 and 1 have damage 700/800, kind 0 and
  reaction 350/400. Both now deliver kind 0 to Dante; the former index-1
  heavy inference was incorrect. This was checked against the executable.

Corpses retain wall/floor checks but are excluded from live body contacts.
The test-only minion-clear command retires actors rather than leaving a
brain alive behind a death flag. Leviathan's fixed organ hurt anchors are
checked against scene bounds, since the wall-mounted organs extend beyond
the walkable-floor envelope.

Gigapede entry/exit contacts on its authored carapace path are retained as
`expected_wall` records, with separate counters. Segment adjacency and rays
along that carapace identify a scripted boundary crossing; other in-room
contacts still fail. These records are not silently discarded or counted as
zero mesh intersections. The collision audit continues to test proxy bodies,
not weapons, cloth or exact triangle-to-triangle character meshes.

The merged-build observation sweep covered all 93 scripted encounter cases:
55,819 frames and 194,491 body samples, with no empty or incomplete captures.
Its remaining ordinary-body case was M18 / 420: nine contacts during a landing
against a doorway shoulder. Extending the wall-swept escape search from four
to eight rings clears that exact 419-to-420 replay, with zero reports across
601 room frames (`enemy_m18_corner_final_1115.png`, also frame 1119).
Direct-room audit mode retains completed rooms until their requested sampling
time ends, avoiding the blocking Mission Clear menu during wave-retirement tests.

Ported from the PS2 executable (`SLUS_213.61`) and the enemy's own data. The code is in
`psp/testroom/enemy.c`. `tools/gen_enemy_ai.py` reads the data off the player's disc at build time, so
none of it is in the repo.

The class is confirmed by its RTTI: `FUN_002b6450` is `CEm000`'s set-up, and its AI table is `0x5a8300`.
Five more classes share the same code with their own tables: `2bdd20` (`CEm001`), `2c49b0`, `34c5a0`,
`3534c0`, `42a7c0` (`CEm007`).

## Three layers

| Layer | Game | What it does |
| --- | --- | --- |
| Plan | byte at `+0xb4`, updated by `FUN_00367670` | What it means to do. Most run one action, and go back to deciding when it ends. |
| Action | byte at `+0xcc`, enter `FUN_0036bff0`, update `FUN_0036b940` | A state with its own turning, jumps and reactions. Action 14 does nothing. |
| Script | chunk 38 of the pack, run by `FUN_00254f10` | Which motions play, from where, and when attacks are live. |

`A[n]` in the decompiled code is the same object through its second vtable (`this + 0xa0`):
`A[0xc]` reads the action, `A[0x28]` sets it.

## How it decides

Plan 24 walks and watches. Each frame it adds to a gauge from the first zone Dante is in:

| Within | Of its facing | Gauge per frame |
| --- | --- | --- |
| 250 cm | 45 degrees | 10 |
| 1500 cm | 1 degree | 0.5 |

Past the threshold (100 at rank 0) it resets the gauge and picks from a weighted list
(`FUN_0032b7b0`). The list depends on whether it got the attack token, the row on Dante being under
300 cm, and the column on the rank. At rank 0:

| | Under 300 cm | 300 cm or more |
| --- | --- | --- |
| With the token | swing (27) | leap 70, taunt A 15, taunt B 15 |
| Without | leap 70, taunt A 15, taunt B 15 | the same |

Higher ranks add the wide swing (28) and side steps (33, 34).

## What each plan does

| Plan | Action | Script | Motion | Notes |
| --- | --- | --- | --- | --- |
| 24 walk | 15 | 1, 2 or 3 | `hl-01`, `02`, `03` | Root moves it 1.8 to 2 cm a frame. Turns at most 0x100 a frame. |
| 27 swing | 16 | 47 | `hl-47` | **Sphere live at frame 63 for 4 frames.** Turns 4 degrees a frame over frames 15 to 30. |
| 28 wide swing | 17 | 48 | `hl-48` | **Live at frame 65 for 8 frames**, on the scythe hand. |
| 32 leap | 21 | 55, 56, 57 | `hl-05`, three pieces | One of two arcs at random: up 44, forward 4, gravity -2, or up 33, forward 8, gravity -1.5. |
| 33, 34 step | 22, 23 | 8, 9 | `hl-08`, `hl-09` | About 200 cm sideways. |
| 51, 52 taunt | 58, 59 | 85, 86 | `hl-65`, `hl-66` | |

The swing's frame 63 was also measured on the HD Collection before the script was decoded.

Measured against the HD Collection in Turbo: the swing lasts 143 to 144 frames and hits at 63 to 64, the
taunts last 233 and 223, and a leap lasts about 64 and peaks at 458 or 343 cm. All match the port.

## Being hit

| Reaction | Action | Motions (scripts) | Notes |
| --- | --- | --- | --- |
| Flinch from the front | 0 | `hl-16` or `hl-17` | Turns at most 0x200 a frame. |
| Flinch from behind | 1 | `hl-17` or `hl-18` | |
| Thrown | 2 | `hl-18` from frame 0, then from 22, `hl-56`, then `hl-20` to get up | Gravity -0.54. Slides on landing, then lies for 45 to 90 frames. |
| Hit in the air | 12 | script 33, then `hl-23`, `hl-29`, `hl-55`, `hl-21` | Hangs for 10 frames, then falls. |

## Measured, or a guess

Things that are still not the game's:

- **The attack token.** The manager at `(0x1d0d70c + 0x20) + 0x840` hasn't been read. The game passes
  it 3.0 (`FUN_00369180`). The port treats this as "no one else may attack for 3 seconds", which fits the
  HD Collection: 49 swings from 4 enemies, never under 3.0 real seconds apart.
- **The rank.** The game takes it from a mission object not read yet. The port fixes it at 0, which
  gives what Mission 2 on Easy shows on the HD Collection: plans 27, 32, 51 and 52.
- **How hard Dante's moves throw it.** The game reads an angle and speed from each move's hit record, and
  Dante's moves don't carry those yet. Measured so far: a combo's throw is flat at speed 20, and High Time
  uses action 7, rising to 302 cm in Turbo. `SLAM_SPEED` is still a guess, and which moves throw at all is
  based on few samples, since a maxed Dante kills too quickly to measure much.
- **Being pushed into a leap** (`M[0x34]` in plan 24). The port does it when another enemy shoves one more
  than 1 cm a frame. On the HD Collection 44% of leaps start within 8 frames against about 19% of taunts, at
  all distances from Dante, which is what suggests this. With it the port's leap share is 81% against 83%.
- **Bodies.** Enemies stop 96 cm apart and 88 cm from Dante, measured. How much Dante gives when pushed
  (`ENEMY_YIELDS`) is an estimate.
- **Dante's hurt shape.** The port uses a vertical capsule, `DANTE_*`, not the game's.
- **Knock-back on Dante.** `OP6` selects an attack record. Pride's records 0 and 1
  both specify light kind 0; the wide swing increases damage to 800 rather than
  changing that kind. See the executable check above.

Not ported yet:

- Guarding (plan 15, slot `0x24`): can't fire at rank 0, where its list only holds "don't".
- Rage (plan 41, `FUN_0032bc90`): only fires when another enemy dies, and these don't.
- The spawn-in (plan 14, action 13). They start already walking.
- Hits while lying down, and hitting walls.
- Effects and sounds (`M[0x48]`), and DMD's Devil Trigger on enemies.

## Testing it

```
python tools/ai_trace.py 1 1800                        # one enemy, Dante standing still
python tools/ai_trace.py 8 1500                        # eight
python tools/ai_trace.py 1 900 "attack 0" "hit 30 2"   # a standing one, launched on frame 30
python tools/psp_bench.py ramp ai100                   # 100 on the PSP, logic only
python tools/psp_bench.py ramp draw100                 # 100 on the PSP, drawn
```

Hit kinds: 0 light, 1 heavy, 2 launch, 3 air, 4 slam.

## The other kinds (2026-11-04)

From the class set-ups and the factory table, decoded in `docs/ENEMY_SPECS.md`:

- **Every Seven Hell** shares this code, the scripts (chunk 38) and the `hl-NN` motions. Each has its own body
  (em000.pac [1] Pride, [5] Gluttony, [8] Lust, [13] Sloth, [18] Wrath, [19] Greed) and decision table.
  `tools/gen_enemy_ai.py` reads the Pride's, Gluttony's (`0x5ab9d0`) and Sloth's (`0x5b0e60`) tables.
- **Hell Sloth:** plans 46 to 49. Warps (actions 41, 42, 43) hide it from frame 44 to 54 of `hl-46` and put it
  900, 600 or 300 cm from Dante at a random angle. The warp strike (57) reappears 300 cm away into the Pride's
  swing from its frame 25.
- **Hell Gluttony:** plan 45 winds up (action 56) and spews (31): at frame 129 of `hl-52`, a burst of three
  spheres of radius 100 reaching 400 cm ahead, a heavy hit.
- **Enigma** (CEm013, em013.pac): turns in place. Its three pairs of bow arms each have a cooldown (rank 0
  tables). Each volley takes the same 3-second token and is one arrow or a fan of three. An arrow homes when
  Dante is within 1000 cm (35 a frame, up to 65, 240 frames), and is lobbed onto his feet otherwise.
  The arms' motions play over their own bones only. Hops, ledge falls and statue spawns aren't in.
- **Gigapede** (CEm023, em023.pac): see `docs/ENEMY_SPECS.md` section C ("In the port"). Head and 7
  segments, each an enemy slot, following the routes of its 18 baked paths: homing and rolling orbs, belly
  beams, shells, the roll and the shake-off when Dante stands on it.
- **Health** in light hits is the Pride's 6 scaled by the parameter blocks' (inferred) health: Gluttony 700,
  Sloth 1200, Enigma 600, Gigapede 2800, against the Pride's 250.

Tested on the PSP with `CMP_SCRIPT.TXT` options `kind n` (the dummies' kind), `lineup 1` (one of each) and
`aitrace 1`. In a run on the PSP the Enigma let go 5 arrows in 23 seconds, all landing on a standing Dante. The
Sloth chose plans 46 to 49, and the Gluttony 45.
