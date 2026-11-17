# The camera

How the PS2 game (SLUS_213.61) places its camera, and how the port copies it. Addresses are the PS2
executable's. The port's code is `psp/testroom/camera.c`; its data comes from `tools/gen_cameras.py`
(`build/testroom/generated/camera_runtime.h`, local only).

## Pieces

| class (typeinfo name) | vtable | what it is |
|---|---|---|
| CCameraCtrl | 0x604450 | picks the camera every frame (`FUN_002bcbd0`) and blends between them |
| CCameraPlayer | 0x602078 | the follow camera (update `FUN_0024e9c0`) |
| CCameraPan (CAM type 0) | 0x603f70 | a fixed eye that turns to look at Dante |
| CCameraRail (type 1) | 0x603f40 | eye on a spline, driven by Dante's place on a second spline |
| CCameraRailW (type 2) | 0x603f10 | two rails; no room uses it |
| CCameraRel (type 3) | 0x603ee0 | a fixed eye/target pair; no room uses it |
| CCameraClip / CHideClip | 0x602010 / 0x602040 | camera collision helpers |

All cameras share the base update `FUN_002aea60` (eye `+0x70`, target `+0x80`, vertical FOV `+0x20`,
roll `+0x24`; view matrix `FUN_00252fb0`/`FUN_00131990`, projection `FUN_001ac750`: focal length from
the screen *height*, so the FOV is vertical).

## Where the data lives

A room's stage data slot (GDATA slot + 1) is a PAC whose entry 3 is the `CAM` chunk; the room's stage
slot holds the `# GAME` text whose `CONFIG` set has the follow camera's settings. `FUN_00239b60` loads
both on room entry: `FUN_002bc890` indexes the CAM zones, `FUN_002fe8f0`/`FUN_002ff0f0` parse the text,
`FUN_0024e5e0` resets the follow camera to its defaults and `FUN_0024e6a0` applies the room's values.

### `# GAME` keys (FUN_002ff0f0)

| key | meaning | default (FUN_0024e5e0) |
|---|---|---|
| `cam_rotx a` | pitch of the follow camera, degrees | 14.5 (0.25307 rad) |
| `cam_len n b` | distance, normal and "battle" (lock-on framing) | 460, 400 |
| `cam_aov a` | vertical FOV, degrees | 55 (0.95993 rad, base `FUN_00252f20`) |
| `cam_init x y z` | up to 8 "safe" eye points (see start-up below) | none |
| `hitattr camhit` | that collision set blocks the camera | |

The parser reads tokens, not lines: `;` is only a token it doesn't know, so a `commented'' `; cam_aov 57` after
`cam_aov 64` still applies (room 111: the PS2's follow camera has FOV 57, 348 cm out, level). A set header must
be its own token, so `;# SET 0 CONFIG` is skipped (room 110's real values are the next CONFIG's 10 deg, 500, 55).
The parser starts every value at -1 and `FUN_0024e6a0` ignores negative ones, so `cam_rotx -5` (rooms
130, 406) means the default 14.5. Target height above Dante's feet is 140 in every room (`+0xd0`).

### CAM chunk

`"CAM\0"`, u16 version, u16 zone count at +6, zones from +0x10. A zone is a 0x40 header, then its
camera's parameters (0x70 for types 0 and 3, 0x450 for type 1, 0x860 for type 2, none for 4 and 5), then
`header[0x14]` areas of 0x50 bytes.

Zone header: `[1]` camera type, `[2]` Dante's shadow type + 1 for the zone (0 = the room's `shadow` key; `FUN_002bd660` -> `FUN_002e1100`), `[6]` 1 = flight zone (only during Air Raid,
Nevan's Devil Trigger flight: player `+0x28ae` and `+0x28a9`, set from the weapon's table by `FUN_0025b420`), `[0x14]` area count, `[0x15..0x34]` 16
transitions `{from zone, frames}`, `[0x35]` priority (lower wins), `[0x3c]` a sound/BGM group.

Area: four corners `xyzw` (+0x00), height (+0x40). Dante's position is inside when his y is between
corner 0's y and that plus the height and he's on the inner side of all four edges (`FUN_001c1510`).

Type 0 (Pan) parameters: `[0]` axis lock (1: target y from the fixed point, 2: target x/z from it), `[1]`
target (0: the fixed point at +0x10, 1: Dante's matrix times +0x20, 2: Dante + offset +0x30), `[2]` zoom
mode (0 FOV, 1 dolly), `[3]` jump mode, +0x04 FOV factor while locked on, +0x40 zoom table `{near, far,
zoom at near, zoom at far}` on the eye-to-target distance (0 = none), +0x50 eye, +0x60 FOV, +0x64 roll.

Type 1 (Rail) parameters: the same first bytes, target offsets at +0x10 (matrix) / +0x20 (world), zoom
table at +0x30, eye spline points at +0x40 (32 x xyz), path spline at +0x1c0, FOV per point at +0x340,
roll per point at +0x3c0, point count at +0x440.

## Choosing a camera (FUN_002bcbd0, every frame)

1. The zone Dante was in last frame keeps the camera if he's still inside one of its areas.
2. Otherwise, or against it, the other zones he's inside compete: a flight zone wins during Air Raid; then the lowest priority number; ties go to the lower index. A zone of higher priority
   than the kept one takes over only if he's inside its area shrunk toward its middle by 60 cm
   (`FUN_002bc990`: scale (sqrt(area) - 60) / sqrt(area)).
3. Outside every zone the last camera stays.
4. On a change, the new zone's transition list is searched for the old zone: if it's there with N frames,
   the old eye, target, FOV and roll are blended out over N frames as `f = (k/N)^2`, k = N-1 .. 0
   (`out = old*f + new*(1-f)`); otherwise it's a cut. Entering a type 4 zone from a non-follow zone
   restarts the follow camera from the current eye's bearing (`FUN_0024e750`); type 5 keeps it.

## Fixed cameras

Base update (`FUN_002aea60`): the target eases toward its goal by a step of `d^2 * 0.0005` per frame
(d = distance to the goal); the eye is placed exactly. The zoom table scales the FOV (mode 0) or the
eye's distance to keep the framing (mode 1: `len * tan(fov*z)/tan(fov)`).

Pan (`FUN_002af2a0`): eye fixed, target as above.

Rail (`FUN_002af5a0`): both splines are C2 cubic Hermite curves through their points, parametrised by
chord length in metres (`FUN_002cb300`, tangents from a tridiagonal solve in `FUN_002cbd50`). Dante is
projected onto the path spline (`FUN_002cb530`: nearest point, then the nearest point on the segment to
either side); the camera's place on the path moves toward that by at most 25 cm per frame; the eye is the
eye spline at the same segment and fraction; FOV and roll are lerped between the points.

Jump modes (`[3]`): 1 keeps the eye's height above the target from the jump's start; 2 holds the target's
height at the jump start; 3 both.

## The follow camera (CCameraPlayer, FUN_0024e9c0)

Per frame:
- goal target = Dante + (0, 140, 0) (`FUN_0024f2a0`); goal eye = goal target plus the *current*
  horizontal bearing to the eye, raised to the room's pitch, at the room's distance. So it never swings
  behind him by itself; it only follows.
- eye and target each move toward their goals by `d^2 / 2000` per frame (`+0x1e0` = 1000, step
  `d * d / (2 * 1000)`); for small d that's a slow drift, so it lags behind a running Dante.
- L2/R2 (pad 0x8000/0x2000) turn it round him at 1 degree per frame; released, the speed falls by
  0.1 degree per frame per frame.
- the camera-reset button (pad config 0x400) turns it to behind Dante (his yaw + 180) at a constant
  speed of a sixth of the angle per frame: 6 frames for any angle.
- collision (`FUN_0024f560`): a 50 cm sphere is swept from the old eye to the new one against the stage
  collision (only `camhit` sets block it); if it was stopped more than a little, a 50 cm sphere is swept
  from the target out to the eye and the eye is put where it stops (`FUN_0024f640`).

Lock-on framing (`FUN_0024f370`, mode 1 when the lock-on target can be framed): the target is between
Dante's head and the enemy, the distance is half their separation plus the battle `cam_len`. Leaving it,
the eye and target steps are capped at 15 per frame until they settle.

Start-up (`FUN_0024e7d0`): the bearing is Dante's start yaw plus the POS record's camera offset (+0x14,
degrees); the eye is placed at the room's pitch and distance, and the collision sweep starts from the
nearest `cam_init` point, so the first eye is inside the room. It stays put until Dante moves.

## Boss fights

Room entry also sets up a second CCameraPlayer at scene `+0x47c0` and gives it boss fields
(`FUN_00320730`); its update is `FUN_003204c0`. While the global byte 0x61ac1c is above 0, CCameraCtrl
uses that camera instead of the zones, and 0x61ac1c picks the boss's own camera function: 1
`FUN_004fc1d0`, 2 `FUN_004fc650`, 3 `FUN_004fbdf0`, 4 `FUN_005125c0`, 5 `FUN_00509b20`, 6 `FUN_0051d890`,
7 `FUN_0051c250`, 8 `FUN_00321620`, 9 `FUN_0051ce80`, 10 `FUN_0051ddc0`, 11 `FUN_0051c860`, 12
`FUN_0051b0a0`, 13 `FUN_00321500`, 14 `FUN_00544950`. Its first frame (`FUN_00320850`) puts the eye 400
behind the target, 3 degrees up. When the boss camera is done it sets 0x61ac1c to 0x80, which makes
CCameraCtrl put the zones' camera back (`FUN_002bd310(ctrl, 1)`).

## Event cameras (the scripts' cuts)

The event scripts' camera moves (docs/EVENT_SCRIPTS.md, ops 49-4f) run on a third camera at scene `+0x4a80`.
Its data is the room data PAC's **entry 8, the `DCA` chunk**, handed over on room entry by `FUN_00239b60`
(`FUN_003201d0`: data at +0x90, cuts from data + 0x10).

`"DCA\0"`, u16 cut count at +6, cuts from +0x10, **0x410 bytes each**: eye points (32 × xyz) at +0, target
points (32 × xyz) at +0x180, FOV per point (radians) at +0x300, roll per point at +0x380, point count (byte) at
+0x400, frame count (s32) at +0x404. 458 cuts in 141 rooms.

| op | function | what |
|---|---|---|
| 4d n | `FUN_0023e790` → `FUN_003201f0` | start cut n (ignored if the room hasn't it): both splines built as the rails' (`FUN_002cb300`), time 0, end = frames − 1; scene +0x5be0 = 1 puts this camera up |
| 4e n | `FUN_0023e7c0` → `FUN_00320490` | the same, held (+0x1154): the cut stays on its first frame |
| 4f | `FUN_0023e7f0` → `FUN_00320450` | the script waits until time ≥ end |
| 4c | `FUN_0023e6e0` | the event ends: +0x5be0 = 0, the follow camera reset (`FUN_0024e740`), the zones' camera again |
| 75 z v | `FUN_0023e850` | the zone's byte at ctrl +0xa0 + 16 z: 1 turns zone z off (`FUN_002bcbd0` never picks it), 0 back on |
| 76 a | `FUN_0023e8b0` | the follow camera put at Dante's yaw + a degrees (`FUN_0024e7d0`) |
| 77 p l b f | `FUN_0023e860` | the follow camera's pitch, distance, battle distance, FOV, as a room's CONFIG (`FUN_0024e6a0`) |
| 59 n | `FUN_0023e3d0` | Dante to the room's POS record n |

Per frame (`FUN_003202b0`): t = time / end (time clamped to end); with n points, segment s = floor((n − 1) t) (the
last segment at t = 1) and u the fraction within it, so the points are evenly spaced **in time**, not by length;
eye and target are the two splines at (s, u) (`FUN_002cb410`), FOV and roll lerped between points s and s + 1.
Then, unless held, time += the game speed (1 a frame). Eye and target are placed exactly, no easing.

In the port: `tools/gen_cameras.py` writes the cuts (`cam_cuts`, their points in `cam_points` with the target as
`path`), camera.c plays them (`camera_event_cut`, `camera_event_done`, `camera_event_end`, `camera_zone_enable`,
`camera_follow_set`, `camera_follow_turn`), and messages.c runs the scripts' ops (docs/EVENT_SCRIPTS.md).

## Checked against the PS2

`tools/ps2_camera.py` (PCSX2, Mission 3, Dante held at a spot) against the port's camera.c at the same spot.
`python tools/camera_check.py` re-runs these against camera.c on the host in a few seconds:

| room, Dante | PS2 eye / target | port |
|---|---|---|
| 002 (1700, 3500), rail zone 1 | 2573.1 300.4 3826.0 / 1700 +134 3500 | 2573.3 300.4 3826.7 / same |
| 002 (2500, 4500), rail zone 0 | 2469.6 104.4 5070.4 / +180 | 2469.6 104.4 5070.4 / +180 |
| 002 (2500, 2600), rail zone 0 | 2566.8 180.7 3133.9 | 2566.8 180.7 3133.9 |
| 003 (2500, 2500), rail, FOV 60 | 2867.9 207.7 2106.7 | 2867.9 207.7 2106.7 |
| 003 (2300, 1600), pan zone 5, FOV 55 | 2382.2 84.8 2053.7 / 2332 132 1752 | same |
| 003 (2798, 2919), rail | 2786.7 196.2 2308.6 | 2786.8 196.2 2308.4 |
| 001 street, follow | 443 out, 115 up from Dante + 140 | 445 out, 115 up |
| 300 (1614, 3650), rail zone 13, roll 0.051 | 2140.5 137.7 3929.0 | 2140.6 137.7 3929.0 |
| 100 (2500, 3500), rail zone 0 with its zoom table: FOV 48.76 | 2401.8 415.1 2594.8 | 2401.8 415.2 2594.7, FOV 48.8 |
| 100 (2490.7, 4148.3), the same: FOV 45.54 | 2356.3 208.3 2920.6 | same, FOV 45.5 |
| 100 (3143.2, 2246.8), rail zone 1, target lifted | 2440.7 150.5 3779.4 / 2500.1 579.1 2572.5 | 2440.7 150.5 3779.4 / 2500.3 581.0 2569.0 |
| 400 (2572.3, 9761.8), M18's opening, rail zone 1, FOV 70 | 2671.5 -228.7 10616.6 | same |
| 201 (1275.4, 3729.7), M9's start, pan zone 0 | 2018.0 336.1 3804.5 / 1275.2 172.0 3729.3 | same |
| 201 (2499.9, 3640.0), pan zone 3 | 2546.7 348.9 3040.2 | same |
| 106 (2500.1, 2950.0), M16's start, rail zone 0 | 2922.9 480.7 2426.8 | same |
| 219 (1997.2, 2497.2), rail zone 2, FOV 70 | 2250.4 622.0 3195.1 | same |

## Known differences that aren't the camera

- M18's opening (room 400): the camera is the game's (table above), but the port shows black where the PS2
  shows white fog and a bright backdrop through the room's openings. The port draws no fog; the room's fog
  isn't decoded (LIG2's header colours are a lead; `FogColor` in the executable is a model-script command).
  Every other mission's opening shows at most 1.3% of the screen at the clear colour.

## The port

- `camera.c` runs the same zone selection, Pan and Rail cameras, blends, and the follow camera's goals,
  steps, buttons and sphere collision, with the room's values. The PSP's screen is 16:9, the PS2's 4:3:
  the vertical FOV is the game's, so the PSP shows more at the sides.
- Roll (rail points' and pan cameras' +0x64, up to 10 degrees, mostly inside Leviathan) turns the view about
  its axis (sceGumRotateZ before the look-at). Checked at Mission 8's start (room 300, rail zone 13, 3 degrees):
  the PS2's eye 2140.5 137.7 3929.0 and the port's agree, and the picture leans the same way (the opposite sign
  didn't).
- Boss cameras: Cerberus's (id 3, `FUN_004fbdf0`) is ported: target 150 over Dante, eye 400 out at 4 degrees,
  turned away from the arena centre (2520, 200, 3000; data 0x5e0730), or behind Dante toward his lock-on
  target, re-aimed only past 15/35 degrees (pitch clamped -20..15); blocked, the eye stops at the wall and
  both rise up to 20; it never comes within 800 of the centre; eased by 0.9 a frame (`FUN_0042ec50`);
  FOV 50. Vergil's in 144 (id 9, `FUN_0051ce80` -> `FUN_0051d0b0`) too: target 40% of the way from Dante
  (+150) to Vergil (+100), distance their gap / tan 22.5 (at least 400); past 800 it drops to 2 degrees and
  turns behind Dante toward him (5 degrees off) at 0x7f a frame, 400 further back; otherwise 15 degrees with
  the yaw held; eased 0.95; its first frame looks from Dante to the boss, 5 degrees round. Not ported: the
  FOV swell to 80 on Vergil's flag +0xbb4f, his other rooms (234 `FUN_0051d270`, else `FUN_0051d530`).
  The other bosses use the follow camera until theirs are ported.
- Boss camera ids from their set-up functions (vtable +0x130 calling `FUN_003595b0`): 2 Geryon (CEm029),
  3 Cerberus (CEm025), 4 Gigapede (CEm023), 5 CEm026, 6 CEm028, 7 CEm030, 8 Leviathan (`FUN_00461260`),
  9 Vergil (CEm035), 10 Doppelganger (CEm034), 11 CEm031, 12 Arkham (CEm032), 13 `FUN_00405930`.
  Gigapede's (`FUN_005125c0`) switches on scene `+0x24 -> +0x4511` (set by `FUN_0023ebe0` from the zone's
  `[0x3c]`): 0, 1, 2, 3, 6, 100, 101 put the eye at fixed points (table 0x5e0c20, about 20 m up) with the
  target over Dante (`FUN_00512c00`); anything else orbits 600 out toward the head (`FUN_005126e0`). Which
  of these the fight in 111 uses wasn't seen, so the port keeps its own Gigapede view (main.c).
- The PSP has no L2/R2, so the follow camera's manual turn isn't there; SELECT is the camera reset.
- Approximations: the spline projection is a search on each
  candidate segment, not the game's polynomial roots (same nearest point); the follow camera's lock-on
  framing; flight zones follow the port's Air Raid state; the stage collision the camera sweeps against is
  the port's whole HITS set, not just the `camhit` sets.
