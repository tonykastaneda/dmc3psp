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
| 006 | Ice Guardian's Chamber | Cerberus (EST 43, class 25 = `CEm025`, `psp/testroom/cerberus_boss.c`, `docs/CERBERUS_SPEC.md`), with a boss bar. His death opens the door to 100, which ends the mission |

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

The lift and its switch are the room's own models (`tools/gimmicks.py` bakes a lift room's LIFT and SWITCH
sets into its stage; the lift's batches are tagged 254 and drawn raised to its height). The
Astronomical Board lies as a glow with sparks rising off it: key items (`CItemKey`, vtable `0x60d820`) are an
effect with a pick-up range of 200 cm, not a model. Lift speed is an estimate. Where a start point has no floor under it (110's, on a gimmick that isn't
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
| 422 | The Jester (EST 62 = class 44, `CEm044`, SE only), on his own model (em037, GDATA 140) with a simplified fight (`docs/ENEMY_SPECS.md` E). Arena st422, GDATA 824 |
| 100 | Chamber of Echoes. At the top, O at the tablet (EVE area 3) sets the Astronomical Board; the Vajura then shows on its ledge above the door to 101 (100's ITM record) |
| 102 | O at the stand (EVE area 0) with the Vajura opens the cage round the Soul of Steel. Taking it starts group 3: Enigmas and Gluttonies |
| 113 | Surge of Fortunas: strike the switch (or O by it) and ride the lift. Each of groups 0-3 (Prides, set at 2000-5000 high) drops onto it as it comes within 1200 of their height, and it holds still while they're alive; any that fall 1500 below it are gone. It rises at 250 cm a second (an estimate) to the door to 120 |
| 120 | Azure Garden. The Divinity Statue (in its stage): O by it opens the shop. Its place is the room's EVE area of type 5, which opens the shop on examine (FUN_00209c00) |
| 121 | Agni & Rudra (EST 45, class 27), on their own model (em026, GDATA 131) with a simplified fight (`docs/ENEMY_SPECS.md` F). Both down clears the mission |

Load, against what's been measured on the PSP: 111's seven Blood-goyles skin about 3,900 vertices (Mission 2's
street, at 30 FPS on hardware, skins about 7,700 for its six Prides); Agni & Rudra are 2 x 1,142 vertices, the
Jester 1,387, and the Gigapede about 2,100 rigid triangles (parts in the walls skipped). None of Mission 5 has
been run on hardware yet: PPSSPP only.

Gates: 111's doors to 112 and 110 wait for 422. 112's door to 113 waits for the Soul of Steel, set at 112's EVE area 1
(by the pit), which also raises a bridge over the pit (from its floor at 3000 to the one at 3500). The bridge is
drawn as 20 plain stone steps (the game's platform gimmick, sets 105-107, isn't converted).

Key items (gen_rooms.py KEY_ITEMS, ITEM_USES, ITEM_HOLDS, ITEM_GATES, ITEM_OPENS): the mission starts with the
Astronomical Board (36, from Mission 4). Each lies as a glow at its ITM record and is taken with O within 200; each
is used with O inside the EVE area the script checks. Approximations: the script's reveals (100's wall 201 and the
Vajura's stand 400, 102's cage 100-104) aren't animated. The cage is hidden once the Vajura's set, and the Vajura's
model stays after it's taken. There are no messages or event cameras.

### Missions 6 to 9: what their event scripts run

Decoded from EventTbl06-09 (GDATA 244-247) the way Mission 3-5's were. The decoded listings and a group census
are kept locally (`orchestration/evt/evt_m6..m9.txt`, `groups_m6_m9.txt`), not in the repo. Stage slots: room
1xx is GDATA 324 + 4 x (n - 100), 2xx is 512 + 4 x (n - 200), 3xx is 680 + 4 x (n - 300); each room's data is
the next slot. Enemies are EST spawn ids, then the class from the table at `0x599690`. Classes named from
the walkthrough where it names a new enemy in that room: 5 Greed, 7 Envy, 10 Arachne, 17 Damned Pawn (EST
35-38 and 56 all share its pack, so the chessmen), 31 Nevan, 40 Vergil. Room names are matched by door links
and the walkthrough's order. Easy and Normal are listed; where they're the same, once.

### Mission 6: "Family ties"

After demo 321 and the start screen, it starts in 122 at start 0. Only 122, 124, 125, 126 and 127 have script
sections; 120, 121 and 123 run nothing.

| Room | Area | What runs |
| --- | --- | --- |
| 122 | Mute Goddess' Chamber | The statue (gimmick 300) takes the three Essences (items 40-42) by O in area 0, one at a time, counter 5 counting them. The second blasts the rubble off the front door (gimmick 200, effects 436-438) and closes area 1; the third reveals Artemis (item 30, given if Dante hasn't got it). No fight |
| 123 | Chamber of 3 Trials | No section: the pedestals' messages and the Divinity Statue only |
| 126 | Trial of Wisdom | Counter 0 steps 0 -> 1 -> 2 -> 3 only through the right doorway in turn: door 3, then 1, then 2 (the walkthrough's 4, 2, 3 lights: right, left, centre); a wrong one sets 4, locks all four doors and starts group 0 until it's cleared. At 3 the doors retarget to 123 start 3 and the Essence of Intelligence (item 42) is open. Group 0: Easy 5 Prides + 2 Blood-goyles, Normal 3 Prides, 2 Lusts, 2 Blood-goyles |
| 125 | Trial of Skill | Area 0 starts the spikes (gimmick 100); area 1 stops them. The Essence of Technique is item 41. Once it's taken, group 0 on the way back: 4 Gluttonies, 2 Blood-goyles |
| 124 | Trial of the Warrior | Area 0 starts it: group 0 (5 Prides, 3 Gluttonies, 2 Lusts, 2 Sloths, 1 Wrath) under a barrier (enemy_mgr_19 1). Crest switches 100 and 101 (flags 32, 33) both lit drop the barrier; they run down after 2400 frames (Easy, Normal), 1800 (Hard), 1200 (Very Hard, DMD), and the barrier comes back. All dead: the Essence of Fighting (item 40). Group 1 is the cutscene-skip copy of group 0 |
| 127 | The Dark Corridor | Area 0 plays demo 15, which clears the mission |

In the port (`psp/testroom/mission6.c`, hooked from `main.c`; room 127 is GDATA 432): the whole route plays
from 122's start to Mission Clear in PPSSPP. 126's doorway order, its lock and fight, and the retargeted
doors are the script's. 124's switches work as Mission 3's (damage charges them), and stay lit for the
script's time. The barrier is drawn as a purple mist and makes the enemies take no damage while it's up, and
a wall shuts the corridor during the fight. 122's rubble (STAY 100) and its walls go when the second Essence
is placed (`STORY_WALLS` in `gen_rooms.py`). Stand-ins and estimates:

- **125's spike trap** (`CStageSetNeedle`) isn't decoded. Rows of spikes come down the corridor from the left
  wall, the right wall, the floor or the ceiling, from the walkthrough's description; their speed, spacing and
  700 damage are estimates.
- **The barrier's effect** (each enemy's vfunc +0x114, through a sub-object) isn't decoded: "no damage" is the walkthrough's
  reading. Where 124's switches and corridor wall stand depends on the pillars' joint 1, which isn't read.
  The pillars don't rise.
- **Essences** are glows, picked up with O (as Mission 4's board), with their name shown. **Artemis** (item 30)
  isn't a gun in the port yet; the statue only shows its name. Messages, cameras and demos 15/321 aren't in.

### Mission 7: "A chance meeting"

Starts in 127 at start 2 after demo 16. 127 -> 114 Heavenrise Chamber -> 115 Divine Library (group 1: 6
Damned Pawns; the Orihalcon Fragment, item 43) -> 114 -> 134 Pitch-Black Void -> 135 Skull Spire (group 0: Easy
6, Normal 8 Pawns) -> 136 Tranquil Souls Room (groups 0, 1: Easy 3, Normal 4 Greeds; group 2: 2 Greeds and a
Hell Vanguard; item 45, Siren's Shriek) -> 137 Lift Room (item 43 restores the lift) -> 100 -> 101 Entranceway
(group 1: Gluttony and 3 Lusts; group 2: 4 Lusts, a Wrath) -> 105 Cursed Skull Chamber (groups 0, 2: Easy 3,
Normal 4 Lusts and 3 Greeds) -> back up -> 138 Moonlight Mile -> 142 (demo 17) -> 144 Peak of Darkness: Vergil
(group 0, EST 58, class 40). Demo 322 after him clears the mission. Its script also carries Mission 6's rooms
(122-126) and 111-113 for backtracking; 111's group 4 is 2 Enigmas and 5 Blood-goyles.

### Mission 8: "Leviathan"

Starts in 300 (Leviathan's Stomach, the eyeballs and tusks) after demo 323. Hell Envies (EST 25, class 7) in
302 (Easy 4, Normal 6), 304 (8), 305 (10), 308 (10) and 312 (10), each group 0. The Heart of Leviathan in 302
isn't an EST enemy: it's gimmicks 1000-1002 (flag 32), woken by using the Ignis Fatuus (item 46) at area 0. Its fall plays demo 20, which clears the mission.

The port now registers M8 and all st300-st313 assets. Main route: 300 -> 301 -> 302 -> 303 -> 304 -> 305,
then 306 -> 307 -> 308 -> 309 -> 302. Room 305 gives Ignis Fatuus at its native ITM position after its
Envy wave; O in 302's native EVE area 0 uses it and wakes the Heart/lungs plus the native Envy group.
The three organs use their native models and skeletal motions (`LEVIATHAN_SPEC.md`). The protected core,
side-organ collapse/recovery, charged laser/projectiles, and orb absorption are implemented.

Five native eye meshes at 300's BREAK sets 120/130/140/150/160 gate its exit. They currently take three
light hits, persist through room visits, and remove the complete fang mesh once all five are broken;
the original's progressive fang poses and blinking are not reproduced yet. Goo doors use the room's
BREAK meshes and must be struck before their nearby exit works. Their default durability is approximate.

Acid uses the authored HITS triangles whose material byte is 13. PCSX2 Normal observation at
(469,-131,3061) confirmed 25 HP losses about every seven frames, while the flesh sample caused none.
The port applies that amount while grounded on those triangles. Rooms 303/307's Nausika chase actors,
liquid UV animation, and the real-time story cameras are not implemented. Complete route and boss-clear
validation is still in progress; room loading, native Envy waves, Ignis pickup and the acid floor have
been captured in PPSSPP.


### Mission 9: "Faded memories"

Starts in 201 (The Rotating Bridge) after demo 324. 202 Provisions Storeroom: group 0, 4 Arachnes (EST 28,
class 10; group 4 the skip copy). 206 Subterranean Lake gives item 31. 208: groups 0, 1, 5 Arachnes each
(item 47). 209 Limestone Cavern: 6 Prides and 5 Gluttonies. 210 Sunken Opera House: Nevan (group 0, EST 49,
class 31), with demos 22 and 23 around the fight; the script's AFTER_DEMO 24 clears the mission. 210 also gives item 26.

In the port (rooms 201-210 are GDATA 516-552): the route plays from 201 to Mission Clear in PPSSPP. 202's ambush,
208's fight as the Ambrosia (47) is taken, 209's relief taking it to open door 1, and Nevan in 210 come from the
script and the generic key-item tables in `gen_rooms.py`. 204, 205 and 207 run their group 0, a guess from the walkthrough
(the script has no section for them). Stand-ins and estimates:

- **203's light beam** (CRYSTAL 102) isn't decoded: lighting the reset panel (crest switch 100, damage as Mission 3's)
  takes down seal 101 on the door to 204 instead of smashing the right crystals.
- **205's and 207's bladed pillars** hurt on touch from their COLLISION cylinders (`atk`, `platk`; `parse_hazards`),
  once a second; the 700 damage is an estimate and the blades don't turn.
- Mission Clear comes as Nevan falls (`CLEAR_ROOMS`), not by 210's door 1 and demo 24. 206's item 31 isn't in.
- **Arachnes** (em010, GDATA 121, pack 5 in the table at 0x58e740) have their own model and motions (`arachne.c`):
  they run at Dante, lunge and leap. Which motion is which is read from their root paths; their class's decision
  tables, the web shot and the spiderlings aren't in. Health is an estimate.
- **Nevan** (em028, GDATA 133, pack 19) has her own model and motions (`nevan_boss.c`), reclining in the air as her
  body is modelled: she hovers, glides at Dante, sweeps her blade, sends batches of bats (glows, not models) and
  vanishes to his far side. Her lightning and kiss aren't in; health, speeds and timings are estimates.
- 210's floor draws white: its translucent layer isn't blended as the game does (not looked into yet).

## What the missions do now, and what's a stand-in

Working: the stage, spawn points from the measurements above, waves, enemies dying, Mission Clear, and back to Mission Select.

Stand-ins and estimates:

- **Enemies:** each of the six Seven Hells has its own model. The Pride, Sloth and Gluttony use their own
  decision tables, the Sloth warps and the Gluttony spews (`docs/ENEMY_AI.md`). The Lust, Wrath and Greed use
  the Pride's table, so the Wrath doesn't explode yet. The Enigma has its own logic, and the Gigapede is a full boss fight. Classes
  not ported spawn as Prides. Cerberus is his own boss fight (`docs/CERBERUS_SPEC.md`). Mission 2 now runs groups 0 and 1,
  then the Hell Vanguard in group 3; his health bar, teleport attacks and death lead into Mission Clear
  (`docs/VANGUARD_SPEC.md`).
- **Encounters run from the game's scripts**, with Easy and Normal each using their own column.
- **Enemy health** is the Pride's 6 light hits (chosen by eye), and the others in proportion to their parameter
  blocks' health. Vanguard uses his decoded 3000 base health. Dante's real damage isn't read.
- **Dante can't die.** His health falls by the attack records' damage and stops at 1.
- **Doors** open once a room's fight is over; while it's on, each door shows a red seal (a tinted board, as
  Mission 3's blue one is drawn), fading out when it's done. The game seals only the doors its script locks.
- **Real-time cutscenes** aren't in, Mission 2's intro among them.
- **Sound** is only in movies. There's no music or effects in menus or the game.
- **No saves.**

## Next steps, in order

1. The Lust's, Wrath's and Greed's own tables and moves (the Wrath's explosion).
2. The seals' own models (the lifts and their switches are in; the seals are tinted boards).
3. Boss fidelity improvements, listed in CERBERUS_SPEC.md and VANGUARD_SPEC.md.
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
