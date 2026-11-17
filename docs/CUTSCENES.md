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
  is Mission 1's opening (GDATA 1042, 6305 frames, 20 camera cuts); 53 Vergil joining Dante against Arkham (GDATA
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
- **Cameras** (`.cam`, 416 bytes for a cut): a 0x60-byte lead, `CAM\0`, the cut's length twice (268), then a base
  value for each of its channels (-45.9; 30394, 7083, 2576; 29283, 2424, 58.5: [inferred] a roll or field of view, the
  eye and the target), then keyed channels that look like MOT's curve records. Not decoded yet.
- **Hide tracks** (`.hid`): `HID\0` after a 0x90 lead, then 1.0 / 10.0 pairs and a run of per-frame bytes [not decoded].

## Porting plan

1. Bake each demo to a streamed `DEMO_nnn.BIN` on the memory stick (the EBOOT is 27 MB already; demo 5 is 8 MB raw):
   its models and textures through the existing converters, its motions as MOT, its cameras and timelines.
2. A player in the test room: run the clip timelines on the demo clock (SetFrame, Life x ClipScale), pose each slot's
   model by its motion clip (CnsPos slots on their parent's bone), drive the camera from the `.cam` of the current
   cut, and hide what the hide tracks hide. Hand control back by the `09 D` section as the game does.
3. Decode `.cam` (find its reader from `FUN_002186e0`'s set-up) and `.hid`; then messages (subtitles), fades and
   sounds.
