"""Canonical public names for the private development tree.

Export paths and text together so includes, generators, launchers, manifests
and release checks use the same layout. This does not alter gameplay logic.
Public exports are already canonical; their exporter uses an identity layout.
"""
from __future__ import annotations

import re
from pathlib import Path

# Specific names first, then directory names. Keep C symbols/ABI names intact.
NAMES = (
    ("gen_redux_roomrom", "gen_redux_assets"),
    ("roomrom_debug_runtime.h", "engine_runtime.h"),
    ("roomrom_enemy_state.h", "enemy_state.h"),
    ("roomrom_main_state.h", "engine_state.h"),
    ("roomrom_vram_map.h", "vram_layout.h"),
    ("a4_probe_main", "game_main"),
    ("a4_probe_asm", "game_startup"),
    ("build_debug", "build_rom"),
    ("Debug-Latest.md", "Zelda-Latest.md"),
    ("Debug_raw.md", "Zelda_raw.md"),
    ("Debug.out", "Zelda.out"),
    ("Debug.md", "Zelda.md"),
    ("Debug.bat", "Build.bat"),
    ("debug_project", "rom_project"),
    ("tools/builder", "tools/converter"),
    ("tools.builder", "tools.converter"),
    ("tools/debug", "tools/build"),
    ("src/debug", "src/platform"),
    ("RoomRom", "engine"),
)


def rename(path: str) -> str:
    for old, new in NAMES:
        path = path.replace(old, new)
    return path


def rewrite(data: bytes) -> bytes:
    text = data.decode("latin-1")
    for old, new in NAMES:
        for slash in ("/", "\\", "\\\\"):
            text = text.replace(old.replace("/", slash), new.replace("/", slash))
    # pathlib fragments in Python, including single quotes inside f-strings.
    for quote in ('"', "'"):
        text = text.replace(quote + "builder" + quote, quote + "converter" + quote)
        for parent, child in (("tools", "build"), ("src", "platform")):
            pattern = rf'({quote}{parent}{quote}\s*/\s*){quote}debug{quote}'
            text = re.sub(pattern, rf'\g<1>{quote}{child}{quote}', text)
    text = text.replace("[Debug]", "[Build]").replace("Debug built:", "ROM built:")
    return text.encode("latin-1")


def is_public(root: Path) -> bool:
    return (root / "engine").is_dir() and not (root / "RoomRom").is_dir()
