# Port jobs

- [ ] Soul Eaters: Missions 11 and 15
- [ ] The Abyss: Missions 17 and 19
- [ ] Jester: simplified fight
- [ ] Agni & Rudra: simplified fight
- [ ] Nevan: no lightning or kiss; health, speeds and timings are estimates
- [ ] Arachne: no web shot or spiderlings; health is an estimate
- [ ] Arkham: one fight runs both rounds, and Vergil doesn't join
- [ ] Mission 11: the wagon ride (216) is skipped; a door takes Dante straight to the far platform
- [ ] Mission 16: the swinging-sphere traps aren't in
- [ ] Mission 19: the mirrors aren't in
- [ ] Mission 19: the hourglass isn't in
- [ ] Secret Mission 12: the moving blocks aren't in
- [x] Messages: the event scripts' message blocks run with the game's text in a box (text from the disc at build time, kept local); flags the missions keep in C aren't linked, so a few flag-gated texts don't show or show late (docs/EVENT_SCRIPTS.md)
- [ ] Cameras: the script's camera moves are mostly skipped (the messages half of this job is done)
- [ ] Bloody Palace: rooms 422-449, the floor flow, boss floors, the Mission Select row and a floor counter
- [ ] Real-time cutscenes: none play yet (Mission 2's intro, the Vergil scenes and others); the biggest job left
- [x] Shadow pass: every enemy and boss has Dante's soft floor shadow, sized per kind (sizes are estimates), fading with height; Dante's own now shows (it was hidden under the floor's decals). No faked AO: the rooms carry the game's baked lighting in their vertex colours, which the port already uses. Props have none; the game's own shadow records (SHW) aren't decoded
