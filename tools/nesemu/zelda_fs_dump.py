"""Offline replacement for tools/file_select_test/dump_fs_{chr,nametable}.lua.

Same input script from power-on (BizHawk frames): 300 idle frames, Start
held one frame, released, 119 more frames; then PPU $0000-$1FFF (CHR) and
$2000-$23FF (nametable 0 + attributes). tools/extract_fs_assets.py reads
the two dumps from tools/file_select_test/ref/. The file-select look is
Zelda Redux's, so the builder runs this on the user's Redux ROM.

Usage:
    python tools/nesemu/zelda_fs_dump.py --rom <Zelda Redux.nes> [--out-dir tools/file_select_test/ref]
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from nesemu.nes import Nes  # noqa: E402
from nesemu.test_intro_captures import BIZHAWK_FRAME_OFFSET  # noqa: E402


def fs_dump(rom: bytes) -> tuple[bytes, bytes]:
    nes = Nes(rom)
    nes.run_frames(BIZHAWK_FRAME_OFFSET + 300)
    nes.set_buttons("Start")
    nes.run_frame()
    nes.set_buttons()
    nes.run_frames(1 + 118)
    nt = bytes(nes.ppu_bus_read(0x2000 + i) for i in range(0x400))
    return bytes(nes.chr), nt


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--out-dir", type=Path, default=ROOT / "tools" / "file_select_test" / "ref")
    args = ap.parse_args()
    chr_data, nt = fs_dump(args.rom.read_bytes())
    args.out_dir.mkdir(parents=True, exist_ok=True)
    (args.out_dir / "fs_chr.bin").write_bytes(chr_data)
    (args.out_dir / "fs_nt.bin").write_bytes(nt)
    print(f"wrote fs_chr.bin ({len(chr_data)}) and fs_nt.bin ({len(nt)}) to {args.out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
