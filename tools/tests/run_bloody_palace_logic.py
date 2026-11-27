"""Run after build_testroom generates bloody_palace_runtime.h from the owner's ISO."""
from pathlib import Path
import shutil
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
out = ROOT / 'build' / 'bloody_palace' / 'logic.exe'
out.parent.mkdir(parents=True, exist_ok=True)
compiler = shutil.which('gcc') or shutil.which('clang')
if not compiler:
    raise SystemExit('A host C compiler is required')
subprocess.run([compiler, '-std=c99', '-Wall', '-Wextra', '-Werror',
                '-I'+str(ROOT/'psp/testroom'), '-I'+str(ROOT/'build/testroom/generated'),
                str(ROOT/'psp/testroom/bloody_palace.c'), str(Path(__file__).with_name('bloody_palace_logic.c')),
                '-o', str(out)], check=True)
subprocess.run([str(out)], check=True)
record = out.with_name('record.exe')
subprocess.run([compiler, '-std=c99', '-Wall', '-Wextra', '-Werror',
                '-I'+str(ROOT/'psp/testroom'), '-I'+str(ROOT/'build/testroom/generated'),
                str(ROOT/'psp/testroom/bloody_palace.c'), str(Path(__file__).with_name('bloody_palace_record.c')),
                '-o', str(record)], check=True)
with tempfile.TemporaryDirectory(dir=out.parent) as directory:
    subprocess.run([str(record), directory], check=True)
slots = out.with_name('attack_slots_logic.exe')
subprocess.run([compiler, '-std=c99', '-Wall', '-Wextra', '-Werror',
                '-I'+str(ROOT/'psp/testroom'), str(Path(__file__).with_name('attack_slots_logic.c')),
                '-o', str(slots)], check=True)
subprocess.run([str(slots)], check=True)
