"""Check player capacities, DT refill, activation and depletion without PSP rendering."""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    compiler = shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        raise RuntimeError("A host C compiler is required")
    folder = ROOT / "build/tests"
    folder.mkdir(parents=True, exist_ok=True)
    binary = folder / "hud_stats.exe"
    subprocess.run([compiler, "-O1", "-I", str(ROOT / "psp/testroom"),
                    str(ROOT / "tools/tests/hud_stats.c"),
                    str(ROOT / "psp/testroom/player.c"), "-lm", "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
