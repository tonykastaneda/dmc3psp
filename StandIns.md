# Stand-ins to circle back to

Everything the port does that isn't the game's own code or data yet: stand-ins (the port's own behaviour in place of
the game's), estimates (numbers not read from the game) and inferences (read, but how it's used is a reasoned guess).
Grouped by area, each with where it lives. Keep this list current: when one is fixed, delete its line; when a new
one goes in, add it here as well as in the code. Collected 2026-11-17 from every `stand-in`, `[estimate]`,
`[inferred]` and `[approximation]` in psp/testroom, tools and docs, plus each mission's "Stand-ins and estimates".

## Systems that touch everything

- **Dante held** (his action 0x2e, `combat_hold`): Nevan's kiss holds him and he mashes free as the game counts it,
  and he plays her held motion (her pack's [3] slot 20, as the game hands it him), but breaking free doesn't play
  her [3] 21, her throw at the end is a knockdown without damage, and other blows pass him while he's held. The Soul
  Eater's bite holds him too (60 to break free) in its held loop ([9] 12), without its lift (10, 11) or end (14). The
  Arachne's web still holds him in the heavy flinch stopped at frame 10 (its motions, [15], aren't wired).
  `combat.c` held_update, `enemy.c` hold_take.
- **Real-time cutscenes** (docs/CUTSCENES.md) play at missions' openings and clears and at the bosses' entrances and
  defeats that stay in their room, with English subtitles (centred, not at the PS2's x), fades and their own sound (voices and music), but: their P / G effects and cloth aren't shown; a CnsPos piece's root on its parent bone [inferred]; the field of view taken as vertical
  [inferred]; an entrance demo plays on entering the room, not in its area; demos between rooms play as Dante arrives in the
  next; demo stages are coarser than rooms (subdivision) to fit memory. PortJobs.
- **Sound**: movies, cutscenes, the missions' music (two stream channels per room from the event scripts; the battle
  track held until a fight and the stage channel dipping under it [inferred]; tracks loop at their ADX loop points) and
  the system bank's effects (menus, orbs; that it's the game's type 0 is inferred). Dante's own bank and his ten weapons' play from his moves'
  events; footsteps and the enemies' and rooms' banks aren't in yet. MUSIC.md. GAME_FLOW.md.
- **No saves**; **Game Over / Continue**: Dante goes down and the mission restarts 150 frames later. GAME_FLOW.md.
- **Event mode**: during a script's event the enemies wait (what the game's event mode stops isn't read).
  `main.c` ~4296, EVENT_SCRIPTS.md.
- **Enemy health and Dante's damage**: Dante's hits are 42 light / 84 heavy (×1.1, Easy ×2, guns 20%) for every boss
  [inferred], a bullet 8 against the Jester and the Hells [estimate]; health is the Pride's 6 light hits by eye and
  the rest in proportion to their parameter blocks. `enemy.c`, `vergil_boss.c`, `geryon_boss.c`.
- **Hit detection on Dante's moves**: every generated move uses one stand-in reach; Quicksilver and Doppelganger styles
  aren't compared with the game; shotgun spread and tracers are stand-in effects. MOVESET_RE.md, `combat.c`.
- **Stand-in guard motion** without a loop range. `combat.c` ~1436.
- **Shadows**: sizes per kind fitted by eye [estimates]; props have none; the game's SHW records aren't decoded.
  `enemy.c` shadow_radius.
- **Effects**: many particle programs and model effects are motes, glows or `fx_explosion` (Nevan's bats, the Jester's
  throws, Geryon's missiles, Vergil's 0x338, Agni & Rudra's 0x311 / 0x312 (the charged blades' 0x24c / 0x24d are the game's, riding the hand), Cerberus's unported
  particle programs, the Vanguard's P312 dissolve). NATIVE_FX.md, VANGUARD_EFFECTS.md, CERBERUS_SPEC.md.
- **Gimmick models** aren't converted: seals are tinted boards, lifts stone blocks, switches a hub with blades and
  flames, Essences and key items glows. `main.c` ~2706 / 2808 / 2933, `gimmicks.py`, `mission6.c`, `mission7.c`.
- **Hit shapes** (types 2 / 3 / 4 / 6) are read by their use [inferred]. ENEMY_SPECS.md G.

## Enemies

- **Seven Hells**: the Lust, Wrath and Greed decide with the Pride's table (the Wrath doesn't explode); the attack
  token's manager (FUN_00369180) isn't read: "no one else attacks for 3 s". ENEMY_AI.md, GAME_FLOW.md.
- **Abyss**: the Pride's hit-script list (its block's +0x14 pairs aren't read); its scythe is the largest of [3]-[5]
  [inferred]. `gen_enemy_ai.py`, `prepare_skinned_model.py`.
- **Damned Pawns**: the chess master that times their turns isn't ported; EST 36-38 (the other chessmen) and 39 / 40
  (Queen, King) spawn as Pawns or Prides. `enemy.c`, GAME_FLOW.md (Missions 14, 18).
- **Hell Greeds**: when they summon is an estimate. `greed.inc`.
- **Blood-goyle**: its bullets fly at 25 a frame for 120 frames [estimate]; the split timing [estimate]; its rest
  value 5 and stone's triple damage [inferred]; stone drawn untextured grey. ENEMY_SPECS.md D.
- **Arachne**: its class is ported (ENEMY_SPECS H); stand-ins: its hits' bones (a sphere 100 ahead), flag 4 taken as
  its scripts' first event, the wall's normal (the ray back, flattened) and its pose there (turned 90 degrees), 0x6d's
  web-line swing (a leap), 0x6f's speed (8), the web's struggle count (35 [inferred]), its fall when struck on a wall,
  root motion x1.5 [inferred]; webs drawn as motes; spiderlings (0x76) and the charge (0x78) never chosen at rank 0.
  `enemy.c` The Arachne, PortJobs.
- **Fallen**: its damage handler isn't decoded [estimates]; facing taken from Dante, not his facing; on the ground it
  closes 3 a frame to 320 from him; its wings take front hits [approximation]; health 1500 by the port's scale
  [estimate]; its spear is straight at his chest (the shell isn't decoded). `fallen.c`, `enemy.c` ~5383.
- **Dullahan**: which script is which [inferred]. `dullahan.c`.
- **Soul Eater**: Dante is held from its bite and struggles free (above), but isn't lifted or turned with it, and his struggle's count isn't reset as the hold starts; the rank-0 blink reappears ahead of Dante's facing, not the camera's; its
  dodge (16) isn't triggered (needs Dante's move 0x11); the gaze cone ±60° [inferred]; 1000 DT a rune [inferred].
  `enemy.c` soul_*.
- **Jester**: all three encounters and his second set run; stand-ins: the charge (0x7f) turns at the port's arena
  clamp, his throws are taken to end on the floor (+0x4818) [inferred], the second set's effects and room flag aren't
  shown, his idle's turn rate [inferred], the bomb and the hand trap are his own models but rigid (the hand's motions aren't played: it rises and sinks by an estimate), the cross and the bomb play the game's effects but the bomb's burst and the trap's ring (P particles) are motes, a bullet's stun value 8
  [estimate], the arena clamp is the stand-in's. `enemy.c` The Jester, `jester.c`, PortJobs.
- **Agni & Rudra**: the opening (state 64) is ported; Rudra's 60-frame delay reads +0x2ee0 as his place in the pair
  (inference); they wait as statues (ar71 / ar72) where the game starts them in state 1 at y 100 (the cutscene's) and
  wake within 900, when struck or after 600 frames [estimate]. The pedestal climb, the guard and the hops are the game's; stand-ins: a blow's "Dante's +5 is 0-2" (a
  guarded blade counts 2) is taken as always, and its "+5 is 3, +6 his id" (counts 2 in a swing) as never [inferred];
  Dante's recoil from a guarded blow (the hit's +0x34) isn't modelled; a wall's push for the leap (vtable +0x1c0) is
  read as the walls pass moving him over 1 cm since his own move [inferred]; the survivor's moves below each health
  mark (57 / 58) aren't picked (their shots aren't decoded); the pedestal's sword flash (0x3b3 / 0x3b4) isn't shown and
  the landings' effects (0x2c, 0x2e5) are dust; the effects (0x311 / 0x312 a burst within 300 as record 12
  [estimate]). `agni_rudra.c`, `enemy.c` Agni & Rudra, PortJobs.
- **Nevan**: 23's swarm homes as decoded, but leaves from her chest (her formation isn't kept) and its vectors are taken in her frame [inferred]; breaking free of the kiss doesn't play her [3] 21 (above); 29 / 38's event 5
  (FUN_003ba660) isn't read; Shl03-05's shapes beyond their numbers; the teleport's material steps (+0x9c / +0xa0)
  aren't shown; her 7 / 8 teleports keep to the stage instead of the game's wall ray; her bats are glows; her speed
  carried between moves isn't confirmed. `nevan_boss.c`.
- **Beowulf**: which of Dante's blows are his kind-4 records (the port's heavy, slam, launch [inferred]); a feather heads
  out from his centre 200 up [inferred] and its burst isn't drawn; the run stops at once, not at its next stride's event; his charge (4) from the
  run isn't taken. `enemy.c` Beowulf, PortJobs.
- **Lady**: her grapple (0x1d) and ledge mode (0x1e) are ported, but the wire isn't drawn and she rises straight up
  (its direction, 0044b780, isn't read); the ledges aren't in the port's collision, so she perches at y 501 by hand
  until her hop; FUN_00449120 taken as the ledge nearest Dante; the hop's landing 0x0e plays 0x61's (its slot is
  empty); 0x8f fires one pistol shot [inferred];  what 0x1b / 0x1c fire (a rocket, a
  grenade each [inferred]); 0x5c's motion (its slot is empty: the gun's draw); her run's speed [estimate]; the room
  clamp (1500 about the middle, for its walls); shots per burst (3; the second gun 1); which record each weapon uses
  [inferred]; her flinches and knock-back gauge. `lady_boss.c`, PortJobs.
- **Cerberus**: two particle programs stand in. CERBERUS_SPEC.md.
- **Vergil**: gravity 1 [inferred]; effect 0x338 is a hit spark; his drop lands where Dante stands; his hit reaction
  isn't decoded (flinches only between attacks, CEm035's table 0x5dcc90 unread); his coat and Yamato's sheath drawn
  rigid (no cloth); his long reaction (motion 111) where the demo takes over. `vergil_boss.c`.
- **Geryon**: gallop laps 12 [approximation]; both bodies play motion n for action n [inferred]; the cart's missiles'
  path; the shockwave's trigger (within 500 for 90 frames) [inferred]; his caps' record [inferred]; the order of his
  runs; the cart's bone layout [inferred]. `geryon_boss.c`, GERYON_SPEC.md.
- **Doppelganger**: the middle region within 550 [estimate]; which dark sixth he comes up in [inferred]; his coat
  rigid (its cloth em031_01.clt isn't run); his sword's offset [inferred]; the crests' lighting is a glow.
  `doppelganger_boss.c`, DOPPELGANGER_SPEC.md.
- **Arkham**: one fight runs both rounds; Vergil joins the second as Dante's partner (Dante's input a frame late, as the
  game), but plays Dante's moveset on his body (his own isn't ported), with no coat or sword, untouchable; Arkham stays where the EST puts him; his body radius 220 and Legions' speed 8 [estimates]; Legions drawn
  rigid, their count and the summon clock's reload [estimates]; his hits' damage not from records; demo 53 / 328 not
  played. `arkham_boss.c`, ARKHAM_SPEC.md, PortJobs.
- **Hell Vanguard**: the P312 dissolve. VANGUARD_EFFECTS.md.

## Missions and rooms

- **Mission 3**: switch and seal models (stand-ins), the unlock camera event. BAR_CRANK_SPEC.md.
- **Mission 6**: 125's spike trap (CStageSetNeedle not decoded; speed and spacing estimates, `needles.c`, shared with
  131 and Secret Mission 11); the barrier's effect (vfunc +0x114: "no damage" by the walkthrough); 124's switches'
  and wall's places; the pillars don't rise; Essences and Artemis glows; messages, cameras and demos 15 / 321.
- **Mission 7**: 114's jump pads' arc; 105's hanging ball (CStageSetHang hit counts, stand-in blocks); 127's door
  into 114 lands on the ground floor; 144 has no rain.
- **Mission 9**: 203's light beam (CRYSTAL 102) isn't decoded (the reset panel drops seal 101); 205's / 207's bladed
  pillars' 700 damage [estimate] and the blades don't turn; Mission Clear as Nevan falls (not door 1 / demo 24);
  206's item 31; 204 / 205 / 207 run group 0 by the walkthrough [inferred].
- **Mission 11**: the wagon ride's camera moves (ops 0x7c / 0x7d), clock and lights, the truck's enemy part, and the track's chaining (three baked copies) are the port's (GAME_FLOW); 214's slide blocks (300 × zure a hit, stone boxes, don't stop Dante); Mission Clear as
  Beowulf falls, not demo 30. `wagon.c`, `mission11.c`.
- **Mission 12**: Mission Clear handling of start 1 [approximation]. `mission12.c`.
- **Mission 13**: 216 is a static 8 × 4 m deck (the ride doesn't move); Dante leaves 216 at 239 start 1; 201's bridge
  isn't turned by item 50; Quicksilver laid as a glow at start 2.
- **Mission 14**: the adjudicator takes any 20 melee hits (no style rank, no Hyper Fist), a stone block; 236 runs
  groups 2-6 in a row; its panels stay as the stage has them.
- **Mission 15**: the portal is struck from the walkway; its wait 120 frames [estimate]. `mission15.c`.
- **Mission 16**: the hanging spheres are plain spheres; gravity, damage and switch pairing [estimates]; the items
  aren't held back by them; Lady (above).
- **Mission 17**: 128's God-cubes aren't moved (Dante comes in at start 2); 131's spikes are Mission 6's stand-in.
- **Mission 18**: gates are doors; the bingo board's rule (CStageSetBingo) read as 3 × 3 in door order; the mirror is
  a glow; 401's chessmen spawn as Pawns or Prides.
- **Mission 19**: 405's mirrors run (mission19.c) with a green glow for their effect and tint, the port's burst for the
  shatter, the port's 42 / 84 a blow and its heavy hit for the throw; 403 and 406 are only passed through; the hourglass's HUD is a countdown,
  group 0 respawns in place of groups 1 and 0, the glass's turn isn't played. `mission19.c`, PortJobs.
- **Mission 20**: see MISSION20_SPEC.md.
- **Secret Missions**: 600's goal [inferred]; 605's 60 s limit [estimate]; 608's wagons (CStageSetTruck's timetable,
  the walkthrough's eight wagons at 2.5 m/s [estimate], shots pass every wall); 610's spikes (Mission 6's stand-in);
  611's block sides don't stop Dante, their start when he enters is the port's. `secret_missions.c`, GAME_FLOW.md.
- **Bloody Palace**: rooms 422-449, its floor flow, boss floors, the Mission Select row and a floor counter. PortJobs.
- **Rooms data**: 137's floor heights from doors and starts [inferred]; gimmicks not converted simply appear in the
  script's order [inferred]; a box over 102's platform for the ride up. `gen_rooms.py`.

## Models and motions picked by reading, not by code

- Which motion is which for the Arachne (anCNN), Beowulf (bwNN), Lady (ldCNN), the Blood-goyle, the Jester: read from
  their root paths and lengths [inferred] where their scripts don't name them. `prepare_skinned_model.py`.
- Arkham's origin height over the floor [inferred]; the Legion in its rest pose. `arkham_boss.c`.
- Item 6 is the Yellow Orb; style slot 2 Dark Slayer; some shop bits set in code [inferred]. SHOP.md.
