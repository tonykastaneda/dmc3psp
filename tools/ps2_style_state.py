"""PS2 (harness PCSX2): a savestate of Mission 2's street with Dante in one style, from the 100% save on card 2.
python tools/ps2_style_state.py <rows down from Swordmaster in the style list: -1 TR, 0 SM, 1 GS, 2 RG> <name>
Boots fresh, loads the save, Normal Mission 2, Customize -> Style, starts, skips the cutscene, holds HP, puts the
save's swapped buttons back to default, places Dante at the test spot, saves state slot 1 and copies it to
build/refemu/sstates/style_<name>.p2s. Screenshots of each step in analysis/ref/menu/ms_<name>_*.png."""
import glob, shutil, struct, subprocess, sys, time
sys.path.insert(0, 'tools')
import pcsx2_ref as r

rows, name = int(sys.argv[1]), sys.argv[2]
P = 0x17E0A40
SLOT1 = glob.glob('build/refemu/sstates/SLUS-21361 (*).01.p2s')
DEFAULT_BUTTONS = [0x1000, 0x4000, 0x2000, 0x8000, 0x10, 0x40, 0x20, 0x80, 4, 1, 0x200, 8, 2, 0x400, 0x100]


def shot(tag):
    r.shot(f'analysis/ref/menu/ms_{name}_{tag}.png')


def press(k, wait, hold=0.12):
    r.focus(); r.press(k, hold); time.sleep(wait)


subprocess.run(['powershell', '-Command', 'Get-Process pcsx2-qt -ErrorAction SilentlyContinue | Stop-Process -Force'])
time.sleep(2)
r.start(); time.sleep(28)
press('cross', 6)
for _ in range(3):
    press('start', 5)
shot('1title')
press('cross', 4); press('down', 0.8); press('cross', 4)      # Load Game, memory card slot 2
press('left', 1.0); press('cross', 2); press('left', 1.0)     # save 1 (Dante), "OK to load?" -> Yes
shot('2load')
press('cross', 6); press('cross', 6)                          # load, then Mission Select
for _ in range(4):
    press('up', 0.35, 0.1)
for _ in range(18):
    press('left', 0.25, 0.08)
shot('3select')
press('cross', 16)                                            # Normal, Mission 2 -> briefing (Customize)
shot('4brief')
press('cross', 5); press('cross', 4)                          # Customize -> Style page (on Swordmaster)
for _ in range(abs(rows)):
    press('up' if rows < 0 else 'down', 1.2)
shot('5style')
press('cross', 3); press('triangle', 2.5); press('triangle', 4)
press('left', 1.0)
shot('6start')
press('cross', 20); press('start', 10)                        # Mission Start, skip the cutscene
p = r.Pine()
for i, v in enumerate(DEFAULT_BUTTONS):
    p.write16(0x6C37DA + 2 * i, v)
for _ in range(12):
    p.writef(P + 0x2AD4, 5300.0); p.writef(P + 0x50, 2750.0); p.writef(P + 0x54, 30.0); p.writef(P + 0x58, 2450.0)
    time.sleep(0.25)
shot('7game')
r.focus(); time.sleep(0.2); r.key(0x70, True); time.sleep(0.08); r.key(0x70, False); time.sleep(3)
print('style byte', p.read(P + 0x3C6C, 1)[0], 'level', p.read(P + 0x3C74, 1)[0])
shutil.copy(SLOT1[0], f'build/refemu/sstates/style_{name}.p2s')
print('saved', f'build/refemu/sstates/style_{name}.p2s')
