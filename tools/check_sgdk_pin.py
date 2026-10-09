#!/usr/bin/env python3
"""check_sgdk_pin.py — verify SGDK submodule SHA matches the pin.

Per debate 003 Rule SGDK-2. Source of truth: tools/sgdk_pin.json.
A git checkout is checked by commit SHA. The release package carries SGDK
as plain files (no .git), checked by content_sha256: sha256 over the sorted
"<path>\0<file sha256>\n" lines of every file under the submodule.
Exit 1 on drift unless --accept-sgdk-bump is passed.
    --write-content-hash   record content_sha256 from the pinned checkout
"""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
PIN_PATH = REPO / "tools" / "sgdk_pin.json"


def content_sha256(root: Path) -> str:
    h = hashlib.sha256()
    for p in sorted(root.rglob("*"), key=lambda q: q.relative_to(root).as_posix()):
        rel = p.relative_to(root)
        if p.is_file() and ".git" not in rel.parts:
            h.update(rel.as_posix().encode() + b"\0"
                     + hashlib.sha256(p.read_bytes()).hexdigest().encode() + b"\n")
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument(
        "--accept-sgdk-bump",
        action="store_true",
        help="Skip drift check (use only when intentionally bumping the pin).",
    )
    ap.add_argument("--write-content-hash", action="store_true",
                    help="record content_sha256 of the pinned checkout in the pin file")
    args = ap.parse_args()

    if not PIN_PATH.exists():
        print(f"FAIL: missing {PIN_PATH}", file=sys.stderr)
        return 1

    pin = json.loads(PIN_PATH.read_text())
    pinned_sha = pin["pinned_sha"]
    submodule_path = REPO / pin["submodule_path"]

    if not submodule_path.exists():
        print(f"FAIL: SGDK submodule not present at {submodule_path}", file=sys.stderr)
        return 1

    if not (submodule_path / ".git").exists():
        # Release package: no git metadata, compare file contents.
        digest = content_sha256(submodule_path)
        if digest != pin.get("content_sha256"):
            msg = (f"SGDK files (content {digest[:8]}) != pinned {pin['pinned_tag']} "
                   f"(content {pin.get('content_sha256', 'unrecorded')[:8]}).")
            if args.accept_sgdk_bump:
                print(f"WARN (--accept-sgdk-bump): {msg}", file=sys.stderr)
                return 0
            print(f"FAIL: {msg} Extract the whole package again.", file=sys.stderr)
            return 1
        print(f"OK: SGDK files match {pin['pinned_tag']} (content {digest[:8]}).")
        return 0

    try:
        head = subprocess.check_output(
            ["git", "-C", str(submodule_path), "rev-parse", "HEAD"],
            text=True,
        ).strip()
    except subprocess.CalledProcessError as e:
        print(f"FAIL: git rev-parse failed: {e}", file=sys.stderr)
        return 1

    if head != pinned_sha:
        msg = (
            f"SGDK submodule HEAD {head[:8]} != pinned {pinned_sha[:8]} "
            f"({pin['pinned_tag']}, {pin['pinned_date']})."
        )
        if args.accept_sgdk_bump:
            print(f"WARN (--accept-sgdk-bump): {msg}", file=sys.stderr)
            print(
                "  Remember to update tools/sgdk_pin.json + docs/sgdk_audit.md "
                "+ regen parity baselines + regen Final.md checksum in the same commit.",
                file=sys.stderr,
            )
            return 0
        print(f"FAIL: {msg}", file=sys.stderr)
        print(
            "  To intentionally bump: pass --accept-sgdk-bump after updating "
            "tools/sgdk_pin.json and docs/sgdk_audit.md.",
            file=sys.stderr,
        )
        return 1

    if args.write_content_hash:
        pin["content_sha256"] = content_sha256(submodule_path)
        PIN_PATH.write_text(json.dumps(pin, indent=2) + "\n", encoding="utf-8", newline="\n")
        print(f"wrote content_sha256 {pin['content_sha256'][:8]} to {PIN_PATH.name}")
    print(f"OK: SGDK pinned to {pinned_sha[:8]} ({pin['pinned_tag']}).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
