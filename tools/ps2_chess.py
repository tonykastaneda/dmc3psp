"""PS2 (harness PCSX2): Mission 18's chess board (room 401), for comparing the chessmen with the port.
python tools/ps2_chess.py [seconds to record]
Boots, loads the 100% save on card 2 with the health cheat (docs/PCSX2_CHEATS.md), Normal Mission 18, skips demo 49,
sets Dante on room 400's mirror (GATE 555, (2300, 1050, -1075)), and once the chessmen exist saves a state
(build/refemu/sstates/m18_chess.p2s). Then it records the board with Dante standing to analysis/ref/chess_ps2.mp4,
logging every chessman (CEm017 / CEm021 by vtable: EST +0x46, action +5, sub +6, place +0x50, HP +0xea4) each 0.1 s to
analysis/ref/chess_ps2.txt. Only its own PCSX2 (by PID) is closed."""
import glob, os, shutil, struct, sys, time
sys.path.insert(0, 'tools')
import pcsx2_ref as r

SECONDS = float(sys.argv[1]) if len(sys.argv) > 1 else 60.0
PLAYER = 0x17E0A40
VTABLES = (0x60c2b0, 0x60d850)   # CEm017, CEm021
DEFAULT_BUTTONS = [0x1000, 0x4000, 0x2000, 0x8000, 0x10, 0x40, 0x20, 0x80, 4, 1, 0x200, 8, 2, 0x400, 0x100]


def shot(tag):
    r.shot(f'analysis/ref/menu/chess_{tag}.png')


def press(k, wait, hold=0.12):
    r.focus(); r.press(k, hold); time.sleep(wait)


def chessmen(p):
    found = []
    for a in range(0x1000000, 0x2000000, 0x40000):
        blob = p.read(a, 0x40000)
        for vt in VTABLES:
            pat = struct.pack('<I', vt)
            i = blob.find(pat)
            while i >= 0:
                if i % 16 == 0:
                    found.append(a + i)
                i = blob.find(pat, i + 4)
    return sorted(found)


os.makedirs('analysis/ref/menu', exist_ok=True)
emu = r.start(cheats='health')
try:
    time.sleep(28)
    press('cross', 6)
    for _ in range(3):
        press('start', 5)
    shot('0title')
    press('cross', 7); shot('0aload')                             # Load Game
    press('down', 1.5); shot('0bslot2'); press('cross', 5)        # memory card slot 2
    shot('0csaves'); press('left', 1.5); press('cross', 3)        # save 1
    shot('0dok'); press('left', 1.5); press('cross', 8)           # "OK to load?" -> Yes
    shot('0eloaded'); press('cross', 8)                           # then Mission Select
    for _ in range(4):
        press('up', 0.35, 0.1)
    for _ in range(2):
        press('left', 0.25, 0.08)
    shot('1select')
    press('cross', 14)                                            # Normal, Mission 18: demo 49
    shot('2demo')
    press('start', 8)
    shot('3card')
    press('left', 1.5); press('cross', 15)                       # Mission Start
    shot('4room400')
    p = r.Pine()
    for i, v in enumerate(DEFAULT_BUTTONS):
        p.write16(0x6C37DA + 2 * i, v)
    print('Dante at', p.f32(PLAYER + 0x50), p.f32(PLAYER + 0x54), p.f32(PLAYER + 0x58))
    men = []
    t_end = time.time() + 120
    tries = [(2300.0, -1075.0, 'circle'), (2300.0, -1075.0, 'cross'), (2300.0, -1075.0, 'lup')]
    k = 0
    while not men and time.time() < t_end and k < len(tries):
        x, z, stick = tries[k]; k += 1
        for i in range(15):   # held in the mirror's middle (its centre 1050 up), pressing
            p.writef(PLAYER + 0x50, x); p.writef(PLAYER + 0x54, 1050.0); p.writef(PLAYER + 0x58, z)
            if i % 3 == 0: r.focus(); r.press(stick, 0.1)
            time.sleep(0.15)
        shot(f'5try{k}')
        time.sleep(6)
        men = chessmen(p)
        print('try', k, x, z, stick, 'Dante', round(p.f32(PLAYER + 0x50)), round(p.f32(PLAYER + 0x58)), 'chessmen', len(men))
    shot('5room401')
    print('chessmen', len(men), [hex(a) for a in men])
    if men:
        time.sleep(4)
        r.save_state(1)   # slot 1 over PINE (F1 saved the current slot, 1)
        time.sleep(4)
        slot = sorted(glob.glob('build/refemu/sstates/SLUS-21361 (*).0*.p2s'), key=os.path.getmtime)[-1]
        shutil.copy(slot, 'build/refemu/sstates/m18_chess.p2s')
        print('saved state from', slot)
        men = chessmen(p)
        rec = r.record_start('analysis/ref/chess_ps2.mp4')
        log = open('analysis/ref/chess_ps2.txt', 'w')
        t0 = time.time()
        while time.time() - t0 < SECONDS:
            dp = struct.unpack('<3f', p.read(PLAYER + 0x50, 12))
            row = [f'{time.time() - t0:6.2f} dante {dp[0]:.0f} {dp[1]:.0f} {dp[2]:.0f}']
            for a in men:
                h = p.read(a, 8)
                est = p.u8(a + 0x46)
                pos = struct.unpack('<3f', p.read(a + 0x50, 12))
                row.append(f'{a:x} est={est} act={h[5]} sub={h[6]} at {pos[0]:.0f} {pos[1]:.0f} {pos[2]:.0f} '
                           f'hp={p.f32(a + 0xea4):.0f} cmd={p.u8(a + 0x488)}')
            log.write(' | '.join(row) + '\n')
            time.sleep(0.1)
        log.close()
        r.record_stop(rec)
        shot('6end')
finally:
    emu.kill()
    print('closed pcsx2 pid', emu.pid)
