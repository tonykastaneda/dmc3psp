"""Host checks for native chess scheduling rules; generated tables stay local."""
from pathlib import Path
import shutil
import subprocess
ROOT=Path(__file__).resolve().parents[2]
out=ROOT/'build/chess_logic';out.mkdir(parents=True,exist_ok=True)
cc=shutil.which('clang') or shutil.which('gcc')
if not cc:raise RuntimeError('A host C compiler is required')
binary=out/'chess_logic.exe'
subprocess.run([cc,'-O1','-I',str(ROOT/'psp/testroom'),'-I',str(ROOT/'build/testroom/generated'),str(ROOT/'tools/tests/chess_logic.c'),str(ROOT/'psp/testroom/chess.c'),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)
