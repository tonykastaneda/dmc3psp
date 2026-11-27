# The heap on the PSP: what holds it, and how to measure it

## Measuring

- **Free heap after a run.** Every focus run's FOCUS_DONE ends with
  `heap free_kb=N largest_kb=M`. `heap_probe` (main.c) measures it the same way MEMORY.TXT does at boot: it
  allocates blocks of 16 MB, then halving down to 64 KB, and touches them.
- **By call site.** Build with `pwsh -File tools/build_testroom.ps1 -HeapStat`.
  - `heapstat.c` wraps malloc, memalign, calloc, realloc and free and keeps each call site's live bytes.
  - HEAPSTAT.TXT gets a dump at boot and at the end of each run (labelled with the room and the heap left).
  - `python tools/heapstat_report.py` names the sites with psp-addr2line.
  - The tables take 128 KB of the test build's memory. Don't ship a -HeapStat build.
- **Starving it.** The script line `heapleave KB` holds all but about that much of the heap from the room's start,
  to see what a room does when memory runs out.

## What holds it (Nov 26, 2026, Mission 20's street)

| Site | Live | Notes |
|---|---|---|
| `stage_load` | 5400 KB | One reserve, taken at the first load, big enough for the largest room. A cutscene keeps its data in the spare tail (demo.c). Kept as it is: it stops fragmentation. |
| `parse_ordered` (motion.c) | 3597 KB | All 1375 motions, parsed at boot. |
| `menu_load` | 2378 KB | MENU.BIN without its pictures (was 10266 KB, below). |
| `enemy_init` | about 1860 KB | Every kind's index and weapon buffers. |
| Bosses' `*_init` | about 2700 KB | Vergil, Cerberus, Geryon, Arkham, the Doppelganger and the Vanguard. Allocated at boot for every mission until the boss data change (below). |
| `page_load` (credits_fight.c) | 768 KB | The staff roll's three page slots. |
| `spawn_one` | 106 KB a slot | An enemy slot's skinned vertices, made the first time the slot is used. |

## What was changed

- **MENU.BIN's pictures (c91a796).** The 29 full-screen pictures (title, Mission Select, the 20 banners, clear,
  shops: 512 × 272 at 16 bits, 7.9 MB) had been held in memory through every mission.
  - They now stay in the file. `image_get` (menu.c) reads the one on screen into a single 272 KB buffer when it
    changes. Only one is drawn a frame.
  - Shots before and after are the same pixel for pixel.
- **The bosses' model buffers (Nov 26).** Each boss module's `init` now only looks up its motions at boot.
  - The skinned vertices, index copies and swizzled textures are made by its `load_data` at the boss's first spawn
    (and for Vergil drawn as the partner).
  - `X_data_release` frees them when no such boss is up. `enemy_set_count(0, ...)`, which every room change calls,
    runs all six releases.
  - A boss fought again (a Palace boss floor, a retry) is made again at its spawn.
  - The Vanguard's spawn already makes or borrows its slot's vertices, so his `load_data` makes only the indices
    and textures.

| Room | Free before | Free after |
|---|---|---|
| Mission 1 | 7.5 MB | 10.4 MB |
| Mission 20 room 012 (the street) | 6.3 MB | 9.2 MB |
| Boss rooms with their boss up: Cerberus 6, Geryon 228, the Doppelganger 139, Arkham 421, Vergil 411, Mission 2's Vanguard | 7.5-8.3 MB | 8.4-10.4 MB |
| Palace boss floors 433 (Cerberus) and 439 (the Doppelganger) | about 7.2 MB | 9.5 and 9.9 MB |

The "before" column is the Bloody Palace merge build, whose larger effect banks use about 0.7 MB more than c91a796.

Shots against a build that loads at boot (`lzold_*` / `lz_*`, the same frames):
- Doppelganger, Arkham, Vergil and Palace floor 439: identical.
- Geryon at frame 400: within 10/255.
- Cerberus at frame 850: 7 single pixels of his room's ice motes. His model is the same.

Free heap after the MENU.BIN change alone (c91a796, before the Palace merge), 300 frames into each room:

| Room | Free |
|---|---|
| Mission 20 room 012 (the ending street, 4 enemies, a staff page) | 7.0 MB (was 0-128 KB) |
| Mission 20 room 411 (Vergil) | 8.1 MB |
| Rooms 100, 120, 206, 143, 237, 225, 420 (the largest stages) | 8.1 MB each |

The stage reserve is a fixed size, so a room's own size doesn't change what's left. What changes it is what the
room adds: its enemy slots, effects and pages.

## When it runs out

With `heapleave 1` in the street (about 0 KB left), enemies whose slot hasn't had one before can't get their
vertices.
- `spawn_one` leaves the slot empty and the script counts the spawn, so the enemy never appears.
- The street then ends with 14-15 kills out of 100 spawns. Nothing crashes.
- The Vanguard takes over a gone Vanguard's buffers (499e089), so he still comes.

The PPSSPP crash seen twice near the Vanguard's spawn on Nov 26 (exit 0xC0000005, a build with 0 KB free and
without 499e089) hasn't been reproduced:
- 4 runs of the street's auto-fight with the current build;
- 3 more with the heap starved;
- 2 starved with 499e089's buffer reuse switched off.

All of them run to the end.
