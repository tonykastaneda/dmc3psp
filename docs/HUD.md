# PSP player HUD

The cockpit uses the existing local `hud_runtime.h` exported by `tools/prepare_hud.py`.
The gothic health frame is `BAR_FRAME`; the DT row uses the ten individual `GEMS`
quads. Extracted meshes, textures, and reference captures remain gitignored.

Vitality starts at 10,000 HP and occupies 75 pixels at (46, 13), seven pixels high.
Each Blue Orb adds 1,000 HP and 7.5 pixels, up to 20,000 HP / 150 pixels.
The end ornaments stay 22 pixels wide as the middle grows. A pale damage chunk
holds for 0.35 seconds, then catches up at 65% of the gauge per second.
The existing style emblem and transient weapon icons remain.

DT starts with three rune units; each Purple Orb adds one, up to ten.
The runes sit at y=28 with a 13-pixel pitch and show fractional energy.
Activation requires three units; active DT consumes one unit per simulation second
and turns off at zero. Quicksilver and Doppelganger share that pool and threshold.
Confirmed melee contacts restore 0.18 units each; bullet hits restore 0.025.
Refill is suppressed while any DT consumer is active. These rates are port tuning,
not claims of exact PS2 timing. Purchases refill the capacity they increase;
the per-frame capacity sync does not heal or recharge an unchanged gauge.

`hud_draw_boss(name, health, maximum)` accepts any boss without knowing its class.
Its 342-pixel fill sits at (69, 253), beneath a centred name in the existing font.
A null name, nonpositive HP, or nonpositive maximum hides it. Main supplies the
existing enemy boss query; that query excludes defeated actors and dying Cerberus,
whose separate head HP can survive the start of his death sequence.
The boss bar has the same delayed damage chunk as vitality.

No HUD heap allocations occur during play. Vertices use the existing GU display-list
arena. All ten partially filled runes share one textured draw (at most 120 vertices).
The steady player HUD uses six draws, excluding transient weapon icons. The boss
gauge adds four geometry draws plus the existing font draws for its name and shadow.

## Verification

Build and host checks:

```powershell
./tools/build_testroom.ps1
python tools/tests/run_hud_stats.py
```

Run these scenes sequentially, with no other PPSSPP or PCSX2 instance open:

```powershell
python tools/ppsspp_scene.py hud_room --street --script tools/tests/hud_room.scene
python tools/ppsspp_scene.py hud_dt --street --script tools/tests/hud_dt.scene
python tools/ppsspp_scene.py hud_capacity --street --script tools/tests/hud_capacity.scene
python tools/ppsspp_scene.py hud_cerberus --mission 3 --room 6 --script tools/tests/hud_cerberus.scene
```

`capacity blue purple` sets test purchase counts. The scene completion report now
includes HP, capacity, DT energy, active state, and melee/bullet hit counters.

Checked in PPSSPP on 2026-11-16:

- Street damage: 6,500/10,000 HP; frame 63 shows the chunk, frame 110 the settled bar.
- Expanded capacity: 13,000/20,000 HP after damage, ten full runes, longer frame.
- DT: starting runes drain to zero and DT stops; two subsequent gun hits restore 0.050 units.
- Cerberus: boss name/bar present during combat, absent after the scripted kill at frame 179.
- Host checks cover reset defaults, capacity limits, refill only on capacity increases,
  hit gain clamping, suppression during DT, activation threshold, and automatic shutdown.

Screenshots are local in `analysis/testroom/`: `hud_room_20.png`, `hud_room_63.png`,
`hud_room_110.png`, `hud_capacity_20.png`, `hud_capacity_63.png`,
`hud_capacity_110.png`, `hud_dt_20.png`, `hud_dt_60.png`, `hud_dt_195.png`,
`hud_dt_300.png`, `hud_cerberus_60.png`, `hud_cerberus_160.png`, and
`hud_cerberus_290.png`. PSP hardware performance still needs a device check.
