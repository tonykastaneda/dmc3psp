"""AutoLink, the XMB plugin that restarts PSPLink (psp/autolink). Everything here goes over PSPLink.

    python tools/autolink.py build      # compile it to build/autolink/autolink.prx
    python tools/autolink.py install    # copy it to the card, add it to PLUGINS.TXT, backing that up
    python tools/autolink.py arm [n]    # let it launch PSPLink up to n times (default 5)
    python tools/autolink.py disarm     # the PSP behaves normally again
    python tools/autolink.py remove     # take it off the card and out of PLUGINS.TXT
    python tools/autolink.py status
    python tools/autolink.py soak [n]   # n launches in a row: does the link survive or come back?

By hand, with the card in a PC: delete SEPLUGINS/AUTOLINK.PRX.
"""
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import psp_bench as bench

ROOT = bench.ROOT
SOURCE = ROOT / "psp" / "autolink"
OUT = ROOT / "build" / "autolink"
BIN = ROOT / ".tools" / "pspdev" / "bin"
SDK = ROOT / ".tools" / "pspdev" / "psp" / "sdk"
CARD_DIR = "ms0:/SEPLUGINS"
PLUGIN_LINE = "VSH, ms0:/SEPLUGINS/autolink.prx, on"
BACKUP = ROOT / "analysis" / "testroom" / "PLUGINS_before_autolink.TXT"


def tool(name, *args):
    env = dict(os.environ, PATH=f"{BIN};{os.environ['PATH']}")  # cc1 needs the toolchain's DLLs
    subprocess.run([str(BIN / name), *map(str, args)], check=True, cwd=str(OUT), env=env)


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    exports = subprocess.run([str(BIN / "psp-build-exports.exe"), "-b", str(SOURCE / "exports.exp")],
                             check=True, capture_output=True, text=True).stdout
    (OUT / "exports.c").write_text(exports)
    for name, sources in (("autolink", ("main.c", "stub.S")), ("toxmb", ("toxmb.c", "toxmb_stub.S"))):
        elf = OUT / f"{name}.elf"
        tool("psp-gcc.exe", "-O2", "-G0", "-Wall", "-Wextra", "-D_PSP_FW_VERSION=660", f"-I{SDK / 'include'}", f"-L{SDK / 'lib'}",
             f"-specs={SDK / 'lib' / 'prxspecs'}", f"-Wl,-q,-T{SDK / 'lib' / 'linkfile.prx'}", "-nostartfiles", "-nostdlib",
             "-Wl,-zmax-page-size=128", *[SOURCE / x for x in sources], OUT / "exports.c",
             "-lpspctrl_driver", "-lpspmodinfo", "-lpspsdk", "-lpspkernel", "-lc", "-lgcc", "-o", elf)
        tool("psp-fixup-imports.exe", elf)
        tool("psp-prxgen.exe", elf, OUT / f"{name}.prx")
        print("built", OUT / f"{name}.prx", (OUT / f"{name}.prx").stat().st_size, "bytes")


def shell(command, seconds=8):
    log = bench.BUILD / "pspsh_out.txt"
    with open(log, "wb") as f:
        s = subprocess.Popen([str(bench.TOOLS / "pspsh.exe"), "-e", command], cwd=str(bench.BUILD),
                             stdin=subprocess.DEVNULL, stdout=f, stderr=f)
        end = time.time() + seconds
        while time.time() < end and s.poll() is None:
            time.sleep(0.2)
        if s.poll() is None:
            s.kill()
            s.wait()
    return log.read_bytes().decode("ascii", "replace")


def need_link():
    if not bench.shell_ready():
        raise SystemExit("PSPLink isn't answering: open it on the PSP.")


def to_card(local, name):
    """host0: is build/testroom, so anything going to the card passes through there."""
    temp = bench.BUILD / local.name
    if temp.resolve() != local.resolve():
        shutil.copyfile(local, temp)
    answer = shell(f"cp host0:/{local.name} {CARD_DIR}/{name}")
    if temp.resolve() != local.resolve():
        temp.unlink()
    return answer


def read_list():
    answer = shell(f"cp {CARD_DIR}/PLUGINS.TXT host0:/PLUGINS_now.TXT")
    path = bench.BUILD / "PLUGINS_now.TXT"
    if not path.exists():
        raise SystemExit("couldn't read PLUGINS.TXT: " + answer)
    data = path.read_bytes()
    path.unlink()
    return data


def write_list(data):
    temp = bench.BUILD / "PLUGINS_new.TXT"
    temp.write_bytes(data)
    answer = to_card(temp, "PLUGINS.TXT")
    temp.unlink(missing_ok=True)
    print(answer.strip())
    if read_list() != data:
        raise SystemExit("PLUGINS.TXT didn't read back the same: check it on the card.")


def install():
    need_link()
    print(to_card(OUT / "autolink.prx", "autolink.prx").strip())
    data = read_list()
    if not BACKUP.exists():
        BACKUP.write_bytes(data)
        print("backed up the plugin list to", BACKUP)
    if b"autolink.prx" in data.lower():
        print("already in PLUGINS.TXT")
        return
    newline = b"\r\n" if b"\r\n" in data else b"\n"
    if not data.endswith(newline):
        data += newline
    write_list(data + newline + b"#AutoLink" + newline + PLUGIN_LINE.encode() + newline)
    print("added to PLUGINS.TXT")


def arm(count=5):
    need_link()
    temp = bench.BUILD / "AUTOLINK_ON.TMP"
    temp.write_text(str(count))
    print(to_card(temp, "AUTOLINK.ON").strip())
    temp.unlink(missing_ok=True)


def disarm():
    need_link()
    print(shell(f"rm {CARD_DIR}/AUTOLINK.ON").strip())


def remove():
    need_link()
    data = read_list()
    kept, skip = [], False
    lines = data.split(b"\n")
    for line in lines:
        text = line.strip().lower()
        if text == b"#autolink" or b"autolink.prx" in text:
            continue
        kept.append(line)
    write_list(b"\n".join(kept).rstrip(b"\r\n") + (b"\r\n" if b"\r\n" in data else b"\n"))
    print(shell(f"rm {CARD_DIR}/AUTOLINK.ON").strip())
    print(shell(f"rm {CARD_DIR}/autolink.prx").strip())


def status():
    need_link()
    listing = shell(f"ls {CARD_DIR}")
    print("on the card:", [line.split()[-1] for line in listing.splitlines() if "autolink" in line.lower()])
    print("in PLUGINS.TXT:", b"autolink.prx" in read_list().lower())
    answer = shell(f"cp {CARD_DIR}/AUTOLINK.ON host0:/AUTOLINK_now.TMP")
    temp = bench.BUILD / "AUTOLINK_now.TMP"
    print("launches left:", temp.read_text() if temp.exists() else "not armed")
    temp.unlink(missing_ok=True)


def soak(runs):
    """Launch the game over and over, and see whether the link survives or comes back."""
    results = []
    script = ["frames 100", "turbo 1", "fps 30", "ramp 60", "rampfrom 1", "rampto 1", "enemy 3000"]
    for run in range(1, runs + 1):
        if bench.wait_for_link() is None:
            print(f"run {run}: PSPLink never came back; stopping", flush=True)
            results.append("lost")
            break
        arm(5)
        began = time.time()
        try:
            bench.run_scene(script, 60)
            outcome = "finished"
        except (RuntimeError, TimeoutError) as error:
            outcome = f"failed: {error}"[:80]
        took = bench.wait_for_link()
        left = "?"
        if took is not None:
            shell(f"cp {CARD_DIR}/AUTOLINK.ON host0:/AUTOLINK_now.TMP")
            temp = bench.BUILD / "AUTOLINK_now.TMP"
            left = temp.read_text() if temp.exists() else "none"
            temp.unlink(missing_ok=True)
        recovered = left == "4"
        state = "LOST" if took is None else "recovered through the XMB" if recovered else "kept the link"
        print(f"run {run}: {outcome}, {state}, {time.time() - began:.0f} s, launches left {left}", flush=True)
        results.append(state)
        if took is None:
            break
    if results[-1:] not in (["LOST"], ["lost"]):
        disarm()
    print("summary:", {x: results.count(x) for x in set(results)})


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "status"
    if what == "soak":
        soak(int(sys.argv[2]) if len(sys.argv) > 2 else 10)
    elif what == "arm":
        arm(int(sys.argv[2]) if len(sys.argv) > 2 else 5)
    else:
        {"build": build, "install": install, "disarm": disarm, "remove": remove, "status": status}[what]()
