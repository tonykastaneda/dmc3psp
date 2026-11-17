# Port jobs

- [ ] Soul Eaters: Missions 11 and 15
- [ ] The Abyss: Missions 17 and 19
- [ ] Jester: his attacks, throws, defence and stun meter are the game's; not yet: his second set of moves (later encounters and the health threshold), his idle actions, the throws' effects, a bullet's stun value (estimate)
- [ ] Agni & Rudra: brain, attacks, reactions to Dante's moves, turns and knockbacks are the game's; not yet: the pair's opening and team moves, the scripts' guard stances, the hops' records, the effects
- [ ] Nevan: no lightning or kiss; health, speeds and timings are estimates
- [ ] Arachne: no web shot or spiderlings; health is an estimate
- [ ] Arkham: one fight runs both rounds, and Vergil doesn't join
- [ ] Mission 11: the wagon ride (216) is skipped; a door takes Dante straight to the far platform
- [x] Mission 16: 146's shelled sphere and 109's pair swing, lower on their switches, wear and knock as decoded; stand-ins: plain spheres, gravity and damage estimates, and the items aren't held back by them yet (finishing them wasn't shown in PPSSPP)
- [ ] Mission 19: the mirrors aren't in
- [x] Mission 19: the hourglass runs the script's 90-second timer and the room's own sand and spin motions, and the Abyss come back when it runs out; stand-ins: a countdown for its HUD, group 0 respawned in place of groups 1 and 0, the glass's turn not played
- [x] Secret Mission 12: the moving blocks run their own motions from the room's data and carry Dante; their sides don't stop him (stand-in), and the motion's start when he enters is the port's
- [x] Messages: the event scripts' message blocks run with the game's text in a box (text from the disc at build time, kept local); flags the missions keep in C aren't linked, so a few flag-gated texts don't show or show late (docs/EVENT_SCRIPTS.md)
- [x] Cameras: the scripts' in-engine events play their camera cuts (the rooms' DCA chunks), zone switches, follow-camera settings and warps, skippable with START; stand-ins: the enemies wait during an event, and groups the script starts in an event are the port's waves, already there
- [ ] Bloody Palace: rooms 422-449, the floor flow, boss floors, the Mission Select row and a floor counter
- [ ] Real-time cutscenes: none play yet (Mission 2's intro, the Vergil scenes and others); the biggest job left
- [x] Shadow pass: every enemy and boss has Dante's soft floor shadow, sized per kind (sizes are estimates), fading with height; Dante's own now shows (it was hidden under the floor's decals). No faked AO: the rooms carry the game's baked lighting in their vertex colours, which the port already uses. Props have none; the game's own shadow records (SHW) aren't decoded
