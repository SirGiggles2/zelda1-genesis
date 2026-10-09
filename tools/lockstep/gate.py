"""Lockstep gate (T-141): the one PASS/FAIL rule for NES vs Genesis RAM.

Shared by diff.py (post-run verdict) and capture.lua (Genesis fail-fast;
presets.to_lua emits the tables below into preset.lua), so both sides
check exactly the same cells.

KEY cells gate every compared tick:
  GameMode $12, GameSubmode $13, FrameCounter $15, Random $18..$24,
  RoomId $EB, Link invincibility $4F0, Items $657..$67E (inventory, HP,
  keys, rupees); object slot 0 (Link) X $70 / Y $84 / dir $98; object
  slots 1..11 type $34F+i, and X/Y/dir while that slot's type is nonzero
  on either console (an empty slot's leftovers are not game state).
Padded rows (Genesis FrameCounter jump replaying skipped NES frames,
T-137; capture lists them in <OUT>.pad) are not compared.
Allow entries (preset "allow"): [addr, nes, gen, first_tick, last_tick,
"T-###"] - one exact value pair over one tick window, tied to an open
tracker row.

Full-RAM ratchet: every other unmasked cell. Per preset, the baseline
tools/lockstep/baselines/<preset>.json maps cell -> first diverging tick.
A cell missing from the baseline, or diverging earlier than its baseline
tick, fails; a baseline cell that no longer diverges is IMPROVED
(diff.py --bless rewrites the baseline). T-142 burns the baselines down.
"""
from __future__ import annotations

import json
from pathlib import Path

import numpy as np

ROW = 0x800
BASELINES = Path(__file__).resolve().parent / "baselines"

# Documented platform differences (never compared).
MASKS = [(0x0000, 0x000F), (0x0100, 0x01FF), (0x0200, 0x02FF), (0x0300, 0x033F),
         (0x0066, 0x006F), (0x05F0, 0x061F), (0x07E0, 0x07FF)]

KEY_GLOBAL = ([0x12, 0x13, 0x15] + list(range(0x18, 0x25)) + [0xEB, 0x4F0]
              + list(range(0x657, 0x67F)) + [0x70, 0x84, 0x98])
SLOT_TYPE = 0x34F          # + slot 1..11
SLOT_CELLS = (0x70, 0x84, 0x98)
SLOTS = range(1, 12)


def masked(a: int) -> bool:
    return any(lo <= a <= hi for lo, hi in MASKS)


def key_cells() -> set[int]:
    out = set(KEY_GLOBAL)
    for i in SLOTS:
        out.add(SLOT_TYPE + i)
        out.update(b + i for b in SLOT_CELLS)
    return out


KEY = key_cells()
RATCHET = [a for a in range(ROW) if not masked(a) and a not in KEY]


def rows(data: bytes) -> np.ndarray:
    if len(data) % ROW:
        raise ValueError(f"bad .ram size {len(data)}")
    return np.frombuffer(data, dtype=np.uint8).reshape(-1, ROW)


def allowed(allow: list, a: int, t: int, nv: int, gv: int) -> str | None:
    for e in allow:
        addr, x, y, lo, hi = (int(v, 0) if isinstance(v, str) else int(v) for v in e[:5])
        if addr == a and x == nv and y == gv and lo <= t <= hi:
            return e[5] if len(e) > 5 else "allow"
    return None


def key_failures(nes: np.ndarray, gen: np.ndarray, allow: list, pad: set[int],
                 limit: int = 20) -> tuple[list[tuple], dict[str, int]]:
    """[(tick, addr, nes, gen)] of non-allowed KEY mismatches (first `limit`),
    plus allow hit counts by tracker row."""
    n = min(len(nes), len(gen))
    glob = np.array(KEY_GLOBAL)
    fails: list[tuple] = []
    hits: dict[str, int] = {}
    diff_t = np.nonzero((nes[:n, glob] != gen[:n, glob]).any(axis=1)
                        | (nes[:n, SLOT_TYPE + 1:SLOT_TYPE + 12] != gen[:n, SLOT_TYPE + 1:SLOT_TYPE + 12]).any(axis=1)
                        | (nes[:n, 0x70:0x70 + 12] != gen[:n, 0x70:0x70 + 12]).any(axis=1)
                        | (nes[:n, 0x84:0x84 + 12] != gen[:n, 0x84:0x84 + 12]).any(axis=1)
                        | (nes[:n, 0x98:0x98 + 12] != gen[:n, 0x98:0x98 + 12]).any(axis=1))[0]
    for t in diff_t:
        t = int(t)
        if t in pad:
            continue
        rn, rg = nes[t], gen[t]
        cells = list(KEY_GLOBAL)
        for i in SLOTS:
            cells.append(SLOT_TYPE + i)
            if rn[SLOT_TYPE + i] or rg[SLOT_TYPE + i]:
                cells.extend(b + i for b in SLOT_CELLS)
        for a in cells:
            if rn[a] == rg[a]:
                continue
            why = allowed(allow, a, t, int(rn[a]), int(rg[a]))
            if why:
                hits[why] = hits.get(why, 0) + 1
                continue
            if len(fails) < limit:
                fails.append((t, a, int(rn[a]), int(rg[a])))
            else:
                return fails, hits
    return fails, hits


def ratchet_cells(nes: np.ndarray, gen: np.ndarray, pad: set[int]) -> dict[int, int]:
    """cell -> first diverging tick, over RATCHET cells, padded rows skipped."""
    n = min(len(nes), len(gen))
    idx = np.array(RATCHET)
    ok = np.ones(n, dtype=bool)
    for t in pad:
        if t < n:
            ok[t] = False
    d = (nes[:n, idx] != gen[:n, idx]) & ok[:, None]
    any_d = d.any(axis=0)
    first = d.argmax(axis=0)
    return {int(idx[j]): int(first[j]) for j in np.nonzero(any_d)[0]}


def load_baseline(name: str) -> dict[int, int] | None:
    p = BASELINES / f"{name}.json"
    if not p.exists():
        return None
    return {int(k, 16): v for k, v in json.loads(p.read_text(encoding="utf-8"))["cells"].items()}


def save_baseline(name: str, cells: dict[int, int], ticks: int) -> Path:
    BASELINES.mkdir(parents=True, exist_ok=True)
    p = BASELINES / f"{name}.json"
    p.write_text(json.dumps({"preset": name, "ticks": ticks,
                             "cells": {f"0x{a:03X}": t for a, t in sorted(cells.items())}},
                            indent=1) + "\n", encoding="utf-8")
    return p


def ratchet_verdict(cells: dict[int, int], base: dict[int, int] | None,
                    compared: int) -> tuple[list, list, list]:
    """(new, earlier, improved). Cells past the compared range cannot be judged
    'improved' (a fail-fast run stops early)."""
    base = base or {}
    new = sorted((t, a) for a, t in cells.items() if a not in base)
    earlier = sorted((t, a, base[a]) for a, t in cells.items() if a in base and t < base[a])
    improved = sorted(a for a, t in base.items() if a not in cells and t < compared)
    return new, earlier, improved


def lua_tables(allow: list) -> str:
    """Gate tables for capture.lua (Genesis fail-fast). 0-based NES offsets."""
    def arr(xs):
        return "{" + ",".join(str(x) for x in xs) + "}"
    al = []
    for e in allow:
        a, x, y, lo, hi = (int(v, 0) if isinstance(v, str) else int(v) for v in e[:5])
        al.append("{" + f"{a},{x},{y},{lo},{hi}" + "}")
    return (f"PRESET.gate = {{global={arr(KEY_GLOBAL)}, slot_type={SLOT_TYPE}, "
            f"slot_cells={arr(SLOT_CELLS)}, allow={{{','.join(al)}}}}}\n")
