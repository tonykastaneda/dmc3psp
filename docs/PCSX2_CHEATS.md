# PCSX2 cheats for reference captures

Three cheats for the harness PCSX2 (`build/refemu`, PCSX2 2.9) running SLUS-213.61 (CRC `25FC361B`).
They exist so reference captures (bosses, the credits fight, Bloody Palace) aren't cut short by deaths.
Each one is a separate labelled group, so any of them can be left off.

## Turning them on

```
python tools/pcsx2_ref.py start --cheats all              # health, dt and damage
python tools/pcsx2_ref.py start --cheats health,dt        # any comma list of health, dt, damage
python tools/pcsx2_ref.py start                           # cheats off (the default)
```

From Python: `pcsx2_ref.start(cheats="health,dt")` or `pcsx2_ref.start(["damage"])`. `start()` with no
argument turns them off, so older scripts never inherit someone else's cheats. For a PCSX2 started by hand,
use `python tools/pcsx2_ref.py cheats on [list]` and `cheats off`.

How it works: `set_cheats` writes the game's own settings file, `build/refemu/gamesettings/SLUS-21361_25FC361B.ini`,
with `[EmuCore] EnableCheats` and one `[Cheats] Enable = <group name>` line per cheat. The global `PCSX2.ini`
keeps `EnableCheats = false` (`setup()` still writes that). The codes live in
`build/refemu/cheats/SLUS-21361_25FC361B.pnach`. It's local, because `build/` is gitignored. If it's missing,
recreate it from the listing at the end. The pnach's `patch=1` lines are applied every frame, so they also
survive loading a save state.

## The codes

The player is the static object at **0x17E0A40**. It sits in the ELF's .bss, which runs to 0x1E5AB00, so the
address is fixed. Field offsets are as in docs/SHOP.md and docs/MOVESET_RE.md: HP **+0x2ad4**, max HP
**+0x2aa8**, DT **+0x28c4**, max DT **+0x28c8**.

### Infinite Health (`health`): byte 0x17E32ED = 1 every frame

- The game's own **can't-die flag, player +0x28ad**. It is set by `FUN_0021f500` (0x21fcb4, 0x21fd10) in some
  modes.
- With it set:
  - The player's per-frame update `FUN_002e7f10` copies max HP into HP (0x2e83a8: `if (+0x28ad == 1) hp = max`).
  - The death check in `FUN_0021fe30` (0x220eb4) won't call the die function `FUN_002e8780`, which also checks
    the flag first (0x2e8794).
- Hits still land: Dante flinches and is knocked down, but the bar refills on the next frame.
- **Why not the damage function:** the obvious patch fails. That would be `FUN_0021ecc0` (`hp -= x` unless
  +0x28ac), with a nop at 0x21ecc8. Enemy hits don't go through it. They go through the shared damage component at
  player +0x2a64 (vtable 0x605730, subtract routines at its +0x14 `0x2f17a0` and +0x18 `0x2f22c0`). That
  component writes HP through its pointer at +8 to the HP block (player +0x2ad0, HP at +4). Enemies use the same
  component, so patching it would make them immortal too.
- +0x28ac is the "dead" flag: the die function sets it (0x2e87a0), and it stops damage, healing and DT changes.

### Infinite Devil Trigger (`dt`): word 0x0021EAF0 = 0

- `FUN_0021eae0` is the player's DT drain (`dt -= x`, floored at 0; 0x21ec94).
- Its first test (0x21eaf0, `bne v0,a2`) skips the early return unless the game object's byte +8
  (`*0x1d0d70c`) is 1. Making that test a nop makes the function always return at once.
- Gains (`FUN_0021ea30`) still fill the gauge, so start the capture with it full or fill it once.

### One-Hit Kills (`damage`): 0x002E0D50..5C = `lui at,0x447a; mtc1 at,f0; jr ra; nop`

- The players' attack-power virtual, `0x2e0d50`, normally returns player +0x2a18: 1.0, or the DT / style
  multiplier.
- The patch makes it return **1000.0**.
- Every enemy damage model multiplies a hit by the attacker's vfunc +0x68 (docs/CERBERUS_SPEC.md §5,
  docs/BAR_CRANK_SPEC.md).
- The attacker passed is the player's hit sub-object at player +0x40. Its +0x68 is the thunk `0x206920`
  (`j 0x2e0d50` with `a0 -= 0x40`). Dante's main vtable is 0x602650.
- Five vtables hold 0x2e0d50 directly at +0x68: the slots at 0x601bec, 0x60276c, 0x60290c, 0x6067cc and
  0x615f8c.
- Inferred: these are the other player-type characters, Vergil when playable and possibly the Doppelganger and
  Lady. **With this cheat on and Infinite Health off, a player-type enemy would kill Dante in one hit.**
- 0x2e0d58/5c were padding nops before `0x2e0d60`.
- Some bosses have rules that keep a pool at 1 or ignore hits while armoured (Cerberus' ice, Geryon's shells).
  Those rules still apply.

## Verification (Nov 26, 2026)

Mission 2's street from `build/refemu/sstates/style_rg.p2s`, with five Hell Prides (CEm000, HP 250).

- **Pride HP field:** its damage component sits at +0x26f4, and the component's +8 points to the HP block, so HP is
  at obj+0x2360.
- **Health:** with the failed damage-function nop, Dante (3900 HP in the state) died standing among the
  Prides within 20 s (REBIRTH screen). With the +0x28ad flag, he took repeated hits (state 0x2c) at 20000/20000
  HP for 20 s (`analysis/ref/cheats/01_idle_hit.png`, full bar).
- **DT:** with Devil Trigger on (L1) and the cheat off, DT drained 10000 → 2212 in 12 s (660 a second). With the
  cheat it stayed at 10000 for 12 s (`02_dt_on.png`, `02_dt_control.png`).
- **Damage:** these runs held R1 to lock on and pressed Triangle (Rebellion). Without the cheat, a hit did 80-130
  (250 → 150 → 70 → 0, four hits per Pride). With the cheat, every Pride a swing reached went 250 → 0 in one hit,
  three in one arc (`05_cheat_swing1.png`, `05_control_swing1.png`).

The screenshots are in `analysis/ref/cheats/`, which is local (gitignored). The test scripts were throwaway: they
loaded the state into slot 3 and drove the pad through `pcsx2_ref`.

## The pnach (for recreating it)

```
gametitle=Devil May Cry 3 - Dante's Awakening Special Edition (SLUS-21361) [25FC361B]

[Infinite Health]
author=dmc3psp
description=Player +0x28ad (can't-die) = 1 every frame: the update refills hp to max.
patch=1,EE,017e32ed,byte,01

[Infinite Devil Trigger]
author=dmc3psp
description=FUN_0021eae0 (DT drain) always returns early: nop the bne at 0x21eaf0.
patch=1,EE,0021eaf0,word,00000000

[One-Hit Kills]
author=dmc3psp
description=The players' attack-power vfunc 0x2e0d50 returns 1000.0.
patch=1,EE,002e0d50,word,3c01447a
patch=1,EE,002e0d54,word,44810000
patch=1,EE,002e0d58,word,03e00008
patch=1,EE,002e0d5c,word,00000000
```

## Running in the background (2026-11-27)

`tools/pcsx2_ref.py` no longer needs PCSX2 in front. Its pad is a virtual Xbox 360 pad (ViGEmBus, `vgamepad`):
`setup()` binds the private copy's Pad1 to the XInput slot that pad lands in (the first free one; the user's own pads
keep theirs) and turns the XInput source on; XInput is read without focus. States load and save over PINE
(`load_state(slot)` / `save_state(slot)`, opcodes 0x0A / 0x09) instead of the F1 / F3 hotkeys. Shots and recordings
come from the window's own drawing (`grab`: PrintWindow with PW_RENDERFULLCONTENT; the Vulkan window comes out, also
covered). `start()` opens the window without activating it (and hands the foreground back if it takes it), and moves
it to a second display when there is one. `focus()` does nothing unless the keyboard path is asked for
(`PCSX2_REF_KEYBOARD=1`). The private copy is muted ([SPU2/Output] OutputMuted, OutputVolume 0, FastForwardVolume 0).
Checked: boot, the title's warning dismissed by the pad, slot 1 loaded, the stick and triangle moving Dante, a 3 s
recording; GetForegroundWindow sampled every 50 ms from before the launch never was the emulator (0 of 840).
The HD Collection (`tools/hd_ref.py`) can't go the same way: it reads XInput slot 0 only and stops while unfocused,
so it keeps the keyboard and the focus; its shots now use the window's drawing where that isn't black.
