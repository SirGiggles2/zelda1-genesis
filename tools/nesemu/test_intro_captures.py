"""Gate: the headless NES reproduces the BizHawk intro captures byte-for-byte.

tools/intro_demo/nes_full/{chr,oam,palram}_<frame>.bin were captured from
NesHawk with no input, every 10 frames from 2050 to 4700. BizHawk frame N
is this machine's frame N + BIZHAWK_FRAME_OFFSET (power-on origin).

Usage: python tools/nesemu/test_intro_captures.py <rom.nes>
Developer check only: the captures are game data and do not ship.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from nesemu.nes import Nes  # noqa: E402

CAPS = ROOT / "tools" / "intro_demo" / "nes_full"
BIZHAWK_FRAME_OFFSET = 9


def main() -> int:
    frames = sorted(int(m.group(1)) for p in CAPS.glob("palram_*.bin")
                    if (m := re.fullmatch(r"palram_(\d+)\.bin", p.name)))
    if not frames:
        print(f"no captures under {CAPS}", file=sys.stderr)
        return 2
    nes = Nes(Path(sys.argv[1]).read_bytes())
    want = {f + BIZHAWK_FRAME_OFFSET: f for f in frames}
    fails = []
    while nes.frame < max(want):
        nes.run_frame()
        f = want.get(nes.frame)
        if f is None:
            continue
        for name, mem in (("chr", nes.chr), ("oam", nes.oam), ("palram", nes.palram)):
            if (CAPS / f"{name}_{f}.bin").read_bytes() != bytes(mem):
                fails.append(f"{name}_{f}")
    total = len(frames) * 3
    print(f"intro captures: {total - len(fails)}/{total} byte-identical "
          f"(frames {frames[0]}..{frames[-1]})")
    for f in fails[:20]:
        print(f"  DIFF {f}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
