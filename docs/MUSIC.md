# Music

The game streams its music from SDATA.AFS (154 `.adx` files: battle, boss, stage and ambience tracks, the cutscenes'
own tracks, menus). The port plays the missions' music, the cutscenes' sound and the system bank's sound effects.

## In the missions

- **Event op 0x70** `bgm t0, t1, c0, c1` (`FUN_0020be20` -> `FUN_0032cef0`) sets two stream channels, a track and a
  command each. A track is an index into the word table at `0x5b14b0`, whose value is an SDATA index: 1 -> 2
  (Battle_01), 36 -> 135 (Stage_01), 48 -> 33 (Kaze_01, wind), 86 -> 31 (Jester), 18 -> 147 (T_boss).
- **Commands** (`FUN_0032cef0`): 1 / 2 stop or pause; 3-6 fade out (0xb4 or 5 frames); 7 / 8 load and play (8 with a
  0xb4 fade); 9-11 play if it isn't the track already on (crossfading); 12-16 only set a deferred state (5, 6, 8, 9)
  that the sound manager acts on later (not read).
- **The scripts' pattern** (each room's first bgm op, 159 rooms): channel 0 holds the room's battle track with command
  12, channel 1 plays its stage music or ambience (15 / 16); boss rooms fade channel 0 (3) and play the boss track on
  channel 1 (10).

## In the port

`tools/gen_music.py` writes `music_runtime.h` (mission, room, SDATA track and command per channel) and converts each
track used (40, 200 MB) to `BGM_nnn.BIN`: `BGM2`, rate, sample pairs, the loop's start and end, the IMA coder's
state at the loop's start, then the movies' IMA ADPCM, a byte per stereo pair. The loop is the ADX header's (version 4:
flag +0x24, start +0x28, end +0x30, in 48 kHz samples; Option.adx plays a 35 s opening once, then loops from there); a
track without one (Result.adx) loops whole. `psp/testroom/music.c` plays two hardware channels, a streaming thread
each, to the loop's end and back to its start:

- command 0 nothing, 1 / 2 stop, 3-6 fade out, 12 hold the track and fade it in while enemies are alive in the room
  (out again once it's clear) [inferred: state 5's handling isn't read], the rest play (7 / 8 at once, others fading in
  over 1.5 s, a track already on kept).
- The other channel dips to 15% under battle music [inferred]. Cutscenes and movies silence it.

Checked in PPSSPP: Mission 3's room 2, Kaze_01 on channel 1 and Battle_01 coming in on channel 0 when the fight starts,
streaming at 44.1 kHz.

## Menus

The menus play Option.adx (SDATA 129; the game's menu code loads track 0x81) and the mission-clear screen Result.adx
(130; 0x82 in the results screen's code), at full volume on channel 0 (`music_menu`); a mission's start stops them.

## Sound effects

The effects are banks of SPU ADPCM samples that the game's own EE code plays (the IOP only drives the SPU):

- **A bank** is three files: a table `dbsT` (u32 count at +12, then 32-byte records from +16, one per sound id), a
  `Head` (chunks `Prog`, `Smpl`, `Vagi` at its +0x14 / +0x18 / +0x1c) and the samples. As a PAC (GDATA 141-205
  and on) they're its entries 0, 1 and 2; `FUN_0033dff0(manager, type, pac)` installs one for a type (the table through
  `FUN_00304010`, the head through `FUN_00302c80`, the samples uploaded by `FUN_00302560`).
- **Playing an id** (`FUN_00304110`, its record from `FUN_00304030`: the type's table at `0x1e58180[type]`): +0 the
  command (1 a note), +6 the program, +7 the note, +12 volume, +13 pan (0x40 the centre), +14 i16 pitch in cents, +22
  i16 the id it chains to (-1 none). The note picks a tone of the program (14 bytes: +2 / +3 its key range, +8 volume,
  +9 pan, u16 +12 its Smpl); the Smpl (12 bytes) gives the root key (+5), volume (+6) and its Vagi (u16 +10); the Vagi
  (16 bytes) the sample's offset, size and rate. The pitch is 2^((note - root) / 12) of the sample's rate.
- **Callers**: `FUN_0033e630(manager, type, id, pan, bank)` plays one where it is, `FUN_0033e820` at a place;
  types 3-6 become 2 with the bank's base from `0x5af3c0`, 2 with `0x5af400`; the event scripts' `se` op uses the
  former. Type 0 is the **system bank**: GDATA 170 (dbsT, 100 ids), 169 (Head) and 171 (samples) [inferred: the ids its
  callers pass are its filled records]. The menus play 2 as the cursor moves, 3 on a choice and 4 going back
  (`00415290`); an orb plays 0 (sizes below 3) or 1 as it's taken (`FUN_002f3750`).

In the port, `tools/gen_sfx.py` resolves the system bank into `SFX_SYS.BIN` (per id: the sample, volume, pan, its
playback step at 44.1 kHz and the id it chains to; then the samples as they are), and `psp/testroom/sfx.c` mixes up to
eight voices on a hardware channel of its own, decoding the ADPCM as it plays (16-byte frames: a shift, one of five
filters, 28 nibbles; the end flag stops it) from a 192 KB cache it fills from the memory stick. Hooked: the menus'
cursor, choice and back (title, Mission Select, the shop, the pause menu) and the orbs. Checked in PPSSPP: the bank
loads, a sound's whole sample is decoded and mixed (125 of 126 frames to its end flag).
## Cutscenes

See CUTSCENES.md (Sound): each real-time demo streams its own SDATA track (`<package>.adx`).

## Not yet

- **Sound effects** beyond the system bank's menus and orbs: the player's, the enemies' and the rooms' banks (types
  1, 2 and 7) and the calls that play them.
- The deferred states' exact handling (commands 12-16).
- The title's and Game Over's music (Continue.adx, SDATA 27, by its name; the port's Game Over is a 2.5 s pause before
  the restart).
