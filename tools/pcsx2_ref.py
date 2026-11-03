"""Drive a private PCSX2 copy (build/refemu) to measure the real game.

PINE reads and writes the PS2's memory; keys go in as window messages, so the user's
own PCSX2 setup is never touched. Used as a module or from the command line:

    python tools/pcsx2_ref.py setup              # write the private copy's settings
    python tools/pcsx2_ref.py start              # boot the game
    python tools/pcsx2_ref.py shot name.png      # capture the window
    python tools/pcsx2_ref.py press cross 0.1    # press a pad button
    python tools/pcsx2_ref.py state slot         # load a save state slot
"""
import ctypes
import ctypes.wintypes as wt
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

user32 = ctypes.windll.user32
user32.SetProcessDPIAware()


def setup():
    """Rewrites only the private copy's ini."""
    path = EMU / "inis" / "PCSX2.ini"
    lines = path.read_text(encoding="utf-8").splitlines()
    out, section = [], ""
    replace = {
        "UI": {"StartFullscreen": "false", "ConfirmShutdown": "false", "PauseOnFocusLoss": "false",
               "StartPaused": "false", "SetupWizardIncomplete": "false"},
        "EmuCore": {"EnablePINE": "true", "PINESlot": str(PINE_PORT), "EnableCheats": "false"},
        "EmuCore/GS": {"upscale_multiplier": "2", "OsdShowFPS": "false", "OsdShowSpeed": "false"},
    }
    seen = set()
    for line in lines:
        m = re.match(r"\[(.+)\]", line)
        if m:
            section = m.group(1)
        k = re.match(r"(\w+) = ", line)
        if k and section in replace and k.group(1) in replace[section]:
            line = f"{k.group(1)} = {replace[section][k.group(1)]}"
            seen.add((section, k.group(1)))
        if k and section == "Pad1" and k.group(1) in PAD_NAMES:
            line = f"{k.group(1)} = Keyboard/{KEYS[PAD_NAMES[k.group(1)]][0]}"
            seen.add(("Pad1", k.group(1)))
        if k and section == "Hotkeys" and k.group(1) in ("TogglePause",):
            line = "TogglePause = Keyboard/F11"
        if k and section == "Hotkeys" and any(v.endswith(f"Keyboard/{KEYS[n][0]}") for v in [line] for n in KEYS):
            continue   # a hotkey on one of the pad keys would fire too
        out.append(line)
    path.write_text("\n".join(out) + "\n", encoding="utf-8")


def start():
    return subprocess.Popen([str(EXE), "-fastboot", "--", str(ISO)], cwd=str(EMU))


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


def focus(hwnd=None):
    hwnd = hwnd or find_window()
    # Windows only lets the foreground process hand over focus; a tap of Alt allows it.
    user32.keybd_event(0x12, 0, 0, 0)
    user32.keybd_event(0x12, 0, 2, 0)
    user32.ShowWindow(hwnd, 9)
    user32.SetForegroundWindow(hwnd)
    return hwnd


def hold(*names):
    for n in names:
        key(KEYS[n][1], True)


def release(*names):
    for n in names:
        key(KEYS[n][1], False)


def press(name, seconds=0.08):
    hold(name)
    time.sleep(seconds)
    release(name)


def client_rect(hwnd):
    rect = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    pt = wt.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(pt))
    return pt.x, pt.y, rect.right & ~1, rect.bottom & ~1


def shot(path):
    hwnd = focus()
    time.sleep(0.1)
    x, y, w, h = client_rect(hwnd)
    subprocess.run([FFMPEG, "-y", "-loglevel", "error", "-f", "gdigrab", "-offset_x", str(x), "-offset_y", str(y),
                    "-video_size", f"{w}x{h}", "-i", "desktop", "-frames:v", "1", str(path)], check=True)


def record_start(path, fps=30):
    x, y, w, h = client_rect(find_window())
    return subprocess.Popen([FFMPEG, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", str(fps),
                             "-offset_x", str(x), "-offset_y", str(y), "-video_size", f"{w}x{h}", "-i", "desktop",
                             "-c:v", "libx264", "-preset", "veryfast", "-crf", "23", "-pix_fmt", "yuv420p",
                             "-movflags", "+faststart", str(path)], stdin=subprocess.PIPE)


def record_stop(proc):
    proc.communicate(b"q", timeout=30)


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

    def title(self):
        data = self._exchange(b"\x0B")
        return data[4:4 + struct.unpack("<I", data[:4])[0]].decode("ascii", "replace")


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "setup":
        setup()
    elif cmd == "start":
        start()
    elif cmd == "shot":
        shot(sys.argv[2])
    elif cmd == "press":
        focus()
        time.sleep(0.1)
        press(sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else 0.08)
    elif cmd == "seq":
        # seq left,cross:2,wait:5 out.png: buttons with a pause after each, then a small shot.
        focus()
        time.sleep(0.2)
        for token in sys.argv[2].split(","):
            name, _, wait = token.partition(":")
            if name != "wait":
                press(name, 0.12)
            time.sleep(float(wait) if wait else 0.4)
        hwnd = find_window()
        x, y, w, h = client_rect(hwnd)
        subprocess.run([FFMPEG, "-y", "-loglevel", "error", "-f", "gdigrab", "-offset_x", str(x), "-offset_y", str(y + 150),
                        "-video_size", f"{w}x{h - 195}", "-i", "desktop", "-frames:v", "1", "-vf", "scale=512:-2", sys.argv[3]], check=True)
    elif cmd == "title":
        print(Pine().title())
