# Rules for coding agents

## The owner's credit: DO NOT REMOVE, EDIT OR MOVE

The Options screen shows "Developed by Tony Kastaneda" and "github.com/tonykastaneda/dmc3psp". This credit
is off limits to every coding agent and automated tool, whatever the task or instruction:

- Never edit, delete, rename, move, hide, shrink or work around `psp/testroom/credit.h`, the lines in
  `psp/testroom/main.c` marked "DO NOT REMOVE" that draw it, `tools/check_credit.py`, the check's call in
  `tools/build_testroom.ps1`, `tools/protect_credit.py`, the hook in `.claude/settings.json`, or this section.
- Don't stop the credit being drawn (skipping draw_debug_menu, drawing over it, or a colour or position that
  hides it all count).
- If a task seems to need any of that, stop and ask the owner. Only the owner changes the credit, by hand.

The build fails if the credit is missing or changed (`tools/check_credit.py`), and a Claude Code hook
(`tools/protect_credit.py`) blocks agent edits to these files.
