"""One staged scenario on both consoles, headless, for byte checks.

A scenario is a lockstep preset (save card) plus explicit stages: Lua
bodies on NES work RAM, run at script step N on both consoles exactly as
tools/lockstep/capture.lua runs them. After the script, `post` steps run
(each [frames, "UDLRABSs"], ["stage", lua] or ["snap", N]: run_lockstep
--snap files for tick N, read by screen_diff.py and the verify_* tools)
and the final capture is written
in the layout tools/lockstep/verify_sprites.py reads:

    nes.oam nes.chr nes.palram nes.ram   (tools/nesemu, the user's ROM)
    gen.vram gen.cram gen.ram gen.png    (tools/emu/gpgx.py, Zelda.md)

    python tools/emu/scenario.py ROM.nes scenario.json OUTDIR

scenario.json: {"preset": "t002_q2_l1", "script": [[400, ""]],
  "clock": "play", "stages": [[50, "wr(0x10,1)"], ...],
  "post": [["stage", "for k=1,10 do wr(0x34F+k,0) end"], [63, ""]]}
"script"/"clock"/"stages" default to the preset's own. "snap_steps": [a, b]
snaps after every script step a..b (tick = step number, on the preset's
clock, so both consoles stay aligned through mode changes).
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "tools" / "lockstep"))
import presets  # noqa: E402
from nes_preset_run import NesSession  # noqa: E402
from preset_run import Session  # noqa: E402


# capture.lua SEED (T-101): FrameCounter, Random $18..$24, StunCycle $26,
# read on the NES at sync and written into the Genesis at its sync.
SEED_CELLS = [0x15] + list(range(0x18, 0x25)) + [0x26]


def snap(s, out: Path, tick: int):
    """run_lockstep --snap layout: <plat>.fNNNNN.<oam|chr|pal|nt|ram> for
    the NES, <plat>.fNNNNN.<vram|cram|vsram|ram> for the Genesis."""
    tag = f"f{tick:05d}"
    if isinstance(s, NesSession):
        for ext, data in (("oam", s.oam()), ("chr", s.chr()), ("pal", s.palram()),
                          ("nt", bytes(s.n.ciram)), ("ram", s.nes(0, 0x800))):
            (out / f"nes.{tag}.{ext}").write_bytes(data)
    else:
        for ext, data in (("vram", s.g.vram()), ("cram", struct.pack(">64H", *s.g.cram())),
                          ("vsram", s.g.vsram()), ("regs", s.g.regs()), ("ram", s.nes(0, 0x800))):
            (out / f"gen.{tag}.{ext}").write_bytes(data)
        s.g.screenshot(out / f"gen.{tag}.png", scale=1)   # the displayed frame (triage)


def drive(s, p: dict, sc: dict, seed: dict | None = None, out: Path | None = None) -> dict:
    s.write_save(p["nes_wram"] if isinstance(s, NesSession) else p["gen_slot0"])
    s.enter_fs()
    if seed is None:
        seed = {a: s.nes8(a) for a in SEED_CELLS}
    else:
        for a, v in seed.items():
            s.write_nes(a, v)
    lo, hi = sc.get("snap_steps", [0, -1])
    s.script(sc.get("script", p["script"]), sc.get("clock", p["clock"]),
             [tuple(x) for x in sc.get("stages", p["stages"])],
             on_step=(lambda i: snap(s, out, i) if lo <= i <= hi else None))
    for step in sc.get("post", []):
        if step[0] == "stage":
            s.run_stage(step[1])
        elif step[0] == "snap":
            snap(s, out, int(step[1]))
        else:
            s.press(step[1], int(step[0]))
    return seed


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path, help="user's NES Zelda ROM")
    ap.add_argument("scenario", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--debug-rom", type=Path, default=ROOT / "builds" / "Zelda.md")
    a = ap.parse_args()
    sc = json.loads(a.scenario.read_text())
    spec = json.loads((ROOT / "tools" / "lockstep" / "presets" / f"{sc['preset']}.json").read_text())
    p = presets.build(spec)
    a.out.mkdir(parents=True, exist_ok=True)

    n = NesSession(a.rom)
    seed = drive(n, p, sc, out=a.out)
    (a.out / "nes.oam").write_bytes(n.oam())
    (a.out / "nes.chr").write_bytes(n.chr())
    (a.out / "nes.palram").write_bytes(n.palram())
    # screen_diff.py reads NES RAM as one 2 KB row per tick (nes.ram);
    # with snaps, row N is snap N's RAM, else the single final row.
    ticks = sorted(int(f.name[5:10]) for f in a.out.glob("nes.f*.ram"))
    if ticks:
        rows = bytearray(0x800 * (ticks[-1] + 1))
        for t in ticks:
            rows[t * 0x800:(t + 1) * 0x800] = (a.out / f"nes.f{t:05d}.ram").read_bytes()
        (a.out / "nes.ram").write_bytes(bytes(rows))
    else:
        (a.out / "nes.ram").write_bytes(n.nes(0, 0x800))

    g = Session(a.debug_rom)
    drive(g, p, sc, seed, out=a.out)
    (a.out / "gen.vram").write_bytes(g.g.vram())
    (a.out / "gen.cram").write_bytes(struct.pack(">64H", *g.g.cram()))
    (a.out / "gen.ram").write_bytes(g.nes(0, 0x800))
    g.g.screenshot(a.out / "gen.png", scale=2)

    meta = {k: (n.nes8(addr), g.nes8(addr)) for k, addr in
            (("GameMode", 0x12), ("Submode", 0x13), ("RoomId", 0xEB), ("Level", 0x10),
             ("FrameCounter", 0x15))}
    (a.out / "meta.json").write_text(json.dumps(meta, indent=1))
    print(" ".join(f"{k} NES ${v[0]:02X} GEN ${v[1]:02X}" for k, v in meta.items()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
