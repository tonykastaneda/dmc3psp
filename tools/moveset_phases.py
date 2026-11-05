"""Action -> phase programs for every weapon, read from the action handlers
(FUN_002625d0's targets; analysis/ghidra/export/decompiled). Each handler is a
small state machine: play a script, wait for a trigger, go to the next script.

A phase is P(script, ...):
  adv     what ends the phase:
            end      the motion ends (default)
            flag1 / flag2  script window 0 byte 4 (player+0x24d5) reaches 1 / 2
            land     Dante reaches the floor
            mash     motion end: `loop` plays while the button is mashed, else next
            charge   the button is released (motion loops from `start` until then);
                     `tiers` picks the next script by charge time (FUN_00279 family)
            timer    `frames` frames, replaying the motion
  start   motion frame to start at (FUN_002500d0's first argument)
  air     (speed, speed_accel, vy, gravity) per 60 fps frame: FUN_002e3f50 and
          FUN_002e3ef0, applied when the phase starts. A number is an offset into
          weapon config chunk 11 (player+0x2844), a 4-tuple holds offsets or floats.
  flag_air  the same, applied when the script's flag reaches 1 during the phase

Moves marked APPROX below select variants on state the test room doesn't have
yet (Royalguard gauge, gun stance tables, Nevan's hit-dependent forks) and are
listed in docs/MOVESET_RE.md.
"""
from dataclasses import dataclass, field
from typing import Optional


@dataclass
class P:
    script: str
    adv: str = "end"
    start: float = -1.0
    air: Optional[tuple] = None
    flag_air: Optional[tuple] = None
    loop: Optional[str] = None
    loop_start: float = -1.0
    tiers: tuple = ()          # (script, start) x3: released early / charged / charged and looped once
    frames: float = 0.0
    btn: str = "TRI"
    lock: Optional[str] = None  # script when locked on (the shot tables' aimed column)
    shot: int = 0               # shots fired as the phase starts
    auto: float = 0.0           # frames between further shots during the phase
    refire: float = 0.0         # adv="shot": frames before another press fires again
    layered: bool = False       # upper-body shot over the legs (FUN_0021e620 with param_4 = 1)


def wcfg_air(spd, vy):
    """FUN_002e3f50(cfg[spd], cfg[spd + 4]) and FUN_002e3ef0(cfg[vy], cfg[vy + 4])."""
    return ("cfg", spd, spd + 4, vy, vy + 4)


def vy_air(vy):
    """FUN_002e3ef0(cfg[vy], cfg[vy + 4]) alone: straight up, no forward speed."""
    return ("vy", vy, vy + 4)


def single(script):
    return [P(script)]


# Beowulf charge moves: tap, or hold into a charge loop and release (charge time
# against config +0x504, then whether the loop has played once: player+0x2856).
def charge(tap, loop, loop_start, hi, himax, release_start, after=()):
    return [P(tap, adv="flag1"), P(loop, adv="charge", start=loop_start,
            tiers=((tap, release_start), (hi, release_start), (himax, release_start)))] + list(after)


NEVAN_END = [P("6:4", adv="stance", frames=3600.0)]   # action 99: config +0x63c = 3600 frames; any input leaves it

ACTIONS = {
    # Cerberus
    30: single("4:3"), 31: single("4:4"), 32: single("4:5"), 33: single("4:6"), 34: single("4:7"), 35: single("4:8"),
    36: [P("4:10"), P("4:26")],
    37: [P("4:27"), P("4:29", adv="mash", loop="4:31"), P("4:26")],
    38: [P("4:19", adv="end", flag_air=vy_air(0x54c)), P("4:30", adv="land"), P("4:24", start=61.0)],
    39: [P("4:19", adv="end", flag_air=vy_air(0x54c)), P("4:25", adv="land"), P("4:24", start=61.0)],
    40: [P("4:11", adv="land", air=wcfg_air(0x3c8, 0x3d0))],
    41: [P("4:22"), P("4:23")],
    42: [P("14:0", adv="mash", loop="14:10", loop_start=19.0, btn="CIR"), P("14:1")],
    43: [P("14:7", adv="mash", loop="14:11", loop_start=19.0, btn="CIR", air=wcfg_air(0x3e8, 0x3f0)), P("14:8", adv="land")],
    44: single("14:2"),
    45: [P("14:3"), P("14:4")],
    46: [P("14:5"), P("14:6")],
    # Agni & Rudra
    50: single("5:3"), 51: single("5:4"), 52: single("5:5"), 53: single("5:6"), 54: single("5:7"), 55: single("5:8"), 56: single("5:9"),
    57: [P("5:10"), P("5:25")],
    58: [P("5:20"), P("5:21")],
    59: [P("5:20"), P("5:21", adv="flag2"), P("5:22")],
    60: [P("5:20"), P("5:21", adv="flag2"), P("5:22", adv="flag2"), P("5:23")],
    61: [P("5:17", adv="land", air=wcfg_air(0x338, 0x340))],
    62: single("5:18"),
    63: [P("5:19", adv="land", flag_air=vy_air(0x350))],
    64: [P("5:11", adv="mash", loop="5:30", loop_start=201.0), P("5:26")],
    65: single("15:0"), 66: single("15:1"),
    67: [P("15:2"), P("15:3")],
    68: [P("15:4", adv="land", air=wcfg_air(0x388, 0x390))],
    69: [P("15:5", adv="land", air=wcfg_air(0x39c, 0x3a4))],
    70: [P("15:6", adv="land", air=wcfg_air(0x3b0, 0x3b8)), P("15:7")],
    71: [P("15:8", adv="mash", loop="15:10", loop_start=68.0, btn="CIR"), P("15:9")],
    # Nevan (every ground move ends in the guitar stance, action 99)
    80: [P("6:3")] + NEVAN_END,
    # HD Collection traces: a short strum, then the long one.
    81: [P("6:5"), P("6:9")] + NEVAN_END,
    82: [P("6:7"), P("6:10")] + NEVAN_END,
    83: [P("6:11", adv="mash", loop="6:41", loop_start=60.0), P("6:12")] + NEVAN_END,
    84: [P("6:16", adv="flag1"), P("6:17", adv="charge", tiers=(("6:36", -1.0), ("6:36", -1.0), ("6:36", -1.0)))] + NEVAN_END,
    85: [P("6:16", adv="flag1"), P("6:17", adv="charge", tiers=(("6:36", -1.0), ("6:36", -1.0), ("6:36", -1.0)))] + NEVAN_END,
    86: [P("6:21"), P("6:35", adv="timer", frames=30.0), P("6:20")] + NEVAN_END,
    87: [P("6:21"), P("6:19", adv="timer", frames=30.0), P("6:20")] + NEVAN_END,
    88: [P("6:15", adv="timer", frames=19.0, air=wcfg_air(0x43c, 0x444)), P("6:34", adv="land", air=wcfg_air(0x43c, 0x444))],
    89: [P("16:0"), ] + NEVAN_END,
    90: [P("16:7", adv="land", air=("raw", 0.0, 0.0, 0.0, -0.3))],
    91: [P("16:8", adv="land", air=("raw", 0.0, 0.0, 0.0, -0.3))],
    92: [P("16:3"), P("16:21", start=31.0), P("16:4")] + NEVAN_END,
    93: [P("16:5")] + NEVAN_END,
    94: [P("16:9", adv="mash", loop="16:22", loop_start=51.0, btn="CIR"), P("16:10")] + NEVAN_END,
    95: [P("6:25"), P("6:26", adv="timer", frames=60.0), P("6:27")],
    # Beowulf
    110: charge("7:3", "7:24", 94.0, "7:41", "7:42", 8.0),
    111: charge("7:4", "7:25", 115.0, "7:43", "7:44", 29.0),
    112: charge("7:5", "7:34", 144.0, "7:45", "7:46", 64.0),
    113: charge("7:7", "7:27", 158.0, "7:47", "7:48", 84.0, after=[P("7:8")]),
    114: charge("7:9", "7:28", 253.0, "7:49", "7:50", 154.0),
    115: [P("7:10", adv="mash", loop="7:40", loop_start=232.0), P("7:11")],
    116: [P("7:12", adv="land", flag_air=("raw", 0.0, 0.0, -8.0, -0.1)), P("7:13")],
    117: charge("7:14", "7:29", 99.0, "7:51", "7:52", 23.0),
    118: charge("7:14", "7:29", 99.0, "7:51", "7:52", 23.0, after=[P("7:15", adv="land", flag_air=vy_air(0x27c))]),
    119: [P("7:15", adv="land", flag_air=vy_air(0x27c))],
    120: [P("7:16", adv="land", air=vy_air(0x534)), P("7:17")],
    121: [P("7:30", adv="flag1"), P("7:33", adv="charge", tiers=(("7:31", -1.0), ("7:53", -1.0), ("7:54", -1.0))), P("7:55")],
    122: [P("7:30", adv="flag1"), P("7:33", adv="charge", tiers=(("7:31", -1.0), ("7:53", -1.0), ("7:54", -1.0))), P("7:55")],
    123: charge("17:0", "17:8", 96.0, "17:0", "17:0", 25.0),
    124: charge("17:1", "17:9", 108.0, "17:10", "17:11", 28.0),
    125: [P("17:2", adv="land", flag_air=wcfg_air(0x2a8, 0x2a0)), P("17:1", start=29.0)],
    126: [P("17:4", adv="mash", loop="17:4", btn="CIR", air=wcfg_air(0x4c8, 0x4c0)), P("17:5", adv="land")],
    127: [P("17:6", adv="land", air=wcfg_air(0x2c4, 0x2bc))],
    128: [P("17:3"), P("17:7", adv="land", air=vy_air(0x2b0))],
    # Guns. FUN_002688c0 (FUN_0021e620 with param_4 = 1: the upper body only). The
    # shot column is 3 of the table in combat_data.h. player+0x299f counts the shots:
    # odd ones play the motion from 0 and end at 13, even ones from 15 and end at 28
    # (the two guns); another press fires again 7 frames after a shot. combat.c does this.
    # Air, Gunslinger and Artemis fire counts are still APPROX.
    130: [P("8:6", adv="shot", frames=13.0, refire=7.0, shot=1, layered=True)],
    131: [P("8:6", adv="shot", frames=13.0, refire=7.0, shot=1, layered=True)],
    # Air shots, HD Collection traces: a kick upward each, and more presses fire again (combat.c).
    132: [P("8:32", adv="land", air=("raw", 0.0, 0.0, 11.0, -1.25), shot=1)],
    133: [P("8:32", adv="land", air=("raw", 0.0, 0.0, 11.0, -1.25), shot=1)],
    134: [P("18:0", shot=2)],
    # Rain Storm (FUN_0026b8b0): vy / gravity from the config +0x18c / +0x190 (5, -0.35) at the start, then
    # +0x1a0 / +0x1a4 (0, -0.5) in the loop; the end keeps falling on those.
    135: [P("18:15", air=("vy", 0x18C, 0x190)), P("18:16", adv="mash", loop="18:16", btn="CIR", air=("vy", 0x1A0, 0x1A4), auto=7.0, shot=1), P("18:17")],
    136: [P("8:33"), P("8:34", adv="mash", loop="8:34", btn="SQ", auto=7.0, shot=1), P("8:39")],
    # Shotgun (the motion runs to its end, FUN_0026be70)
    140: [P("9:6", shot=1, layered=True)], 141: [P("9:6", shot=1, layered=True)],
    142: [P("9:39", adv="land", air=("raw", 0.0, 0.0, 13.0, -1.25), shot=1)],
    143: [P("9:39", adv="land", air=("raw", 0.0, 0.0, 13.0, -1.25), shot=1)],
    144: [P("19:0", shot=1)],
    145: [P("19:1", adv="land", air=("raw", 0.0, 0.0, 0.0, -0.3), shot=1)],
    146: [P("19:2"), P("19:3", adv="timer", frames=20.0, shot=1), P("19:4")],
    147: [P("19:5", adv="mash", loop="19:5", btn="CIR", shot=1)],
    # Artemis
    # Fires on release: a few frames of 10:3, then 10:4 (HD Collection traces).
    150: [P("10:3", adv="timer", frames=4.0), P("10:4", shot=1)], 152: [P("10:3", adv="timer", frames=4.0), P("10:4", shot=1)],
    151: [P("10:13", adv="land", air=("raw", 0.0, 0.0, 0.0, -0.3), shot=1)], 153: [P("10:13", adv="land", air=("raw", 0.0, 0.0, 0.0, -0.3), shot=1)],
    154: [P("20:2", shot=1)],
    155: [P("20:3", adv="mash", loop="20:3", loop_start=69.0, btn="CIR", shot=1), P("20:4")],
    # Spiral / Kalina Ann (FUN_0026f480 / FUN_00270300 pick from the tables above)
    160: [P("11:7", lock="11:8", shot=1)],  # with a kick back of about 60 cm (HD Collection traces)
    161: [P("21:0", shot=1)], 162: [P("21:1", shot=1)],
    170: [P("12:7", lock="12:8", shot=1)],
    171: [P("22:2", adv="timer", frames=139.0, shot=1), P("22:3")],
    172: [P("22:0"), P("22:1", shot=1)],
    # Royalguard release. APPROX: the gauge (player+0x3c86) picks among 13 scripts;
    # with no gauge in the test room this is the level-0 release.
    190: [P("24:15"), P("24:16")], 191: [P("24:17")], 192: [P("24:40")], 193: [P("24:46")],
    194: [P("24:18", air=("raw", 0.0, 0.0, 0.0, 0.0)), P("24:19", adv="land", air=("raw", 0.0, 0.0, 0.0, -0.3))],
    195: [P("24:20", adv="land", air=("raw", 0.0, 0.0, 0.0, -0.3))],
    196: [P("24:52", adv="land", air=("raw", 0.0, 0.0, 0.0, -0.3))],
    197: [P("24:58", adv="land", air=("raw", 0.0, 0.0, 0.0, -0.3))],
}

WEAPON_NAMES = {0: "REBELLION", 1: "CERBERUS", 2: "AGNI & RUDRA", 3: "NEVAN", 4: "BEOWULF",
                5: "EBONY & IVORY", 6: "SHOTGUN", 7: "ARTEMIS", 8: "SPIRAL", 9: "KALINA ANN"}
