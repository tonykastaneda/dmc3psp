"""Run tests on the real PSP through PSPLINK and read the results back.

The PSP must be plugged in with PSPLINK open.

    python tools/psp_bench.py play              # just start the game, for a person to play
    python tools/psp_bench.py ramp              # add dummies until the frame rate has gone
    python tools/psp_bench.py ramp full         # with every dummy posed every frame
    python tools/psp_bench.py ramp fire         # with Dante firing, for the effects' cost
    python tools/psp_bench.py ramp qs           # in Quicksilver
    python tools/psp_bench.py ramp devil        # in Devil Trigger

Results: analysis/testroom/bench_<name>.txt
"""
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "testroom"
TOOLS = ROOT / ".tools" / "psplink"
OUT = ROOT / "analysis" / "testroom"
# What costs what: standing dummies, with one thing changed.
TESTS = {
    "base": ["attack 0", "rampto 4"],
    "nostage": ["attack 0", "rampto 8", "nostage 1"],
    "notex": ["attack 0", "rampto 4", "notex 1"],
    "nolight": ["attack 0", "rampto 4", "nolight 1"],
    "far": ["attack 0", "rampto 4", "ring 900"],
}
STEP_FRAMES = 120   # 4 seconds at 30 FPS: 2 to settle, 2 measured


def hostfs_running():
    out = subprocess.run(["powershell", "-NoProfile", "-Command", "(Get-Process usbhostfs_pc -ErrorAction SilentlyContinue).Id"],
                         capture_output=True, text=True).stdout
    return bool(out.strip())


def start_hostfs():
    """After a run resets PSPLINK its shell channel only repeats "Resetting psplink", until this restarts."""
    subprocess.run(["powershell", "-NoProfile", "-Command", "Stop-Process -Name usbhostfs_pc,pspsh -ErrorAction SilentlyContinue"], capture_output=True)
    time.sleep(1)
    subprocess.Popen([str(TOOLS / "usbhostfs_pc.exe")], cwd=str(BUILD), stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(5)


def shell_answer(command, seconds=5):
    """A short command's output, capped: the shell can flood after a reset."""
    log = BUILD / "pspsh_out.txt"
    with open(log, "wb") as f:
        shell = subprocess.Popen([str(TOOLS / "pspsh.exe"), "-e", command], cwd=str(BUILD), stdin=subprocess.DEVNULL, stdout=f, stderr=f)
        end = time.time() + seconds
        while time.time() < end and shell.poll() is None:
            time.sleep(0.2)
        if shell.poll() is None:
            shell.kill()
            shell.wait()
    return log.read_bytes()[:400].decode("ascii", "replace")


def shell_ready():
    if not hostfs_running():
        start_hostfs()
    answer = shell_answer("pwd")
    if "host0:/" in answer and "Resetting" not in answer:
        return True
    start_hostfs()
    answer = shell_answer("pwd")
    return "host0:/" in answer and "Resetting" not in answer


def run_scene(script_lines, timeout):
    """Launch a scripted scene and wait for it. Raises rather than waits if it didn't boot."""
    done = BUILD / "FOCUS_DONE.TXT"
    memory = BUILD / "MEMORY.TXT"
    done.unlink(missing_ok=True)
    (BUILD / "BENCH_OUT.TXT").unlink(missing_ok=True)
    (BUILD / "FOCUS.TXT").write_text("cmp\n")
    (BUILD / "CMP_SCRIPT.TXT").write_text("".join(line + "\n" for line in script_lines))
    before = memory.stat().st_mtime if memory.exists() else 0.0
    if not shell_ready():
        (BUILD / "FOCUS.TXT").unlink(missing_ok=True)
        raise RuntimeError("PSPLINK's shell isn't answering: restart PSPLINK on the PSP")
    log = BUILD / "pspsh_run.txt"
    with open(log, "wb") as f:
        shell = subprocess.Popen([str(TOOLS / "pspsh.exe"), "-e", "./testroom.prx"], cwd=str(BUILD), stdin=subprocess.DEVNULL, stdout=f, stderr=f)
        try:
            start = time.time()
            booted = False
            while not done.exists():
                if not booted and memory.exists() and memory.stat().st_mtime > before:
                    booted = True
                    print("booted on the PSP", round(time.time() - start, 1), "s", flush=True)
                if not booted and time.time() - start > 45:
                    raise RuntimeError("the game didn't boot: " + log.read_bytes()[:300].decode("ascii", "replace"))
                if time.time() - start > timeout:
                    raise TimeoutError("the PSP didn't finish: it may have crashed")
                time.sleep(1)
            time.sleep(3)   # let it reset back to PSPLINK
        finally:
            if shell.poll() is None:
                shell.kill()
                shell.wait()
            (BUILD / "FOCUS.TXT").unlink(missing_ok=True)


def ramp(variant):
    lines = ["frames 100", "turbo 1", "fps 30", f"ramp {STEP_FRAMES}", "enemy 3000"]
    if variant == "full":
        lines.append("anim 0")
    elif variant == "fire":
        lines.append("fire 15")
    elif variant == "qs":
        lines.append("qs 1")
    elif variant == "devil":
        lines.append("devil 1")
    elif variant == "stand":
        lines.append("attack 0")
    elif variant in TESTS:
        lines += TESTS[variant]
    run_scene(lines, 32 * 5 + 60)
    rows = [list(map(int, line.split())) for line in (BUILD / "BENCH_OUT.TXT").read_text().splitlines()]
    text = [f"Dummy ramp, {variant or 'attacking, AUTO animation'}, on the PSP, Turbo",
            "dummies   FPS  late  worst ms | update  skin  draw   gpu (ms) | cpu+gpu"]
    best = 0
    for d, frames, late, gap, elapsed, update, skin, draw, gpu, _ in rows:
        fps = frames * 1000000.0 / elapsed if elapsed else 0.0
        if fps >= 29.5 and late <= frames * 0.02:
            best = d
        text.append(f"{d:7d} {fps:5.1f} {late:5d} {gap / 1000:9.1f} | {update / 1000:6.2f} {skin / 1000:5.2f} {draw / 1000:5.2f} {gpu / 1000:5.2f}      | {(update + skin + draw + gpu) / 1000:6.1f}")
    text.append(f"Holds 30 FPS up to {best} dummies.")
    report = "\n".join(text)
    print(report)
    (OUT / f"bench_{variant or 'auto'}.txt").write_text(report + "\n")


def play():
    """Start the normal game on the PSP from the build folder; returns once it has booted."""
    memory = BUILD / "MEMORY.TXT"
    (BUILD / "FOCUS.TXT").unlink(missing_ok=True)
    before = memory.stat().st_mtime if memory.exists() else 0.0
    if not shell_ready():
        raise RuntimeError("PSPLINK's shell isn't answering: restart PSPLINK on the PSP")
    subprocess.Popen([str(TOOLS / "pspsh.exe"), "-e", "./testroom.prx"], cwd=str(BUILD), stdin=subprocess.DEVNULL,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    start = time.time()
    while not (memory.exists() and memory.stat().st_mtime > before):
        if time.time() - start > 30:
            raise RuntimeError("the game didn't boot")
        time.sleep(0.5)
    print("booted on the PSP")


if __name__ == "__main__":
    if sys.argv[1] == "play":
        play()
    if sys.argv[1] == "ramp":
        ramp(sys.argv[2] if len(sys.argv) > 2 else "")
