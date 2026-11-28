"""Drive a private PCSX2 copy (build/refemu) to measure the real game.

PINE reads and writes the PS2's memory and loads / saves states. The pad is a virtual Xbox 360 pad (ViGEmBus,
the `vgamepad` package) that the private copy reads through its XInput source, which works without focus; shots
come from the window itself (PrintWindow with PW_RENDERFULLCONTENT), so it can sit covered or on another display.
Nothing here brings PCSX2 to the front: it starts without activation and moves to a second display if there is
one. The old keyboard path (real key events to the focused window) is kept: set PCSX2_REF_KEYBOARD=1 or pass
keyboard=True to setup() / start(). Only build/refemu's own settings are written; the private copy is muted.
Used as a module or from the command line:

    python tools/pcsx2_ref.py setup              # write the private copy's settings
    python tools/pcsx2_ref.py start              # boot the game (cheats off)
    python tools/pcsx2_ref.py start --cheats all # ... with health, dt, damage (or a comma list of them)
    python tools/pcsx2_ref.py shot name.png      # capture the window
    python tools/pcsx2_ref.py press cross 0.1    # press a pad button
    python tools/pcsx2_ref.py state slot         # load a save state slot
"""
import ctypes
import ctypes.wintypes as wt
import os
import re
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EMU = ROOT / "build" / "refemu"
EXE = EMU / "pcsx2-qt.exe"
ISO = next(ROOT.glob("*.iso"))
FFMPEG = r"C:\ffmpeg\bin\ffmpeg.exe"
PINE_PORT = 28011

# Pad button -> keyboard key name in PCSX2, and Windows virtual key code.
KEYS = {
    "up": ("Up", 0x26), "down": ("Down", 0x28), "left": ("Left", 0x25), "right": ("Right", 0x27),
    "lup": ("W", 0x57), "ldown": ("S", 0x53), "lleft": ("A", 0x41), "lright": ("D", 0x44),
    "cross": ("K", 0x4B), "circle": ("L", 0x4C), "square": ("J", 0x4A), "triangle": ("I", 0x49),
    "l1": ("Q", 0x51), "r1": ("E", 0x45), "l2": ("R", 0x52), "r2": ("T", 0x54),
    "start": ("Return", 0x0D), "select": ("Backspace", 0x08), "l3": ("Z", 0x5A), "r3": ("X", 0x58),
}
PAD_NAMES = {
    "Up": "up", "Down": "down", "Left": "left", "Right": "right",
    "LUp": "lup", "LDown": "ldown", "LLeft": "lleft", "LRight": "lright",
    "Cross": "cross", "Circle": "circle", "Square": "square", "Triangle": "triangle",
    "L1": "l1", "R1": "r1", "L2": "l2", "R2": "r2", "Start": "start", "Select": "select", "L3": "l3", "R3": "r3",
}
HOTKEYS = {"LoadStateFromSlot": "F3", "SaveStateToSlot": "F1", "NextSaveStateSlot": "F2", "TogglePause": "Space"}
KEYBOARD = os.environ.get("PCSX2_REF_KEYBOARD") == "1"

# Pad button -> (the virtual 360 pad's control, PCSX2's XInput source binding name).
XPAD = {
    "up": ("DPAD_UP", "DPadUp"), "down": ("DPAD_DOWN", "DPadDown"), "left": ("DPAD_LEFT", "DPadLeft"),
    "right": ("DPAD_RIGHT", "DPadRight"), "cross": ("A", "A"), "circle": ("B", "B"), "square": ("X", "X"),
    "triangle": ("Y", "Y"), "l1": ("LEFT_SHOULDER", "LeftShoulder"), "r1": ("RIGHT_SHOULDER", "RightShoulder"),
    "start": ("START", "Start"), "select": ("BACK", "Back"), "l3": ("LEFT_THUMB", "LeftStick"),
    "r3": ("RIGHT_THUMB", "RightStick"), "l2": ("LT", "+LeftTrigger"), "r2": ("RT", "+RightTrigger"),
    "lup": ("LY+", "-LeftY"), "ldown": ("LY-", "+LeftY"), "lleft": ("LX-", "-LeftX"), "lright": ("LX+", "+LeftX"),
}

user32 = ctypes.windll.user32
user32.SetProcessDPIAware()
_xinput = None


def _xinput_slots():
    """XInput user indices (0-3) with a pad connected now."""
    global _xinput
    if _xinput is None:
        for name in ("xinput1_4", "xinput1_3", "xinput9_1_0"):
            try:
                _xinput = getattr(ctypes.windll, name)
                break
            except OSError:
                continue
    state = (ctypes.c_byte * 16)()
    return [i for i in range(4) if _xinput.XInputGetState(i, ctypes.byref(state)) == 0]


_pad = None
_held = set()


def pad():
    """The virtual pad, made on first use and kept for this process's life (PCSX2 sees it as a hot-plugged pad)."""
    global _pad
    if _pad is None:
        import vgamepad
        _pad = vgamepad.VX360Gamepad()
        _pad.reset()
        _pad.update()
        time.sleep(0.6)   # let PCSX2's XInput source see it arrive
    return _pad


def pad_slot():
    """The XInput slot (user index) the virtual pad takes: ViGEm gives it the first free one, so the user's own
    pads keep theirs, and a later process's pad lands in the same slot while those don't change."""
    before = set(_xinput_slots()) if _pad is None else None
    index = pad().get_index()   # 1-based (ViGEm's user index + 1)
    if before is not None:
        new = sorted(set(_xinput_slots()) - before)
        if new:
            return new[0]
    return index - 1 if 1 <= index <= 4 else 0


def setup(keyboard=KEYBOARD):
    """Rewrites only the private copy's ini: Pad1 on the virtual pad's XInput slot (or the keyboard), XInput on,
    PINE on, sound off (the [SPU2/Output] keys this PCSX2 2.9 writes: OutputMuted, OutputVolume, FastForwardVolume)."""
    path = EMU / "inis" / "PCSX2.ini"
    lines = path.read_text(encoding="utf-8").splitlines()
    out, section = [], ""
    slot = 0 if keyboard else pad_slot()
    replace = {
        "UI": {"StartFullscreen": "false", "ConfirmShutdown": "false", "PauseOnFocusLoss": "false",
               "StartPaused": "false", "SetupWizardIncomplete": "false", "HideMouseCursor": "false"},
        "EmuCore": {"EnablePINE": "true", "PINESlot": str(PINE_PORT), "EnableCheats": "false"},
        "EmuCore/GS": {"upscale_multiplier": "2", "OsdShowFPS": "false", "OsdShowSpeed": "false"},
        "SPU2/Output": {"OutputMuted": "true", "OutputVolume": "0", "FastForwardVolume": "0"},
        "InputSources": {"XInput": "true", "SDL": "true", "SDLRawInput": "true"},
    }
    seen = set()

    def flush(sec):
        for key_name, value in replace.get(sec, {}).items():
            if (sec, key_name) not in seen:
                out.append(f"{key_name} = {value}")
                seen.add((sec, key_name))

    for line in lines:
        m = re.match(r"\[(.+)\]", line)
        if m:
            # Keys the section lacks go at its end, before the blank lines that close it.
            blanks = 0
            while out and out[-1] == "":
                out.pop()
                blanks += 1
            flush(section)
            out += [""] * blanks
            section = m.group(1)
        k = re.match(r"(\w+) = ", line)
        if k and section in replace and k.group(1) in replace[section]:
            line = f"{k.group(1)} = {replace[section][k.group(1)]}"
            seen.add((section, k.group(1)))
        if k and section == "Pad1" and k.group(1) in PAD_NAMES:
            name = PAD_NAMES[k.group(1)]
            line = (f"{k.group(1)} = Keyboard/{KEYS[name][0]}" if keyboard
                    else f"{k.group(1)} = XInput-{slot}/{XPAD[name][1]}")
            seen.add(("Pad1", k.group(1)))
        if k and section == "Hotkeys" and k.group(1) in ("TogglePause",):
            line = "TogglePause = Keyboard/F11"
        if k and section == "Hotkeys" and any(v.endswith(f"Keyboard/{KEYS[n][0]}") for v in [line] for n in KEYS):
            continue   # a hotkey on one of the pad keys would fire too
        out.append(line)
    flush(section)
    path.write_text("\n".join(out) + "\n", encoding="utf-8")
    return slot


# Reference-capture cheats (docs/PCSX2_CHEATS.md). The codes live in the local, gitignored
# build/refemu/cheats/SLUS-21361_25FC361B.pnach; each is a labelled group turned on by name in the game's
# own settings file, so the global ini (and everyone else's runs) keeps cheats off.
CHEATS = {"health": "Infinite Health", "dt": "Infinite Devil Trigger", "damage": "One-Hit Kills"}
GAME_INI = EMU / "gamesettings" / "SLUS-21361_25FC361B.ini"


def set_cheats(names=()):
    """names: keys of CHEATS ('health', 'dt', 'damage'), or () for none. Rewrites the game's settings file."""
    if isinstance(names, str):
        names = [n for n in names.split(",") if n]
    names = list(CHEATS) if names == ["all"] else names
    unknown = [n for n in names if n not in CHEATS]
    if unknown:
        raise ValueError(f"unknown cheats {unknown}; choose from {list(CHEATS)} or all")
    lines = ["[EmuCore]", f"EnableCheats = {'true' if names else 'false'}", "", "[Cheats]"]
    lines += [f"Enable = {CHEATS[n]}" for n in names]
    GAME_INI.parent.mkdir(exist_ok=True)
    GAME_INI.write_text("\n".join(lines) + "\n", encoding="utf-8")


def start(cheats=(), keyboard=KEYBOARD):
    """cheats: a list or 'health,dt,damage' / 'all' for this run. Off unless asked for, so older scripts
    that call start() never inherit someone else's cheats. The window opens without taking focus (and the
    foreground window is handed back if Qt takes it anyway), then moves to a second display if there is one."""
    set_cheats(cheats)
    if not keyboard:
        setup(keyboard=False)   # the pad's slot, the mute, background-safe settings
    before = user32.GetForegroundWindow()
    info = subprocess.STARTUPINFO()
    info.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    info.wShowWindow = 4   # SW_SHOWNOACTIVATE
    proc = subprocess.Popen([str(EXE), "-fastboot", "--", str(ISO)], cwd=str(EMU), startupinfo=info)
    hwnd = None
    for _ in range(100):
        time.sleep(0.2)
        hwnd = find_window()
        if hwnd:
            break
    if hwnd:
        to_second_display(hwnd)
        _restore_foreground(before, hwnd)
    return proc


def _monitors():
    rects = []

    def cb(hmon, hdc, rect, data):
        r = rect.contents
        rects.append((r.left, r.top, r.right, r.bottom))
        return True
    proto = ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HMONITOR, wt.HDC, ctypes.POINTER(wt.RECT), wt.LPARAM)
    user32.EnumDisplayMonitors(None, None, proto(cb), 0)
    return rects


def to_second_display(hwnd):
    """Moves the window onto a display that isn't the primary one (left alone with one display), without
    activating it."""
    mons = [m for m in _monitors() if not (m[0] == 0 and m[1] == 0)]
    if not mons:
        return False
    left, top, right, bottom = mons[0]
    rect = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    w, h = min(rect.right - rect.left, right - left), min(rect.bottom - rect.top, bottom - top)
    SWP_NOACTIVATE, SWP_NOZORDER = 0x10, 0x4
    user32.SetWindowPos(hwnd, None, left + 40, top + 40, w, h, SWP_NOACTIVATE | SWP_NOZORDER)
    return True


def _restore_foreground(before, emu):
    """If the emulator's window took the foreground as it opened, give it back to what had it."""
    for _ in range(10):
        if user32.GetForegroundWindow() != emu or not before:
            return
        user32.SetForegroundWindow(before)
        time.sleep(0.1)


def find_window():
    found = []
    pid_want = None
    result = subprocess.run(["powershell", "-NoProfile", "-Command",
                             f"(Get-Process pcsx2-qt | Where-Object {{ $_.Path -eq '{EXE}' }}).Id"],
                            capture_output=True, text=True)
    if result.stdout.strip():
        pid_want = {int(x) for x in result.stdout.split()}

    def cb(hwnd, _):
        pid = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid_want and pid.value in pid_want and user32.IsWindowVisible(hwnd):
            buf = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(hwnd, buf, 256)
            rect = wt.RECT()
            user32.GetClientRect(hwnd, ctypes.byref(rect))
            found.append((rect.right * rect.bottom, hwnd, buf.value))
        return True
    user32.EnumWindows(ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)(cb), 0)
    if not found:
        return None
    return max(found)[1]


def _scan(vk):
    return user32.MapVirtualKeyW(vk, 0)


EXTENDED = {0x25, 0x26, 0x27, 0x28}


def key(vk, down):
    """Real keyboard input: PCSX2 reads keys from the focused window."""
    KEYEVENTF_KEYUP, KEYEVENTF_EXTENDEDKEY = 2, 1
    flags = (0 if down else KEYEVENTF_KEYUP) | (KEYEVENTF_EXTENDEDKEY if vk in EXTENDED else 0)
    user32.keybd_event(vk, _scan(vk), flags, 0)


def focus(hwnd=None, force=False):
    """The window's handle. Only the keyboard path (or force) brings it to the front: the pad needs no focus."""
    hwnd = hwnd or find_window()
    if not (KEYBOARD or force):
        return hwnd
    # Windows only lets the foreground process hand over focus; a tap of Alt allows it.
    user32.keybd_event(0x12, 0, 0, 0)
    user32.keybd_event(0x12, 0, 2, 0)
    user32.ShowWindow(hwnd, 9)
    user32.SetForegroundWindow(hwnd)
    return hwnd


def _pad_apply():
    import vgamepad
    p = pad()
    p.reset()
    lx = (1.0 if "lright" in _held else 0.0) - (1.0 if "lleft" in _held else 0.0)
    ly = (1.0 if "lup" in _held else 0.0) - (1.0 if "ldown" in _held else 0.0)
    for n in _held:
        control = XPAD[n][0]
        if control == "LT":
            p.left_trigger(255)
        elif control == "RT":
            p.right_trigger(255)
        elif not control.startswith("L"):
            p.press_button(getattr(vgamepad.XUSB_BUTTON, "XUSB_GAMEPAD_" + control))
        elif control in ("LEFT_SHOULDER", "LEFT_THUMB"):
            p.press_button(getattr(vgamepad.XUSB_BUTTON, "XUSB_GAMEPAD_" + control))
    p.left_joystick_float(lx, ly)
    p.update()


def hold(*names):
    if KEYBOARD:
        for n in names:
            key(KEYS[n][1], True)
        return
    _held.update(names)
    _pad_apply()


def release(*names):
    if KEYBOARD:
        for n in names:
            key(KEYS[n][1], False)
        return
    _held.difference_update(names)
    _pad_apply()


def press(name, seconds=0.08):
    hold(name)
    time.sleep(seconds)
    release(name)


def stick(x, y):
    """The left stick at (x, y), each -1..1 (y up), on the virtual pad. (0, 0) lets it go."""
    p = pad()
    p.left_joystick_float(x, y)
    p.update()


def client_rect(hwnd):
    rect = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    pt = wt.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(pt))
    return pt.x, pt.y, rect.right & ~1, rect.bottom & ~1


gdi32 = ctypes.windll.gdi32


def grab(hwnd=None, crop=None):
    """The window's client area as (width, height, BGRA bytes), drawn by the window itself (PrintWindow,
    PW_RENDERFULLCONTENT: DWM's copy, so covered or off-screen-display windows still come out). crop: (x, y, w,
    h) inside the client area."""
    hwnd = hwnd or find_window()
    rect = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    ww, wh = rect.right - rect.left, rect.bottom - rect.top
    hdc = user32.GetWindowDC(hwnd)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, ww, wh)
    old = gdi32.SelectObject(mem, bmp)
    user32.PrintWindow(hwnd, mem, 2)   # PW_RENDERFULLCONTENT
    cx, cy, cw, ch = client_rect(hwnd)
    ox, oy = cx - rect.left, cy - rect.top
    if crop:
        ox, oy, cw, ch = ox + crop[0], oy + crop[1], crop[2] & ~1, crop[3] & ~1

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG), ("biPlanes", wt.WORD),
                    ("biBitCount", wt.WORD), ("biCompression", wt.DWORD), ("biSizeImage", wt.DWORD),
                    ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD),
                    ("biClrImportant", wt.DWORD)]
    out_dc = gdi32.CreateCompatibleDC(hdc)
    out_bmp = gdi32.CreateCompatibleBitmap(hdc, cw, ch)
    old2 = gdi32.SelectObject(out_dc, out_bmp)
    gdi32.BitBlt(out_dc, 0, 0, cw, ch, mem, ox, oy, 0x00CC0020)
    info = BITMAPINFOHEADER(ctypes.sizeof(BITMAPINFOHEADER), cw, -ch, 1, 32, 0, 0, 0, 0, 0, 0)
    buf = ctypes.create_string_buffer(cw * ch * 4)
    gdi32.GetDIBits(out_dc, out_bmp, 0, ch, buf, ctypes.byref(info), 0)
    gdi32.SelectObject(out_dc, old2); gdi32.DeleteObject(out_bmp); gdi32.DeleteDC(out_dc)
    gdi32.SelectObject(mem, old); gdi32.DeleteObject(bmp); gdi32.DeleteDC(mem)
    user32.ReleaseDC(hwnd, hdc)
    return cw, ch, buf.raw


def _write_png(path, w, h, bgra, vf=None):
    cmd = [FFMPEG, "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "bgra", "-s", f"{w}x{h}", "-i", "-",
           "-frames:v", "1"]
    if vf:
        cmd += ["-vf", vf]
    subprocess.run(cmd + [str(path)], input=bgra, check=True)


def shot(path, crop=None, vf=None):
    """A still of the window (no focus needed)."""
    if KEYBOARD:
        hwnd = focus()
        time.sleep(0.1)
    w, h, data = grab(crop=crop)
    _write_png(path, w, h, data, vf)


class _Recorder:
    def __init__(self, path, fps, crop=None, seconds=None):
        import threading
        self.hwnd, self.crop, self.frames_left = find_window(), crop, int(seconds * fps) if seconds else None
        self.w, self.h, _ = grab(self.hwnd, crop)
        self.fps, self.stop_flag = fps, False
        self.proc = subprocess.Popen([FFMPEG, "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "bgra",
                                      "-s", f"{self.w}x{self.h}", "-framerate", str(fps), "-i", "-",
                                      "-c:v", "libx264", "-preset", "veryfast", "-crf", "23", "-pix_fmt", "yuv420p",
                                      "-movflags", "+faststart", str(path)], stdin=subprocess.PIPE)
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _run(self):
        step, due = 1.0 / self.fps, time.perf_counter()
        frame = None
        while not self.stop_flag:
            w, h, data = grab(self.hwnd, self.crop)
            if (w, h) == (self.w, self.h):
                frame = data
            now = time.perf_counter()
            while due <= now and frame is not None:   # constant rate: repeat a frame if a grab ran long
                self.proc.stdin.write(frame)
                due += step
                if self.frames_left is not None:
                    self.frames_left -= 1
                    if self.frames_left <= 0:
                        self.stop_flag = True
                        break
            time.sleep(max(0.0, due - time.perf_counter()))

    def communicate(self, *_args, **_kw):
        self.stop_flag = True
        self.wait()

    def wait(self, timeout=None):
        """Until a timed recording has its frames (or now, after communicate); then the file is finished."""
        self.thread.join(timeout=timeout)
        if not self.proc.stdin.closed:
            self.proc.stdin.close()
        return self.proc.wait(timeout=60)

    def poll(self):
        return None if self.thread.is_alive() else self.wait()


def record_start(path, fps=30, crop=None, seconds=None):
    """A video of the window from its own drawing (no focus, works covered). crop: (x, y, w, h) in the client area;
    seconds: it stops by itself (then wait()), else stop it with record_stop."""
    return _Recorder(path, fps, crop, seconds)


def record_stop(proc):
    proc.communicate(b"q", timeout=30)


def load_state(slot):
    """Loads a save state slot over PINE (no hotkey, no focus)."""
    Pine().load_state(slot)


def save_state(slot):
    Pine().save_state(slot)


class Pine:
    """PCSX2's IPC. Each message: u32 total size, then opcode + arguments."""
    def __init__(self):
        self.sock = socket.create_connection(("127.0.0.1", PINE_PORT), timeout=10)

    def _exchange(self, body):
        self.sock.sendall(struct.pack("<I", len(body) + 4) + body)
        head = self._recv(4)
        size = struct.unpack("<I", head)[0]
        data = self._recv(size - 4)
        if data[0] != 0:
            raise IOError("PINE failed")
        return data[1:]

    def _recv(self, n):
        buf = b""
        while len(buf) < n:
            part = self.sock.recv(n - len(buf))
            if not part:
                raise IOError("PINE closed")
            buf += part
        return buf

    def read(self, addr, size):
        """Any length, batched in 8 byte reads."""
        out = bytearray()
        for start in range(0, size, 4096):
            chunk = min(4096, size - start)
            words = (chunk + 7) // 8
            body = b"".join(b"\x03" + struct.pack("<I", addr + start + i * 8) for i in range(words))
            out += self._exchange(body)[:chunk]
        return bytes(out)

    def u8(self, a): return self.read(a, 1)[0]
    def u16(self, a): return struct.unpack("<H", self.read(a, 2))[0]
    def u32(self, a): return struct.unpack("<I", self.read(a, 4))[0]
    def f32(self, a): return struct.unpack("<f", self.read(a, 4))[0]
    def write8(self, a, v): self._exchange(b"\x04" + struct.pack("<IB", a, v))
    def write16(self, a, v): self._exchange(b"\x05" + struct.pack("<IH", a, v))
    def write32(self, a, v): self._exchange(b"\x06" + struct.pack("<II", a, v))
    def writef(self, a, v): self._exchange(b"\x06" + struct.pack("<If", a, v))

    def save_state(self, slot): self._exchange(b"\x09" + struct.pack("<B", slot))   # MsgSaveState
    def load_state(self, slot): self._exchange(b"\x0A" + struct.pack("<B", slot))   # MsgLoadState

    def title(self):
        data = self._exchange(b"\x0B")
        return data[4:4 + struct.unpack("<I", data[:4])[0]].decode("ascii", "replace")


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "setup":
        setup()
    elif cmd == "start":
        # start [--cheats health,dt,damage | --cheats all]: without the flag, cheats are turned off for the run.
        flag = sys.argv[3] if len(sys.argv) > 3 and sys.argv[2] == "--cheats" else ""
        start(flag)
    elif cmd == "cheats":
        # cheats on [health,dt,damage] (default all) | cheats off: for a PCSX2 started by hand (start() resets them).
        set_cheats((sys.argv[3] if len(sys.argv) > 3 else "all") if sys.argv[2] == "on" else "")
    elif cmd == "shot":
        shot(sys.argv[2])
    elif cmd == "press":
        focus()
        time.sleep(0.1)
        press(sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else 0.08)
    elif cmd == "state":
        load_state(int(sys.argv[2]))
    elif cmd == "seq":
        # seq left,cross:2,wait:5 out.png: buttons with a pause after each, then a small shot.
        focus()
        time.sleep(0.2)
        for token in sys.argv[2].split(","):
            name, _, wait = token.partition(":")
            if name != "wait":
                press(name, 0.12)
            time.sleep(float(wait) if wait else 0.4)
        _, _, w, h = client_rect(find_window())
        shot(sys.argv[3], crop=(0, 150, w, h - 195), vf="scale=512:-2")
    elif cmd == "title":
        print(Pine().title())
