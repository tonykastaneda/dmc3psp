# The ending-credits fight (Mission 20, room 012): reference

After Vergil (room 411) and demos 57 and 58, Mission 20 enters **room 012, start 0**, the ruined street. Dante,
in his usual model, fights the street's enemies with **Lady as an ally**. Meanwhile the staff credits
fade in and out over the action, and a **kill counter** runs at the top right. After a fixed time the screen fades
to black on "PRESENTED BY CAPCOM". With **100 or more kills** an extra line appears. Then demo 59, demo 60 and
Mission Clear.

Sources:
- The ELF (`SLUS_213.61`) and the Ghidra exports. Addresses are below.
- The disc's EventTbl20 (`tools/event_dump.py <iso> 20`) and room 012's EST (GDATA 321).
- A PCSX2 capture (Normal, with the cheats from `docs/PCSX2_CHEATS.md`) described at the end.

[I] marks an inference; everything else was read from code or data, or seen in the capture.

## 1. The event script (EventTbl20, room 12)

```
AFTER_DEMO 58 -> goto_room [12, 0]
ROOM 12
  BLOCK [1]  spawn_group [0, 0]; op_84 [2, 0]               on enter
  BLOCK [0]  op_8c [13620]; op_7e [2]; op_7e [3]; wait [300]; op_7e [4]; play_demo [59]; end_block 1
AFTER_DEMO 59 -> play_demo 60;  AFTER_DEMO 60 -> mission_clear
```

All of these ops are in the interpreter `FUN_0020be20`.

| Op | What it does |
|---|---|
| `84 [2, 0]` | `FUN_00313f50(evt+0x790, 0, 0, 0)`. It switches the HUD counter object (evt+0x790, made by `FUN_0030e820`) to **mode 0: the kill count**. Arg 1 picks mode 1 and arg 0 picks mode 2 (the other counters, e.g. Bloody Palace's). |
| `8c [n]` | Waits **n raw frames**: one per frame, not scaled by game speed, unlike op 0x58. 13620 frames is 227 s. |
| `7e [2]` | Ends the roll. If the staff roll (evt+0x794) exists, it sets roll+0x55 = 1. If the kill count (`*(game+0x20) + 0x8a0`, game = `*0x1d0d70c`) is **≥ 100**, it also sets roll+0x57 = 1, save byte `*(game+4) + 0xa1` = 1 [I: "saw the extended ending"] and `0x1d0de20 \|= 0x80000000`. |
| `7e [3]` | `FUN_0023e800(evt+0x784)`, then `FUN_00195cf0(0x6a2530, 5, 0)` and `(…, 6, 0)`. [I: it stops two of the task manager's groups. In the capture Dante froze in place from this point on.] |
| `7e [4]` | `FUN_0023e810(evt+0x784)` and `FUN_00195d50(0x6a2530)`, 300 frames later. Then demo 59. |

The room's on-enter block runs once, so the clock starts as the room loads. Nothing else ends the sequence: the
mission doesn't wait for the enemies to be cleared, and Dante can't fail it. Can he die here? Not tested: the capture
had Infinite Health on.

## 2. The ending mode flag: game+10

Byte `*(0x1d0d70c) + 10` is **1 for the whole street** (read live), and 0 again afterwards. The scene's set-up,
`FUN_00239b60`, checks it:

- **0:** it builds the normal HUD parts (`FUN_00326470`, `FUN_0035a9b0`, `FUN_0033f9b0`).
- **1:** it builds the staff roll instead (`FUN_00517b70(0)`, stored by `FUN_00210070` at evt+0x794). So the
  health and DT bars, the style meter and the orbs aren't shown.

The counter's draw, `FUN_00313a40`, also hides its second number (digits 0-2) while the flag is set. Only the kill
count (digits 3-5, at most 999) is shown: in the capture, red digits at the top right.

Not found: what sets the byte. [I: the room or mission set-up, for M20 room 012.]

## 3. The staff roll (CStaffRoll, vtable 0x6156e0)

Its methods are `0x517af0` (destroy), `0x517c90` (update), `0x5180e0`, `0x5180f0`, `0x518100`, and `0x518110` (the
fade's draw).

- **Create:** `FUN_00517b70` makes a 0x58-byte object (init `FUN_00517a40`) and adds it to the task manager
  `0x6a2530` (group 0x2c). It sets the clock **+0x48 = 5460.0**, the first record's time.
- **Update:** the clock goes up 1 a frame. The state is byte +4, through jump table `0x5ff490`:

| State | What |
|---|---|
| 0 | Record index +0x50 = 0. → 1 |
| 1 | Load the record's file: `FUN_001afd50(0x6c3810, id, 1, 8)`, with id → file through `FUN_004392b0` and the language (`0x6c37d2`). → 2. An id < 0 → 3. |
| 2 | Wait for the load. → 3 |
| 3 | When record.time < clock and slot (index % 3) is free, make a **page** (`FUN_00518300(file, layout, duration)`) in slot +0x3c[index % 3]. Next record. → 1, or → 4 at the end (time < 0). |
| 4 | Wait for +0x55 (op 7e [2]). → 5 |
| 5 | Fade +0x4c += 2 a frame up to 0x80, drawn by `0x518110` as a 512×256 screen quad. In the capture it **fades the scene to black**; the pages stay on top. → 6 if +0x57 (≥ 100 kills), else → 8. |
| 6, 7 | Load id 4536, then make a page with layout 1 and duration -1 (stays). → 8 |
| 8 | Wait for +0x56. → 9. In the capture it was never set: the scene ended in state 8. |
| 9, 10, 11 | Wait for the slots to empty, then free the object. |

**The records** (`0x5e0d00`, 16 bytes each: float time, id, float duration, layout; the list ends at time -1):

- 35 pages, ids **4501-4535** in order. Times: 5460, 5820, 6180, 6600, 6960, 7380, 7740, 8160, 8520, 8880, 9240,
  9720, 10080, 10440, 10800, 11220, 11580, 12000, 12360, 12780, 13140, 13560, 13920, 14340, 14700, 15060, 15420,
  15840, 16260, 16620, 16980, 17400, 17760, 18120, 18540.
- So a page every 360 or 420 frames (12000 → 12360 is 360). Because the clock starts at 5460, the first page shows
  at once and the last at **local frame 13080**, 540 frames before op 7e [2].
- Durations: 300 for the first page, then 360, and **-1 for the last** ("PRESENTED BY CAPCOM", which stays).
- Layouts (the word stored at page+0x6c4): 1, 0, 2, 0, 2, … alternating, with 2 on page 17 (as on 16) and 1 on the
  last three.
  - No code in the page class reads +0x6c4.
  - In the capture the pages alternate between the left and right halves of the screen, and the last is centred.
  - [I: each page's position is in its own model, and the layout word is unused.]

**The files:** `FUN_004392b0` maps id 4501 + k to the word at gp−0x6358 + 4k. These are file ids `index << 16 |
archive` (docs/CUTSCENES.md), gp = 0x620170.

- **Pages 4501-4535 are GDATA 3140-3174.**
- **Id 4536, the 100-kill line, is GDATA 3175 + language**, from table `0x5c8a50` (0x0c67 0000 + lang << 16). It
  reads "Rock on!! You killed over 100 demons!!" under the logo.
- Each file is a PAC of 2 (266,624 bytes): a `MOD` card model and its texture.

**A page** (CUIDStaffRoll, vtable 0x615710, 0x700 bytes; made by `FUN_00518300`, update `0x518430`):

| State | What |
|---|---|
| 0 | Build the model from PAC [0]/[1] at an identity matrix (+0x680): the card is drawn as modelled. |
| 1 | Fade in: alpha +0x6c8 += 4 a frame to 0x40, given to the model's draw (vfunc +0x84). |
| 2 | Hold: duration +0x6c0 −1 a frame. A negative duration holds forever. |
| 3 | Fade out −4 a frame. |
| 4 | Done: the slot is freed. |

## 4. The enemies (room 012 EST, group 0)

Easy and Normal are the same (Hard and above weren't dumped). They come in order from one thread. Each spawn
waits until **3 or fewer are alive**, then 5 or 10 frames, so **at most 4** are up at once:

1. Lady: EST 55, class 37. `op 8 [0, 1]` makes her variant 1, the ally. Then `op 11 [0]` [not decoded].
2. Four Prides (class 0, variant 2), 5 frames apart.
3. Enemies 5 to 99, mostly Prides (variant 2). Mixed in as every 20th enemy:
   - the 20th: a Lust (class 8, variant 2);
   - the 40th: class 12. That's a **Sloth** by the factory table in docs/GAME_FLOW.md (12-15 Sloth); GAME_FLOW's
     Mission 20 section calls it a Greed.
   - the 60th: a Gluttony (class 4);
   - the 80th: a Lust again.
4. A wait for ≤ 1 alive and 10 frames, the 99th, a wait of 20 frames, then the **Hell Vanguard (class 26)**. That
   is the "100th demon" the walkthrough mentions.
5. `op 10 [0]`, end. **After that nothing spawns**: the street isn't endless. There are exactly 100 enemies.

Totals: 95 Prides, 2 Lusts, a Sloth (class 12), a Gluttony and a Vanguard.

Spawn points:
- 22 points between x 2420-3294 and z 2059-2793, at y 631-658. Points are reused round-robin by the script, 4-6
  times each.
- Lady spawns at (3017, 633, 2512) and the Vanguard at (3079, 633, 2337).
- Dante's start: the capture began at (3193, 652, 2037).

The kill counter is enemy manager +0x8a0 (manager = `*(game+0x20)`). `FUN_002fae40` resets it when a group starts.
Event case op 0x1a reads it for scripts.

## 5. Lady, the ally (CEm037 with +0x485e = 1)

The constructor `FUN_00442610` sets **+0x485e = (spawn variant +0x4a8 == 1)**. In the mode dispatch
`FUN_00444c90`, every hostile mode (0-0x29) then becomes mode 0x30, and the ally uses modes 0x2f-0x33. Her body,
motions, turn rate (1820 a frame, `0x61a4f4`) and actions are her boss ones (docs/ENEMY_SPECS.md §J).

**Target, each frame** (`FUN_0044c8b0`):
- She takes the **nearest enemy to her** from Dante's lock-on list (`player+0x2b58`, `FUN_002f96b0` /
  `FUN_002fa2e0`).
- A sight test (`FUN_00359b40`, gp params at −0x5c10 / −0x5c70) sets +0x4861. Inferred: if Lady is in front of Dante,
  she skips enemies outside her own cone once she has a candidate.
- Its position is copied to +0x150; +0x4860 = 1 if she has one.

**Mode 0x30, aim** (`FUN_00448350`):

| Step | What |
|---|---|
| 0 | Action 0x45 (her walk). → 1 |
| 1 | No target → mode 0x33. Else turn to it; once it's within her cone (±45°, reach 10000, `0x61a500`), action 0x49 (aim). → 2 |
| 2 | Every **20 frames** (timer +0x483c ≥ 20, `0x61a568`) she fires: **mode 0x32** if +0x4838 ≥ 100 (`0x61a570`), else **mode 0x31**. In between she keeps turning to the target. |

In step 2 she also checks her timer +0x4840 and her distance to Dante:
- If the timer is over 600 (`0x61a468`) and he's **between 300 and 600** (`0x61a46c` / `0x61a470`) → mode 0x2f.
- If either of her guns' motions has ended → 0x2f.
- Otherwise she keeps her aim pose (0x49).

+0x4838 is the rocket's counter: `FUN_004497f0` (her shot's event) adds **5** a bullet when her sight flag +0x4861 is set, else 1, up to **100** (0x61a574 / 0x61a570); `FUN_0044cc40` takes **50** (0x61a578) [I: as the rocket goes].

**The other modes:**
- **0x31** (`FUN_00448650`): action **0x85** (the pistol burst from her mode 0x1a; motion [6] 50), turning to the
  target, until the action reports 0x90. → 0x30
- **0x32** (`FUN_00448710`): action **0x56** (Kalina Ann's rocket; 0x57 / 0x58 by height, `FUN_0044ad40`). → 0x30
- **0x33** (`FUN_00448010`, no target): her walk. → 0x2f if Dante is between 300 and 600 (`0x61a45c` /
  `0x61a460`), else → 0x30.
- **0x2f** (`FUN_004480f0`): she moves to a spot and goes back to 0x30 within 100 of it (`0x61a464`).
  [I: the spot is near Dante: she regroups with him.]

**Her shots' damage.** Her bullet is made by `FUN_00483ad0` (0x470 bytes, vtable 0x6106a0). It's given her ally flag
at +0x46d. `FUN_004c89b0` sets up the bullet's hit with **team 3** (hits Dante) or, for the ally, **team 1** (hits
enemies), using the same attack record either way: **0x5cbc40, damage 80**. The next record, 0x5cbc80, is 500. The
port applies it in its own health units (a Pride's 250 is 6): 2 of Dante's pistol hits a bullet, and 18 heavy ones
for the rocket's 1500 (`record_damage`), stopping when the target dies.

**Her aim pose.** Action 0x49 is PAC [5] slot 27 (`ld527`, 7488 bytes on the disc). It's now exported and played.

**Enemies and her.** The Seven Hells' code (0x2c0000-0x2dffff) gets its target from the player (game+0xc, read in 16
functions) and nothing in it refers to her or the ally flag. Only the hostile Lady calls `FUN_003595b0(10, ...)`
and makes the boss gauge (`FUN_00443520`). [I: enemies don't target or hurt her, as in the port.]

Not decoded: whether her kills count (the counter counts every death [I]).

In the capture she stays close to Dante the whole time, shooting the nearest Prides with her pistols and now and
then the launcher.

## 6. The capture (PCSX2, Nov 26, 2026)

The 100% save, Normal, Mission 20 from Mission Select, with cheats `all`. Vergil died in seconds; demo 57 and
demo 58 were skipped with Start. The cheat script then held R1 and pressed Triangle for 255 s.

- **Video:** `analysis/ref/credits/credits_fight.mp4`, 255 s with no audio, from just after demo 58 to demo 59.
- **Telemetry every ~2 s:** `analysis/ref/credits/credits_log.txt`, with the kills, game+10, the roll's state, clock and
  index, +0x55/+0x56/+0x57, and the fade.
- **Stills:** `cf_00.png`-`cf_17.png` every 15 s, and `v_grid.png`. The grid shows a left page at 30 s; the last
  page, 114 kills; the fade with "Rock on!!", 121 kills; NOW LOADING; and demo 59.
- **Save states:** `build/refemu/sstates/m20_vergil_start.p2s` (Vergil's fight, start) and `m20_credits_start.p2s`
  (taken right after skipping demo 58). The second one hasn't been loaded back yet.

All of these are local and gitignored.

Measured:
- The roll's clock was 5927 at 2.4 s and 19178 when op 7e [2] fired (state 4 → 8 at 223.5 s). That matches 5460 +
  13620 + the room's load.
- The kills reached 100 at 187 s and 121 by the end. So with one-hit kills the 100 enemies plus re-counted
  (Lady's?) kills went past 100. [I: there are only 100 spawns, so 121 needs more deaths than spawns. Something else
  counts too, e.g. multi-part enemies or sand-respawned Prides. Not decoded.]
- Dante's position froze from 223.5 s (op 7e [3]). game+10 went back to 0 at 227 s (Now Loading).
- No results screen was captured (the recording stopped in demo 59).

## 7. For the port (what to build)

1. **The flow:** room 012 as now (docs/MISSION20_SPEC.md):
   - Spawn Lady as an ally (variant 1) with the group, instead of leaving her out.
   - Run the clock: 13620 frames → fade (64 frames to black) → the 100-kill line if kills ≥ 100 → freeze → 300 →
     demo 59 → 60 → Mission Clear.
2. **The HUD:** while the ending flag is set, only the kill counter (3 digits, top right). No health, DT, style or
   orbs.
3. **The roll:** the 35 records above. Each page is a textured card model (GDATA 3140-3174) drawn at the identity,
   fading in at alpha 4 a frame to 0x40, holding, and fading out. At most three at once. The last stays. The extended
   card is GDATA 3175 + language (English = 0 [I]).
4. **Spawns:** one list of 100 with the ≤ 3-alive gate. No refill: once the Vanguard is dead the street is empty
   until the timer ends.
5. **Lady:** modes 0x30-0x33 / 0x2f above: nearest-enemy targeting, shots every 20 frames, the rocket by her
   counter, and keeping 300-600 from Dante.

Open questions:
- What sets game+10.
- The meaning of 7e [3] / [4] (task groups 5 and 6 [I]).
- EST ops 10 and 11.
- Lady's +0x4838 and her hurt box.
- The BGM: the capture has no audio. Check it in PCSX2 with sound.
- Who sets roll+0x56. Inferred: the next scene, or nobody.

## 8. In the port (`psp/testroom/credits_fight.c`)

Built from sections 1-5. Each part was compared side by side with the PCSX2 capture, and the comparisons are kept
in `analysis/testroom/` (local).

- **The street.** Mission 20 enters room 012 after Vergil, as before. On entry the module starts the room's clock.
  The room's EST group 0 runs under the script runner as it was (100 enemies, the ≤ 3-alive gates, the Vanguard last).
  - The ending flag swaps the cockpit, the boss gauge and the orb count for the kill counter (`hud_draw_kills`).
  - The counter is id100.pac's ORBS mesh, parts 3-5, with the digit corners of 0x5f5b50.
  - Every enemy death in the street counts, up to 999. Comparison: `credits_s1_vs_ps2.png`.
- **Lady.** Her spawn record's op 8 [0, 1] calls `enemy_set_spawn_param`, which makes `lady_boss.c`'s brain the
  ally (`lady_make_ally`), running modes 0x2f-0x33 as in section 5.
  - enemy.c gives her the nearest enemy as her target and lands her shots on it.
  - Dante can't lock on to her or hit her.
  - The spawn script's alive gates leave her out.
  - Comparison: `credits_s2_vs_ps2.png`.
- **The roll.** `tools/prepare_staffroll.py` writes STAFF_00-35.BIN from the disc (local).
  - The roll runs on its own clock from 5460 with the 35 records, three slots, fades of 4 a frame and the holds.
  - Each page is drawn 4:3 in the middle of the screen. Comparisons: `credits_s3_vs_ps2.png`, `credits_s3b_vs_ps2.png`.
- **The ending.** At clock 13620:
  - The kill count against 100 decides the card.
  - The freeze stops Dante's and the enemies' updates.
  - The fade goes 2 a frame to black, then the card shows if he earned it.
  - 300 frames later the street clears into Mission 20's clear demos 59 and 60, then Mission Clear.
  - Comparison: `credits_s4_vs_ps2.png`.
- **Tests.** `python tools/ppsspp_scene.py <name> --mission 20 --room 12` starts the street directly. Script lines:
  - `creditsat N`: start at frame N of the street's clock.
  - `creditskills N`: start with N kills.
  - `killevery N`: kill everything every N frames.

  The FOCUS log's `credits` line reports the clock, the roll, the kills, the stage, the card and Lady's ally mode.

**Stand-ins** (also in StandIns.md):
- **The counter's place:** matched to the capture.
- **Lady's damage on enemies:** her attack records' damage (80 a bullet, 1500 the rocket), given as that many of
  Dante's hits in the port's health units. Only her target can take them.
- **Lady's estimates and gaps:**
  - 0x85's three shots and the rocket's frame are estimates; her sight flag (5 or 1 a bullet) is taken as set.
  - Enemies don't attack her [inferred: their AI targets the player, section 5].
- **The freeze:** op 7e [3] / [4] stop and resume all of Dante's and the enemies' updates [inferred: task groups 5
  and 6].
- **Not used by the port:** EST ops 10 and 11, and what sets game+10. The port's ending flag is "Mission 20,
  room 012".
- **The music:** whatever the room's BGM table plays. The capture has no audio, so the credits' track isn't checked.
- **Dante's model:** not a stand-in. The PS2 draws his usual Mission 20 model here (open coat, bare chest: `analysis/testroom/dante_ps2_vs_port.png`, the capture against the port). The ending flag (game+10) in the player constructors (002b6450, 002bdd20, 002c49b0, 0034c5a0, 003534c0, 0041c240, 0042a7c0) only skips a HUD part (FUN_0033b610); nothing there swaps his model.
- **Pages:** cut to 256 colours, and loaded from the memory stick as each slot frees.

**The full flow** (`credits_full`, Mission 20 from room 411 with `tools/tests/mission20_clear.scene`): Vergil falls,
demos 57 and 58 play, and room 012 starts the street on its own (`credits on=1 ... ally=1 mode=0x33` at the run's end).
Test runs may now go to 72000 frames, so the whole mission fits. `m20_whole3` (30000 frames, after the lighting and
the master merge of Nov 26 evening) runs Vergil, demos 57 and 58, the street to clock 13920 with 100 spawns and 100
kills, the last the Hell Vanguard, the card, then demo 59.

- **The 100th enemy.** Before 499e089 that run ended on 99 kills with no card. Room 012 has only 64-128 KB of heap
  left (the FOCUS log's `heap` line). The Vanguard came into an enemy slot that had never had one, its skinning
  buffers couldn't be allocated, and `vanguard_spawn` gave up, so the slot dropped him unseen and uncounted.
  - A slot now takes over the buffers of one whose Vanguard is gone (slot 0's from `vanguard_init`) before it
    allocates.
  - The `credits` line now reports `spawns`, `fails` (no free slot) and `last` (the last kill's kind) for this.

**Frame rate.** PPSSPP PERF.TXT, the street with Lady, 4 enemies and a page up:

| Run | Frames a second | Update | Skin | Draw |
|---|---|---|---|---|
| `credits_perf` (stills only), 1800 frames | 56-59 | ~2.0 ms (max 13-26) | 1.5 ms | 8.3-9.0 ms |
| `credits_perf_vid` (`--video`) | 20-23 | ~2.0 ms | 1.5 ms | 9.1-9.8 ms |
| `m2_perf` (Mission 2's street, the port's usual) | 39-58 | 2.2-10 ms | 1.5 ms | 6.0 ms |

- The 20 FPS in the step-2 stills came from `--video`: it has the game write a BMP of every frame. The frame's own
  costs are the same with or without it, about 12 ms of a 16.7 ms frame.
- Without video the street holds 56-59, at or above the port's usual rate. Mission 2's street dips to about 40 in
  its fights.
- The periodic 13-26 ms update spike (2-3 missed frames a second) is there in every room, not just the street.
- Nothing in the street was changed for speed until the Vanguard (below).

**The Vanguard's cost** (Nov 26, after the 100th enemy was fixed and actually came):
- With him up, the street fell to 30 FPS. Draw went from about 10 ms to 16-17 ms, with the stage chunks unchanged.
- On his own in the test street he cost about 7.5 ms more draw than a Pride (12.8 ms against 5.2).
- Most of that was his CPU skinning (`vanguard.c` `skin_part`). For every vertex and influence the robes rebuilt and
  multiplied their bone's matrix, and every part subtracted the bone's rest position.
- Now each bone's transform is made once per part, with the rest offset folded into its translation.
- The shots before and after match within float rounding (largest difference 4 of 255).

| Run | Frames a second | Draw |
|---|---|---|
| `vgskin_before` / `vgskin_after` (test street, the Vanguard alone) | 53-56 / 53-56 | 12.8 / 9.0 ms |
| `vg_perf` / `vg_perf2` (the street, fastwaves, the Vanguard up) | 29-30 / 52-54 | 16.0-16.7 / 11.8 ms |
**The camera-up drop (Nov 27).** A shot at frame 900 looked up into the fog at 30-40 FPS. The sky wasn't the cause.
The FOCUS log's `upd` line (main.c's PROF sections, and the gaps between them) found three costs:
- **Dante held in a ±960 box.** Since master's mode-first startup, only the test street's boot set
  `player_room_limit` (and `player_floor_fn`, `camera_auto_follow`). Every mission kept player.c's default, 960.
  - Whenever he moved he was clamped to ±960, and the wall sweep walked him back across the room in 15 cm steps.
  - That cost 4-10 ms on 211 of 1500 frames, and pinned him at the street's edge (the camera against a wall:
    the "sky" shot).
  - The menu branch now sets them as boot did before.
- **The staff pages' reads.** A page read whole at once took about 13 ms on the PSP's clock. Now sceIoOpenAsync /
  sceIoReadAsync run while the slot is free, polled a frame at a time. The roll's slow frames went from 8 to 1 (a
  page due before its read is done).
- **The sky pass** drew every chunk of every BATCH_SKY set, culled or not. It now uses `chunk_visible`, which tests
  the sides and the eye, not the far plane.
  - Draw: the Palace's room 423 (7,782 sky vertices) 8.0 → 6.8 ms; the street 9.8 → 9.3 ms. The shots are the same
    (the Palace's differs only in the FPS digits).
  - The rooms with sky sets: 012, 423-425, 427-432, 434, 442, 444.

`stage_collide` also skips a wall triangle out of reach by its bounds (the per-frame log is unchanged over
1500 frames). In a focus run, `mission_update` spends about 12 ms every 30 frames on M6_LOG.TXT; `notrace 1` turns
that off along with the bosses' logs.

| Run (room 012, 1500 frames, `notrace 1`) | Frames a second | Update |
|---|---|---|
| before (`sky_gap`) | 53.4 (dips to 36) | 2.8 ms |
| after (`sky_fix1`) | 58.6 (47 in the loading second) | 1.7 ms |

## 9. The street's look (fog, sky, brightness)

Before this the port's street was nearly black with purple enemies (a matched view averaged 28/13/23 against the
PS2's 97/58/81). Three causes, each checked in PCSX2:

- **Fog.** Room 012's LIG2 header has a second colour, (95, 90, 100). The game keeps a float copy at 0xec4ed0.
  Turning it red in PCSX2 turns the sky and the street red, more with distance, so it's the room's fog colour.
  - The port draws the 3D pass with `sceGuFog` in that colour, and clears the screen to it (the sky's colour behind
    the dome). The 2D pass (counter, roll) isn't fogged. `credits_fight_fog()`, called from `render_scene`.
  - The fog's distances aren't in the room's data that was read. Near 0 / far 4100 cm is fitted to the fog factor
    measured in PCSX2: red fog against the normal one, about 0.17 at 0-1000 cm [estimate].
- **The sky dome.** st012's model 0 ("light scr") is a cloud dome up to y 45052, past the port's 16000 far plane.
  Stage sets reaching above 12000 are flagged (batch flag 8) and drawn first, without depth, with the far plane at
  80000 (50c1a9c).
- **Brightness.** The PS2 shows st012's stage at about 3.5 times the port's usual stage brightness (1.25 times the
  vertex colour). Measured with PCSX2 and the port at the same camera, with the fog's share taken out.
  - `tools/prepare_stage.py` bakes slot 320 (st012) at half of 1.25 × 3.5. The runtime draws it with
    `GU_FRAGMENT_2X` (`stage_set_double`), so colours can go past 255 the way the PS2's doubled vertex colour does.
  - Why the PS2 draws this room brighter isn't decoded (the room's material or light records) [estimate].

Matched view (eye 2906/1088/1950, `analysis/testroom/match2_vs.png`, local): the port averages 91/61/81 against the
PS2's 97/58/81.
