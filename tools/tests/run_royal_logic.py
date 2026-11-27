"""Host checks for the Damned Queen and King brains (psp/testroom/queen.c, king.c, royal.c)."""
from pathlib import Path
import shutil
import subprocess
ROOT = Path(__file__).resolve().parents[2]
out = ROOT / 'build/royal_logic'
out.mkdir(parents=True, exist_ok=True)
cc = shutil.which('clang') or shutil.which('gcc')
if not cc:
    raise RuntimeError('A host C compiler is required')
binary = out / 'royal_logic.exe'
src = ROOT / 'psp/testroom'
subprocess.run([cc, '-O1', '-Wall', '-I', str(src), str(ROOT / 'tools/tests/royal_logic.c'), str(src / 'queen.c'),
                str(src / 'king.c'), str(src / 'royal.c'), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
