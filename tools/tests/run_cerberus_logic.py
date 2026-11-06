"""Exercise Cerberus C logic with exported PS2 MOTs; rendering is stubbed."""
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]

def main():
    folder = ROOT / 'build/tests/cerberus_native'
    folder.mkdir(parents=True, exist_ok=True)
    source = (ROOT / 'psp/testroom/cerberus_boss.c').read_text()
    constants = sorted(set(re.findall(r'\bGU_[A-Z_0-9]+\b', source)))
    functions = sorted(set(re.findall(r'\b(sce\w+)\(', source)))
    (folder / 'pspgu.h').write_text('#pragma once\n' +
        ''.join(f'#define {name} {i}\n' for i, name in enumerate(constants)) +
        ''.join(f'{"void *" if name == "sceGuGetMemory" else "void "}{name}();\n' for name in functions))
    (folder / 'pspgum.h').write_text('#pragma once\ntypedef struct {float x,y,z;} ScePspFVector3;\n')
    (folder / 'pspkernel.h').write_text('#pragma once\n')
    (folder / 'psputils.h').write_text('#pragma once\nvoid sceKernelDcacheWritebackRange();\n')
    (folder / 'render_stubs.c').write_text(''.join(
        f'void *{name}() {{return 0;}}\n' if name == 'sceGuGetMemory' else f'void {name}() {{}}\n'
        for name in functions))
    compiler = shutil.which('clang') or shutil.which('gcc')
    if not compiler:
        raise RuntimeError('A host C compiler is required')
    binary = folder / 'cerberus_logic.exe'
    subprocess.run([compiler, '-O1', '-Wno-deprecated-non-prototype',
        '-I', str(folder), '-I', str(ROOT / 'build/testroom/generated'),
        '-I', str(ROOT / 'psp/testroom'), str(ROOT / 'tools/tests/cerberus_logic.c'),
        str(ROOT / 'psp/testroom/motion.c'), str(folder / 'render_stubs.c'),
        '-lm', '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, cwd=ROOT)

if __name__ == '__main__':
    main()
