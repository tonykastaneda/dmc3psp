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

The live camera's channel 12 is a vertical field of view (`FUN_001ac750` builds the projection with
m11 = 1/tan of half that angle) and channel 13 is roll in radians. cm027's tighter crop is the pre-rendered movie
(below), not a different fov. The darker, warmer light and the faint veins came with the Light track (below).

**The Light track** was the shared cause of the lighting differences (Arkham's face lit darker and warmer on the PS2,
the ending's Dante under blue moonlight). The port lit demos with the room's fixed key / fill (ambient 0x90), so
every face came out bright and neutral. What the game does:

- `FUN_00218fc0` parses a clip: ValidType (+0x121), IsMinus (+0x122), Type (+0x123), Life (+0x124), Src / Dst Pos,
  Force, Range and Rgb (u16 x3), MoveType. `FUN_002569c0` starts it: Force and Range kept as 1 / x² (`FUN_00133bd0`),
  a light record at clip +0x30; `FUN_0027fb10` runs it each frame (its life counts down by ClipScale; Type 1 eases
  Src to Dst over it) and `FUN_0027fc80` files it: ValidType bit 8 into the scene's one slot (+0x40), the rest into
  its light list (+0x14).
- `FUN_0027f230` lights a model (called from the model draws `FUN_001aa110` / `001aa360` / `001aa540`) with flags: a
  light whose ValidType, less bit 4, is 1 reaches models with flag 1 or 2, 2 those with flag 2, 3 those with flag 1.
  Flag 2 is the default, and the stage keeps it (below). Flag 1 is a model whose clip sets Light 2. Bit 4 of a light
  reaches only a model asking for it (flag 0x10). The slot's light comes first.
  `FUN_0027f610` makes each one: the vector to it, w 1 / Force²; its colour (negated if IsMinus), w 1 / Range²; and
  its rank, 1 − d² / Range² clamped at 0 (a 0 range: 1). `FUN_0027f7e0` keeps the three best ranked.
- The VU1 microcode that shades with them isn't read: the port takes a light's strength as 1 − d² / Force² (a 0
  force: whole) and its colour / 128 [inferred], at each actor's centre, as a PSP directional light toward it.

**The stage keeps flag 2, and its picture is the baked vertex colour.** A CDraw is constructed with light flag 2
(`FUN_00281010` stores 2 at +1476). The model clip's Light key (`FUN_00219dc0`, the string at 0x618488, a short at
clip +1762) is the only writer, applied in the model update at 0x2366cc: 0 leaves the flag, 1 sets 2, 2 sets 1.
`FUN_001aa110` ORs 0xC into that flag before `FUN_0027f230`, so both light lists are walked and the class-3 bit is
still not set. Class 3 reaches only a model whose clip sets Light 2. No Model00 clip in the unpacked demos sets
Light. Demos 3, 5 and 59 set no Light key on any slot, so the stage takes the same class-1 and class-2 lights as the
cast, and their ValidType 3 clips hit nothing. Other demos set Light 2 on later slots only (15, 41, 42, 49, 55, 60);
those models would drop class 2 and take class 3. ColChange (clip +91, `FUN_0021c280`) is unused in 3, 5 and 59.

The port's stage mesh is that SCM colour (0x80 = 1) times the stage gain, with lighting off. The class-1 and class-2
lights already fall on the cast. Multiplying the same colours onto the bake is the formula the stills reject: demo 5
frame 300's two full-strength class-2 lights are (120, 100, 70) and (64, 64, 32), and the stage region already
matches without them. Against the PS2 halves of cl2's compare sheets (current shots, letterbox interior):

- Demo 5. Frame 300, (10, 50, 160, 200): PS2 [43.8, 26.0, 14.1], PSP [47.7, 28.9, 16.2] (`fog5_300.png`). Frame
  1380, (280, 45, 460, 150): [64.3, 38.6, 21.0] against [63.2, 37.9, 23.2]. Frame 180's horizon is the unfogged sky.
- Demo 59 frame 700, ground (5, 60, 140, 210) and (340, 60, 470, 210): PS2 [24.2, 28.6, 38.7] and [29.4, 35.1, 49.6],
  PSP [27.1, 30.8, 44.0] and [32.1, 36.8, 53.3] (`fog59_700.png`). Same hue, a few levels brighter. st506's vertex
  colours are grey, so the blue is the texture. A blue ValidType 6 on that frame would push it the wrong way. Frame
  3700 is a hair close-up; the PS2 is brighter there, which is the skipped glow, not a stage class.
- Demo 3's sheet is frames 360, 540 and 1980 (cl2's `fin3_360.png`, `fin3_540.png` and `fin3_1980.png` match the
  sheet's right halves). Frame 360's open region (40, 70, 160, 150): PS2 [25.3, 18.0, 13.1], PSP [24.4, 18.5, 14.1]
  (`stlight3b_360.png`). The right of that frame is the room-prop cover, not a darker light. Frame 540's two halves
  are different pictures, so it is not a colour reading. Frame 1980 is brighter on the PSP and its two regions do not
  move by one scale.

The stage stays the bake. The VU program that shades a stage with the flag-2 lights is still unread.

`prepare_demo.bake_lights` bakes the clips (80 bytes each, after the fades; their count is the low 16 bits of the header's +0x3c, and the high 16 bits are the GFG fog clips) and
`demo.c` demo_lights sets them per actor, with an ambient of 0x30 [stand-in, by eye]. Checked against PCSX2:
`analysis/testroom/arkham_face_lit_compare.png` (demo 1's cm027) and `ending_dante_lit_compare.png` (demo 59, from
the Mission 20 credits state, `analysis/ref/cast/credits_to_ending.mp4`).

**Blocky faces in captures are PPSSPP's setting, not the port.** `demo.c` (and every model draw) asks for bilinear
(`sceGuTexFilter(GU_LINEAR, GU_LINEAR)`) on full-size pages (no mips, no downscale: `pl103_02`'s face page is 256x128
as on the disc). The shared `ppsspp.ini` forces Nearest (`TextureFiltering = 2`), which overrides it in every
`ppsspp_scene.py` capture. `--filter auto` runs one scene with the game's own filtering (`--appendconfig`, the
shared ini untouched): `analysis/testroom/filt_d1_4130.png` beside `lit_d1_4130.png` (same frame, Nearest). A PSP
filters as asked, so it shows the smooth one.

**Arkham's cheek marks (cm027) are the right page, too strong.** The reddish streaks on his left cheek (the viewer's
right) are `pl103_02` object 2: a shell over the left half of his face (0.03-0.2 cm outside the skin), texture 1 (a
4-bit page of blue-grey veins on black, one CLUT, alpha 0x80), blend 3 (subtractive). There is one variant: the
package has one `pl103_02.ptx`, its pages one CLUT each, and no hide clip touches slot 12 during cm027; `TexMode` /
`ClutMode` (Model clip +0x620 / +0x621, `FUN_00219dc0`) are load settings passed to the model object (`FUN_002342b0`,
vtable +0x24 / +0x28), not a variant pick. The PS2 draws it too: its frame at 208 s shows faint, neutral dark streaks
under that eye in the same place (`analysis/ref/cast/` and `arkham_face_filter_compare.png`). The port's are stronger
and hue-shifted (bluish lines subtracted from warm skin leave red-brown), so the subtracted source on the PS2 is
weaker or more neutral (its vertex colour or alpha for this object). Not settled: the Program track's No 5 / No 6
(210 200 180 / 30 20 20 in demo 1) look like a colour scale and an ambient, but its parser is the stub `FUN_00217440`
(returns 1), so the game ignores them [inferred from the parser table at 0x590748].
- **Why the PS2 differs: its cm027 is a movie.** GS dumps of that frame (PCSX2, Mission 1 on Normal from the 100% save,
  taken at 184.7 s by matching the picture, and the Demo Digest's Opening) hold no model draws at all: each frame is
  one 640x512 IMAGE upload. Demo 1 is in no mission's script; Mission 1 opens with movie 319 (M1_INTRO), Capcom's
  pre-rendered take of the same scene, and the port plays that movie there too. So the PS2 never draws demo 1 in real
  time, and every PS2 picture of it (`arkham_face_compare.png` and the rest) is the movie. The movie's faint marks
  are its own render, not a sign that the real-time data is wrong. A fair real-time check of his face is a demo the
  game plays live (demo 5, Mission 3's opening).
- **Live, the PS2 draws the marks.** Demo 5 (Mission 3's opening, Normal, recorded from PCSX2 at
  `analysis/ref/cast/m3_open_ps2.mp4`) shows Arkham's red marks on his neck and left cheek, so the live engine draws
  his overlays (`analysis/testroom/arkham_demo5_compare.png`, the PS2 left, the port right, frames 1260 / 1380). Still
  different there, not the face's pages: demo 5's sky is orange on the PS2 and dark blue in the port (its stage and
  ScrEfc), and the port lights his face greyer.
- To read the PS2's GS state for a draw: a GS dump of the frame (`build/refemu` hotkey Shift+F8, sent as VK 0xA0 +
  F8; uncompressed with `[EmuCore/GS] GSDumpCompression = 0` in the game's own settings file). The Demo Digest's
  "Opening" (mission select, DEMO DIGEST, scene select right once) is a pre-rendered movie of demo 1 (its dump is
  full-frame IMAGE uploads, no draws), so the dump has to come from Mission 1 itself.

**Skies, letterbox, framing and the characters' colour** (checked against PCSX2 in Mission 3's opening, demo 5;
Mission 2's opening, demo 3; the ending, demo 59: `analysis/testroom/demo5_sky_compare.png`, `demo3_compare.png`,
`demo59_compare.png`, recordings in `analysis/ref/cast/`):
- **Sky.** A demo stage's sky and horizon are its objects far wider than any play area (over 400 m; st501_3's dome
  is 370 km across and reaches y 74201, with its far ground, cloud layer and sun glow). The cutscene crops (`--view`,
  `--crop`) dropped them, so demo 5 showed a flat blue clear colour. `prepare_stage` now keeps those objects whole
  and flags them sky (drawn first, without depth, farthest first, one chunk each), and `stage.c` draws the sky pass with
  its far plane at 500000 cm (it was 80000; depth is off for it, so this costs no precision).
- **The SCM object's blend.** An SCM object record's byte 8 is its blend, as a MOD object's (`scm_ps2` reads it;
  `prepare_stage` flags the batch 16 additive / 32 subtractive; `stage.c` blends with `model_blend.h`): st501_3's
  sun glow (texture 8, additive) is the orange blaze on the PS2's horizon.
- **Exact stage palettes.** Stage pages of 256 colours or fewer now keep them byte for byte (as the models' do since
  the palette fix); FASTOCTREE had posterised the smooth sky into blotches. Every room's and demo's stage is rebaked.
- **Letterbox.** The PS2 draws demos with cinema bars, 0.712 of the height showing (measured in PCSX2 in demos 3, 5
  and 59): `demo_letterbox`, 39 lines top and bottom.
- **Framing.** Channel 12 is the vertical angle (`FUN_001ac750`: the Y scale is 1/tan of half that angle, and the
  screen-height halfword is in the focal length and again in the Y divisor, so the height cancels). Drawn raw across
  the PSP's 272 lines that angle frames wider than the PS2 stills, so a cutscene uses the 326-line fit
  (`demo_screen_fov`, `2*atan(tan(fov/2)*272/326)`) [stand-in, fitted to the PCSX2 stills]. The picture stays the
  full 16:9 width, so a demo shows more to either side than a 4:3 television and there are no side bars. Test line
  `rawfov 1` passes channel 12 raw. Evidence, against the PS2 halves of `demo5_sky_compare.png`, `demo3_compare.png`
  and `demo59_compare.png`: raw demo 5 frame 1260 (`sky5_1260.png`) brings the hooded figure fully in, where the
  still holds it at the right edge, and `fit5_1260.png` matches the still; raw frame 300 (`sky5_300.png`) shows a
  figure at the right edge the still crops, and `fit5_300.png` matches; raw demo 59 frame 700 (`sky59_700.png`) has
  Dante smaller, and `fit59_700.png` matches. With the fit active, `fov5_1260.png` matches `fit5_1260.png` and
  `fov59_700.png` matches `fit59_700.png`. Demos 3, 5, 6, 16, 44 and 53 were checked at wide frames; the extra
  width is more of the same stage, so none is cropped back to 4:3.
- **The characters' colour.** The Program track's No 5 (a colour scale, 128 = 1.0: demo 1 210 200 180, demo 47 15 20
  25) and No 6 (their ambient: 30 20 20 in demos 1-5) [inferred from their values] scale the demo's lights and set
  the ambient (`demo_program_colour`), replacing the by-eye 0x30 ambient where a demo sets them. Arkham's face in demo
  5 is now warm like the PS2's. The world's ValidType 3 lights stay off characters: tried, they darkened him.
- **ScrEfc fog (GFG).** A clip is a depth ramp: MinAlpha/128 at StartZ, MaxAlpha/128 at EndZ (128 = 1, as a fade's
  alpha), colour the clip's MaxColor (ABGR). Demo 5 frames 0-1501 (so 180, 300 and 1260) are Type 1, StartZ 7000,
  EndZ 9000, MinAlpha 0, MaxAlpha 20, MaxColor R87 G47 B22; frames 1501-5395 are 9000-11000, MaxAlpha 20, R130 G80
  B40. Demo 59 has no GFG. `demo_fog` calls sceGuFog. Hardware fog is full colour at its far, so that far is where
  the same line would hit 1 (the first clip: near 7000, far 19800) [inferred: the draw that would clamp past EndZ
  was not the function read, so past EndZ the haze keeps thickening until that far]. The sky pass turns fog off;
  the dome sits far beyond it and would become a flat colour. The credits fight's fog is unchanged and wins if
  both would apply. A bin from before this has fog count 0 (the high half of +0x3c) until `prepare_demo` is run
  again; that does not rebake `DSTG`. Checked on demo 5 (`fog5_180.png`, `fog5_300.png`, `fog5_1260.png`,
  `fog5_1380.png`) and demo 59 (`fog59_700.png`, `fog59_3700.png`) against cl2's PS2 stills. The display-list build
  matched a `nofog 1` run of demo 5 within 2 us (about 4.6 ms on the close shot, 6.4-7.2 ms on the wider ones).
  Frame 300's distant left moved toward that brown; frame 1260 sits inside StartZ, so it stays sharp where the PS2
  still is the FCS blur. Demo 59 frame 700 has no fog clip and stays on the previous picture.
- **Not ported: ScrEfc blur and glow.** FCS is a focus band (Distance, OutStartZ/OutEndZ, InStartZ/InEndZ, Color,
  Power, Range). Demo 5 frame 180 is the opening clip, Power 0. Frame 300 is Power 1, Distance 800, out of focus
  from 500 to 1000. Frame 1260 is Power 2, Distance 260, out of focus from 300 to 800. DOF is StartZ, EndZ, Color,
  Power: demo 59 only at 2915 (0-300, Power 2) and 3309 (800-1100, Power 3), so frame 700 has none, and its FCS
  ends at frame 300. SGA is a feedback bloom (Color, Alpha, Scale, DirNum, DirOffset, Feedback, Attenuate, and an
  8x16 kernel), not a tint. Demo 5 has it only on frames 0-271 (frame 180: grey, Alpha 48, Scale 16, DirNum 4,
  Feedback 4, Attenuate 0.85); frames 300 and 1260 have none. Demo 59 has it the whole cut (frame 700: R140 G132
  B128, Alpha 64, Scale 8, DirNum 4, DirOffset 23, Feedback 5, Attenuate 0.9) plus a BGA of the same family
  (Alpha 38, Feedback 4). Nothing in the renderer samples the frame at a reduced size, and a blur of the 480x272
  frame is the pass that does not fit under about 1.5 ms, so neither glow nor blur is drawn. ATF is not part of this.
- Demo 3's over-the-shoulder shot (frame 1260) was the near plane cutting Dante; the cap is 25 cm, below.

**Demo 3's over-the-shoulder "holes" were the near plane, not culled triangles.** Frame 1240 puts the eye 30-40 cm
in front of Dante's chest while it looks at enemies metres away. With the near plane at a third of the eye-to-target
distance capped at 50 cm, his whole front was cut away and the port showed the inside of his back through the
gaps (the "holes"). The PS2 shows his chest and mouth there (`analysis/testroom/near_plane_demo3.png`). The cap is now
25 cm (`main.c` render_scene); wide shots show no depth fighting (demo 5) and frame times are unchanged.
Frame 360 of the same demo is the same gap seen from behind: the PS2 shows Dante's back and coat, the 50 cm shots
(`fr3_360.png`, and cl2's `fin3_360.png` which used the 326-line fit) show the street through where he stands, and
the 25 cm cap draws a dark surface across the right of the frame (`n3_360_360.png`).
- At 25 cm a dark slab with a light edge (likely Rebellion, `wp100`) sits in front of the eye on the right of that
  frame; the PS2 shows the street there. At 40 cm the slab goes but his chest is cut again, so the PS2's near plane
  alone doesn't explain it: the weapon's place in that cut is still to check.
- `near_clip.c` clips near-crossing triangles (per-model sphere test, each vertex's depth cached once, only the
  crossing triangles redrawn clipped). In PPSSPP the GE already draws them clipped, so it changed nothing visibly and
  cost about 2 ms a frame with the camera against Dante (7.1 against 5.2 ms of draw time). It is off by default
  (test line `nearclip 1`) until real hardware shows whether its GE drops such triangles.

**The "Rebellion slab" in demo 3 was the room's props and the GE's near clipping, not a weapon.** Rebellion
(`wp100`, slot 9) sits where its motion puts it: hiding it, and every other actor slot, left the slab in place.
Shots taken by the cutscene's own frame (`demoshot N`) and slot-by-slot hiding (`demohide N`) found three causes:
- **Room props on the pieces' hide tracks.** A cutscene staged in a game room (`st001` is room 001) copies the room's
  `ROOM_nnn.BIN`, which holds the room's placed STAY props. The cutscene's own pieces textured from the room
  (`ss001_00/01`) are left out of the cast, and those props stand in for them, but always shown: `ss001_01` (the
  arch's building, around the camera of frames 2466-2964) is hidden until 2964 on the PS2. Stage batches now carry
  their SCM object (index + 1) or prop (256 + its index) in the batch's 8th word; `prepare_demo.piece_prop_events`
  matches each left-out piece to the props inside its bounds and gives them its H / D clips (demo 3: prop 2), so the
  PS2's view of Dante by the bus (frame 2700) and the arch behind him at 3300 both come out.
- **Near-plane clipping.** With `GU_CLIP_PLANES` on, PPSSPP's GE clips the room's walls at the near plane into a slab
  in front of the lens; the PS2 drops such triangles. The cutscene stage pass now runs with clip planes off, so the
  GE drops them itself, at no cost (`nearcull N`: 2 default, 1 a CPU walk at about 1.4 ms, 0 the old clipping).
- **Hide clips without files.** A Hide clip's `HideKind` H hides the slot and D shows it with no `.hid` named; they
  were skipped, so pieces meant to stay hidden (`ss001_01` until 2964, the coats in slots 32 / 33) showed.
  `prepare_demo` keeps them, and the stage's own hide track (its slot's `.hid` files: demo 3 hides `st001` objects
  3 and 23-26 from 2282) is applied by object (`stage_hide_events`, `demo.c` stage_hide_apply).
- `gen_rooms.py` built a room's stage only when its bin was missing, so rooms built on 2026-11-04 had missed the
  exact palettes and these tags; it now rebuilds a room when `prepare_stage.py` / `scm_ps2.py` are newer (about 8
  minutes once).

- **Motion clips held at their end.** A slot whose clip has ended and whose next hasn't begun dropped to its rest pose
  at the world origin. The game keeps the last frame: demo 59's sword (`wp122`, one 154-frame clip) stays leaning on
  the desk. `demo.c` demo_clip_now holds the last clip begun (and keeps its motion loaded).
- Test lines: `demoshot N` (a still at the cutscene's own frame N, `name_dN.png`), `demohide N` (slot N not drawn;
  -2 no effects, -3 no actors).

- **A per-object `.hid` clip lasts its Life** [inferred]: 203 of 208 such clips keep their keys within an authored
  Life; the 5 others all have Life 10, the placeholder for "until the next clip" (33 clips), with keys well past it.
  So a clip with a Life over 10 ends there (shown again) unless another clip of that slot begins first. Demo 59's
  `ss506_00_c01_1.hid` (Life 300) no longer hides its object for the whole cutscene.
- **Comparing by time drifts.** The PCSX2 recordings (10 fps) drift from the demo clock by up to about 2 s over a
  cutscene, so a still at a predicted time can be another moment (demo 3's coat cut, frame 1684, looked like a
  different camera: it isn't). Comparison sheets pick the PS2 frame that looks most like the port's within +-3 s of
  the predicted time: `demo3_weapons_compare.png`, `demo5_demoshot_compare.png` (4 of 4 match),
  `demo59_demoshot_compare.png` (3 of 4; at frame 2300 the PS2 shows the photo on the table and no Dante, the port
  his legs and no photo; at 3580 the object in his hand differs: open).

- **A `.hid` keys the model's own objects, not its draws.** A MOD / SCM object with several sub-meshes is several
  draws; the channels were applied by draw index, so past the first multi-sub-mesh object they hid the wrong piece
  (demo 59's `ss506_00`: 9 objects, 11 draws, its object 6, a neon-sign quad, hid a door). Each draw object now
  carries its source object (`source`) and the clip's channel table is laid out per draw from it.
- **SCM pieces' blends.** `scm_rig` (a cutscene's animated SCM piece) now reads each object's byte 8 like a MOD's:
  demo 59's "Devil May Cry" neon sign (`ss506_00` objects 6-8, additive) glows at frame 3800 instead of dark quads.
- **Demo 59 rechecked:** the phone in Dante's hand (3580) matches the PS2 at 3590-3610, where the PS2 frame really
  is (the earlier sheet's match landed on a nearby moment of a fast spin; the motion's rotation keys are a clean
  per-frame spin). The photo at 2300 is in the stage (`st506` object 3, texture 15, on the board at the left) and
  in place: the port's tighter framing leaves it just outside the frame (with `rawfov 1` its corner shows at the
  left edge; the PS2 shows it whole). That is the cutscene fov fit (`demo_screen_fov`), not a missing object.

Weapon placement checked against PCSX2 (`analysis/testroom/demo3_weapons_compare.png`, `demo59_weapons_compare.png`):
Rebellion in demo 3 (overhead, thrown, pointed at the end) matches; demo 59's sword was missing from the desk (now
held there). Still open in demo 59: the book (in `ss506_00`, slot 8) is missing from the desk at frame 3040 and open in
Dante's hand at 3580 where the PS2's is closed: how long a per-object `.hid` clip's keys last after its Life isn't
known (the port keeps the last clip begun).

**The rest of the cast** against the PS2 (PCSX2 from `m20_credits_start.p2s` / `m20_vergil_start.p2s`): Lady, the
credits' Prides and Dante in the ending street (`analysis/testroom/cast_credits_compare.png`) and the ending model in
demo 59 (black shirt under the coat, as the PS2) show no texture fault; Vergil (demo 57), the Jester (demo 10) and
Arkham's blob (demo 53) were looked at in the port only (no save state reaches those demos): their textures and
faces read right. The ending street's colour is that bake: demo 59 frame 700 is the same hue as the PS2, a few
levels brighter, not a missing blue light.

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

## Cloth

A demo's cloth is the gameplay chain (`CCnsChain`, one bone stepped by `FUN_0022f270`). The `.clt` names the free
bones and the spring values; the physics runs every frame. The motion does not bake the swing. Wind in these scripts
is 0, and the files carry no capsule list (gameplay coats keep theirs in code).

**Which script.** `FUN_00219dc0` (`0x219dc0`) reads `Cloth` on a model clip. The usual path keeps the model PAC's
`.clt` (`[3]` / `[4]`) at the clip +`0x34`; a disk path is the other branch at `0x21ab2c`. `FUN_002342b0`
(`0x2342b0`) loads it onto the model once: `FUN_00230060` reads `ClothNum`, then `FUN_0022fbd0` once per `ClothNo`
(stride 192, chains at model +11312). `FUN_0021aec0` (`0x21aec0`) reads a motion clip's `Cloth` onto that clip
+`0x24`, and `FUN_00213720` (`0x213720`) loads it only when the pointer is set, so a clip with no `Cloth` key keeps
the chains already loaded. A clip that names a file replaces them until a later clip names another. The same name in
the model PAC and the motion PAC can be a different script: demo 5's `wp105.clt` starts at bone 4 with gravity
-0.05 in the model copy, and at bone 3 with gravity -0.02 in the motion copy.

**ClothMode.** The key is stored at the motion clip +`0x100` and the value at +`0x102`. `FUN_00213720` writes a
byte at model +16308: a nonzero value becomes 1, and a clip with no key leaves the byte. The model update at
`0x236140` steps the chains (vtable +200) whenever they are loaded. The byte gates `FUN_00230150` (`0x230150`)
only, which multiplies the change in the owner's matrix onto each chain node that names this model (node +`0xfc`),
so the chains ride the root and the body's own motion and gravity are what swing them. Demo 5's `pl101_01` is bound
from the model clip, never sets ClothMode, and still steps. ClothMode 0 (demo 2 slot 13, demo 5 slot 13, parts of
demos 23 and 48) skips that ride.

**Reset.** `ResetClt` stores kind 1 at the motion clip +`0xe8`, `ResetClt2` kind 2, and `ResetCltLevel` the
halfword at +`0xea`. `FUN_00213720` copies the kind to model +16272 and the level to +16276. Kind 1 also sets a
one-shot at +16280 that clears the velocity words at each chain node +464 before the shared path. When the kind is
above 0, the frame update calls `FUN_002302d0`: the node's velocity is zeroed and its matrix copied back from the
rest at +`0x104`, then the chain updates `level` more times when `level` > 0. Level 20 (the usual cut) is a snap
to rest and twenty settle steps. Level -1 is the snap with no extra settle. The update just before the snap is
overwritten by it. A reset clip often names no file: the chains already loaded are snapped at the cut.

**Who it hangs on.** Coat pieces are their own MODs, on a CnsPos bone, and the `.clt` lists that piece's free
bones. The `_01` piece sits on bone 5 and the `_05` piece on bone 3: `pl101` in demo 5 (the Mission 3 opening;
`pl101` is that demo's Dante), `pl100` in the later missions (demo 44, the Lady entrance, and demo 53). `pl102_01`
is the other coat on bone 5 in demos 8, 16 and 44. The same keys drive weapons and scenery: `wp105` (a tassel, axis
NX) and `em108` curtains (bones 4-21, usually from the model clip). `pl101_01` is one chain, bone 2 on NY then
bones 4, 6 and 8 on Y (gravity -0.02, spring 0.02, stiffness 0.3). Across the unpacked scripts the bone axes are
Y 4143, NY 139, NX 129, Z 75, NZ 44, X 33, and 27 chains mix axes, so a chain is not one axis. One motion
`pl101_01.clt` (demo 53) has a Bone line whose axis token is not an axis.

**The other simulator.** `ClothSim1D` (model clip +`0x38`, `FUN_002ec400`, applied from `FUN_002342b0` when that
pointer is set) is a separate system. `ClothSim1DCnsPos0` / `CnsPos1` store bone pairs on the model work
(+`0x664`). It is in demos 6, 7, 39 and 41 only. Coats use the chain above.

## In the port

`tools/prepare_demo.py <iso> <id>` bakes DEMO_nnn.BIN (the camera per demo frame: each camera clip's `.cam` evaluated
at (frame - SetFrame) / ClipScale + SkipFrame from its start, MOT's Hermite with per-frame slopes, a key with bit 15
clear linear to the next) and DSTG_nnn.BIN (slot 00's `.scm` and texture set through `tools/prepare_stage.py --scm --ptx`,
its play area the cameras' reach). `psp/testroom/demo.c` plays it at 60 frames a second: the stage and the camera
(channel 12 as a vertical field of view, then the 326-line fit `demo_screen_fov` [stand-in]; channel 13 as roll).
The picture is the full 16:9 width, with the cinema bars above and below; test line `rawfov 1` passes channel 12
raw. Gameplay uses the same wider frame. Room 421 texture 6, the crack page that drew as a black plate across demo 53's frame 2300, is added, so the plate no longer replaces the stone. Test line `demo n`. Checked in PPSSPP: demo 5's tower top, gate and
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

**Cloth**: a slot's `.clt` chains step with `cloth.c` (`psp/testroom/demo_cloth.c`) in world space. The step is
`FUN_0022f270` (gravity on world Y) and the joints keep last frame's place, so a bone that moves leaves them behind.
ClothMode 1, and only 1, rides the parent model's root first (`FUN_00230150`: the new root matrix times the inverse
of the last), which takes that root's travel and turn back out. In these demos bone 0 stays at the origin and the
hip channel (bone 1) carries the world pose, so the ride follows the hips; riding bone 0 lets a cut lay the chains
out along the jump and the coat leaves as a flat sheet. Demo 5's coat never sets the key, so it trails the whole pose. The model clip's script holds until a motion clip names another. `ResetClt` and `ResetClt2` snap to rest
and, when `ResetCltLevel` is above 0, step that many times at the pose held for the cut (level -1 snaps only; a level
above 32 is capped; the game's update just before the snap is overwritten by it). Each bone keeps its own axis, NY
included. These scripts' wind is 0 and they name no capsules. A coat on Dante's 23-bone body still gets the
gameplay capsules (spine and both legs), so the chains can't hang through him. `ClothSim1D` is not run. A Bone line whose axis token
is not an axis is skipped. Demo 53's motion `pl101_01.clt` puts two Bone records on one line (`Bone 6 Bone 8 X`);
bone 8 is X and bone 6 takes that axis.

Not yet: P effects.

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
