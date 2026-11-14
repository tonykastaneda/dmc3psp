"""PS2 (harness PCSX2): Mission 7's Vergil, from the 100% save on card 2, to check the port's timings.
python tools/ps2_vergil.py [seconds to record]
Boots fresh, loads the save, Normal Mission 7, starts and skips the cutscene. In 127 it finds the door record to
114 in RAM and points it at 144 (the Peak of Darkness, whose room script spawns Vergil), walks Dante into it,
and records the fight with Dante standing still (HP held) to analysis/ref/vergil_ps2.mp4, logging Vergil's AI
state (+0xb9d4) and action (+0xb9d7) each 0.1 s to analysis/ref/vergil_ps2.txt."""
import struct, subprocess, sys, time
sys.path.insert(0, 'tools')
import pcsx2_ref as r

SECONDS = float(sys.argv[1]) if len(sys.argv) > 1 else 90.0
DEFAULT_BUTTONS = [0x1000, 0x4000, 0x2000, 0x8000, 0x10, 0x40, 0x20, 0x80, 4, 1, 0x200, 8, 2, 0x400, 0x100]
EM035_VTABLE = 0x613580


def shot(tag):
    r.shot(f'analysis/ref/menu/vg_{tag}.png')


def press(k, wait, hold=0.12):
    r.focus(); r.press(k, hold); time.sleep(wait)


def find_words(p, words, lo=0x100000, hi=0x2000000):
    """Addresses where the u32 sequence appears (4-byte aligned)."""
    pat = b"".join(struct.pack("<I", w) for w in words)
    hits, step = [], 0x40000
    for a in range(lo, hi, step):
        blob = p.read(a, step + len(pat))
        i = blob.find(pat)
        while i >= 0:
            if i % 4 == 0 and a + i < hi:
                hits.append(a + i)
            i = blob.find(pat, i + 4)
    return hits


subprocess.run(['powershell', '-Command', 'Get-Process pcsx2-qt -ErrorAction SilentlyContinue | Stop-Process -Force'])
time.sleep(2)
r.start(); time.sleep(28)
press('cross', 6)
for _ in range(3):
    press('start', 5)
press('cross', 4); press('down', 0.8); press('cross', 4)      # Load Game, memory card slot 2
press('left', 1.0); press('cross', 2); press('left', 1.0)     # save 1 (Dante), "OK to load?" -> Yes
press('cross', 6); press('cross', 6)                          # load, then Mission Select
for _ in range(4):
    press('up', 0.35, 0.1)
for _ in range(13):
    press('left', 0.25, 0.08)
shot('1select')
press('cross', 14)                                            # Normal, Mission 7: demo 16 starts
shot('2demo')
press('start', 8)                                             # skip it
shot('3card')
press('left', 1.5); press('cross', 15)                       # Mission Start
shot('3b')
p = r.Pine()
P = 0
t_end = time.time() + 90
while not P and time.time() < t_end:
    # Dante: CPlDante's vtable (0x602650) at +0 of the player object.
    hits = find_words(p, [0x602650], 0x1000000, 0x2000000)
    P = hits[0] if hits else 0
    if not P:
        time.sleep(3)
print('Dante object', hex(P))
t_end = time.time() + 30
while time.time() < t_end and not (1000.0 < p.f32(P + 0x50) < 5000.0):
    time.sleep(1)
for i, v in enumerate(DEFAULT_BUTTONS):
    p.write16(0x6C37DA + 2 * i, v)
shot('3room127')
print('Dante at', p.f32(P + 0x50), p.f32(P + 0x54), p.f32(P + 0x58))
# The doors stay in RAM as their text (the room's data), read when one's used: 127's door to 114 becomes one to 144.
patched = 0
for a in range(0x100000, 0x2000000, 0x40000):
    blob = p.read(a, 0x40000 + 64)
    i = blob.find(b'NextRoom\t\t114\r\nNextPosId\t\t1')
    if i >= 0:
        p.write8(a + i + 11, ord('4'))           # 114 -> 144
        p.write8(a + i + 26, ord('0'))           # start 1 -> 0
        print('door text patched at', hex(a + i), p.read(a + i, 30))
        patched += 1
        break
print('patched', patched)
# Into the door: stand in its box and press O.
for _ in range(6):
    p.writef(P + 0x50, 2859.0); p.writef(P + 0x58, 1690.0)
    time.sleep(0.2)
press('circle', 3); press('circle', 3)
time.sleep(10)
press('start', 3)
shot('4after_door')
# Find Vergil: CEm035's vtable at +0 of an object.
vergil = 0
t_end = time.time() + 40
while not vergil and time.time() < t_end:
    hits = find_words(p, [EM035_VTABLE], 0x1000000, 0x2000000)
    vergil = hits[0] if hits else 0
    time.sleep(1)
print('Vergil at', hex(vergil))
rec = r.record_start('analysis/ref/vergil_ps2.mp4')
log = open('analysis/ref/vergil_ps2.txt', 'w')
t0 = time.time()
while time.time() - t0 < SECONDS:
    p.writef(P + 0x2AD4, 20000.0)
    if vergil:
        s = p.read(vergil + 0xB9D4, 4)
        pos = struct.unpack('<3f', p.read(vergil + 0x50, 12))
        dp = struct.unpack('<3f', p.read(P + 0x50, 12))
        d = ((pos[0] - dp[0]) ** 2 + (pos[2] - dp[2]) ** 2) ** 0.5
        log.write(f'{time.time() - t0:6.2f} state={s[0]:#x} prev={s[1]:#x} action={s[3]:#x} d={d:.0f} hp={p.f32(vergil + 0xb820):.0f}\n')
    time.sleep(0.1)
log.close()
r.record_stop(rec)
shot('5end')
print('done')
