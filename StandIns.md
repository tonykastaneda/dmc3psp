# Stand-ins to circle back to

Everything the port does that isn't the game's own code or data yet: stand-ins (the port's own behaviour in place of
the game's), estimates (numbers not read from the game) and inferences (read, but how it's used is a reasoned guess).
Grouped by area, each with where it lives. Keep this list current: when one is fixed, delete its line; when a new
one goes in, add it here as well as in the code. Collected 2026-11-17 from every `stand-in`, `[estimate]`,
`[inferred]` and `[approximation]` in psp/testroom, tools and docs, plus each mission's "Stand-ins and estimates".

## Systems that touch everything

- **Dante held** (his action 0x2e, `combat_hold_named`): his side of each hold is the game's: Nevan's [3] 20 and 21
  (broken free) and her throw (FUN_0022b9b0: turned about, then his own knockdown, no damage); the Soul Eater's lift,
  10, 11, the held loop and 14 as it lets go, turned toward it (FUN_0049dac0); the Arachne's [15] 0, 1 and 2 as he
  struggles (FUN_003f8080). Stand-ins: the Arachne's index 4 (its web's end) and the Soul Eater's let-go root motion
  aren't played, and a blow while he's held takes its damage without moving him [inferred]. `combat.c` held_update.
- **Real-time cutscenes** (docs/CUTSCENES.md) play at missions' openings and clears and at the bosses' entrances and
  defeats that stay in their room, with English subtitles (centred, not at the PS2's x), fades and their own sound (voices and music), but: cloth steps in world space (ClothSim1D's ropes run as decoded, the floor factor taken as 0; ClothMode 1 rides the hips, because bone 0 stays at the origin; a coat gets Dante's own capsules (bones 0-22 match) though whether cutscenes apply capsules isn't read; a chain that never sets the key trails the pose); a CnsPos piece's root on its parent bone [inferred]; an entrance demo plays on entering the room, not in its area; demos between rooms play as Dante arrives in the
  next; demo stages are coarser than rooms (subdivision) to fit memory. PortJobs. Their Light track lights the cast
  (`demo.c` demo_lights): the light's strength 1 - d² / Force² and colour / 128 [inferred: the VU1 lighting isn't
  read], taken at each actor's centre as a directional light; the characters' ambient (`DEMO_AMBIENT`) [stand-in,
  matched by eye]; IsMinus lights left out. The stage is the baked SCM colour: no stage clip sets Light, so it keeps
  flag 2 and ValidType 3 stays idle, and the class-2 lights are not multiplied onto the bake (demos 3, 5 and 59;
  docs/CUTSCENES.md).
- **Cutscene framing and colour** (`demo.c`): the letterbox's 0.712 [measured]; the Program track's No 5 / No 6 as
  colour scale and ambient [inferred from values]. (The cutscene field of view is no longer a stand-in: channel 12's
  raw angle, as `FUN_001ac750` builds it, matches the PS2 once its frames are matched by picture, not by drifting time.)
  Cutscene fog is the ScrEfc GFG track through sceGuFog (`demo_fog`): MinAlpha/128 at StartZ to MaxAlpha/128 at
  EndZ, colour MaxColor. The far distance is extended so the factor at EndZ equals that fraction, because hardware
  fog reaches full colour at its far [inferred]. The sky dome is left unfogged. FCS / DOF blur and SGA / BGA glow are a
  quarter-size GE pass (post_fx.c): the blur's depth bands and the glow's threshold are the port's, FCS's near band and
  SGA's directional smear (Scale, DirNum, Attenuate) aren't modelled, and its look isn't compared with the PS2 yet. Demo 3's over-the-shoulder shot shows holes where the near plane cuts Dante.
- **Model blends** (`model_blend.h`): a MOD object's byte 8 read as the GS ALPHA table index [inferred]; its bits 4
  and 5 unknown.
- **Sound**: movies, cutscenes, the missions' music (two stream channels per room from the event scripts; the battle
  track held until a fight and the stage channel dipping under it [inferred]; tracks loop at their ADX loop points) and
  the system bank's effects (menus, orbs; that it's the game's type 0 is inferred). Dante's own bank and his ten weapons' play from his moves'
  events, his footsteps and the crest switches' and seals' sounds the room's own bank (type 8); the event scripts'
  se / se_3d play in the blocks messages.c runs when they're the room's (kind 0), the system's (1) or Dante's (4)
  bank (not placed); the enemies' banks (types 2-6), the scripts' type 7 sounds and the blocks messages.c doesn't
  keep, the other gimmicks' sounds and the sounds' distance falloff (FUN_0033d8d0) aren't in yet. MUSIC.md. GAME_FLOW.md.
- **No saves**; **Game Over / Continue**: Dante goes down and the mission restarts 150 frames later. GAME_FLOW.md.
- **Event mode**: during a script's event the enemies wait (what the game's event mode stops isn't read).
  `main.c` ~4296, EVENT_SCRIPTS.md.
- **Enemy health and Dante's damage**: Dante's hits are 42 light / 84 heavy (×1.1, Easy ×2, guns 20%) for every boss
  [inferred], a bullet its record's 10 (Ebony & Ivory's, 0x5a5a30 +4) against every enemy, kept in fractions of a unit by the Hells and the like; health is the Pride's 6 light hits by eye and
  the rest in proportion to their parameter blocks. `enemy.c`, `vergil_boss.c`, `geryon_boss.c`.
- **Hit detection on Dante's moves**: his moves hit with their own attack caps (GDATA 0 chunks 6 / 7, switched on by
  the scripts' ops 0x10-0x1F), but the ordinary enemies' bodies are the port's 48 cm cylinder (their hurt caps aren't
  read), bosses and the big enemies still take the old 260 cm reach; the attack records' damage (ops 6 / 7, the bank's
  records at 0x5a7240) reaches the ordinary enemies that count health (his attack power, +0x2a18, taken as 1), but
  not the bosses or the enemies with their own health code, and the records' other fields (reaction kind, hit stop)
  aren't applied. Quicksilver and Doppelganger styles aren't compared with the game; the Shotgun's pellets are its
  shell's (FUN_002ee930) at level 0 only (the gun's level isn't carried), with no tracers. MOVESET_RE.md, `combat.c`.
- **The held guard** loops from 24:30's start (frame 10), as the guard state replaying the script would [inferred:
  FUN_00224850 isn't decoded]. `combat.c`.
- **Shadows**: sizes per kind fitted by eye [estimates]; props have none; the game's SHW records aren't decoded.
  `enemy.c` shadow_radius.
- **Effects**: many particle programs and model effects are motes, glows or `fx_explosion` (the Jester's
  trap ring, Geryon's missiles, Cerberus's unported
  particle programs). NATIVE_FX.md, VANGUARD_EFFECTS.md, CERBERUS_SPEC.md.
- **P effects** (particle programs, `pfx.c`, NATIVE_FX.md "P effects"): the GS blend 0x41 taken as Cd - Cs As; a
  camera-facing emitter's axes from the eye and world up (not the camera's own up); fake
  sprite layers face the camera; the sheet's top taken as a sprite's top; Line02 isn't ported.
- **The Devil Arms' effects** (`update_arm_fx`, NATIVE_FX.md "Dante's effects"): on their bones' positions, turned by
  Dante's yaw only (not the bone's frame); A&R's on the weapon placement's origin, not the weapon object's frames;
  Nevan's V0x2c1 / V0x38e start on any 6:14 / 6:40 / 6:9 / 6:10 / 6:33 as the window bit turns on, not only in
  action 81 / 82's phases. Nevan's V0x38e dome reads whiter and more solid than the PS2's (analysis/ref/pfx/nevan_ab.png).
- **Royalguard's judgement** (`combat_player_hit_kind`, NATIVE_FX.md): the blow's guard class (+0x3e) is taken
  from its hit kind (knockdown kinds class 1, others 0) [estimate]; the style level is 2; grade 5 (+0x3c82) is unreachable in the
  game's own code (set only after a type-5 block, which needs it); the guarded attacker's reaction (`enemy_guarded`) is ported for the Hells and Beowulf only: Nevan's (+0x13c
  object's +0x10) and the other classes' +0x114 handlers aren't read (Lady's and the Enigma's do nothing); and Release's damage from the gauge (+0x2a1c) aren't carried (the port's blows are in
  units of health).
- **Blade trails** (`fx.c` mtrails): the game's trail models bent through the last 11 weapon frames, type 0 added and types 1 / 2
  alpha-blended (the glow's +0x68 parameters 5 / 5 aren't read); the frames between samples are FUN_00133ab0's (y and z
  blended, the frame rebuilt round z by FUN_00133890). NATIVE_FX.md "Blade trails".
- **Devil Trigger** (`update_devil_fx`): the game's V0xdc / E0x140 / V0x3c on his hips, by position and yaw only; the
  PS2's lattice burst as it ends isn't played (its source isn't found).
- **Gimmick models**: crest switches and seal boards are their own models (`gimmick_models.c`), but a switch's flames
  (effect 0x181) and lit effects are glows, the seals' hands (EFM bmodels) and random shimmer (FUN_0031c390) aren't
  drawn, and a seal is drawn on every door while a fight is on. `main.c`, `gimmicks.py`, `mission6.c`.
- **Hit shapes** (types 2 / 3 / 4 / 6) are read by their use [inferred]. ENEMY_SPECS.md G.

## Enemies

- **Seven Hells**: the Wrath's blast uses the port's explosion effect and tests Dante at its feet; friendly fire and its attachment matrix remain unported. ENEMY_AI.md, GAME_FLOW.md.
- **Abyss**: the Pride's hit-script list (its block's +0x14 pairs aren't read); its scythe is the largest of [3]-[5]
  [inferred]. `gen_enemy_ai.py`, `prepare_skinned_model.py`.
- **Damned Chessmen**: the shared chess master is ported (docs/CHESS.md), at fixed difficulty 0. Knight 36, Bishop 37
  and Rook 38 use native moves / attacks (docs/CHESS.md); received-hit scaling, effect attachment matrices, the Bishop's
  global attack gate / wall bounds, the Rook's summon materialization (V1ee and a normal spawn) and fixed rank remain
  approximations. `enemy.c`, GAME_FLOW.md (Missions 14, 18).
- **The Damned Queen and King** (EST 39 / 40, `queen.c`, `king.c`, `royal.c`, `royal.inc`; docs/CHESS_ROYALS_SPEC.md):
  their logic is the decode's, on chess.c's master (commands 20 / 30, the King-death cascade), but: rank 0 always; walls are the room's floor ending or a step up of more than 60 cm (the raised squares), not the
  collision's flags; V0x242's lane lies flat (its fixed -90 X) but isn't stretched (1, 1, 30); the landing / hit shakes (FUN_0040a2a0) and
  their sounds (banks 0x15 / 0x16) aren't played; the King's +0x3c4 is taken as the HP before the hit [inferred];
  the port's launch is native kind 3 (light knock) and its heavy / slam kinds 4 / 7 (heavy knock) [inferred]; their
  death (CEm017's 0x406d50) is a 60-frame sink; damage taken is the port's 1 / 2 a hit.
- **Hell Greeds**: the coffin's flight, landing, summon cap and visuals are estimates. Summons use Prides and the coffins cannot be broken. `greed.inc`.
- **Blood-goyle**: its bullets fly at 25 a frame for 120 frames [estimate]; the split timing [estimate]; its rest
  value 5 and stone's triple damage [inferred]; stone drawn untextured grey. ENEMY_SPECS.md D.
- **Arachne**: its class is ported (ENEMY_SPECS H); stand-ins: its hits' bones (a sphere 100 ahead), flag 4 taken as
  its scripts' first event, the wall's normal (the ray back, flattened) and its pose there (turned 90 degrees), 0x6d's
  web-line swing (a leap), 0x6f's speed (8), the web's struggle count (35 [inferred]), its fall when struck on a wall,
  root motion x1.5 [inferred]; its web's held effect sits at Dante's middle, not his bone; spiderlings (0x76) and the charge (0x78) never chosen at rank 0.
  `enemy.c` The Arachne, PortJobs.
- **Fallen**: its damage handler isn't decoded [estimates]; facing taken from Dante, not his facing; on the ground it
  closes 3 a frame to 320 from him; its wings take front hits [approximation]; health 1500 by the port's scale
  [estimate]; its spear is straight at his chest (the shell isn't decoded). `fallen.c`, `enemy.c` ~5383.
- **Dullahan**: which script is which [inferred]. `dullahan.c`.
- **Soul Eater**: Dante is held from its bite and struggles free (above); the rank-0 blink reappears ahead of Dante's facing, not the camera's; its
  dodge (16) isn't triggered (needs Dante's move 0x11); the gaze cone ±60° [inferred]; 1000 DT a rune [inferred].
  `enemy.c` soul_*.
- **Jester**: all three encounters and his second set run; stand-ins: the charge (0x7f) turns at the port's arena
  clamp, his throws are taken to end on the floor (+0x4818) [inferred], the second set's effects and room flag aren't
  shown, his idle's turn rate [inferred], the bomb stays rigid with estimated tumbling; the hand trap plays its native motion and opacity envelope, the cross and the bomb play the game's effects (the bomb's with its P particles), the trap's ring is motes; ordinary E&I stun uses the native raw10 x1.75, the arena clamp is the stand-in's. G0x1c8 belongs to a separate player-weapon actor, not his hand (ENEMY_SPECS E). `enemy.c` The Jester, `jester.c`, PortJobs.
- **Agni & Rudra**: the opening (state 64) is ported; Rudra's 60-frame delay reads +0x2ee0 as his place in the pair
  (inference); they wait as statues (ar71 / ar72) where the game starts them in state 1 at y 100 (the cutscene's) and
  wake within 900, when struck or after 600 frames [estimate]. The pedestal climb, the guard and the hops are the game's; stand-ins: a blow's "Dante's +5 is 0-2" (a
  guarded blade counts 2) is taken as always, and its "+5 is 3, +6 his id" (counts 2 in a swing) as never [inferred];
  Dante's recoil from a guarded blow (the hit's +0x34) isn't modelled; a wall's push for the leap (vtable +0x1c0) is
  read as the walls pass moving him over 1 cm since his own move [inferred]; the survivor's moves below each health
  mark (57 / 58) aren't picked (their shots aren't decoded); the pedestal's sword flash (0x3b3 / 0x3b4) isn't shown and
  the landings' effects (0x2c, 0x2e5) are dust; the slam hurting within 300 as record 12 [estimate], its effects at
  the hand's floor point [estimate]. `agni_rudra.c`, `enemy.c` Agni & Rudra, PortJobs.
- **Nevan**: 23's swarm homes as decoded, but leaves from her chest (her formation isn't kept) and its vectors are taken in her frame [inferred]; breaking free of the kiss doesn't play her [3] 21 (above); 29 / 38's event 5
  (FUN_003ba660) charges the bats about her (+0x1b7: their touch takes record 7, 500, not 0's 300), which the port
  doesn't keep, so it does nothing; Shl03-05's shapes beyond their numbers; the teleport's darkening is a draw colour
  toward black [inferred: the material words' GS meaning], its G0xe / G0x93 100 up from her [estimate: her bone +0x724];
  her speed
  carried between moves isn't confirmed. `nevan_boss.c`.
- **Beowulf**: which of Dante's blows are his kind-4 records (the port's heavy, slam, launch [inferred]); a feather heads
  out from his centre 200 up [inferred] and its burst isn't drawn; the run stops at once, not at its next stride's event; his charge (4) from the
  run isn't taken. `enemy.c` Beowulf, PortJobs.
- **Lady**: her grapple (0x1d), native hook/skinned cable and its direction (0044b780), and ledge mode (0x1e) are ported;
  her ledges now use the room's authored box collision; FUN_00449120 taken as the ledge nearest Dante; the hop's landing 0x0e plays 0x61's (its slot is
  empty); 0x8f fires one pistol shot [inferred];  what 0x1b / 0x1c fire (a rocket, a
  grenade each [inferred]); 0x5c's motion (its slot is empty: the gun's draw); her run's speed [estimate]; the room
  clamp (1500 about the middle, for its walls); shots per burst (3; the second gun 1); which record each weapon uses
  [inferred]; her flinches and knock-back gauge. `lady_boss.c`, PortJobs.
- **Cerberus**: the breath's generators and the punch's P58 / P59 take the body's turn, not their bone's (the places are the
  bones') [approximation]. NATIVE_FX.md, CERBERUS_SPEC.md.
- **Vergil**: his effects (0x338, the slashes) play from his pack but follow his bones by position only (no bone
  rotation) [approximation]; air movement now consumes vertical root motion and uses the decoded fixed downward dive/floor pull (VERGIL_SPEC.md); his hit reaction
  is decoded (gauges, flinch windows, the brace), but Dante's blows take estimated amounts from his gauges (Dante's
  attack records aren't decoded); his coat and cord swing as their .clt chains, with Dante's body capsules [inferred];
  his long reaction (motion 111) where the demo takes over. `vergil_boss.c`.
- **Geryon**: gallop count now consumes 40 decoded script windows (GERYON_SPEC.md); its high-contact history uses the existing push footprint instead of the native cap record; his death's motion (gh19) [inferred]; the missiles' path and timing are decoded, their hit [estimate] and effects not; the shockwave comes from his gauges as decoded, Dante's blows' amounts [estimates]; his caps' record [inferred]; the cart's bone layout [inferred]. `geryon_boss.c`, GERYON_SPEC.md.
- **Doppelganger**: the seven-disc region selector is decoded (550 middle / 600 outer, reverse priority), and so is where he rises when lit (3c4c40); the underground travel is a jump; his coat's
  chain (em031_01.clt) gets Dante's body capsules [inferred]; his sword's offset [inferred]; the crests' lighting is a glow.
  `doppelganger_boss.c`, DOPPELGANGER_SPEC.md.
- **Arkham**: one fight runs both rounds; Vergil joins the second as Dante's partner (Dante's input a frame late, as the
  game), but plays Dante's moveset on his body (his own isn't ported), with his coat and Yamato, untouchable; Arkham stays where the EST puts him; his body radius 220 and Legions' speed 8 [estimates]; Legions drawn
  in their tail chain's settled pose (its settling and 3ed840's states 4 / 5 not shown), their own AI (3e9cd0) not
  ported; side-hit geometry still uses zones instead of moving caps, and capture release uses a damaging rescue hit instead of the native cap flags/held handle. Damage records, Legion radius/count/clock and mouth hold/drain are decoded (ARKHAM_SPEC.md). `arkham_boss.c`, ARKHAM_SPEC.md, PortJobs.
- **Hell Vanguard**: the model's fade out and in round P312 (the game hides and shows it). VANGUARD_EFFECTS.md.

## Missions and rooms

- **Mission 3**: the unlock camera event. BAR_CRANK_SPEC.md.
- **Mission 6**: the spike traps' level (125, 131, 610; `needles.c`) by the difficulty, not the value the game reads
  (game +4, +0x10, vfunc +0x1c); the barrier's effect (vfunc +0x114: "no damage" by the walkthrough); 124's switches'
  and wall's places; the pillars' and stair's rise eased (their Hermite keys aren't sampled) and the camera's length
  before 111 rises [estimate]; messages, cameras and demos 15 / 321.
- **Mission 7**: 114's jump pads' arc; 105's hanging ball (CStageSetHang decoded: its drop, 1320-frame clock, 1200 HP and five shell stages; stand-ins: its
  EFM model as blocks, its swing a damped pendulum, the switch lit again to restart it [inferred]); 127's door
  into 114 lands on the ground floor; the rooms' STE effects are read only for 144's rain.
- **Mission 9**: 203's light beam runs CStageSetCrystal's layout 0 (crystals drawn as glows, the beam a line); 205's / 207's bladed
  pillars' 700 damage [estimate] (their blades turn by their models' own motion);
  206's item 31; 204 / 205 / 207 run group 0 by the walkthrough [inferred].
- **Mission 11**: the wagon ride's camera moves (ops 0x7c / 0x7d), clock and lights, the truck's enemy part, and the track's chaining (three baked copies) are the port's (GAME_FLOW); 214's slide blocks (CStageSetSlide decoded: damage x zure a hit at 10 cm a frame; Dante's damage 42 / 84,
  stone boxes, don't stop Dante). `wagon.c`,
  `mission11.c`.
- **Mission 12**: Mission Clear handling of start 1 [approximation]. `mission12.c`.
- **Mission 13**: 216 is a static 8 × 4 m deck (the ride doesn't move); Dante leaves 216 at 239 start 1; 201's bridge
  isn't turned by item 50; Quicksilver laid as a glow at start 2.
- **Mission 14**: the adjudicator takes any 20 melee hits (no style rank, no Hyper Fist), a stone block; 236 runs
  groups 2-6 in a row; its panels stay as the stage has them.
- **Mission 15**: the portal is struck from the walkway; its wait 120 frames [estimate]. `mission15.c`.
- **Mission 16**: the hanging spheres are plain spheres; gravity, damage and switch pairing [estimates]; the items
  aren't held back by them; Lady (above).
- **Mission 17**: 128's God-cubes move by their models' motions and carry Dante as Secret Mission 12's blocks do
  (`blocks.c`: tops only, the sides don't stop him); Dante still comes in at start 2 (the climb isn't played through).
- **Mission 18**: gates are doors; the mirror is a glow (the bingo board's rule is CStageSetBingo's, its board model
  isn't drawn lit); all of 401's chessmen (EST 35-40) have their own logic.
- **Mission 19**: 405's mirrors run (mission19.c) with a green glow for their effect (E0x2e0's model texture isn't decoded) and tint, the port's 42 / 84 a blow and its heavy hit for the throw; 403 and 406 are only passed through; the hourglass's HUD is a countdown,
  group 0 respawns in place of groups 1 and 0, the glass's turn isn't played. `mission19.c`, PortJobs.
- **Mission 20**: see MISSION20_SPEC.md.
- **Mission 20's ending street** (`credits_fight.c`, docs/CREDITS_FIGHT.md): the kill counter's place on the PSP screen is matched to the PCSX2 capture, not converted from the PS2's screen units; every death in the street counts once (the PS2 counted 121 for 100 spawns, not decoded). Lady the ally (`lady_boss.c` ally modes 0x2f-0x33): her shots fly at her target and only it takes them (the game's bullet hits any enemy on team 1), their damage from her attack records (80 a bullet, 0x5cbc40; 1500 the rocket) given as that many of Dante's hits; her target is the nearest enemy (FUN_0044c8b0's cone test isn't ported); her rocket counter (+0x4838) fills 5 a bullet up to 100 and the rocket takes 50 (FUN_004497f0 / 0044cc40), every shot taken as in her sight (FUN_00359b40, which picks 5 or 1, isn't ported) [inferred]; 0x85's three shots and the rocket's frame are estimates; enemies don't attack her (the Seven Hells' AI targets the player, game+0xc, and nothing in it refers to her) and she isn't counted by the spawn script's alive gates [inferred]. The staff roll: a page's full alpha 0x40 is drawn as opaque [inferred: the model's alpha doubled]; the pages are the disc's pictures cut to 256 colours (tools/prepare_staffroll.py) and drawn 4:3 in the middle of the screen as the PCSX2 capture shows them; each loads from the memory stick when its slot frees (a short read on the PSP). The ending: op 7e [3] / [4] are taken as stopping and resuming Dante's and the enemies' updates [inferred]; EST ops 10 / 11 and what sets the ending flag (game+10) aren't decoded (the port's flag is Mission 20's room 012); the music is the room's BGM table's (the credits' track isn't checked: the capture has no sound). The look: the fog's colour is LIG2's (checked in PCSX2), but its near 0 / far 4100 is fitted to PCSX2's measured fog [estimate]; the stage is drawn 3.5x the port's usual brightness, measured against PCSX2 at the same camera (why the PS2 draws it brighter isn't decoded) [estimate].
- **Secret Missions**: 600's goal [inferred]; 605's 60 s limit [estimate]; 608's wagons (CStageSetTruckEnemy's in-stop-out run and sounds
  decoded; the event op's arguments for 608 aren't found: eight wagons, 2.5 m/s, a 10 s stop [estimates]; shots pass
  every wall);
  611's block sides don't stop Dante, their start when he enters is the port's. `secret_missions.c`, GAME_FLOW.md.
- **Bloody Palace** (`bloody_palace.c`, docs/BLOODY_PALACE.md): ordinary EST encounters use the Normal column;
  Palace's native attack-slot budget = floor/1000+1 is applied to the shared scheduler; separate boss AI retains its own scheduling. Portals use the original V965/V967/V964
    water/lightning/fire effects through the existing partial native-effect runtime. The portal legend is debug-only. Startup
  smoke now expires rather than looping and uses alpha blending; sprite tiles use inset clamped linear sampling
  (remaining GS blend/particle rendering differences are approximate). Arrival
  green/white orbs use their decoded positions and recovery values, but mote visuals, simplified falling and
  collection within 120 cm; Lady's extra defeat pickups are omitted. The transient Level label uses a widened substitute serif font; native position, fade and slide timing are restored. Arena camera
  initialization starts its collision sweep at Dante's head: the default safe point lies beneath the raised arena.
  Boss entrance/defeat camera sequences are skipped; boss behaviors retain their documented story-mode stand-ins.
  Boss exits 433 / 436 / 448 / 449 show their warp (set 200: V487 and its sounds); the others (seals) keep the item
  glow; unmapped EST enemy classes retain
  the shared enemy mapper's fallback (including some rare ordinary-floor sets and the unported chess pieces).
  Best floor uses the port's BP_BEST.TXT sidecar and a selected-row menu label, not the PS2 memory-card/ranking UI.
  The task's requested kill-time countdown is omitted: EventTbl21 and PCSX2 show no survival timer in this PS2 mode.
- **Rooms data**: 137's floor heights from doors and starts [inferred]; gimmicks not converted simply appear in the
  script's order [inferred]; a box over 102's platform for the ride up. `gen_rooms.py`.

## Models and motions picked by reading, not by code

- Which motion is which for the Arachne (anCNN), Beowulf (bwNN), Lady (ldCNN), the Blood-goyle, the Jester: read from
  their root paths and lengths [inferred] where their scripts don't name them. `prepare_skinned_model.py`.
- Arkham's origin height over the floor [inferred]; the Legion in its rest pose. `arkham_boss.c`.
- Item 6 is the Yellow Orb; style slot 2 Dark Slayer; some shop bits set in code [inferred]. SHOP.md.
