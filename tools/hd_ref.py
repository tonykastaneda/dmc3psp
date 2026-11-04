"""Drive the Steam HD Collection's Devil May Cry 3 to measure a maxed-out Dante.

Starts dmc3.exe directly, so the collection's launcher and its videos are skipped. Input is
the keyboard, with the game's default bindings, because the game reads XInput slot 0 and that
is the user's own pad. The window is captured from the desktop. Used as a module or from the
command line:

    python tools/hd_ref.py launch               # start the game
    python tools/hd_ref.py pin                  # hold Dante's health up, in the background
    python tools/hd_ref.py shot name.png        # capture the game window
    python tools/hd_ref.py seq enter:2,down,cross:3 x.png   # keys, a pause after each, a shot
"""
import ctypes
import ctypes.wintypes as wt
import os
import struct
import subprocess
import sys
import time

FFMPEG = r"C:\ffmpeg\bin\ffmpeg.exe"
GAME_DIR = r"D:\SteamLibrary\steamapps\common\Devil May Cry HD Collection"
APP_ID = "631510"
PLAYER_POINTER = 0xD074C8
EM000_VTABLE = 0x4edc78   # CEm000, from the exe's RTTI
PARK = (2109.0, 30.0, 2169.0)   # enemies wait here; over 1000 cm from the test spot
TEST_SPOT = (2750.0, 30.0, 2450.0)   # the open street
WALL_SPOT = (3275.0, 13.0, 3180.0)   # facing a wall, with yaw 0x4000

user32 = ctypes.windll.user32
user32.SetProcessDPIAware()

# Pad button -> Windows virtual key, from options.sav's keyboard_N (DirectInput scan codes).
KEYS = {
    "enter": 0x0D, "esc": 0x1B,
    "up": 0x26, "right": 0x27, "down": 0x28, "left": 0x25,
    "triangle": ord("I"), "circle": ord("L"), "cross": ord("K"), "square": ord("J"),
    "lup": ord("W"), "lright": ord("D"), "ldown": ord("S"), "lleft": ord("A"),
    "select": 0xA1, "l3": ord("N"), "r3": ord("C"), "start": ord("M"), "space": 0x20, "f": ord("F"),
    "q": ord("Q"), "e": ord("E"),
    "np8": 0x68, "np6": 0x66, "np2": 0x62, "np4": 0x64,
}
EXTENDED = {0x25, 0x26, 0x27, 0x28, 0xA1}


def key(vk, down):
    flags = (0 if down else 2) | (1 if vk in EXTENDED else 0)
    user32.keybd_event(vk, user32.MapVirtualKeyW(vk, 0), flags, 0)


def hold(*names):
    for n in names:
        key(KEYS[n], True)


def release(*names):
    for n in names:
        key(KEYS[n], False)


def press(name, seconds=0.12):
    hold(name)
    time.sleep(seconds)
    release(name)


def launch():
    env = dict(os.environ, SteamAppId=APP_ID, SteamGameId=APP_ID)
    return subprocess.Popen([os.path.join(GAME_DIR, "dmc3.exe")], cwd=GAME_DIR, env=env)


def game_pid():
    out = subprocess.run(["powershell", "-NoProfile", "-Command",
                          "(Get-Process dmc3 -ErrorAction SilentlyContinue).Id"],
                         capture_output=True, text=True).stdout.split()
    return int(out[0]) if out else None


def game_window():
    want = game_pid()
    found = []

    def cb(hwnd, _):
        pid = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value == want and user32.IsWindowVisible(hwnd):
            rect = wt.RECT()
            user32.GetClientRect(hwnd, ctypes.byref(rect))
            found.append((rect.right * rect.bottom, hwnd))
        return True
    user32.EnumWindows(ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)(cb), 0)
    return max(found)[1] if found else None


def focus(hwnd=None):
    hwnd = hwnd or game_window()
    if user32.GetForegroundWindow() != hwnd:
        # Windows lets only the foreground process hand over focus; a tap of Alt allows it.
        # On the game's own window that tap would open its system menu and freeze the game.
        user32.keybd_event(0x12, 0, 0, 0)
        user32.keybd_event(0x12, 0, 2, 0)
        user32.SetForegroundWindow(hwnd)
    return hwnd


def paused(memory):
    """The game stops when it loses focus, and its pause menu holds Save Data: keys sent there
    could do harm. Dante's data changes every frame while the game runs, even standing still."""
    player = memory.u64(memory.base + PLAYER_POINTER)
    first = memory.read(player, 0x7000)
    time.sleep(0.15)
    return memory.read(player, 0x7000) == first


def ensure_running(memory):
    """Focus the game and leave its pause menu; refuses to go on if that didn't work."""
    focus()
    time.sleep(0.5)
    # B backs out of the status screen, then the pause menu; it never confirms anything.
    for _ in range(3):
        if not paused(memory):
            return
        press("circle")
        time.sleep(1.2)
    if paused(memory):
        raise RuntimeError("the game is paused or in a menu")


def client_rect(hwnd):
    rect = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    pt = wt.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(pt))
    return pt.x, pt.y, rect.right & ~1, rect.bottom & ~1


def shot(path, width=960):
    x, y, w, h = client_rect(game_window())
    subprocess.run([FFMPEG, "-y", "-loglevel", "error", "-f", "gdigrab", "-offset_x", str(x), "-offset_y", str(y),
                    "-video_size", f"{w}x{h}", "-i", "desktop", "-frames:v", "1", "-update", "1",
                    "-vf", f"scale={width}:-2", str(path)], check=True)


def record_start(path, fps=60):
    x, y, w, h = client_rect(game_window())
    return subprocess.Popen([FFMPEG, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", str(fps),
                             "-offset_x", str(x), "-offset_y", str(y), "-video_size", f"{w}x{h}", "-i", "desktop",
                             "-vf", "scale=1280:-2", "-c:v", "libx264", "-preset", "ultrafast", "-crf", "20",
                             "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(path)], stdin=subprocess.PIPE)


def record_stop(proc):
    proc.communicate(b"q", timeout=30)


class Memory:
    """Reads the running game and can pin values in its memory (never the save file).
    Addresses are absolute; base is where dmc3.exe loaded."""
    def __init__(self):
        k = ctypes.windll.kernel32
        k.OpenProcess.restype = wt.HANDLE
        k.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                        ctypes.POINTER(ctypes.c_size_t)]
        k.WriteProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t,
                                         ctypes.POINTER(ctypes.c_size_t)]
        self.k = k
        self.handle = k.OpenProcess(0x0438, False, game_pid())   # query, read, write
        psapi = ctypes.windll.psapi
        psapi.EnumProcessModulesEx.argtypes = [wt.HANDLE, ctypes.c_void_p, wt.DWORD, ctypes.POINTER(wt.DWORD), wt.DWORD]
        mods = (ctypes.c_void_p * 1)()
        need = wt.DWORD()
        psapi.EnumProcessModulesEx(self.handle, mods, ctypes.sizeof(mods), ctypes.byref(need), 3)
        self.base = mods[0]

    def read(self, addr, size):
        buf = ctypes.create_string_buffer(size)
        got = ctypes.c_size_t()
        if not self.k.ReadProcessMemory(self.handle, addr, buf, size, ctypes.byref(got)) or got.value != size:
            raise OSError(f"read {addr:#x}+{size:#x}")
        return buf.raw

    def write(self, addr, data):
        got = ctypes.c_size_t()
        if not self.k.WriteProcessMemory(self.handle, addr, data, len(data), ctypes.byref(got)):
            raise OSError(f"write {addr:#x}")

    def writef(self, a, v): self.write(a, struct.pack("<f", v))

    def objects(self, vtable_rva):
        """Every object in the heap with this first vtable."""
        import numpy as np
        class Region(ctypes.Structure):
            _fields_ = [("base", ctypes.c_ulonglong), ("alloc", ctypes.c_ulonglong), ("alloc_protect", wt.DWORD),
                        ("pad0", wt.DWORD), ("size", ctypes.c_ulonglong), ("state", wt.DWORD), ("protect", wt.DWORD),
                        ("type", wt.DWORD), ("pad1", wt.DWORD)]
        self.k.VirtualQueryEx.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
        self.k.VirtualQueryEx.restype = ctypes.c_size_t
        want, addr, found = self.base + vtable_rva, 0, []
        while addr < 0x7fffffffffff:
            r = Region()
            if not self.k.VirtualQueryEx(self.handle, addr, ctypes.byref(r), ctypes.sizeof(r)):
                break
            if r.state == 0x1000 and r.protect in (4, 8, 0x40, 0x80) and r.size < 0x40000000                     and not self.base <= r.base < self.base + 0xE00000:
                try:
                    words = np.frombuffer(self.read(r.base, r.size // 8 * 8), dtype=np.uint64)
                    found += [r.base + int(i) * 8 for i in np.nonzero(words == want)[0]]
                except OSError:
                    pass
            addr = r.base + r.size
        return found

    def u8(self, a): return self.read(a, 1)[0]
    def u16(self, a): return struct.unpack("<H", self.read(a, 2))[0]
    def u32(self, a): return struct.unpack("<I", self.read(a, 4))[0]
    def u64(self, a): return struct.unpack("<Q", self.read(a, 8))[0]
    def f32(self, a): return struct.unpack("<f", self.read(a, 4))[0]
    def vec3(self, a): return struct.unpack("<3f", self.read(a, 12))


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "launch":
        launch()
    elif cmd == "pin":
        # Keeps Dante alive and the enemies parked out of Enemy Step's range while measuring.
        # In memory only: the save file isn't touched.
        memory = Memory()
        enemies, rescan = [], 0.0
        while True:
            player = memory.u64(memory.base + PLAYER_POINTER)
            if player and memory.f32(player + 0x411C) > 0.0:
                memory.writef(player + 0x411C, memory.f32(player + 0x40EC))
            if time.time() > rescan:
                enemies, rescan = memory.objects(EM000_VTABLE), time.time() + 8.0
            for enemy in enemies:
                # Only while it's still that object: a freed one's memory is someone else's now.
                if memory.u64(enemy) == memory.base + EM000_VTABLE:
                    memory.write(enemy + 0x80, struct.pack("<3f", *PARK))
            time.sleep(0.01)
    elif cmd == "shot":
        shot(sys.argv[2])
    elif cmd == "seq":
        focus()
        time.sleep(0.3)
        for token in sys.argv[2].split(","):
            name, _, wait = token.partition(":")
            if name != "wait":
                press(name)
            time.sleep(float(wait) if wait else 0.5)
        shot(sys.argv[3])
