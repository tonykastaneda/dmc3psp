# Music

The game streams its music from SDATA.AFS (154 `.adx` files: battle, boss, stage and ambience tracks, the cutscenes'
own tracks, menus). The port plays the missions' music and the cutscenes' sound; there are no sound effects yet.

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
track used (38, 192 MB) to `BGM_nnn.BIN`: `BGM1`, rate, sample pairs, then the movies' IMA ADPCM, a byte per stereo
pair. `psp/testroom/music.c` plays two hardware channels, a streaming thread each, looping:

- command 0 nothing, 1 / 2 stop, 3-6 fade out, 12 hold the track and fade it in while enemies are alive in the room
  (out again once it's clear) [inferred: state 5's handling isn't read], the rest play (7 / 8 at once, others fading in
  over 1.5 s, a track already on kept).
- The other channel dips to 15% under battle music [inferred]. Cutscenes and movies silence it.

Checked in PPSSPP: Mission 3's room 2, Kaze_01 on channel 1 and Battle_01 coming in on channel 0 when the fight starts,
streaming at 44.1 kHz.

## Menus

The menus play Option.adx (SDATA 129; the game's menu code loads track 0x81) and the mission-clear screen Result.adx
(130; 0x82 in the results screen's code), at full volume on channel 0 (`music_menu`); a mission's start stops them.

## Cutscenes

See CUTSCENES.md (Sound): each real-time demo streams its own SDATA track (`<package>.adx`).

## Not yet

- **Sound effects.** `FUN_0033ee10` offsets an effect id by the owner's bank base (`0x5af430`) and sends it to the IOP
  sound driver over RPC (`FUN_003027a0`). The banks aren't Sony HD/BD and the id-to-sample mapping lives in the IOP
  module, so porting them means reverse engineering that driver.
- The deferred states' exact handling (commands 12-16).
- ADX loop points: the port loops the whole track.
- The title's and Game Over's music (Continue.adx, SDATA 27, by its name; the port's Game Over is a 2.5 s pause before
  the restart).
