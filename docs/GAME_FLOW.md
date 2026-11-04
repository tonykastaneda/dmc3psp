# Game flow: menus, cutscenes and Missions 1 and 2

The port follows the scenes named in the executable's RTTI. Only Easy and Normal are offered for now.

| Scene | Real game | Port |
| --- | --- | --- |
| `CSceneBoot` | Saving-icon notice, needs OK | Not in |
| `CSceneOpening` | Warning, logos, opening movie, then the title. Left alone, the title goes back round after about 30 s | Opening movie, then the title |
| `CSceneStartMenu` | New Game, Load Game, Options | All three shown. New and Load both go to Mission Select. Options is locked |
| `CSceneMisSelect` | A row per difficulty, Bloody Palace, Demo Digest, Total Ranking, Gallery, Save, Load | Easy and Normal rows, mission 1 or 2. SQUARE toggles the Turbo tag |
| `CSceneMisStart` | Banner, Mission Start, Customize | Banner, Mission Start. Customize is locked |
| `CSceneGameMain` | The mission | The mission, see below |
| `CSceneResult` | Mission Clear with ranks and results | Mission Clear heading only, then back to Mission Select |

Things measured on the HD Collection that shaped this:

- Choosing Mission 1 plays the opening movie and the Mission 1 intro, 326 s in all, and they can't be skipped. Only then comes the briefing, with Customize highlighted. The port lets you skip.
- Mission 2 opens with about a minute of "you guys wrecked my shop", then Now Loading. No movie on the disc shows it, so it's taken to be a real-time scripted event (GDATA has 22 `EVT` entries, not read yet). The port has no intro for Mission 2 until those run.
- The pause menu is Status, Save Data, Quit the Mission, Restart the Mission, Options, Exit. Restart goes back to the briefing on the real game. In the port it restarts straight away, without the movie. Status and Save Data are locked, and the debug toggles live under Options.

## Purpose-built for 16:9

The originals are 4:3. `tools/prepare_menu.py` recomposes each screen from its layers instead of stretching:

- **Title:** Vergil anchors left, Dante right, the logo stays centred, and the white and the band span the screen.
- **Mission Select:** the list stays at the left, and the background's lettering keeps its proportions while its plain part extends. The costume portrait is left out on request.
- **Banners:** zoomed to fill the width, losing some of the top and bottom.
- **Text:** an item's centre spreads out with the screen, but the item keeps its true shape.
- **Movies:** filled to the width and cropped top and bottom, as the HD Collection does.

The HUD (`psp/testroom/hud.c`) isn't the PS2 cockpit. It's a slim health bar and Devil Trigger segments in the corner, after Monster Hunter's. Weapon icons show for 2 s when switched, then fade. The style emblem still comes from the game's art.

## Assets

Everything comes off the player's disc when building:

| What | Where |
| --- | --- |
| Menu art and layout | GDATA slots, flat MODs with TIM2 sets. Some pair a MOD with the set before it, others after: title 2820, Mission Select 3183, banners 1506 (M1) and 1514 (M2) in English, strip 1673, Now Loading 2831, pause 2075, Mission Clear 1665 |
| Other menus, not used yet | Options 2091, language 2116, load/save 3528, costumes 3191-3223, styles from 1723 |
| Mission 1's stage | `st000`, slot 272, with its data in 273 |
| Mission 2's stage | `st001`, slot 276, with its data in 277 |
| Movies | MDATA1.AFS and MDATA2.AFS: MPEG-2 at 512x320, ADX sound, about 45 minutes and 2.2 GB |

Which movie is which, so far: MDATA2 0 the opening, 1 the Mission 1 intro, 2 Temen-ni-gru rising, 3 Lady. Later story scenes are the rest of MDATA2. MDATA1 11 to 17 include trailers and making-of footage.

Movies are converted for the PSP's hardware JPEG decoder (`tools/prepare_movies.py`, `psp/testroom/movie.c`): 480x272 JPEG frames at 30 FPS with IMA ADPCM sound, about 200 to 300 KB/s, playing at a true 30 FPS on the PSP. The two in use come to 88 MB. Conversion runs at install time, so it needs ffmpeg on the player's PC.

## Enemies in each mission

Measured on the HD Collection with Dante's health held up. Classes come from the executable's RTTI.

| Class | What it is |
| --- | --- |
| `CEm000` | Hell Pride. Earlier notes called it the Sloth, which was wrong: Sloths come about midway through the game |
| `CEm002` | Seen in Missions 1 and 2. Not identified by name yet |
| `CEm004` | Seen in Mission 2. Not identified by name yet |
| `CEm008` | Hell Vanguard, Mission 2's boss, with a boss health bar |

- **Mission 1, Easy:** 3 Prides at the start. Later waves weren't measured, because Dante stood still.
- **Mission 1, Normal:** Prides, with `CEm002` from about 49 s.
- **Mission 2, Normal:** Prides and `CEm002`, `CEm004` from about 74 s, then with the street clear, the Hell Vanguard at about 138 s.
- **Mission 2, Easy:** 4 Prides were seen at the start in an earlier run. No census with fighting yet.

## Rooms and doors

Each stage's data slot has its doors as text (box, next room, which player start, press or walk through, and
mission conditions), `POS` for the player starts, and `EST`, the encounter scripts. `tools/gen_rooms.py` reads them.
In `EST`, sets of ops follow `12[set]`: `2[class, ?]` then `3[x, y, z]` spawns, `9[1, n]` waits n frames, and
`9[0, n, f]` waits until n or fewer are alive. A set's number isn't its mission: which set a room uses in a
mission is chosen in `MISSIONS` by matching what the HD Collection spawned, and the game's own rule isn't read.

| Mission | Rooms | How sure |
| --- | --- | --- |
| 1 | 000 | Its door to 001 is commented out on the disc, so clearing the room ends it |
| 2 | 001 | Clearing the script ends it, as the Vanguard fight isn't in |
| 3 | 002, 009, then 003 to 006, ending at the door to 100 | Mission 4 starts in 100, and every door checks out as reachable on foot. Fights in 002 and 009 match the HD census by class |
| 4 | 100, 110, 111, 112, ending at the door to 113 | **Partly stand-in.** See below |

Mission 4, "Find the key to the stars", isn't right yet:

- The door from 112 to 113 is on a ledge at about y=3500. A flood over the collision, allowing steps up to 400 cm between neighbouring cells, gets no higher than 3130 from the room's start. That could be a jump across a gap, which the flood doesn't try, or something unported. 113's door to 120 is at y=5500 with the flood topping out at 150. The temporary end triggers under the door at any height.
- The HD route went on through 113, 120, 121 and 122, and its true end isn't known. Teleporting through missed the key pickup and whatever else the mission needs.
- The HD census saw no enemies in those rooms in the few seconds spent in each, so the port's fights (sets in 110 and 111) are stand-ins.
- There are no key items and no bosses.

Stage geometry: some stages' scene nodes are relative to their root, others (002, 009, 011) absolute, with the root
elsewhere. `choose_scm` in `prepare_stage.py` picks by comparing with the collision. A few stray objects in 002
sit far outside the room and aren't explained.

Cameras are the port's follow camera, not the game's. Its `CAM` zones aren't read. In tight rooms it can start
inside scenery, as at Mission 3's start, where the first view is poor.

## What the missions do now, and what's a stand-in

Working: the stage, spawn points from the measurements above, waves, enemies dying, Mission Clear, and back to Mission Select.

Stand-ins and estimates:

- **All enemies are Prides.** Every class in a script spawns one, including flying `CEm013`. `CEm002`, `CEm004` and the Hell Vanguard spawn points hold Prides. `CEm002` and `CEm004` have bodies in the same pack (slots [8] and [18] of GDATA 117) and AI tables already mapped (`docs/ENEMY_AI.md`), so they're the next step. The Vanguard is its own work.
- **Encounters run from the game's scripts**, but difficulty isn't applied: Easy and Normal get the same set.
- **Enemy health** is 6 light hits, chosen by eye. Their real health and Dante's real damage aren't read.
- **Dante can't die.** His health falls by the attack records' damage and stops at 1.
- **Doors** open once a room's fight is over. The game's seals and door models aren't drawn.
- **Real-time cutscenes** aren't in, Mission 2's intro among them.
- **Sound** is only in movies. There's no music or effects in menus or the game.
- **No saves.**

## Next steps, in order

1. `CEm002` and `CEm004`: bodies and AI tables.
2. Read the mission event scripts for real waves, doors, triggers and real-time cutscenes.
3. Enemy health and Dante's damage from the game, then dying and Game Over.
4. Result screen values and ranks.
5. Hell Vanguard.
6. Customize, Options, Load and Save.
7. Music and sound effects.

## Testing it

```
python tools/flow_test.py                 # the flow on the PSP, screenshots in analysis/testroom
python tools/flow_test.py --mission 2
python tools/autolink.py status           # the recovery plugin
```

In `CMP_SCRIPT.TXT`: `menu 1` runs the real flow, `movie n` plays n frames of each movie, `mkey button frame` and `gkey button frame` press buttons in menus and in game, `mshot frame` saves a menu picture, and `killevery n` kills everything every n frames.
