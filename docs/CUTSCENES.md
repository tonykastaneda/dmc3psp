# Real-time cutscenes ("demos")

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
  from its frame 1261). The port takes DispOn / DispOff per slot; per-object channels need the mesh's objects kept apart
  (the bake merges them by texture) [not yet].

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
329 m01_v01); by that order the port's M3_INTRO_A / B (MDATA2 2, 3) are Mission 2's clear and Mission 6's opening
[to check].
Not yet: per-object hide channels, cloth, a second stage piece (demo 5's slot 12 .scm), effects, messages, fades,
sound, and the demos inside missions (boss entrances and defeats).

**Clears**: each mission's clear demos (the AFTER_DEMO sections ending in mission_clear and the demos chained into them):
3: 8; 4: 10; 6: 15; 8: 20; 9: 24; 10: 26; 11: 30; 12: 35; 15: 42; 18: 50; 20: 59, 60 (`mission_clear_demos`), played when
the mission clears, then its clear screen. Not yet: 2's 114 and 14's 326, one scene whose city stage (128,000 triangles,
11 MB) neither fits nor draws at speed. A rig whose bones aren't in parent order (em134_00) is left out of a demo. Test
line `forceclear n`.

**Bosses**: the event scripts' ROOM blocks play a boss's entrance when counter 0 is still 0 and Dante is in an area of
the room, and its defeat when the counter is at its step and the room's enemies are cleared; AFTER_DEMO then puts
Dante at a start of the room. The port (`room_demos`): 3: 6 / 7 (Cerberus), 5: 12 / 13, 9: 22 / 23 (Nevan), 11: 28 / 29
(Beowulf), 12: 32 / 33 (Geryon), 16: 44 (Lady), on first entering the room [the area isn't checked] and once its fight
is over; 19's 53 (Vergil joins) when Arkham's second round begins. Afterwards the room's stage is read back and Dante
put at the start; nothing restarts. Not yet: those that go on to another room (7's 17, 13's 37, 16's 45, 17's 47 / 48,
19's 52, 20's 56-58) and 12's 34 (Geryon's second).

## Porting plan

1. Bake each demo to a streamed `DEMO_nnn.BIN` on the memory stick (the EBOOT is 27 MB already; demo 5 is 8 MB raw):
   its models and textures through the existing converters, its motions as MOT, its cameras and timelines.
2. A player in the test room: run the clip timelines on the demo clock (SetFrame, Life x ClipScale), pose each slot's
   model by its motion clip (CnsPos slots on their parent's bone), drive the camera from the `.cam` of the current
   cut, and hide what the hide tracks hide. Hand control back by the `09 D` section as the game does.
3. Decode `.cam` (find its reader from `FUN_002186e0`'s set-up) and `.hid`; then messages (subtitles), fades and
   sounds.
