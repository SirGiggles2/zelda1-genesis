"""Your own VGMs in your own build (local only; nothing is shipped or uploaded).

Converts every .vgm in a folder with SGDK's xgmtool and writes
build/local_music/local_music.c: one XGM blob per file plus a table from NES
song request to blob. src/sgdk_adapter/audio_adapter.c plays a song from
that table when the build defines ZELDA_LOCAL_MUSIC (build_rom.py does
when ZELDA_LOCAL_MUSIC_C points at the file). Songs without a file keep the
port's own music.

Files are matched to NES songs by name (case-insensitive):
    title / intro                -> $80 title
    overworld                    -> $01 overworld
    underworld / dungeon         -> $40 dungeons 1-8
    level9 / level 9 / lastlevel -> $20 level 9
    ganon                        -> $02 Ganon
    triforce / endlevel          -> $04 level cleared (Triforce piece)
    zelda                        -> $06 Zelda rescued
    item                         -> $08 item get
    ending / credits             -> $10 ending

    python tools/converter/local_music.py <folder>
Exit: 0 written; 1 no usable file or a conversion failed.
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT_C = ROOT / "build" / "local_music" / "local_music.c"
XGMTOOL = ROOT / "sgdk" / "bin" / "xgmtool.exe"

# (song request, label, name patterns); first match wins, most specific first.
SONGS = (
    (0x20, "level 9", r"level\s*_?9|last\s*_?level"),
    (0x02, "Ganon", r"ganon"),
    (0x04, "level cleared", r"triforce|end\s*_?level"),
    (0x06, "Zelda rescued", r"zelda"),
    (0x08, "item get", r"item"),
    (0x10, "ending", r"ending|credits"),
    (0x80, "title", r"title|intro"),
    (0x40, "dungeon", r"underworld|dungeon"),
    (0x01, "overworld", r"overworld"),
)


def song_for(name: str) -> tuple[int, str] | None:
    stem = Path(name).stem.lower()
    for song, label, pat in SONGS:
        if re.search(pat, stem):
            return song, label
    return None


def xgm_compile(vgm: Path, out: Path) -> None:
    argv = [str(XGMTOOL), str(vgm), str(out), "-n", "-s"]
    if os.name != "nt":
        argv = ["wine"] + argv
    env = dict(os.environ, WINEDEBUG="-all")
    r = subprocess.run(argv, capture_output=True, text=True, env=env)
    if r.returncode or not out.is_file():
        raise SystemExit(f"ERROR: xgmtool failed on {vgm.name}: {(r.stdout + r.stderr).strip()[-400:]}")


def emit_table(picked: dict[int, tuple[str, Path]], out_c: Path,
               table: str = "local_music", timing: dict | None = None) -> None:
    """One XGM compiler/table format for user overrides and ROM conversion."""
    if table not in ("local_music", "nes_music"):
        raise ValueError(f"invalid music table {table}")
    lines = ["/* Generated locally; never distribute ROM-derived music. */",
             f'#include "{table}.h"', ""]
    rows = []
    with tempfile.TemporaryDirectory() as td:
        for song, (label, p) in sorted(picked.items()):
            out = Path(td) / f"song_{song:02x}.bin"
            xgm_compile(p, out)
            data = out.read_bytes()
            sym = f"{table}_song_{song:02x}"
            lines.append(f"/* ${song:02X} {label}: {p.name} ({len(data)} bytes) */")
            lines.append(f"static const unsigned char __attribute__((aligned(256))) {sym}[{len(data)}] = {{")
            for i in range(0, len(data), 16):
                lines.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",")
            lines.append("};")
            frames, looping = (timing or {}).get(song, (0, False))
            rows.append(f"    {{ 0x{song:02x}u, {sym}, {frames}u, {int(looping)}u }},")
            print(f"  ${song:02X} {label:14s} <- {p.name} ({len(data)} bytes)")
    lines += ["", f"const local_song_t {table}[] = {{", *rows, "};",
              f"const unsigned char {table}_count = {len(rows)}u;", ""]
    out_c.parent.mkdir(parents=True, exist_ok=True)
    out_c.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    guard = table.upper() + "_H"
    (out_c.parent / f"{table}.h").write_text(
        f"#ifndef {guard}\n#define {guard}\n"
        "#ifndef LOCAL_SONG_T_DEFINED\n#define LOCAL_SONG_T_DEFINED\n"
        "typedef struct { unsigned char song; const unsigned char *xgm; unsigned short frames; unsigned char looping; } local_song_t;\n#endif\n"
        f"extern const local_song_t {table}[];\nextern const unsigned char {table}_count;\n"
        "#endif\n", encoding="utf-8", newline="\n")
    print(f"wrote {out_c} ({len(rows)} songs)")


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 1
    folder = Path(sys.argv[1])
    vgms = sorted(p for p in folder.glob("*") if p.suffix.lower() in (".vgm", ".vgz"))
    picked: dict[int, tuple[str, Path]] = {}
    for p in vgms:
        m = song_for(p.name)
        if m is None:
            print(f"  skipped {p.name}: name matches no song")
            continue
        song, label = m
        if song in picked:
            print(f"  skipped {p.name}: {label} already taken by {picked[song][1].name}")
            continue
        picked[song] = (label, p)
    if not picked:
        print(f"ERROR: no usable .vgm in {folder}", file=sys.stderr)
        return 1
    emit_table(picked, OUT_C)
    return 0


if __name__ == "__main__":
    sys.exit(main())
