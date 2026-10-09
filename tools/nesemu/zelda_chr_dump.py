"""Offline replacement for engine/probe_nes_uw_chr_dump.lua.

Boots the ROM to the overworld (controller only), warps to level 1 with
the probe's settle rule, waits 60 frames and writes PPU $1000-$1FFF (the
UW background pattern table, 4096 bytes). engine/tools/gen_redux_uw_bg.py
converts the Redux dump to engine/src/redux_uw_bg.c.

Usage:
    python tools/nesemu/zelda_chr_dump.py --rom <rom.nes> --out engine/out/nes_uw_chr_redux.bin
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from nesemu.nes import Nes  # noqa: E402
from nesemu import zelda_uw_dump as Z  # noqa: E402


def warp_to_uw1(nes) -> bool:
    u8 = nes.bus
    for addr, v in ((Z.CUR_LEVEL, 1), (Z.TARGET_MODE, 2), (Z.TARGET_MIRROR, 2),
                    (Z.GAME_MODE, 0x10), (Z.GAME_SUB, 0)):
        nes.poke(addr, v)
    nes.set_buttons()
    for _ in range(1500):
        nes.run_frame()
        if (u8(Z.CUR_LEVEL) == 1 and u8(Z.GAME_MODE) == 0x05 and u8(Z.GAME_SUB) == 0
                and u8(Z.IS_UPDATING_MODE) == 1 and (u8(Z.CUR_PPU_MASK) & 0x1F) >= 0x18):
            nes.run_frames(60)
            return True
    return False


def uw_bg_chr(rom: bytes) -> bytes:
    nes = Nes(rom)
    if not Z.boot_to_overworld(nes):
        raise SystemExit("boot_failed")
    if not warp_to_uw1(nes):
        raise SystemExit("warp_failed")
    return bytes(nes.chr[0x1000:0x2000])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    data = uw_bg_chr(args.rom.read_bytes())
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(data)
    print(f"wrote {args.out} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
