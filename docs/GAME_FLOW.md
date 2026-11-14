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

**In the port** (`tools/gen_rooms.py` Mission 7, `psp/testroom/mission7.c`): the route above, start to Mission Clear.
- Fights: 115 (6 Damned Pawns), 135 (Pawns), 136 (Greeds), 101 (groups 1 and 2), 105 (once the Crystal Skull's taken),
  144 (Vergil, `docs/VERGIL_SPEC.md`); 136's return ambush (group 2) runs when Dante comes back with the Skull.
- Items: the Orihalcon Fragment (43) in 115 opens 137's lift; the Siren's Shriek (44) in 136 opens 100's doors to
  101; the Crystal Skull (45) in 105 opens 135's door to 138. (Item ids by the event script: 44 is used in 100, 45
  taken in 105.)
- 114's six super jump pads (CATAPULT) throw Dante to the next pad, the last to the top door [approximation: the arc].
  137's and 100's lifts run as one elevator, both ways, and take their door at the end of the run.
- 105: the crest switch brings the ball down; break its shell (it rises and regrows if left alone) and the Skull shows
  [approximation: CStageSetHang isn't decoded; hit counts are estimates, drawn as stand-in blocks].
- 142's area 0 (demo 17) leads to 144. No cutscenes, and 144 has no rain.
- Approximation: 127's door into 114 lands on the ground floor (start 0), not the 9000 balcony Dante drops from.
- Damned Pawns (`pawn.inc`): their hops and strike as decoded; the chess master that times their turns isn't ported.
  Hell Greeds (`greed.inc`): the coffin's flight and summon as decoded; when they summon is an estimate.

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
The port applies that amount while grounded on those triangles. Rooms 303/307 now have the native
Nausika model/motions following its authored chase path, bite attacks, and melee damage/stagger.
Path steering and attack attachment interpolation remain approximate; liquid UV animation and
real-time story cameras are not implemented. PPSSPP checks cover the full Ignis route and a
full-health Heart defeat through normal shotgun damage, reaching Mission Clear.


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
- 210's STAY 150 (a white sheet over the stage) is hidden in Mission 9 on, as the script's gimmick_cmd10 [150, 0] does.

### Mission 10: "The job"

Decoded from EventTbl10 (GDATA 248, PS2 SE); local listings and the stage/EST census are in
`build/mission10_re/evt_m10.txt`, `evt_m10.json`, and `rooms.json`. Demo 25 leads to room 209,
start position 2. Entering area 0 of room 211 plays demo 26, whose AFTER_DEMO section clears the mission.
There is no boss encounter in this script.

Main route: 209 Limestone Cavern → 206 Subterranean Lake → 205 Rounded Pathway → 204 Subground Water Vein
→ 203 Forbidden Land: Front → 202 Provisions Storeroom → 201 The Rotating Bridge → 211 Marble Throughway.
Item 48 (Stone Mask) lies at 209's ITM position (3574.4,2109,826.6). Examine 206 area 0 to use it;
the bridge gimmick 100 moves and barrier 101 disappears. Item 49 (Neo-Generator) is at the lake's ITM
position (10790,3140.5,7390.5). In 201, entering area 1 while carrying it consumes it and enables
bridge pieces 1/2/3 and control area 0. Subsequent examinations of area 0 toggle the bridge's direction.
Room 206 also permits an optional item-31 pickup in area 1 if it was not obtained in M9.

Encounters (Easy and Normal lists are identical unless noted):

| Room | Event group | Enemies / trigger |
|---|---|---|
| 209 | 2 | 6 Hell Prides + 5 Hell Gluttonies on returning from 206. |
| 209 | 1 | 5 Prides + 2 Gluttonies + 2 Sloths, conditional return from 210; no initial spawn from start 2. |
| 207 | 1 | 1 Gluttony + 4 Greeds after entering area 0; optional side trip. |
| 205 | 0 | 3 Dullahans (EST id 30, runtime class 12); group 2 is the cutscene-skip copy. |
| 202 | 1 | 5 Arachnes (EST id 28, runtime class 10) after entering area 0. |

Room-local seal flags 0/1/2 suppress repeated fights in 207/205/202 once cleared during this mission.
The rooms 201/203/204/206/211 have no event-script enemy spawn. Room 211's EST group 0 belongs to
other missions and must not run for M10. The bridge and lake cutscene choreography is a separate
implementation concern from the decoded route and item gates above.

### Mission 11: "Revenge"

From EventTbl11 (GDATA 249; `orchestration/evt/evt_m11.txt` locally). After demo 27 it starts in 212 at start 1.
Rooms 212-217 are GDATA 560-580, 239 and 240 are 668 and 672. Classes: EST 31 Enigma, EST 34 class 16 (em016,
pack 11: the Soul Eater, from the walkthrough), EST 26 Hell Vanguard, EST 51 class 33 (em030, pack 21: Beowulf).

| Room | Area | What runs |
| --- | --- | --- |
| 212 | Gears of Madness | Group 0: 2 Enigmas. Door 1 (to 211) disabled; door 0 leads on |
| 213 | Altar of Evil Pathway | Area 0 (x 3200-3300) locks both doors and starts group 1: 5 Soul Eaters |
| 214 | Altar of Evil | Two SLIDE blocks (300, 301) bound to flags 32 and 33: pushed by melee into their holes, each counts (counter 5). With both, gimmick 200 opens the hidden passage, door 1 (to 239) is enabled and sealed, and group 2 runs: a Hell Vanguard and Soul Eaters (Easy 3, Normal 2). Cleared, the doors open |
| 239 | Temperance Wagon | No section. Area 0 boards the wagon (door 1, an event door, to 216) |
| 216 | (the wagon ride) | A TRUCK set on rails, a 36000-frame clock; as it runs, object waves and groups 0 (5 Prides) and 1 (3 Prides, a Wrath), then on to 240 start 3 (or 239 start 3, riding back) |
| 240 | (the far platform) | Arriving from 216, a camera; area 0 rides back. Door 1 (up the stairs) to 217 |
| 217 | Torture Chamber | Area 0 plays demo 28, then group 0: Beowulf. Beaten, demo 29; area 0 again plays demo 30, which clears the mission |

In the port: the route plays from 212 to Mission Clear in PPSSPP. Stand-ins and estimates:

- **214's slide blocks** (`mission11.c`): melee from behind pushes a block along -z (300 x its zure a hit), and within
  100 cm of its hole it drops in. They're drawn as stone boxes and don't stop Dante. With both in, STAY 200 is hidden
  for the passage, door 1 opens and group 2 runs; until then doors 0 and 1 are held, as block 1 does.
- **The wagon ride (216)** isn't in: 239's area 0 is a door straight to 240's start 3 (`EXTRA_DOORS`), so its clock,
  its object waves and groups 0 and 1 are skipped.
- **Beowulf** (em030, GDATA 135, pack 21) has his own model and motions (`beowulf_boss.c`): the three punches, a
  charge on all fours, a leaping rush, a stomp that sends a pillar rolling at Dante, and a volley of light feathers
  after his body flashes; super armour, a gauge staggers him. Which motion is which is read from their root paths;
  timings, health and damage are estimates, and the pillars are glows that can't be broken.
- **Soul Eaters** (class 16, em016) aren't ported and spawn as Prides. Mission Clear comes as Beowulf falls, not by
  demo 30 at the shiny object.

### Mission 13: "Chaos' warm welcome"

EventTbl13 (GDATA 251) is decoded locally to `build/mission13_re/evt_m13.txt` / `.json` under the hdref lock;
the same directory contains the room/EST census. Demo 36 leads to room 229, start 0. Main route:
229 → 230 → 241 → 233 (crystal-light puzzle and item 51) → 241 → 232 (use item 51 in area 0)
→ demo 37 → 234. Defeating 234's group 0 plays demo 325 and clears the mission.

| Room | Group / condition | Easy and Normal enemies |
|---|---|---|
| 229 | 0, first visit (flag 0 seals both doors) | 8 Damned Pawns (EST 35) + 3 Damned Bishops (EST 37) |
| 241 | 0, arriving from 230 | 6 Pawns + 2 Bishops |
| 241 | 1, arriving from 233 or itself | 4 Pawns + 3 Bishops |
| 234 | 0 | Vergil (EST 58, class 40), EST op 8 [0,1] selects variant 1, Beowulf/Yamato |

230, 232, 233 and 236 have no event-script enemy spawn. 236's chess groups and 228's Beowulf groups belong
to other missions and must not run merely because those rooms have EST data. Room 228 is a disabled rear
passage here. Room 232's door 1 is initially disabled and retargeted to demo 37; item 51 enables it after
the seal-opening event. Item 51 is at 233's native ITM position (3885.3,189.6,131.8).

In room 233, crest switch 100 is bound to flag 32 and timer 120; CRYSTAL set 102 supplies completion flag 34.
Its start/end/relay/cover/no values are 1/2/0/3/1, at (1700,164,299). Flag 34 opens door piece 101 and
sets persistent puzzle flag 1. The port implements this light grid, its reset switch, key gate and return encounter, plus the additive
Beowulf/Devil Trigger Vergil variant. Details and fidelity limits are in `docs/MISSION13_SPEC.md`.
### Mission 12: "Hunting the Hunter"

Decoded from EventTbl12 (GDATA 250; `tools/event_dump.py <iso> 12 --out <dir>` writes the listing locally).
Demo 31 leads to 217 Torture Chamber at start 1 (item 50 is given here; the soul timer it runs isn't ported).
217 door 0 -> 240 -> the Temperance Wagon ride, 216 (groups 2, 3, 4: 18 Seven Hells; then goto_room 239, start 3)
-> 239 door 0 -> 214 (group 1: Prides and Lusts) -> 213 (group 0: 7 Blood-goyles) -> 212 (group 2: 6
Blood-goyles) -> 211 (group 0: a Hell Vanguard (EST 26) and Prides) -> 201 The Rotating Bridge (start 2; item 50
turns it) -> door 3 -> 218 -> door 1 -> 228. In 228, demo 32 and Geryon on the bridge (group 0, EST 50, class 32);
at 80% he leaves, the bridge falls (demo 33) and the fight goes on at start 2 below; after him demo 34, the
Quicksilver (item 34), door 1 and demo 35, Mission Clear.

**In the port** (`tools/gen_rooms.py` Mission 12, `psp/testroom/mission12.c`, `docs/GERYON_SPEC.md`): the route
above, start to Mission Clear.
- 216 has no floor of its own (the wagon is a gimmick over a backdrop): `prepare_stage.py` gives it an 8 x 4 m deck
  at its start, and the ride doesn't move. Its waves cleared, Dante goes on to 239 at **start 1** (starts 2 and 3
  are on the wagon, which has no collision in 239) [approximation].
- 240's wagon is a box door into 216 (EXTRA_DOORS).
- 201's bridge isn't turned by item 50; door 3 is walked to as it stands [approximation].
- 228: Geryon both phases; at 80% Dante is put at start 2. No cutscenes; Quicksilver isn't given.

### Mission 14: "Drive!"

From EventTbl14 (GDATA 252; `orchestration/evt/evt_m14.txt` locally). After demos 327 and 39 it starts in 237 at
start 1. The script has sections only for 237, 214, 236 and 008; the route between them is the walkthrough's,
matched by door links (doors open from Mission 14 on). No boss. Chessmen: EST 35 the Damned Pawn, 36 and 37 the
other pieces (not identified by name; the walkthrough's "new chess-type demon" that charges).

| Room | Area | What runs |
| --- | --- | --- |
| 237 | Lair of Judgement Ruins | Item 27 (Beowulf) at area 0. Door 0 (to 235) is enabled by flag 32, bound to COMBO set 200, a combat adjudicator (weapon "fight", rank C); COLLISION 201 walls the door |
| 235, 225, 231, 233 | Underwater Elevator, Subterranean Lake, Top Obsidian Path, Vestibule | No section |
| 214 | Altar of Evil | Doors 2 and 3 locked, group 0 (5 of EST 30, class 12) until cleared |
| 239, 240 | Temperance Wagon | No section (the wagon, as in Mission 11) |
| 236 | Hell's Highway | 21 areas raise and lower the panels 101-118 as Dante walks the maze; areas 16-20 start groups 2-6 (chessmen: 7 Pawns; 3 of EST 36; 3 + 3; 2 + 1 of EST 37; 4 + 2), each opening more panels when beaten. Flag 1 at the end |
| 221, 204, 007 | Subterranean Garden, Subground Water Vein (its blue light, door 0, takes him to 007), Love Planet | No section |
| 008 | 13th Avenue: Aftermath | Door 0 disabled; area 0 plays demo 326, which clears the mission |

In the port (`psp/testroom/mission14.c`): the route plays from 237 to Mission Clear in PPSSPP. Stand-ins: the
adjudicator takes 20 melee hits of any weapon (no style rank, no Hyper Fist), drawn as a stone block, and takes
COLLISION 201 with it (`STORY_WALLS`); 236 runs groups 2-6 one after another from the start, its panels stay as the
stage has them; EST 36 and 37 spawn as Pawns; the wagon is Mission 11's door from 239 to 240.

### Mission 15: "The gatecrasher"

Decoded from EventTbl15 (GDATA 253; `tools/event_dump.py <iso> 15 --out <dir>` writes the listing locally). Demo 41
leads to 222 Upper Subterran Garden at start 1. Rooms: 222 (Fallen, EST 29), 202 Provisions Storeroom (Arachnes),
219 Devilsprout Lift, 200 Forbidden Land: Front, 212 Gears of Madness (Blood-goyles), 213 Altar of Evil Pathway
(Dullahans), 227, 208, 226 and 224 (Soul Eaters), 211, then 106 Sun & Moon Chamber, where demo 42 clears it.

| Room | What runs |
| --- | --- |
| 222 | Area 0 (by the door) seals it and starts group 0: 2 Fallen (after 521 and 1273 more frames). Cleared, flag 0 |
| 202 | From 222, group 2: 3 Arachnes. From 226 (flag 1 clear), group 3: 6 Arachnes, doors 2 and 3 locked till they're down. Switch 200 (the flame portal) is bound to flag 32 once 200's portal has set flag 2 (else STAY 300 stands there) |
| 219 | Area 0, the control: examined, each Orihalcon Fragment held (52, 53, 54) is set in; with three, the lift (gimmick 100, area 1) runs up to door 4, into 106 |
| 200 | Its portal (switch 100, flag 32) sets flag 2 |
| 212 | Group 3: 3 Blood-goyles. Its portal (switch 200) turns the counter |
| 213 | Group 2: 4 Dullahans. The Orihalcon Fragment 52 lies at its far end (ITM) |
| 224 | Group 0: 2 Soul Eaters (EST 34). Fragment 54 (ITM) |
| 208 | (no section) Fragment 53 on its altar (ITM) |
| 106 | Area 0 plays demo 42: Mission Clear |

**The coloured doors.** Counter 4 (0, 1, 2) picks where each coloured door (a STAY 600 frame) leads; 202's and 212's
script sections `door_retarget` theirs, and the same frames stand by doors in 211, 226 and 227 (no script section;
their portals, switch 100 + STAY 650, are the rooms' own). Read together, each value pairs the six rooms:
0: 222-202, 212-211, 226-227; 1: 222-227, 202-212, 211-226; 2: 222-211, 202-226, 212-227. Striking a portal moves
the counter on one.

**In the port** (`tools/gen_rooms.py` Mission 15, `psp/testroom/mission15.c`, `fallen.c`, `docs/FALLEN_SPEC.md`):
the route 222 -> 202 -> 219 -> 200 (portal) -> 219 -> 202 (portal: 1) -> 212 -> 213 (52) -> 212 (portal: 2) -> 227
-> 208 (53) -> 227 (portal: 0) -> 226 -> 224 (54) -> 226 (portal: 1) -> 211 -> 219 -> the lift -> 106, Mission Clear.
- The Fallen are their own port (`fallen.c`); Soul Eaters (class 16) aren't ported and spawn as Prides.
- One examine at 219's control sets every fragment held (the port's item use now does that for any spot).
- 219's lift isn't drawn or ridden: its platform (area 1) is a door into 106's start 3 once the three are set.
  106 clears as Dante arrives (demo 42 isn't played).
- 212's portal hangs under its 850 walkway, over a floor the port's collision doesn't have: it can be struck from
  the walkway [approximation].
- 222's group starts at once, not when Dante reaches the door; 202's return ambush (group 3) isn't run.
- No cutscenes or camera events; the portals flash, the doors don't change colour.
### Mission 16: "Win or lose"

From EventTbl16 (GDATA 254; `orchestration/evt/evt_m16.txt` locally). After demo 43 it starts in 106 at start 0.
The script has sections for 106, 105, 109, 102, 146, 116, 115 and 119; the route between them is by door links.
Classes: EST 29 class 11 (The Fallen, `fallen.c`), EST 36 and 38 (chessmen, pack 12; the walkthrough's Damned Rooks),
EST 31 Enigma, EST 32 Blood-goyle, EST 55 class 37 (em034, pack 25: Lady).

| Room | Area | What runs |
| --- | --- | --- |
| 106 | Sun & Moon Chamber | Area 1, examined holding them, takes the Golden Sun (56) and the Onyx Moonshard (55) one at a time (counter 5); with both, gimmicks 200/210/211 open and door 0 (to 107) is enabled. A lift (set 100) by area 2 |
| 108 | Entranceway | No section |
| 109 | Waking Sun Chamber | Crest switches 100 and 101 (flags 32, 33) and the hanging sphere 150 (flags 36-38) open gimmick 300; the Golden Sun lies there (set_d4 56). Taking it locks door 0 and starts group 1: 2 Fallen |
| 105 | Cursed Skull Chamber | Group 1 (Prides, Greeds, a Sloth on Easy, Lusts on Normal), door 1 locked until it's cleared |
| 102 | Living Statue Room | Doors 2 and 3 locked, group 2: 2 Enigmas, 4 Blood-goyles |
| 146 | Chamber of Sins | Switch 100 (flag 32) and the swinging sphere 150 (flags 33, 35) open 300/301; the Onyx Moonshard (set_d4 55). Taking it starts group 0: 4 of EST 38, 1 of EST 36 |
| 107, 118, 117, 111, 116 | Ice Guardian's Chamber up to Incandescent Space | No section (116 resets counter 0) |
| 115 | The Divine Library | Area 1 plays demo 44; group 0: Lady. Beaten, demo 45 sends Dante to 119 start 0 |
| 119 | High-fly Zone | Item 33 (Kalina Ann) given; area 1, at the top (y 8961), examined, clears the mission |

In the port: the route plays from 106 to Mission Clear in PPSSPP. Stand-ins and estimates:

- The Golden Sun and the Onyx Moonshard (items 56, 55) use the generic key-item tables (`KEY_ITEMS`, `ITEM_HOLDS`, `ITEM_USES`,
  `ITEM_GATES` in `gen_rooms.py`); the puzzles that free them (109's crest switches and hanging sphere, 146's
  swinging spheres) aren't in, so the items lie open.
- **Lady** (em034, GDATA 138, pack 25) has her own model and motions (`lady_boss.c`): she keeps her distance, strafes
  and shoots, lobs grenades that beep and burst, fires Kalina Ann's rocket, and escapes backwards with her repel
  gun when Dante's close. Motions are read from their root paths; timings, damage and health are estimates; her
  guns and shots aren't models.
- Mission Clear comes as Lady is beaten (`CLEAR_ROOMS`), not by climbing 119. The chessmen of EST 36 and 38 spawn as Pawns; The Fallen
  are Mission 15's (`fallen.c`).

### Mission 17: "Inner-demons"

Decoded from EventTbl17 (GDATA 255; `tools/event_dump.py <iso> 17 --out <dir>`). Demo 46 leads to 133 at start 1.
The route (the walkthrough's names): 133 -> 128 God-cube Chamber (its cubes up to door 2) -> 129 Tri-sealed
Antechamber -> 132 Trial of Wisdom -> 131 Trial of Skill (spikes; its red pod, a CATAPULT, to door 1) -> 140
Pitch-Black Void -> 141 Skull Spire -> 138 Moonlight Mile -> 143 Dark-pact Chamber -> 139 Apparition Incarnate -> 143.

| Room | What runs |
| --- | --- |
| 132 | Doors 0 and 1 locked, group 0: 3 Dullahans; cleared, flag 0 |
| 131 | Areas 0-2 drive the spikes (gimmick 100) |
| 141 | Doors locked, group 0: 4 Abyss (EST 24, class 6, em006); cleared, flag 3 |
| 143 | Counter 0 is 0: area 0 plays demo 47 -> 139 start 0. Counter 0 is 1: area 1 -> Mission Clear |
| 139 | Counter 0: group 0, the Doppelganger (EST 52); beaten, demo 48 -> 139 start 1, counter 0 to 1, item 35 (the Doppelganger style) unless Vergil; door 0 (the middle) back to 143 start 2 |
| 130 | (side) Trial of the Warrior: group 0, Gluttonies, a Sloth and a Hell Vanguard, its seals' times by difficulty |
| 121 | (side) Firestorm Chamber: group 1, 4 Enigmas and a Hell Vanguard |
| 449 | (side) group 2, then flag 5 shuts 129's door 2 |

**In the port** (`tools/gen_rooms.py` Mission 17, `psp/testroom/mission17.c`, `docs/DOPPELGANGER_SPEC.md`): the
route above, start to Mission Clear.
- 128's God-cubes (STAY sets) aren't moved: the door from 133 brings Dante in at start 2, by door 2 [approximation].
- 131's red pod throws Dante to its door (the Mission 7 catapults' code).
- 143's area 0 takes Dante to 139; area 1 is the goal (walk up the slope to it and press O) once 139 is cleared.
- The Abyss aren't ported: Prides stand in. 131's spikes (NEEDLE 100) aren't in.
- No cutscenes; demo 48's item 35 isn't given.

### Mission 18: "Invading hell"

From EventTbl18 (GDATA 256; `orchestration/evt/evt_m18.txt` locally). After demo 49 it starts in 400 at start 0.
Rooms 4xx are GDATA 736 + 4 x (n - 400). The Netherworld's rooms are joined by GATE sets (mirrors: a place and the
next room and start), not doors.

| Room | Area | What runs |
| --- | --- | --- |
| 400 | Unsacred Hellgate | Rocks (set 200, flag 11) rise into a path to the mirror (GATE 555, to 401) |
| 401 | Damned Chess Board | Group 0: 8 Pawns, 2 each of EST 36, 37, 38 and one each of 39 and 40 (the Queen and King). Cleared (flag 13), GATE 500 opens to 402 |
| 402 | Road to Despair | Group 0: 2 Fallen; GATE 555 to 403 |
| 403 | Lost Souls Nirvana | The hub: doors 0-8 lead to 412-420. Coming back from 412 + k lights cell k + 1 of the bingo board (BINGO 888, flags 1-9) and disables door k. A line of lit cells opens GATE 501, the mirror, to 406 and demo 50, Mission Clear. Two green orbs (pickup 9) |
| 412 | | Cerberus (EST 43) |
| 413 | | Gigapede (EST 41) |
| 414 | | Agni & Rudra (EST 45) |
| 415 | | Nevan (EST 49) |
| 416 | | Beowulf (EST 51) |
| 417 | | Geryon (EST 50), on 228's ground |
| 418 | | Doppelganger (EST 52) |
| 419 | | Envies (EST 25: Easy 4, Normal 6) about Leviathan's heart, a gimmick as in 302 |
| 420 | | Two Hell Vanguards (EST 26) and 21 Seven Hells |

Each boss room's door 0 (back to 403) is disabled until its fight is over (door_enable).

In the port (`psp/testroom/mission18.c`): the route plays from 400 to Mission Clear in PPSSPP, and each boss room
runs its boss from its existing file. Stand-ins and estimates:

- The gates are doors (`EXTRA_DOORS`, Mission 18 only): a box 400 across and 800 high round each GATE's place.
- **The bingo board's rule** (CStageSetBingo) isn't decoded: the board is read as 3 x 3 in door order (0 1 2 / 3 4 5 /
  6 7 8), and a full row, column or diagonal opens the mirror (a glow; stepping into it ends the mission).
- 419's heart is spawned as Mission 8 does it (`enemy_add_kind(ENEMY_LEVIATHAN, ...)`); in 417 Geryon starts in his
  ground phase (`geryon_boss_start_on_ground`, added for this). The bosses' arenas are their missions' rooms again, so
  their code runs unchanged.
- 401's Queen and King (EST 39, 40) and the other chessmen spawn as Pawns or Prides; 400's rocks are the stage's own
  floor, climbed with jumps.
### Mission 20: "Screaming souls"

EventTbl20 (GDATA 258), decoded under the hdref lock to local `build/mission20_re/evt_m20.*`,
plays demo 55, the start screen and demo 56 before entering room 411, start 0.
Room 411 group 0 is one Vergil (EST 58, class 40), with EST op 8 [0,2] selecting variant 2.
All five difficulty records use that variant; native HP is 10000. Dante starts at
(2500.9,-208.1,4000.6), Vergil at (2500,-208,2400). Native arena centre is (2500,-200,3750).

After his defeat: demos 57 and 58, then room 012 start 0, group 0. Easy/Normal's ending group
contains Lady (EST 55, variant 1, an ally), 95 Prides (0), 2 Lusts (8), a Greed (12), a Gluttony (4)
and a Vanguard (26). This is a timed ending sequence: op 8c [13620], op 7e [2], op 7e [3],
wait 300, op 7e [4], then demos 59 and 60 and Mission Clear. The exact 8c/7e side effects remain
undecoded; the numeric delay is recorded rather than assigning an unverified opcode name.

In the port: room 411 runs variant 2, then automatically enters 012. Its native ending waves run
with hostile Lady omitted (she is an ally here); the port treats 13620 + 300 as the ending delay
before Mission Clear. No ending demos or credits. See `docs/MISSION20_SPEC.md` for verification
and the boss/ending approximations.
### Mission 19: "Forces collide"

Decoded from EventTbl19 (GDATA 257; `tools/event_dump.py <iso> 19 --out <dir>`). Demo 51 leads to 406 at start 0.
Rooms 4xx are GDATA 736 + 4 x (n - 400).

| Room | What runs |
| --- | --- |
| 406 Room of Fallen Ones | Group 0: 5 Abyss (EST 24); gimmick 500 (the mirror on to 405) once they're down |
| 405 Nirvana of Illusions | Group 0: Abyss, who only die as the green-tinted mirrors are struck (the walkthrough). Area 0, the golden statue: with the Samsara (item 57) the window (gimmick 500, door 1) opens to 408 |
| 404 Infinity Nirvana | Group 0, then 1 and 2 as the hourglass runs out (block 4); the Samsara (item 57) by the hourglass; taken, its large mirror |
| 403 Lost Soul Nirvana | The boss rerun doors (to 412-420) all disabled |
| 408 End of the Line | Its door is retargeted to demo 52, then 421 |
| 421 Forbidden Nirvana | Counter 0: group 0, Arkham (EST 53). Cleared: demo 53 (Vergil), counter 1, group 1: Arkham again. Cleared: demo 328, Mission Clear |

**In the port** (`tools/gen_rooms.py` Mission 19, `psp/testroom/mission19.c`, `docs/ARKHAM_SPEC.md`): 406 -> 405 ->
404 (take the Samsara) -> 405 (set it at the statue, area 0) -> 408 -> 421, Arkham, to Mission Clear.
- With the Samsara, 404's door goes straight back to 405's start 0 (the walk back through 403 and 406 is skipped).
- The Abyss aren't ported: Prides stand in, and die to the sword (405's mirrors aren't in). 404's hourglass isn't in.
- 421 runs one Arkham through both rounds (`arkham_boss.c`); Vergil doesn't join.
- No cutscenes.

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

