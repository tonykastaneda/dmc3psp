# Real-time cutscenes ("demos")

## Palette preservation

The shared model exporter used FASTOCTREE quantisation even for decoded TIM2 pictures already containing
at most 256 RGBA colours. That quantiser merges distinct colours even when an exact PSP palette fits. The
exporter now assigns those colours deterministic CLUT entries in first-pixel order, preserving RGB and alpha
byte for byte at the same texture memory cost. Pictures with more than 256 colours retain the existing quantiser.

Decoded source versus reconstructed PSP palette, before this fix (percentage of changed pixels):

| Local source | Page | Changed pixels | Maximum channel error |
|---|---:|---:|---:|
| Demo 0, `pl100_01.ptx` | 0 | 81.99% | 30 |
| Demo 123, `pl102_01.ptx` | 0 | 76.73% | 30 |
| Demo 10, `pl104_01.ptx` | 0 | 98.17% | 26 |
| Demo 114, `em108.ptx` | 0/1 | 96.01% | 20 |
| Demo 114, `em108.ptx` | 2/3/4 | 87.76% | 24 |

All listed pages reconstruct byte-exactly after the fix. Before it, alpha changed on 8,986 pixels of
`pl100_01` and 24,795 pixels of `pl102_01`. These are pipeline comparisons against the decoded source,
not claims of a PS2 visual match. `python tools/tests/test_indexed_texture.py` checks an exact 256-colour
round trip, distinct alpha values for equal RGB, determinism and the truecolour fallback. Emulator comparison
is tracked separately below; palette preservation alone does not establish the cause of Arkham's face issue.

The face atlas confirms the cast mapping: `pl100` Dante, `pl101` Vergil (blue coat), `pl102` Lady,
`pl103` human Arkham (bald head), `pl104` Jester (white face). The face pages show the same unnecessary
quantisation; all fit an exact 256-entry palette and now reconstruct byte-exactly:

| Face source | Page | Changed pixels before fix | Maximum channel error before fix |
|---|---:|---:|---:|
| Dante, demo 0 `pl100_02.ptx` | 0 | 97.50% | 23 |
| Vergil, demo 0 `pl101_02.ptx` | 0 | 96.13% | 19 |
| Lady, demo 123 `pl102_02.ptx` | 0 | 97.14% | 21 |
| Arkham, demo 1 `pl103_02.ptx` | 0 | 94.73% | 21 |
| Arkham, demo 1 `pl103_02.ptx` | 2 | 97.64% | 26 |
| Jester, demo 10 `pl104_02.ptx` | 0 | 90.20% | 22 |

Their shared 128x64, 16-colour page (page 1 for Dante/Vergil/Lady, page 3 for Arkham) formerly changed
86.65% of pixels, including alpha on 7,098 pixels; it also now reconstructs exactly. These measurements
compare the old quantiser to decoded local source data, independent of model geometry and lighting.

## Arkham's face, compared with the PS2

Checked against PCSX2 (Mission 1 on Normal from the 100% save; `analysis/ref/m1_open_ps2b.mp4`, local), demo 1's
cut cm027 (demo frames 4085-4191, the PS2 at about 207 s): `analysis/testroom/arkham_face_compare.png`. The palette
fix above wasn't the cause. Two things were:

- **The near plane.** Demos drew with a fixed 50 cm near plane, and the close-ups put the eye nearer: cm027's eye is
  41 cm from its target at fov 15, and cm026 (Dante's bowed head, 3840-4085) about 20 cm from his head. The face was
  clipped away and the inside of the back of the head showed (Arkham's "shelf" is his neck's rim from inside; Dante's
  face looked blank, seen from under his chin). The near plane now follows the eye's distance to its target, a third
  of it, 4-50 cm (`main.c` render_scene).
- **Each object's blend.** A MOD object record's byte 8 is its blend. The game's GS ALPHA table at `0x566ec0` (read by
  `FUN_00138b30`'s packets with a 4-bit index) holds 0x44 normal for 0 and 1, 0x48 additive (Cs·As + Cd) for 2 and
  0x42 subtractive (Cd − Cs·As) for 3; byte 8 is read as that index [inferred: the byte isn't traced to the packet].
  Mode 3 is only on Arkham's vein overlays (`pl103_00` object 2 on the crown, `pl103_02` object 2 on his face's left
  side, `pl103_32`: a black page with a blue vein decal, opaque); drawn opaque they were a black and blue patch
  over half his face and a dark cap on his head. Mode 2 is on the eye highlights (`pl100_02`, `pl101_02` object
  4 and the like). `prepare_skinned_model.build_mesh` keeps the byte (`mode`), `prepare_demo` packs it in an object's
  texture word (bits 16-19) and `demo.c` blends by it, without writing depth.

Still different from the PS2 there (not textures): the PS2 frames cm027 tighter (brow to lip; the port shows crown
to lip), so the cameras' fov is likely not vertical as the port takes it. The darker, warmer light and the faint veins
came with the Light track (below).

**The Light track** was the shared cause of the lighting differences (Arkham's face lit darker and warmer on the PS2,
the ending's Dante under blue moonlight). The port lit demos with the room's fixed key / fill (ambient 0x90), so
every face came out bright and neutral. What the game does:

- `FUN_00218fc0` parses a clip: ValidType (+0x121), IsMinus (+0x122), Type (+0x123), Life (+0x124), Src / Dst Pos,
  Force, Range and Rgb (u16 x3), MoveType. `FUN_002569c0` starts it: Force and Range kept as 1 / x² (`FUN_00133bd0`),
  a light record at clip +0x30; `FUN_0027fb10` runs it each frame (its life counts down by ClipScale; Type 1 eases
  Src to Dst over it) and `FUN_0027fc80` files it: ValidType bit 8 into the scene's one slot (+0x40), the rest into
  its light list (+0x14).
- `FUN_0027f230` lights a model (called from the model draws `FUN_001aa110` / `001aa360` / `001aa540`) with flags: a
  light whose ValidType, less bit 4, is 1 reaches models with flag 1 or 2, 2 those with flag 2 (the characters), 3
  those with flag 1 (the stage); bit 4 lights only models asking for it (flag 0x10); the slot's light comes first.
  `FUN_0027f610` makes each one: the vector to it, w 1 / Force²; its colour (negated if IsMinus), w 1 / Range²; and
  its rank, 1 − d² / Range² clamped at 0 (a 0 range: 1). `FUN_0027f7e0` keeps the three best ranked.
- The VU1 microcode that shades with them isn't read: the port takes a light's strength as 1 − d² / Force² (a 0
  force: whole) and its colour / 128 [inferred], at each actor's centre, as a PSP directional light toward it.

`prepare_demo.bake_lights` bakes the clips (80 bytes each, after the fades; their count at the header's +0x3c) and
`demo.c` demo_lights sets them per actor, with an ambient of 0x30 [stand-in, by eye]. Checked against PCSX2:
`analysis/testroom/arkham_face_lit_compare.png` (demo 1's cm027) and `ending_dante_lit_compare.png` (demo 59, from
the Mission 20 credits state, `analysis/ref/cast/credits_to_ending.mp4`).

**Blocky faces in captures are PPSSPP's setting, not the port.** `demo.c` (and every model draw) asks for bilinear
(`sceGuTexFilter(GU_LINEAR, GU_LINEAR)`) on full-size pages (no mips, no downscale: `pl103_02`'s face page is 256x128
as on the disc). The shared `ppsspp.ini` forces Nearest (`TextureFiltering = 2`), which overrides it in every
`ppsspp_scene.py` capture. `--filter auto` runs one scene with the game's own filtering (`--appendconfig`, the
shared ini untouched): `analysis/testroom/filt_d1_4130.png` beside `lit_d1_4130.png` (same frame, Nearest). A PSP
filters as asked, so it shows the smooth one.

**The rest of the cast** against the PS2 (PCSX2 from `m20_credits_start.p2s` / `m20_vergil_start.p2s`): Lady, the
credits' Prides and Dante in the ending street (`analysis/testroom/cast_credits_compare.png`) and the ending model in
demo 59 (black shirt under the coat, as the PS2) show no texture fault; Vergil (demo 57), the Jester (demo 10) and
Arkham's blob (demo 53) were looked at in the port only (no save state reaches those demos): their textures and
faces read right. Still different, not textures: the cameras' framing (fov), the stages' light (the PS2's ending street
is bluer), and a black slab across demo 53's frame 2300 (its stage).

**The blend on gameplay models.** The byte's low 4 bits are the table index (the table has 16 entries: 4 is 0xa1 with
FIX 0x80, Cd − Cs; 5-15 are on no character); bits 4 and 5 (0x10 / 0x20, values 16-19 and 48, nearly all on EFM
effect models) are flags not known yet, so 0x12 now adds and 0x13 subtracts where both were drawn normal before.
`Dmc3ModelObject` carries it (`blend`, 0 where a generator leaves it out) and `model_blend.h` sets it for the
enemies (`enemy_mesh` merges objects by texture and blend, the blended groups drawn last: one more draw for a kind
that has any), Arkham, Cerberus and the demos. Gameplay Dante (pl000) has none. Where it shows
(`analysis/testroom/blend_enemies_grid.png`, `blend_cerberus_pair.png`, `blend_arkham_pair.png`; left `noblend 1`,
right as now): Cerberus's ice (all of `[12]`-`[15]`, additive, now drawn after the heads), Arkham's blob (13
additive objects), the Abyss's vein layer (object 1, subtractive), the Blood-goyle's red layers, the Arachne's
abdomen, the Hells' eyes (object 16), the Fallen's spear tip, the Soul Eater (all additive). Not yet compared with
the PS2 for these (no save state reaches them); the Abyss's body is near white in the port with or without it.

The game's story scenes are **demos**. Event op `02 D` plays demo D and section `09 D` of the mission's event table
decides what follows (docs/EVENT_SCRIPTS.md). Some demos are pre-rendered movies (`.sfd` in MDATA1.AFS / MDATA2.AFS);
most are **real-time**: the engine plays models, motions and cameras from a timeline. None play in the port yet.
`tools/demo_unpack.py` unpacks one and parses its timelines (output under `build/demo/<id>`, local only).

## Where a demo is

- `FUN_0028ef00(loader, D)` queues demo D; `FUN_0028eac0(loader, kind, D)` loads its files by kind (3: the demo
  itself, 0: the linked file, 4: the language file); `FUN_002e4250` is the demo scene (it waits for the load, runs the
  `09 D` section after, and picks the next scene).
- **Record**: the table at `0x5a16d0` (D under 300) or `0x5a1830` (D from 300) points at six words: [0] the demo's
  file, [1] a linked id (-1 for none; read by `FUN_002e41b0`), [2]-[5] the four language files (`0x6c37d3` picks one).
  A file id is `index << 16 | archive`; archive 0 is GDATA.AFS (`0FLIST.DIR`: GData, MData1, MData2, SData).
- **58 real-time demos**: 0-3, 5-13, 15-17, 20, 22-37, 39, 41-53, 55-60, 114, 123, 220, 326, 335 (`--list`). Demo 5
  opens Mission 3 (the tower rising; its files say m03_s00) (GDATA 1042, 6305 frames, 20 camera cuts); 53 Vergil joining Dante against Arkham (GDATA
  1386, 4838 frames, 39 cuts).

## The package (a PAC of 13)

| Entry | What |
|---|---|
| [0] | The scene script (text, Shift-JIS comments). `# Load` and `# Set` sections of `Kind [slot] file` lines. |
| [1] / [2] | Names / PAC of the timeline files (`m03_s00_CameraSet.txt`, `..._Model01Set.txt`, ...). |
| [3] / [4] | Names / PNST of models, textures, stages and cloth: `pl101_00.mod`, `PL101_0x.ptx`, `st501_3.scm`, `em108_04.clt`, `.shw`. |
| [5] / [6] | Names / PAC of the MOT motions, one per slot per cut (`pl101_00_c001.mot`). |
| [7] / [8] | Names / PAC of hide tracks (`DispOn.hid`, `DispOff.hid`, per-cut `.hid`). |
| [9] / [10] | Names / PAC of cameras, one per cut (`cm_c001.cam`). |
| [11] / [12] | Shared data it takes from GDATA and the effect banks (`G 85`, `V 119`) / its PNST. |

**Tracks** (the `# Set` section): `WorkRate`, `Camera`, `Model nn`, `Motion nn`, `Hide nn` for up to 31 slots
(00-30), `Effect`, `EffectI nn`, `Light`, `PadVibe`, `Message`, `ScrEfc`, `Fade`, `Program`, `Sound`. Each timeline
file is a list of clips, `# Clip` ... `# End`, every clip with `SetFrame` (its start on the demo's clock), `SkipFrame`,
`CutFrame`, `ClipScale` (its time scale) and `Life` (its length at scale 1), then its own keys:

- Camera: `Camera` (a `.cam`). Model: `Model` (`.mod` / `.efm` / `.scm`), `Texture`, `Cloth`, `Rot`, `Pos`,
  `CnsPos track bone` (attached to another slot's bone: demo 5's slots 2 and 3 ride slot 1's bones 12 and 8),
  `UseCulling`, `CullingRange`. Motion: `Motion` (a `.mot`). The others' keys are in their parsers below.
- The parsers (by the strings they read): `FUN_002159c0` WorkRate, `FUN_00217ad0` (ChangeType, SrcValue, DistValue),
  `FUN_00217f30` Fade (FadeType, DistRGBA), `FUN_002188b0` PadVibe (LMotorSt..), `FUN_00218b60` (WaitTimer,
  SpTrackNo, SpBoneNo), `FUN_00218fc0` (ValidType, SrcForce, ...), `FUN_00219dc0` Model (CollShape, UseCulling, the OT
  levels), `FUN_0021aec0` (ResetClt, UVScroll, ClothMode, ...), `FUN_0021bc00` (FogColor), `FUN_0021c280` (ColChange,
  AlphaChange, HideKind, ShadowParamSet...); `FUN_00218200` / `00218430` / `002186e0` / `002197c0` the plain ones
  (Camera, Motion, Hide). Files are opened as `/demo/%s/%s` (`FUN_002165e0`, `FUN_00216bf0`).

## The files

- **Models** are the game's MOD (`tools/prepare_skinned_model.py` `build_mesh` reads `pl101_00.mod`: 1907 vertices),
  textures the same texture sets as pl000 [0] (`.ptx`), stages SCM (as `tools/prepare_stage.py`). Demo Dante
  (pl101) has 25 bones.
- **Motions** are MOT (`parse_mot` reads them: `pl101_00_c001.mot`, 268 frames, 25 bones).
- **Cameras** (`.cam`; class CCameraMotion, vtable `0x601d10`; played by CCameraClip, vtable `0x602010`, whose
  update `FUN_0024d010` evaluates it at the clip's frame). `FUN_002116b0(motion, cam)` reads: +4 `CAM\0`, +8 a mask of
  the keyed channels (bit k = channel k, 14 of them), +0xc loops, +0x10 / +0x14 start and end frame, +0x18 / +0x1c the
  loop's span, +0x20-+0x54 a base value per channel; each keyed channel is bound by `FUN_001fa410(track, n, cam)`
  (the n-th present) and evaluated by `FUN_001fae10(frame, track)`. `FUN_00211b60` then builds the camera: **eye** =
  channels 6-8 + channels 0-2, **target** = 9-11 + 0-2, **field of view** = channel 12 (degrees), +0x24 = channel 13.
  Demo 5's cut 1: mask 0x3fc0 (channels 6-13 keyed), 0-268, eye (-46, 30394, 7083), target (2576, 29283, 2424),
  58.5 degrees, channel 13 6.0. The keyed records from +0x58 aren't decoded yet (`FUN_001fa410` / `FUN_001fae10`;
  they look like MOT's curve records). `FUN_00211a10(step)` advances the frame (looping or holding at the end).
- **Hide tracks** (`.hid`): MOT-like. +4 `HID\0`, then +0x1c the model's object count n and n bytes, 1 for each object with a
  channel; at the header's end the channel count, then a record per such object as MOT's: kind 7, a base and scale
  (f32) and 4-byte keys {u16 frame, u16 q}, stepped; value = base + scale x q / 65535: -1 shown, 0 hidden. DispOn.hid
  keys all 100 objects at -1, DispOff.hid at 0; a cut's own (em108_01_c016.hid) keys one object (shown, then hidden
  from its frame 1261). The port bakes each slot's hide clips: DispOn / DispOff switch the slot, a cut's own .hid hides
  its objects one by one (keys stepped, on the .hid's own clock from its first frame, +0x0c); the demo meshes keep the
  MOD's objects apart (not merged by texture) for it.

## In the port

`tools/prepare_demo.py <iso> <id>` bakes DEMO_nnn.BIN (the camera per demo frame: each camera clip's `.cam` evaluated
at (frame - SetFrame) / ClipScale + SkipFrame from its start, MOT's Hermite with per-frame slopes, a key with bit 15
clear linear to the next) and DSTG_nnn.BIN (slot 00's `.scm` and texture set through `tools/prepare_stage.py --scm --ptx`,
its play area the cameras' reach). `psp/testroom/demo.c` plays it at 60 frames a second: the stage and the camera
(field of view as the port's vertical one [inferred]). Test line `demo n`. Checked in PPSSPP: demo 5's tower top, gate and
arena through its cuts. **The cast**: each model slot's MOD (slimmed as the enemies'), its texture set (8-bit, swizzled), its motion clips
(MOT, streamed: the BIN keeps all but the motions resident, 1.5 MB for demo 5, and reads a clip's MOT 20 frames before
it starts, freeing it after). Two things the demos showed: **a cutscene MOT stores a bone's translation records first**,
then rotation, then scale (bits 6-8, 3-5, 0-2; the hips' first records are world positions, a face bone's 0x1ff run
offsets, angles, 1.0s), the port's other motions are read as before; and **a CnsPos piece's root takes its parent
bone's place** (a head is modelled where the neck bone rests, a hand at its forearm bone's origin) [inferred from the
pieces' rests]. **Memory**: a room leaves under 2 MB of heap, so the stage buffer is taken once, at the first load, sized for the largest
room (5400 KB; ROOM_100.BIN is 5.27 MB), and every later stage is read into it (freed and taken again, a room's few MB
didn't always come back in one piece). A demo's stage goes in the room's place and its resident data and skinned
vertices in the buffer's unused tail (`stage_spare`), the heap only past it; `prepare_demo` coarsens a demo's own
stage (`--max-edge`) until the three fit. After the demo the mission starts afresh and the room is read back.

**Hide tracks**: a slot's DispOn / DispOff clips switch it on and off at their SetFrame (a cut's own .hid, keyed per
object, counts as on). **Stages from rooms**: a demo whose stage isn't in its package (st232: room 232) uses the
room's BIN.

**In the missions**: each mission's opening demos, from its event script's first op and the AFTER_DEMO sections
it chains through (`tools/event_dump.py`): 3: 5, 123; 4: 9; 7: 16; 10: 25; 11: 27; 12: 31; 13: 36; 14: 39 (after movie
327); 15: 41; 16: 43; 17: 46; 18: 49; 19: 51; 20: 55 (`mission_opening_demos`, played from the menu's mission start;
START skips). Movies are ids 318 on, MDATA2's rows in order (318 m99_s00 ... 326 m14_c00, 327 m14_s00, 328 m19_c00,
329 m01_v01); by that order (and their pictures: the tower rising; Lady) the port's M3_INTRO_A / B (MDATA2 2, 3) are Mission 2's clear and Mission 6's opening, and play there

**Stage pieces**: a slot's `.efm` (an effect model: the MOD layout) is cast as a MOD; a `.scm` piece (not the stage, stNNN)
as a rigid rig: a bone per node record (parent, local translation and rotation), its objects' vertices in their node's
space on that bone, posed parents first (SCM parents may follow children) with the nodes' rest rotations where its
motion doesn't key them. A piece whose textures are the room's (not in the package), or over 4000 vertices (a whole sub-stage), is left
out. Only the textures a model's objects use are baked (a piece carries the stage's whole set). The cast draws blended (pieces
carry smoke and glow textures).

**Effects**: a demo's own effects are an effect pack ([11] names, [12] body: tools/native_fx.py's layout); the bake keeps
the V / E / G closure of its Effect tracks' V, E and G clips (records, textures, models) as a bank nfx loads at run time
(NFX_DEMO), and the clips {start, V / E, id, place, scale, the slot and bone it rides (SpTrackNo counts the Set section's
lines: slot (n - 2) / 3; SpFlag -1: none)}. demo.c starts them on their frame and moves those on a bone. Of 1510 clips, 590
are V, 275 E and 533 G (generators: emitters, NATIVE_FX.md, played since the rebake that added them; demo 11's
summoning glow is one). 112 P clips (particle programs) aren't played, and timelines that only reach P show nothing.

**Speed**: the demo clock is the PSP's real time, 60 frames a second, whatever the game's speed (Turbo) or a test's step.

**Sound**: a demo's Sound clip names an ADX stream that is the demo's own id, and its file is SDATA.AFS's `<package>.adx` (m03_s00.adx for demo 5): voices and music mixed. prepare_demo decodes it (ffmpeg) to the movies' IMA ADPCM, DAUD_nnn.BIN (`DAU1`, rate, sample pairs, start frame, a byte per stereo pair), and demo.c streams it from the memory stick on its own thread from the clip's frame; both it and the demo clock run on real time. Checked offline: decoded against ffmpeg's own, -42 dB error. 51 tracks, 232 MB.

Not yet: cloth, P effects.

**Clears**: each mission's clear demos (the AFTER_DEMO sections ending in mission_clear and the demos chained into them):
3: 8; 4: 10; 6: 15; 8: 20; 9: 24; 10: 26; 11: 30; 12: 35; 15: 42; 18: 50; 20: 59, 60 (`mission_clear_demos`), played when
the mission clears, then its clear screen. 2's 114 and 14's 326 are one scene whose city stage only fits unsubdivided (its 10,735 triangles; subdivided they
were 128,000): `prepare_stage --max-edge 100000` now means no subdivision, and `--view` drops triangles none of the
demo's cameras sees (prepare_demo writes every 8th frame's camera). A rig whose bones aren't in parent order (em134_00) is left out of a demo. Test
line `forceclear n`.

**Bosses**: the event scripts' ROOM blocks play a boss's entrance when counter 0 is still 0 and Dante is in an area of
the room, and its defeat when the counter is at its step and the room's enemies are cleared; AFTER_DEMO then puts
Dante at a start of the room. The port (`room_demos`): 3: 6 / 7 (Cerberus), 5: 12 / 13, 9: 22 / 23 (Nevan), 11: 28 / 29
(Beowulf), 12: 32 / 34 (Geryon; his 33, the bridge falling, where mission12.c drops it at 80%), 16: 44 (Lady), on first entering the room [the area isn't checked] and once its fight
is over; 19's 53 (Vergil joins) when Arkham's second round begins. Afterwards the room's stage is read back and Dante
put at the start; nothing restarts. Between rooms (`transit_demos`): 7's 17 (142 to 144), 17's 47 (143 to 139), 19's 52 (408 to 421), 20's 57 / 58 (Vergil
beaten in 411, to 012), played as the port's own flow brings Dante into the next room; 17's 48 on the Doppelganger's
defeat; 16's 45 before its clear (the port clears at Lady); 20's 56 after its 55. 2 opens with 3 (after its start screen; the shop fight). 0 and 1 (the prologue's narration, Dante's shop) are in no mission's script, and 220 opens Mission 1 for Vergil (the port's Mission 1 plays movies 318 / 319 for Dante; 318, m99_s00, is the port's boot movie). 13's 37 plays at 232's door 1 into Vergil's arena (the script's door_retarget 1, 1, 37, 0; its AFTER_DEMO, filed after 233, goes to 234 start 0).

**Subtitles**: the Message track's clips each name a message (`No`); the text is in the record's language files (words
2-5: Japanese, English, then two more), a tagged script: `# Msg n` blocks of quoted text with `<PS x y>` (place on the PS2's
640 x 448), `<SZ size>`, `<IT frames>` (per character), `<WT frames>` (wait), `<BR>`, `<FO frames>` (fade out), `<JP n>`,
`<NE>` (the file's own header explains them). The port bakes the English lines, each shown from its clip's frame for the
sum of its waits, centred at its height (DMO3 header: subtitles, count). A clip needn't end with `# End`; the next
`# Clip` closes it.

**Fades**: the Fade track's clips (`FadeType` I / O, `DistRGBA` with 128 opaque, `Life`): the last begun covers the screen,
fading from its colour to clear (I) or to its colour and staying (O); baked into DMO3 +0x28 (fades, count) and drawn over the
scene (`demo_fade`, `fx_screen_tint`).

**Movies**: the story movies play where the event scripts put them: openings 1 (319, M1_INTRO), 6 (321), 8 (323),
9 (324), 14 (327, then demo 39); clears 2 (320), 7 (322), 13 (325), 19 (328) (`tools/prepare_movies.py` D3nn,
`psp/testroom/main.c` play_intro and the clear).

## Porting plan

1. Bake each demo to a streamed `DEMO_nnn.BIN` on the memory stick (the EBOOT is 27 MB already; demo 5 is 8 MB raw):
   its models and textures through the existing converters, its motions as MOT, its cameras and timelines.
2. A player in the test room: run the clip timelines on the demo clock (SetFrame, Life x ClipScale), pose each slot's
   model by its motion clip (CnsPos slots on their parent's bone), drive the camera from the `.cam` of the current
   cut, and hide what the hide tracks hide. Hand control back by the `09 D` section as the game does.
3. Decode `.cam` (find its reader from `FUN_002186e0`'s set-up) and `.hid`; then messages (subtitles), fades and
   sounds.
