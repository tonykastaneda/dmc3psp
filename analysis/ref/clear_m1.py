"""Clears a fight in the reference game: health locked, mashing attacks while moving."""
import random
import struct
import sys
import time
sys.path.insert(0, 'tools')
import pcsx2_ref as r
p = r.Pine()
PL = 0x17e0a40
r.focus(); time.sleep(0.3)
r.key(0x72, True); time.sleep(0.05); r.key(0x72, False); time.sleep(1.5)
p.writef(PL + 0x2ad4, 5300.0)
limit = float(sys.argv[1]) if len(sys.argv) > 1 else 300
t = time.time()
off_since = None
dirs = ['lup', 'lright', 'ldown', 'lleft']
while time.time() - t < limit:
    p.writef(PL + 0x2ad4, 5300.0)
    ctl = p.u8(PL + 0x28ac)
    if ctl:
        off_since = off_since or time.time()
        if time.time() - off_since > 6:
            print('control off', round(time.time() - t, 1), 'pos', struct.unpack('<3f', p.read(PL + 0x50, 12)))
            break
        time.sleep(0.2)
        continue
    off_since = None
    d = random.choice(dirs)
    r.hold(d)
    for _ in range(5):
        r.press('triangle', 0.05)
        time.sleep(0.08)
        p.writef(PL + 0x2ad4, 5300.0)
    r.release(d)
r.shot('analysis/ref/m2.png')
print('done', round(time.time() - t, 1))
