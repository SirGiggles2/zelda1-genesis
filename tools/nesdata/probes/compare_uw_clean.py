"""Byte-diff dungeon room captures against Mesen clean ground truth.

Sources per (rom, level, quest, room):
  mesen  build/mesen_uw_clean/mesen_uw_clean_<rom>_L<l>Q<q>.json   (NES ground truth)
  clean  build/js_uw_clean/nes_uw_level<l>_quest<q>_<rom>.json     (JS NES, clean mode)
  seq    engine/out/nes_uw_level<l>_quest<q>_<rom>.json           (JS NES, sequential = fresh blob)
  blob   committed engine/src/uw_room_blob.c (git HEAD)           (current Zelda.md data)
Compared: nt (30x32), attr (64), palram (32) with the PALRAM mirror cells 16/20/24/28
excluded (nes.py stores them BizHawk-style; Mesen mirrors them; the game never reads them).
T-419.
"""
from __future__ import annotations

import collections
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.nesdata.c_arrays import parse  # noqa: E402

MIRRORS = {16, 20, 24, 28}


def pal(p):
    return [v for i, v in enumerate(p) if i not in MIRRORS]


def key(c):
    return ([v for row in c["nt"] for v in row], list(c["attr"]), pal(c["palram"]))


def from_dump(path: Path):
    d = json.loads(path.read_text())
    return {r["room_id"]: key(r) for r in d["results"] if "nt" in r}


def from_blob(text: str):
    arrs = {a.name: a for a in parse(text, {}).arrays}
    lk = arrs["g_uw_room_lookup"].data
    lookup = [int.from_bytes(lk[i:i + 2], "big") for i in range(0, len(lk), 2)]
    nt, at, pl = arrs["g_uw_room_nt"].data, arrs["g_uw_room_attr"].data, arrs["g_uw_room_palette"].data

    def room(rom, quest, level, rid):
        # gen_uw_blob.py: lookup holds index + 1; nt holds play-area rows 8..29 (704 bytes).
        i = lookup[((rom * 3 + quest) * 10 + level) * 128 + rid] - 1
        if i < 0:
            return None
        return (list(nt[i * 704:(i + 1) * 704]), list(at[i * 64:(i + 1) * 64]), pal(list(pl[i * 32:(i + 1) * 32])))
    return room


def main() -> int:
    head = subprocess.run(["git", "show", "HEAD:engine/src/uw_room_blob.c"], cwd=ROOT, capture_output=True,
                          text=True, check=True).stdout
    blob = from_blob(head)
    stats = collections.Counter()
    worst = []
    for ri, rom in enumerate(("orig", "redux")):
        for level in range(1, 10):
            for quest in (1, 2):
                m = json.loads((ROOT / f"build/mesen_uw_clean/mesen_uw_clean_{rom}_L{level}Q{quest}.json").read_text())
                clean = from_dump(ROOT / f"build/js_uw_clean/nes_uw_level{level}_quest{quest}_{rom}.json")
                seq = from_dump(ROOT / f"engine/out/nes_uw_level{level}_quest{quest}_{rom}.json")
                for rid_hex, cap in m["rooms"].items():
                    rid = int(rid_hex, 16)
                    if "nt" not in cap:
                        stats["mesen_timeout"] += 1
                        continue
                    truth = key(cap)
                    stats["rooms"] += 1
                    for name, other in (("clean", clean.get(rid)), ("seq", seq.get(rid))):
                        if other is None:
                            stats[f"{name}_missing"] += 1
                        elif other == truth:
                            stats[f"{name}_equal"] += 1
                        else:
                            stats[f"{name}_diff"] += 1
                            worst.append((name, rom, level, quest, rid,
                                          [k for k, a, b in zip(("nt", "attr", "pal"), other, truth) if a != b]))
                    b = blob(ri, quest, level, rid)
                    if b is None:
                        stats["blob_missing"] += 1
                    else:
                        play = (truth[0][8 * 32:30 * 32], truth[1], truth[2])
                        if b == play:
                            stats["blob_equal"] += 1
                        else:
                            stats["blob_diff"] += 1
                            worst.append(("blob", rom, level, quest, rid,
                                          [k for k, x, y in zip(("nt", "attr", "pal"), b, play) if x != y]))
    for k in sorted(stats):
        print(f"{k:22} {stats[k]}")
    for w in worst[:40]:
        print("  diff", w)
    return 0


if __name__ == "__main__":
    sys.exit(main())
