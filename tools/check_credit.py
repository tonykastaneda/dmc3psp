"""DO NOT REMOVE OR WEAKEN THIS CHECK (see CLAUDE.md / AGENTS.md).

Fails the build unless the project owner's credit is intact and on screen:
- psp/testroom/credit.h holds both credit lines exactly;
- main.c includes credit.h and draws CREDIT_LINE_1 and CREDIT_LINE_2 in draw_debug_menu (the Options screen).
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LINES = {
    "CREDIT_LINE_1": "Developed by Tony Kastaneda",
    "CREDIT_LINE_2": "github.com/tonykastaneda/dmc3psp",
}


def fail(why):
    print(f"CREDIT CHECK FAILED: {why}. The credit must not be removed or changed (CLAUDE.md / AGENTS.md).")
    sys.exit(1)


def main():
    header = ROOT / "psp" / "testroom" / "credit.h"
    main_c = ROOT / "psp" / "testroom" / "main.c"
    if not header.exists():
        fail("psp/testroom/credit.h is missing")
    text = header.read_text(encoding="utf-8")
    if "DO NOT REMOVE, EDIT OR MOVE THIS CREDIT." not in text:
        fail("credit.h lost its DO NOT REMOVE notice")
    for name, value in LINES.items():
        if not re.search(rf'#define\s+{name}\s+"{re.escape(value)}"', text):
            fail(f"{name} in credit.h isn't \"{value}\"")
    src = main_c.read_text(encoding="utf-8", errors="replace")
    if '#include "credit.h"' not in src:
        fail("main.c doesn't include credit.h")
    m = re.search(r"static void draw_debug_menu\(int viewer\) \{(.*?)\n\}", src, re.S)
    if not m:
        fail("draw_debug_menu (the Options screen) isn't in main.c")
    body = m.group(1)
    for name in LINES:
        if not re.search(rf"draw_text\([^;]*\b{name}\b", body):
            fail(f"draw_debug_menu doesn't draw {name}")
    print("credit check ok")


if __name__ == "__main__":
    main()
