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

`EST` ends in a table of n groups x 5 difficulties (Easy, Normal, Hard, Very Hard, DMD), each an offset into the
ops: `2[class, ?]` then `3[x, y, z]` spawns, `9[1, n]` waits n frames, `9[0, n, f]` waits until n or fewer are
alive. Easy and Normal each get their own column. Which groups a room runs in a mission comes from that
mission's event script (`EventTbl0N.bin`, GDATA 238 + N), decoded in `docs/EVENT_SCRIPTS.md`: op `3e g` starts
group g. Spawn classes are decoded from the game's factory table (`docs/ENEMY_SPECS.md`): 0-3 Pride, 4-7
Gluttony, 8-11 Lust, 12-15 Sloth, 16-19 Wrath, 20-23 Greed, 26 Hell Vanguard, 31 Enigma, 41 Gigapede, 43 Cerberus.

A stage's `# GAME` text holds its gimmicks (`SET n LIFT`, `SWITCH`, `SEAL`, `COLLISION`, `STAY` models).
`gen_rooms.py` reads the LIFT and SWITCH of room 103.

The areas' names and what happens in them are from a walkthrough (`MISSIONwalkthrough.md` in the repo's root);
the room numbers are matched to them through the event scripts.

### Mission 3: "The Devils' tower"

| Room | Area (event script and walkthrough) | What runs |
| --- | --- | --- |
| 002 | 66 Slum Avenue | Groups 2 (2 Enigmas on the ledges), 1, 0, then 3 (Prides, a Lust and a Wrath) |
| 009 | (off the street) | Group 2 |
| 003 | Bullseye | No fight. Sword hits charge the crest switch behind the dart machines and open the blue seal to Love Planet. The Shotgun pickup isn't in |
| 004 | Love Planet | Doors sealed; groups 1 then 0: Hell Sloths and Prides |
| 005 | 13th Avenue | No fight |
| 006 | Ice Guardian's Chamber | Cerberus (class 43) isn't ported, so no fight. Its door to 100 ends the mission |

The Enigmas' spawn points and timing match the HD Collection's census of 002 (2150, 2330 and 2750, 2200,
2.5 s apart). Love Planet couldn't be checked there: the teleporting route can't pass Bullseye's blue seal.

### Mission 4: "Find the key to the stars"

From its event script: 100 Chamber of Echoes -> 101 Entranceway -> 102 Living Statue Room -> 103 Silence
Statuary -> (its lift) -> 110 Incandescent Space -> 100 2nd floor -> 112 Endless Infernum, falling into
104 Chamber of Sins -> back to 112 -> 111 Giantwalker Chamber (Gigapede) -> 110, where the Astronomical Board
(item 36, at the spot in 110's `ITM` chunk) ends it.

| Room | What runs |
| --- | --- |
| 101 | Group 0 (6 Prides). The script has no section for 101, so this group is a guess |
| 102 | Group 1: 6 Enigmas, and Prides (Easy) or Lusts (Normal) |
| 103 | The lift. Strike its switch (or press O by it), then stand on it: it carries Dante up to the door into 110 |
| 110 | Group 0: 6 Gluttonies, 2 Sloths. After the Gigapede, the Astronomical Board: O by it ends the mission |
| 104 | Group 0: Sloths, Lusts and Wraths. The walkthrough says Gluttony; the decoded classes say otherwise |
| 111 | The Gigapede: its routes, attacks, shells and shake-off (`docs/ENEMY_SPECS.md` section C), with a boss bar. Its death opens the door back to 110 |

Doors held shut as the script's flags do (`GATES` in `gen_rooms.py`): 100's way to 112 opens after 110, 112's
to 111 after 104, 110's to 111 never (it's entered from 112), and 112's to 113 isn't in this mission.

Stand-ins: the lift and its switch are plain blocks, and the board a gold one, because their models aren't
converted. Lift speed is an estimate. Where a start point has no floor under it (110's, on a gimmick that isn't
in the stage collision), the room gets a pad at that height. 112's stairs don't collapse: the pit is open,
and falling in is what takes Dante to 104.

Stage geometry: some stages' scene nodes are relative to their root, others (002, 009, 011) absolute, with the root
elsewhere. `choose_scm` in `prepare_stage.py` picks by comparing with the collision. Scenery far outside the
collision's bounds (backdrops tens of km across, as in 110) is cut into pieces as long as 0.3 of its distance from
the play area instead of 500 cm, and stays out of the floor grid. That took room 110 from 29 MB to 3.9 MB, and
it holds 30 FPS on the PSP with its fight on (it was 20).

Cameras are the port's follow camera, not the game's. Its `CAM` zones aren't read. In tight rooms it can start
inside scenery, as at Mission 3's start, where the first view is poor.

### Mission 5: "Of devils and swords"

From its event script (EventTbl05, GDATA 243; `build/hdref/evt_m5.txt` locally). It starts in 111 at start 2.

| Room | What runs |
| --- | --- |
| 111 | 7 Blood-goyles (group 1, EST 32 = CEm014, `docs/ENEMY_SPECS.md` D). The door to 422 opens when they're gone |
| 422 | The Jester (EST 62 = class 44, `CEm044`, SE only). Its arena is st422, GDATA 824. Not ported: a stand-in |
| 102 | Group 3: Enigmas and Gluttonies (the Soul of Steel) |
| 113 | Surge of Fortunas: its lift, and groups 0-3 (Prides, spawned at 2000-5000 high as it climbs) |
| 120 | Azure Garden, the Divinity Statue |
| 121 | Agni & Rudra (EST 45 = CEm027). Not ported: a stand-in. The room's fight over clears the mission |

Gates: 111's doors to 112 and 110 wait for 422; 112's door to 113 waits for 102. The Astronomical Board,
Vajura and Soul of Steel aren't items: the gates stand in for them.

## What the missions do now, and what's a stand-in

Working: the stage, spawn points from the measurements above, waves, enemies dying, Mission Clear, and back to Mission Select.

Stand-ins and estimates:

- **Enemies:** each of the six Seven Hells has its own model. The Pride, Sloth and Gluttony use their own
  decision tables, the Sloth warps and the Gluttony spews (`docs/ENEMY_AI.md`). The Lust, Wrath and Greed use
  the Pride's table, so the Wrath doesn't explode yet. The Enigma has its own logic, and the Gigapede is a full boss fight. Classes
  not ported spawn as Prides, except Cerberus, whose room has no fight. Mission 2 now runs groups 0 and 1,
  then the Hell Vanguard in group 3; his health bar, teleport attacks and death lead into Mission Clear
  (`docs/VANGUARD_SPEC.md`).
- **Encounters run from the game's scripts**, with Easy and Normal each using their own column.
- **Enemy health** is the Pride's 6 light hits (chosen by eye), and the others in proportion to their parameter
  blocks' health. Vanguard uses his decoded 3000 base health. Dante's real damage isn't read.
- **Dante can't die.** His health falls by the attack records' damage and stops at 1.
- **Doors** open once a room's fight is over. The game's seals and door models aren't drawn.
- **Real-time cutscenes** aren't in, Mission 2's intro among them.
- **Sound** is only in movies. There's no music or effects in menus or the game.
- **No saves.**

## Next steps, in order

1. The Lust's, Wrath's and Greed's own tables and moves (the Wrath's explosion).
2. Gimmick models: 103's lift, its switch, the seals, the Astronomical Board.
3. Cerberus; Vanguard fidelity improvements are listed in VANGUARD_SPEC.md.
4. Enemy health and Dante's damage from the game, then dying and Game Over.
5. Result screen values and ranks.
6. Customize, Options, Load and Save.
7. Music and sound effects.

## Testing it

```
python tools/flow_test.py                 # the flow on the PSP, screenshots in analysis/testroom
python tools/flow_test.py --mission 2
python tools/autolink.py status           # the recovery plugin
```

In `CMP_SCRIPT.TXT`: `menu 1` runs the real flow, `movie n` plays n frames of each movie, `mkey button frame` and `gkey button frame` press buttons in menus and in game, `mshot frame` saves a menu picture, and `killevery n` kills everything every n frames.
