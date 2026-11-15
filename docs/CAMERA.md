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

The parser starts every value at -1 and `FUN_0024e6a0` ignores negative ones, so `cam_rotx -5` (rooms
130, 406) means the default 14.5. Target height above Dante's feet is 140 in every room (`+0xd0`).

### CAM chunk

`"CAM\0"`, u16 version, u16 zone count at +6, zones from +0x10. A zone is a 0x40 header, then its
camera's parameters (0x70 for types 0 and 3, 0x450 for type 1, 0x860 for type 2, none for 4 and 5), then
`header[0x14]` areas of 0x50 bytes.

Zone header: `[1]` camera type, `[2]` stick mode + 1 (0 = the room's), `[6]` 1 = "battle" zone (only while
Dante is in his combat stance, player `+0x28ae` and `+0x28a9`), `[0x14]` area count, `[0x15..0x34]` 16
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
2. Otherwise, or against it, the other zones he's inside compete: a battle zone wins while he's in
   combat stance; then the lowest priority number; ties go to the lower index. A zone of higher priority
   than the kept one takes over only if he's inside its area by a margin (`FUN_002bc990`; the port uses
   the plain area).
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

## The port

- `camera.c` runs the same zone selection, Pan and Rail cameras, blends, and the follow camera's goals,
  steps, buttons and sphere collision, with the room's values. The PSP's screen is 16:9, the PS2's 4:3:
  the vertical FOV is the game's, so the PSP shows more at the sides.
- Boss cameras: Cerberus's (id 3, `FUN_004fbdf0`) is ported: target 150 over Dante, eye 400 out at 4 degrees,
  turned away from the arena centre (2520, 200, 3000; data 0x5e0730), or behind Dante toward his lock-on
  target, re-aimed only past 15/35 degrees (pitch clamped -20..15); blocked, the eye stops at the wall and
  both rise up to 20; it never comes within 800 of the centre; eased by 0.9 a frame (`FUN_0042ec50`);
  FOV 50. The other bosses use the follow camera until theirs are ported.
- The PSP has no L2/R2, so the follow camera's manual turn isn't there; SELECT is the camera reset.
- Approximations: the margin test of step 2 above (plain areas); the spline projection is a search on each
  candidate segment, not the game's polynomial roots (same nearest point); the follow camera's lock-on
  framing; "combat stance" is "enemies alive in the room"; the stage collision the camera sweeps against is
  the port's whole HITS set, not just the `camhit` sets.
