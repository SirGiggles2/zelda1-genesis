"""Registry of .nesdata pieces: every NES/Redux-derived byte string in Zelda.md.

Tuple order is link order. `step` names the pipeline step that writes the
piece (see tools/nesdata/assemble.py STEPS); `path` is relative to the work
root; `size` is the exact byte count the extractor must produce (a different
size fails the build: stale registry or wrong ROM).

T-419. Spec: docs/superpowers/specs/2026-10-31-T419-one-file-rom-maker-design.md
"""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PIECE_DIR = "build/nesdata/pieces"


@dataclass(frozen=True)
class Piece:
    name: str
    symbol: str
    step: str
    path: str
    size: int
    align: int = 2


def _p(name: str, symbol: str, step: str, size: int, align: int = 2) -> Piece:
    return Piece(name=name, symbol=symbol, step=step, path=f"{PIECE_DIR}/{name}.bin",
                 size=size, align=align)


def load_registry(path: Path) -> tuple[Piece, ...]:
    """Pieces converted from generated C sources (tools/nesdata/sources.py --register)."""
    if not path.is_file():
        return ()
    import json  # noqa: PLC0415
    return tuple(Piece(name=e["name"], symbol=e["symbol"], step=f"source:{e['source']}",
                       path=f"{PIECE_DIR}/{e['name']}.bin", size=e["size"], align=e["align"])
                 for e in json.loads(path.read_text(encoding="utf-8"))["pieces"])


# Hand-registered pieces first, then the generated-source registry.
PIECES: tuple[Piece, ...] = (
    _p("gray_by_word", "k_gray_by_word", "extract_misc", 1024),
    _p("music_blob", "MusicBlob", "extract_audio", 2757),
) + load_registry(ROOT / "tools" / "nesdata" / "pieces.json")

_BY_NAME = {p.name: p for p in PIECES}


def by_name(name: str) -> Piece:
    return _BY_NAME[name]


def write_piece(name: str, data: bytes, root: Path = ROOT) -> Path:
    """Write one piece's exact in-ROM bytes; a size other than the registry's is an error."""
    piece = by_name(name)
    if len(data) != piece.size:
        raise ValueError(f"nesdata piece {name}: produced {len(data)} bytes, registry says {piece.size}")
    out = root / piece.path
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    return out
