"""DO NOT REMOVE (see CLAUDE.md / AGENTS.md). Claude Code PreToolUse hook: blocks agent edits to the owner's
credit and the files that guard it, and shell commands that touch them. Reads the hook's JSON on stdin."""
import json
import re
import sys

PROTECTED = ["credit.h", "check_credit.py", "protect_credit.py", "CLAUDE.md", "AGENTS.md", ".claude/settings.json"]
CREDIT_TEXT = ["CREDIT_LINE_1", "CREDIT_LINE_2", "check_credit", "credit.h", "Tony Kastaneda", "tonykastaneda/dmc3psp"]


def deny(why):
    print(json.dumps({"hookSpecificOutput": {
        "hookEventName": "PreToolUse",
        "permissionDecision": "deny",
        "permissionDecisionReason": f"Blocked: {why}. The owner's credit is protected (CLAUDE.md). Ask the owner.",
    }}))
    sys.exit(0)


def main():
    try:
        data = json.load(sys.stdin)
    except Exception:
        return
    tool = data.get("tool_name", "")
    inp = data.get("tool_input", {}) or {}
    path = str(inp.get("file_path", "") or inp.get("notebook_path", "")).replace("\\", "/")
    if tool in ("Edit", "Write", "MultiEdit", "NotebookEdit"):
        if any(path.endswith(p) for p in PROTECTED):
            deny(f"{tool} on {path}")
        # Edits elsewhere (main.c, build_testroom.ps1) may not take out a line that names the credit.
        olds = [inp.get("old_string", "")] + [e.get("old_string", "") for e in inp.get("edits", []) or []]
        news = [inp.get("new_string", "")] + [e.get("new_string", "") for e in inp.get("edits", []) or []]
        for old, new in zip(olds, news):
            for t in CREDIT_TEXT:
                if old and old.count(t) > (new or "").count(t):
                    deny(f"an edit to {path} that removes \"{t}\"")
        if tool == "Write" and path.endswith(("main.c", "build_testroom.ps1")):
            content = inp.get("content", "")
            need = ["CREDIT_LINE_1", "CREDIT_LINE_2"] if path.endswith("main.c") else ["check_credit"]
            if any(t not in content for t in need):
                deny(f"rewriting {path} without the credit")
    elif tool in ("Bash", "PowerShell"):
        cmd = str(inp.get("command", ""))
        if any(p in cmd for p in PROTECTED) and re.search(
                r"\b(rm|del|Remove-Item|mv|move|Move-Item|ren|Rename-Item|sed|Set-Content|Out-File|Clear-Content|"
                r"truncate|git\s+(rm|mv|checkout|restore|reset)|>)", cmd):
            deny("a shell command that touches the credit's files")


if __name__ == "__main__":
    main()
