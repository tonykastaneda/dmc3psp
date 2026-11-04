# Enemy AI: the Seven Hells (CEm000 / CComEm000)

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
  it 3.0 (`FUN_00369180`). The port treats this as "no one else may attack for 3 seconds".
- **The rank.** The game takes it from a mission object not read yet. The port fixes it at 0, which
  gives what Mission 2 on Easy shows on the HD Collection: plans 27, 32, 51 and 52.
- **How hard Dante's moves throw it.** The game reads an angle and speed from each move's hit record.
  Dante's moves don't carry those yet, so `HEAVY_*`, `LAUNCH_SPEED` and `SLAM_SPEED` are chosen by eye.
  `LAUNCH_SPEED` gives a rise of about 250 cm.
- **Dante's hurt shape.** The port uses a vertical capsule, `DANTE_*`, not the game's.
- **Knock-back on Dante.** Heavy or not comes from the attack type (`OP6`): the wide swing counts as
  heavy. That reading is from the record's values, not from code.

Not ported yet:

- Guarding (plan 15, slot `0x24`): can't fire at rank 0, where its list only holds "don't".
- Rage (plan 41, `FUN_0032bc90`): only fires when another enemy dies, and these don't.
- The spawn-in (plan 14, action 13). They start already walking.
- Being pushed into a leap (`M[0x34]`, plan 24), hits while lying down, and hitting walls.
- Stage collision on the enemies' own moves.
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
