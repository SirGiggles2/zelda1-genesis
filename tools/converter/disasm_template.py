"""Ship the Zelda disassembly without Nintendo's bytes; rebuild it from the user's ROM.

The extractors read reference/aldonunez/Z_0*.asm (the aldonunez disassembly,
annotated by this project). Its numeric literals in code operands and data
directives are the game's bytes. `strip` replaces every literal whose value
it can prove sits at a known PRG offset with a hole and records the offset;
labels, mnemonics, symbolic operands, comments and anything it cannot prove
stay as text. `restore` fills the holes from the user's ROM and writes the
.asm files back, byte-identical (checked by `strip` before it writes).

    python tools/converter/disasm_template.py strip --rom <zelda.nes>    (developer; needs ca65)
    python tools/converter/disasm_template.py restore --rom <zelda.nes>  (build step)
    python tools/converter/disasm_template.py check --rom <zelda.nes>    (restore == committed)

How strip proves an offset: ca65 assembles the annotated files (the
listing gives every source line's emitted bytes in order); each segment's
byte stream is aligned to its PRG bank (difflib, wildcards for relocated
bytes); a literal becomes a hole only when the bytes it produced are
inside an aligned block, so the ROM byte equals the assembled byte.
Exit: 0 ok; 1 bad input or mismatch.
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ASM_DIR = ROOT / "reference" / "aldonunez"
TPL_DIR = ROOT / "reference" / "aldonunez_tpl"
FILES = [f"Z_0{b}" for b in range(8)]
HOLE = "@@"
PRG_BANK = 0x4000

LIT = r"(?:\$[0-9A-Fa-f]+|%[01]+|[0-9]+)"
MNEMONIC = re.compile(r"^(\s*)([A-Za-z]{3})(\s+)(.*)$")
DIRECTIVE = re.compile(r"^(\s*)(\.(?:BYTE|ADDR|DBYT|WORD))(\s+)(.*)$", re.I)
LABEL = re.compile(r"^(\s*[A-Za-z_@][\w]*:)(.*)$")
OPERAND = re.compile(r"^(#|\(|)(" + LIT + r")(\)?(?:\s*,\s*[XxYy])?\)?(?:\s*,\s*[XxYy])?)$")


def lit_value(text: str) -> int:
    if text.startswith("$"):
        return int(text[1:], 16)
    if text.startswith("%"):
        return int(text[1:], 2)
    return int(text)


def lit_format(text: str) -> str:
    """Format tag that reproduces the literal's spelling from its value."""
    if text.startswith("$"):
        digits = text[1:]
        case = "X" if digits.upper() == digits else "x"
        return f"${len(digits)}{case}"
    if text.startswith("%"):
        return f"%{len(text) - 1}"
    return f"d{len(text)}" if text.startswith("0") and len(text) > 1 else "d"


def fmt_value(fmt: str, v: int) -> str:
    if fmt[0] == "$":
        return "$" + format(v, f"0{fmt[1:-1]}{fmt[-1]}")
    if fmt[0] == "%":
        return "%" + format(v, f"0{fmt[1:]}b")
    return format(v, f"0{fmt[1:]}d") if len(fmt) > 1 else str(v)


def split_comment(code: str) -> tuple[str, str]:
    i = code.find(";")
    return (code, "") if i < 0 else (code[:i], code[i:])


# ---------------------------------------------------------------- strip

def listing_lines(lst: Path) -> list[tuple[int, int, str | None, list[int | None]]]:
    """(segment offset, level, source, bytes) per listing row; continuation rows
    have source None. Bytes of imported symbols are listed as "rr" (None)."""
    rows = []
    for raw in lst.read_text(encoding="latin-1").splitlines():
        m = re.match(r"^([0-9A-F]{6})r (\d+) ", raw)
        if not m:
            continue
        field = raw[11:23] if len(raw) > 11 else ""
        data = [None if t.lower() == "rr" else int(t, 16)
                for t in field.split() if re.fullmatch(r"[0-9A-Fa-f]{2}|rr", t)]
        src = raw[24:] if len(raw) > 24 else ""
        rows.append((int(m.group(1), 16), int(m.group(2)),
                     src if (src or not data) else None, data))
    return rows


def line_bytes(asm_lines: list[str], rows, linked: bytes, ooffs: dict[str, int]
               ) -> tuple[list[list[int]], list[str]]:
    """Emitted bytes per main-file line (resolved from the linked image, so
    imported operands are real values), plus the segment each line is in."""
    out: list[list[int | None]] = [[] for _ in asm_lines]
    addr = [0] * len(asm_lines)
    segs = [""] * len(asm_lines)
    i, seg, cur = 0, "", -1
    for off, level, src, data in rows:
        if src is None:                       # byte continuation of the previous line
            if cur >= 0:
                out[cur].extend(data)
            continue
        if level != 1:
            cur = -1                          # included file: not part of this template
            continue
        if i >= len(asm_lines) or asm_lines[i].rstrip() != src.rstrip():
            raise SystemExit(f"listing out of step at line {i + 1}: {src!r}")
        sm = re.search(r'\.SEGMENT\s+"(\w+)"', src, re.I)
        if sm:
            seg = sm.group(1)
        segs[i] = seg
        out[i] = list(data)
        addr[i] = off
        cur = i
        i += 1
    if i != len(asm_lines):
        raise SystemExit(f"listing ended at line {i} of {len(asm_lines)}")
    resolved = []
    for ln, data in enumerate(out):
        if not data:
            resolved.append([])
            continue
        base = ooffs[segs[ln]] + addr[ln]
        img = list(linked[base:base + len(data)])
        if any(v is not None and v != w for v, w in zip(data, img)):
            raise SystemExit(f"linked image disagrees with the listing at line {ln + 1}")
        resolved.append(img)
    return resolved, segs


def align(stream: list[int], bank: bytes) -> dict[int, int]:
    """stream index -> bank offset where the ROM byte equals the assembled byte."""
    sm = difflib.SequenceMatcher(None, stream, list(bank), autojunk=False)
    out = {}
    for blk in sm.get_matching_blocks():
        if blk.size >= 4:                     # a short match is not evidence
            for k in range(blk.size):
                out[blk.a + k] = blk.b + k
    return out


def literal_slots(code: str, nbytes: int) -> list[tuple[int, int, int, bool]] | None:
    """(start, end, byte index, big_endian / width) for each strippable literal in
    the code part of a line, given how many bytes the line emitted. Width is
    encoded in the 4th field as (width, big_endian)."""
    m = DIRECTIVE.match(code)
    if m:
        kind = m.group(2).upper()
        width, be = {".BYTE": (1, False), ".ADDR": (2, False), ".WORD": (2, False),
                     ".DBYT": (2, True)}[kind]
        body_at = m.end(3)
        slots, pos, idx = [], body_at, 0
        for item in m.group(4).split(","):
            start = pos + (len(item) - len(item.lstrip()))
            tok = item.strip()
            if re.fullmatch(LIT, tok):
                slots.append((start, start + len(tok), idx * width, (width, be)))
            pos += len(item) + 1
            idx += 1
        if idx * width != nbytes:
            return None
        return slots
    m = MNEMONIC.match(code)
    if m and nbytes in (2, 3):
        operand = m.group(4).rstrip()
        om = OPERAND.match(operand)
        if not om:
            return None
        start = m.start(4) + len(om.group(1))
        return [(start, start + len(om.group(2)), 1, (nbytes - 1, False))]
    return None


def strip_file(name: str, lst: Path, prg: bytes, bank: int, linked: bytes,
               ooffs: dict[str, int]) -> tuple[str, list, dict]:
    asm_lines = (ASM_DIR / f"{name}.asm").read_text(encoding="utf-8").split("\n")
    per_line, segs = line_bytes(asm_lines, listing_lines(lst), linked, ooffs)
    bank_bytes = prg[bank * PRG_BANK:(bank + 1) * PRG_BANK]
    # Per segment: concatenated stream, aligned to this file's bank.
    where: dict[tuple[int, int], int] = {}
    for seg in sorted(set(segs)):
        idx = [(ln, k) for ln, data in enumerate(per_line) if segs[ln] == seg
               for k in range(len(data))]
        if not idx:
            continue
        stream = [per_line[ln][k] for ln, k in idx]
        for si, off in align(stream, bank_bytes).items():
            where[idx[si]] = bank * PRG_BANK + off
    holes, out_lines = [], []
    stats = {"stripped": 0, "kept": 0}
    for ln, line in enumerate(asm_lines):
        data = per_line[ln]
        lm = LABEL.match(line)
        prefix, rest = (lm.group(1), lm.group(2)) if lm else ("", line)
        code, comment = split_comment(rest)
        slots = literal_slots(code, len(data)) if data else None
        if not slots:
            if data and re.search(LIT, re.sub(r"[A-Za-z_@]\w*", "", code.split(None, 1)[-1]
                                              if code.split() else "")):
                stats["kept"] += 1
            out_lines.append(line)
            continue
        new, last = [], 0
        for start, end, bi, (width, be) in slots:
            tok = code[start:end]
            offs = [where.get((ln, bi + j)) for j in range(width)]
            raw = [data[bi + j] for j in range(width)]
            if None in offs or None in raw or offs != list(range(offs[0], offs[0] + width)):
                stats["kept"] += 1
                continue
            val = int.from_bytes(bytes(raw), "big" if be else "little")
            fmt = lit_format(tok)
            if lit_value(tok) != val or fmt_value(fmt, val) != tok:
                stats["kept"] += 1
                continue
            new.append(code[last:start] + HOLE)
            last = end
            holes.append([offs[0], width, int(be), fmt])
            stats["stripped"] += 1
        out_lines.append(prefix + "".join(new) + code[last:] + comment)
    return "\n".join(out_lines), holes, stats


def assemble(work: Path) -> None:
    for name in FILES:
        r = subprocess.run(["ca65", f"{name}.asm", "-o", f"{name}.o", "-l", f"{name}.lst",
                            "--bin-include-dir", "dat"], cwd=work, capture_output=True, text=True)
        if r.returncode:
            raise SystemExit(f"ca65 {name}: {r.stderr.strip()[:500]}")
    r = subprocess.run(["ld65", "-C", "Z.cfg", "-o", "prg.bin", "--dbgfile", "z.dbg",
                        *[f"{n}.o" for n in FILES]], cwd=work, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(f"ld65: {r.stderr.strip()[:500]}")


def segment_offsets(dbg: Path) -> dict[str, int]:
    out = {}
    for m in re.finditer(r'^seg\t.*name="(\w+)".*ooffs=(\d+)', dbg.read_text(), re.M):
        out[m.group(1)] = int(m.group(2))
    return out


def cmd_strip(prg: bytes) -> int:
    if not shutil.which("ca65"):
        print("ERROR: ca65 (cc65) is required for strip", file=sys.stderr)
        return 1
    work = Path(tempfile.mkdtemp(prefix="disasm strip "))
    try:
        for p in ASM_DIR.iterdir():
            if p.is_file():
                shutil.copy(p, work / p.name)
        shutil.copytree(ASM_DIR / "dat", work / "dat")
        shutil.copy(TPL_DIR / "Z.cfg", work / "Z.cfg")
        assemble(work)
        linked = (work / "prg.bin").read_bytes()
        ooffs = segment_offsets(work / "z.dbg")
        TPL_DIR.mkdir(exist_ok=True)
        manifest = {"files": {}}
        total = {"stripped": 0, "kept": 0}
        for b, name in enumerate(FILES):
            text, holes, stats = strip_file(name, work / f"{name}.lst", prg, b, linked, ooffs)
            if restore_text(text, holes, prg) != (ASM_DIR / f"{name}.asm").read_text(encoding="utf-8"):
                print(f"ERROR: {name}: restore does not reproduce the file", file=sys.stderr)
                return 1
            (TPL_DIR / f"{name}.asm.tpl").write_text(text, encoding="utf-8", newline="\n")
            manifest["files"][name] = {"holes": holes,
                                       "sha256": sha((ASM_DIR / f"{name}.asm").read_bytes())}
            for k in total:
                total[k] += stats[k]
            print(f"  {name}: {stats['stripped']} literals -> holes, {stats['kept']} kept as text")
        (TPL_DIR / "holes.json").write_text(json.dumps(manifest, separators=(",", ":")) + "\n",
                                            encoding="utf-8", newline="\n")
        print(f"strip: {total['stripped']} holes, {total['kept']} literals kept; "
              f"all {len(FILES)} files restore byte-identical")
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


# ---------------------------------------------------------------- restore

def restore_text(text: str, holes: list, prg: bytes) -> str:
    parts = text.split(HOLE)
    if len(parts) != len(holes) + 1:
        raise SystemExit(f"template has {len(parts) - 1} holes, manifest {len(holes)}")
    out = [parts[0]]
    for (off, width, be, fmt), tail in zip(holes, parts[1:]):
        val = int.from_bytes(prg[off:off + width], "big" if be else "little")
        out.append(fmt_value(fmt, val) + tail)
    return "".join(out)


def sha(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def cmd_restore(prg: bytes, check_only: bool) -> int:
    manifest = json.loads((TPL_DIR / "holes.json").read_text(encoding="utf-8"))
    bad = []
    for name in FILES:
        entry = manifest["files"][name]
        text = restore_text((TPL_DIR / f"{name}.asm.tpl").read_text(encoding="utf-8"),
                            entry["holes"], prg)
        if sha(text.encode("utf-8")) != entry["sha256"]:
            bad.append(name)
            continue
        if not check_only:
            ASM_DIR.mkdir(parents=True, exist_ok=True)
            (ASM_DIR / f"{name}.asm").write_text(text, encoding="utf-8", newline="\n")
    if bad:
        print(f"ERROR: restored {', '.join(bad)} differ from the pinned disassembly "
              "(wrong ROM?)", file=sys.stderr)
        return 1
    n = sum(len(manifest["files"][f]["holes"]) for f in FILES)
    print(f"disasm_template: {'check' if check_only else 'restore'} ok - {len(FILES)} files, "
          f"{n} holes filled from the ROM, sha256 matches")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("action", choices=("strip", "restore", "check"))
    ap.add_argument("--rom", type=Path, required=True)
    a = ap.parse_args()
    ines = a.rom.read_bytes()
    if ines[:4] != b"NES\x1a":
        print("ERROR: not an iNES ROM", file=sys.stderr)
        return 1
    prg = ines[16:16 + ines[4] * PRG_BANK]
    if a.action == "strip":
        return cmd_strip(prg)
    return cmd_restore(prg, a.action == "check")


if __name__ == "__main__":
    sys.exit(main())
