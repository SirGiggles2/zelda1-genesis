"""Run tools/nesdata/assemble.py in this tree while recording every file it opens.

    python tools/release/trace_assemble.py --rom R --redux X --reads reads.json  (then assemble's own args)

Writes the relative paths (inside this tree) of every file opened, in first-open
order, so make_builder.py knows exactly which sources the player's browser needs.
T-419 Task 12.
"""
from __future__ import annotations

import json
import runpy
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    i = sys.argv.index("--reads")
    out = Path(sys.argv[i + 1])
    args = sys.argv[1:i] + sys.argv[i + 2:]
    seen: dict[str, None] = {}
    root = str(ROOT.resolve()).lower()

    def hook(event, payload):
        if event == "open" and isinstance(payload[0], str):
            p = str(Path(payload[0]).resolve() if not Path(payload[0]).is_absolute() else Path(payload[0]))
            if p.lower().startswith(root):
                seen.setdefault(Path(p).relative_to(ROOT.resolve()).as_posix(), None)

    sys.addaudithook(hook)
    sys.argv = [str(ROOT / "tools/nesdata/assemble.py"), *args]
    code = 0
    try:
        runpy.run_path(str(ROOT / "tools/nesdata/assemble.py"), run_name="__main__")
    except SystemExit as exc:
        code = exc.code or 0
    out.write_text(json.dumps(list(seen), indent=0), encoding="utf-8")
    return int(code)


if __name__ == "__main__":
    sys.exit(main())
