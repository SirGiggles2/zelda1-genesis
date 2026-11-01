"""NES ground truth for dungeon room screens: one clean capture per room on Mesen CE AI.

Same boot/warp/force-room/settle procedure as tools/nesemu/zelda_uw_dump.py, but
every room starts from a checkpoint taken right after the level warp, so no
room inherits door/shutter state from a previously captured room. Output:
<out>/mesen_uw_clean_<rom_id>_L<l>Q<q>.json  {room_id: {nt, attr, palram, settle_frames, ...}}

    python tools/nesdata/probes/mesen_uw_clean.py --rom <rom.nes> --rom-id orig|redux \
        --rooms-from engine/out --out build/mesen_uw_clean [--jobs N]

Domains used (enumerated live 2026-10-31): NesInternalRam (2 KB), NesSaveRam
(8 KB = $6000-$7FFF), NesNametableRam (2 KB CIRAM), NesPaletteRam (32 B).
T-419 (adopt reproducible dungeon data, user 2026-10-31).
"""
from __future__ import annotations

import argparse
import json
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "mesen_ai"))
sys.path.insert(0, str(ROOT / "tools"))
from nesemu import zelda_uw_dump as d  # noqa: E402  (addresses, Pad, flow constants)

PAD_BITS = {"Up": 1, "Down": 2, "Left": 4, "Right": 8, "Start": 16, "Select": 32, "B": 64, "A": 128}


class Machine:
    """The nes.py surface zelda_uw_dump uses (bus/poke/run_frame/set_buttons), on Mesen."""

    def __init__(self, emu):
        self.emu = emu
        self.ram = bytes(0x800)
        self.pad = 0

    def refresh(self):
        self.ram = bytes(self.emu.call("read_memory", domain="NesInternalRam", address=0, length=0x800)["bytes"])

    def bus(self, addr):
        if addr < 0x2000:
            return self.ram[addr & 0x7FF]
        if 0x6000 <= addr < 0x8000:
            return self.emu.call("read_memory", domain="NesSaveRam", address=addr - 0x6000, length=1)["bytes"][0]
        raise ValueError(f"unsupported read ${addr:04X}")

    def poke(self, addr, value):
        if addr < 0x2000:
            self.emu.call("write_memory", domain="NesInternalRam", address=addr & 0x7FF, value=value & 0xFF)
        elif 0x6000 <= addr < 0x8000:
            self.emu.call("write_memory", domain="NesSaveRam", address=addr - 0x6000, value=value & 0xFF)
        else:
            raise ValueError(f"unsupported poke ${addr:04X}")
        self.refresh()

    def set_buttons(self, *names):
        self.pad = sum(PAD_BITS[n] for n in names)

    def run_frame(self):
        self.emu.call("set_input", port=0, pad=self.pad)
        r = self.emu.call("step", unit="frame", count=1, timeout_ms=20000)
        if r["stop_reason"] != "step_complete":
            raise RuntimeError(f"step: {r['stop_reason']}")
        self.refresh()

    def ciram(self):
        return bytes(self.emu.call("read_memory", domain="NesNametableRam", address=0, length=0x800)["bytes"])

    def palram(self):
        return list(self.emu.call("read_memory", domain="NesPaletteRam", address=0, length=32)["bytes"])


def settle(m: Machine, target: int, wait_for_ganon: bool):
    """zelda_uw_dump.settle_and_capture on Mesen."""
    stable, prev = 0, None
    m.set_buttons()
    for f in range(1, d.MAX_SETTLE_FRAMES + 1):
        m.run_frame()
        u8, mask = m.bus, m.bus(d.CUR_PPU_MASK)
        if (u8(d.GAME_MODE) == 0x05 and u8(d.GAME_SUB) == 0 and u8(d.IS_UPDATING_MODE) == 1
                and (not wait_for_ganon or u8(0x0445) == 0x02)
                and (mask & 0x1F) >= 0x18 and u8(d.ROOM_ID) == target and mask & 0x18 == 0x18):
            ci = m.ciram()
            state = ci[0:0x400]
            if prev is not None and state == prev:
                stable += 1
                if stable >= d.STABLE_WINDOW:
                    return {"settle_frames": f, "nt": [list(ci[r * 32:(r + 1) * 32]) for r in range(30)],
                            "attr": list(ci[0x3C0:0x400]), "palram": m.palram(),
                            "game_mode": u8(d.GAME_MODE), "game_sub": u8(d.GAME_SUB), "ppu_mask": mask}
            else:
                stable, prev = 0, state
        else:
            stable, prev = 0, None
    return {"settle_frames": d.MAX_SETTLE_FRAMES, "code": "settle_timeout"}


def job(args):
    rom_path, rom_id, level, quest, rooms, out_dir = args
    from client import MesenAI  # noqa: PLC0415
    out = Path(out_dir) / f"mesen_uw_clean_{rom_id}_L{level}Q{quest}.json"
    with MesenAI() as emu:
        emu.load(str(rom_path), seed=419, random_ram=False)
        m = Machine(emu)
        m.refresh()
        boot_ok = d.boot_to_overworld(m)
        d.force_quest(m, quest)
        warp_ok = boot_ok and d.warp_to_level(m, level)
        result = {"rom": rom_id, "level": level, "quest": quest, "boot_ok": boot_ok, "warp_ok": warp_ok, "rooms": {}}
        if warp_ok:
            emu.call("checkpoint", name="warped")
            ganon = level == 9 and quest == 1 and rom_id == "orig"
            for rid in rooms:
                emu.call("restore", name="warped")
                m.refresh()
                d.force_room(m, rid)
                result["rooms"][f"{rid:02X}"] = settle(m, rid, ganon and rid == 0x42)
    out.write_text(json.dumps(result) + "\n", encoding="utf-8")
    return out.name, boot_ok, warp_ok, len(result["rooms"])


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--rom-id", choices=("orig", "redux"), required=True)
    ap.add_argument("--rooms-from", type=Path, default=ROOT / "engine" / "out")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--jobs", type=int, default=6)
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    work = []
    for level in range(1, 10):
        for quest in (1, 2):
            src = json.loads((a.rooms_from / f"nes_uw_level{level}_quest{quest}_{a.rom_id}.json").read_text())
            rooms = [r["room_id"] for r in src["results"]]
            work.append((str(a.rom.resolve()), a.rom_id, level, quest, rooms, str(a.out.resolve())))
    with ProcessPoolExecutor(max_workers=a.jobs) as ex:
        for name, boot_ok, warp_ok, n in ex.map(job, work):
            print(f"{name}: boot={boot_ok} warp={warp_ok} rooms={n}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
