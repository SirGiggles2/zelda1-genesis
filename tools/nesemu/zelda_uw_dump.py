"""Offline replacement for engine/probe_nes_uw_dump.lua.

Same procedure, same RAM pokes, same settle rule, run on tools/nesemu:
boot to the overworld through file registration (controller only), force
the quest, warp to the level (SetTargetMode(2) emulation), then for every
room in engine/data/uw_level{L}_quest{Q}_rooms.json force a mode-3
submode-2 room load and capture CIRAM NT0 + attributes + PALRAM once the
picture has been stable for STABLE_WINDOW frames.

Writes engine/out/nes_uw_level{L}_quest{Q}_{rom}.json in the probe's
format, which engine/tools/uw_aggregate.py merges for gen_uw_blob.py.

Usage:
    python tools/nesemu/zelda_uw_dump.py --rom <rom.nes> [--rom-id orig]
        [--levels 1-9] [--quests 1,2]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from nesemu.nes import Nes  # noqa: E402

IS_UPDATING_MODE = 0x0011
CUR_LEVEL = 0x0010
GAME_MODE = 0x0012
GAME_SUB = 0x0013
CUR_SAVE_SLOT = 0x0016
TARGET_MODE = 0x005B
DOORWAY_DIR = 0x0053
TRIGGERED_DOOR_CMD = 0x0054
TRIGGERED_DOOR_DIR = 0x0055
ROOM_TRANS = 0x004C
ROOM_ID = 0x00EB
NEXT_ROOM_ID = 0x00EC
CUR_OPENED_DOORS = 0x00EE
CUR_PPU_MASK = 0x00FE
OPEN_DOORWAY_MASK = 0x033F
NAME_PROGRESS = 0x0421
FADE_CYCLE = 0x051C
PREV_OPENED_DOORS = 0x0521
SPAWN_CYCLE = 0x0524
CUR_EDGE_SPAWN = 0x0525
CAVE_SOURCE_ROOM = 0x0526
CELLAR_SRC_ROOM = 0x0527
TARGET_MIRROR = 0x0602
LEVEL_INFO_START_ROOM = 0x6BAD
QUEST_NUMBERS = 0x062D
SAVE_ACTIVE = (0x0633, 0x0634, 0x0635)

MAX_BOOT_FRAMES = 20000
MAX_WARP_FRAMES = 1200
MAX_SETTLE_FRAMES = 1500
STABLE_WINDOW = 12


class Pad:
    """The probe's one-button scheduler (hold N frames, then release M)."""

    def __init__(self):
        self.button = None
        self.hold_left = 0
        self.release_left = 0
        self.release_after = 0

    def schedule(self, button, hold=1, release=8):
        if self.hold_left > 0 or self.release_left > 0:
            return
        self.button, self.hold_left, self.release_left, self.release_after = button, hold, 0, release

    def buttons(self):
        if self.hold_left > 0 and self.button:
            self.hold_left -= 1
            if self.hold_left == 0:
                self.release_left = self.release_after
            return (self.button,)
        if self.release_left > 0:
            self.release_left -= 1
        return ()


def advance(nes, buttons=()):
    nes.set_buttons(*buttons)
    nes.run_frame()


def boot_to_overworld(nes) -> bool:
    u8 = nes.bus
    pad = Pad()
    flow = "boot"
    last_name = u8(NAME_PROGRESS)
    name_events = 0
    selects_ignored, last_select_slot = 0, None
    for _ in range(MAX_BOOT_FRAMES):
        mode, slot, name = u8(GAME_MODE), u8(CUR_SAVE_SLOT), u8(NAME_PROGRESS)
        active = [u8(a) for a in SAVE_ACTIVE]
        if flow == "boot":
            if mode == 0x01:
                flow = "select_register"
            else:
                pad.schedule("Start", 2, 3)
        elif flow == "select_register":
            if slot == 0x03:
                flow = "enter_register"
            else:
                pad.schedule("Down", 1, 10)
        elif flow == "enter_register":
            if mode == 0x0E:
                flow, last_name = "type_name", name
            elif mode == 0x01:
                pad.schedule("Start", 2, 14)
        elif flow == "type_name":
            if name != last_name:
                name_events, last_name = name_events + 1, name
            if name_events >= 5:
                flow = "finish_name"
            else:
                pad.schedule("A", 1, 10)
        elif flow == "finish_name":
            # Original: Select moves the cursor to END, Start registers.
            # Zelda Redux ignores Select here (menu_tweaks.asm "Register mode
            # -- Ignore select button") and registers on Start, so after
            # two Selects that leave the cursor in place, press Start.
            if mode != 0x0E:
                flow = "wait_gameplay"
            elif slot != 0x03 and selects_ignored < 2:
                if pad.hold_left == 0 and pad.release_left == 0:
                    if last_select_slot == slot:
                        selects_ignored += 1
                    last_select_slot = slot
                pad.schedule("Select", 1, 10)
            else:
                pad.schedule("Start", 2, 14)
        elif flow == "wait_gameplay":
            if mode == 0x01:
                flow = "start_game"
        elif flow == "start_game":
            if mode != 0x01:
                flow = "wait_gameplay"
            else:
                target = 0
                if active[0] == 0 and active[1] != 0:
                    target = 1
                elif active[0] == 0 and active[1] == 0 and active[2] != 0:
                    target = 2
                if slot != target:
                    pad.schedule("Down" if target > slot else "Up", 1, 10)
                else:
                    pad.schedule("Start", 2, 14)
        advance(nes, pad.buttons())
        if (u8(CUR_LEVEL) == 0 and u8(GAME_MODE) == 0x05 and u8(GAME_SUB) == 0
                and u8(ROOM_ID) == 0x77 and u8(ROOM_TRANS) == 0):
            for _ in range(30):
                advance(nes)
            return True
    return False


def force_quest(nes, quest):
    if quest == 2:
        nes.poke(QUEST_NUMBERS + nes.bus(CUR_SAVE_SLOT), 1)


def warp_to_level(nes, level) -> bool:
    u8 = nes.bus
    for addr, v in ((CUR_LEVEL, level), (TARGET_MODE, 2), (TARGET_MIRROR, 2),
                    (GAME_MODE, 0x10), (GAME_SUB, 0)):
        nes.poke(addr, v)
    nes.set_buttons()
    for _ in range(MAX_WARP_FRAMES):
        nes.run_frame()
        if u8(CUR_LEVEL) == level and u8(GAME_MODE) == 0x05 and u8(GAME_SUB) == 0:
            stable = 0
            for _ in range(60):
                nes.run_frame()
                if u8(CUR_LEVEL) == level and u8(GAME_MODE) == 0x05 and u8(GAME_SUB) == 0:
                    stable += 1
                    if stable >= 30:
                        return True
                else:
                    stable = 0
    return False


def force_room(nes, target):
    for addr in (ROOM_TRANS, DOORWAY_DIR, TRIGGERED_DOOR_CMD, TRIGGERED_DOOR_DIR,
                 CUR_OPENED_DOORS, OPEN_DOORWAY_MASK, PREV_OPENED_DOORS, SPAWN_CYCLE,
                 CUR_EDGE_SPAWN, CAVE_SOURCE_ROOM, CELLAR_SRC_ROOM, FADE_CYCLE):
        nes.poke(addr, 0)
    for addr, v in ((LEVEL_INFO_START_ROOM, target), (ROOM_ID, target), (NEXT_ROOM_ID, target),
                    (GAME_MODE, 0x03), (GAME_SUB, 0x02), (IS_UPDATING_MODE, 0x00)):
        nes.poke(addr, v)


def nt_rows(nes):
    return [list(nes.ciram[r * 32:(r + 1) * 32]) for r in range(30)]


def settle_and_capture(nes, target, wait_for_ganon=False):
    u8 = nes.bus
    stable, prev = 0, None
    nes.set_buttons()
    for f in range(1, MAX_SETTLE_FRAMES + 1):
        nes.run_frame()
        mask = u8(CUR_PPU_MASK)
        if (u8(GAME_MODE) == 0x05 and u8(GAME_SUB) == 0 and u8(IS_UPDATING_MODE) == 1
                and (not wait_for_ganon or u8(0x0445) == 0x02)
                and (mask & 0x1F) >= 0x18 and u8(ROOM_ID) == target and mask & 0x18 == 0x18):
            state = bytes(nes.ciram[0:0x400])
            if prev is not None and state == prev:
                stable += 1
                if stable >= STABLE_WINDOW:
                    return f, nt_rows(nes), list(nes.ciram[0x3C0:0x400]), list(nes.palram)
            else:
                stable, prev = 0, state
        else:
            stable, prev = 0, None
    return MAX_SETTLE_FRAMES, None, None, None


def required_rooms(level: int, quest: int, manifest: dict, rom_id: str = "orig") -> list[int]:
    """Rooms the traversal-derived list omits but the game reaches: the
    boss and Triforce rooms, and in level 9 each Patra room plus every room
    a Patra-room door leads to (formerly tools/converter/inject_boss_rooms.py,
    which synthesized them; here they are captured like any other room)."""
    rooms = {int(manifest[k], 0) for k in ("boss_room_id", "triforce_room_id")
             if manifest.get(k) is not None}
    if level != 9 and rom_id != "orig":
        return sorted(rooms)
    sys.path.insert(0, str(ROOT / "tools" / "converter"))
    import extract_uw_collision as X  # noqa: PLC0415
    data = X.parse_dungeons_c(ROOT / "data" / "rooms" / "dungeons.c")
    tables, _ = X.load_manifest(ROOT / "data" / "rooms" / "MANIFEST.json")
    if rom_id == "orig":
        # T-243: traversal manifests omit reachable rooms (displayed Q2 L5 $01/$02).
        # NES LevelInfo/AttrsA-D own start, reward, cellar endpoints and doors.
        from collections import deque
        li_off, li_size = tables[f"LevelInfoUW{level}"]
        li = list(data[li_off:li_off + li_size])
        if quest == 2:
            patch_off, patch_size = tables[f"LevelInfoUWQ2Replacements{level}"]
            li[0x29:0x29 + patch_size + 1] = data[patch_off:patch_off + patch_size + 1]
        off = tables[f"LevelBlock{'UW1' if level <= 6 else 'UW2'}Q{quest}"][0]
        cellars = {r for r in li[0x34:0x3e] if r < 128}
        rooms.update(r for r in (li[0x2f], li[0x30], li[0x3e]) if r < 128)
        for c in cellars:
            rooms.update((data[off + c], data[off + 128 + c]))
        if level == 9:
            rooms.update(r for r in range(128) if
                ((data[off + 256 + r] & 63) | ((data[off + 384 + r] & 128) >> 1)) in (0x47, 0x48))
        seen = set()
        queue = deque(sorted(r for r in rooms if 0 <= r < 128 and r not in cellars))
        while queue:
            r = queue.popleft()
            if r in seen or r in cellars:
                continue
            seen.add(r)
            a, b = data[off + r], data[off + 128 + r]
            for door, delta in (((a >> 5) & 7, -16), ((a >> 2) & 7, 16),
                                ((b >> 2) & 7, 1), ((b >> 5) & 7, -1)):
                dest = r + delta
                if door == 1 or not 0 <= dest < 128 or (abs(delta) == 1 and r // 16 != dest // 16):
                    continue
                if dest not in seen and dest not in cellars:
                    queue.append(dest)
        return sorted(seen)
    off = tables[f"LevelBlockUW2Q{quest}"][0]
    for room in range(128):
        enemy = (data[off + 0x100 + room] & 0x3F) | ((data[off + 0x180 + room] & 0x80) >> 1)
        if enemy not in (0x47, 0x48):
            continue
        rooms.add(room)
        a, b = data[off + room], data[off + 0x80 + room]
        doors = {"N": (a >> 5) & 7, "S": (a >> 2) & 7, "E": (b >> 2) & 7, "W": (b >> 5) & 7}
        for d, delta in (("N", -16), ("S", 16), ("E", 1), ("W", -1)):
            dest = room + delta
            if doors[d] == 1 or not 0 <= dest < 128:
                continue
            if d in "EW" and dest // 16 != room // 16:
                continue
            rooms.add(dest)
    return sorted(rooms)


def dump_level(rom: bytes, rom_id: str, level: int, quest: int) -> dict:
    rooms_json = ROOT / "engine" / "data" / f"uw_level{level}_quest{quest}_rooms.json"
    manifest = json.loads(rooms_json.read_text(encoding="utf-8"))
    rooms = [int(x, 16) for x in manifest["rooms"]]
    # Appended after the probe's list so its captures are unaffected. A room
    # any same-block level already captures is skipped: the runtime's
    # same-block lookup reaches that entry (uw_render.c find_blob_entry).
    covered = set()
    for lv in (range(1, 7) if level <= 6 else range(7, 10)):
        other = ROOT / "engine" / "data" / f"uw_level{lv}_quest{quest}_rooms.json"
        covered.update(int(x, 16) for x in json.loads(other.read_text(encoding="utf-8"))["rooms"])
    rooms += [r for r in required_rooms(level, quest, manifest, rom_id) if r not in covered]
    nes = Nes(rom)
    out = {"system_id": "NES", "boot_ok": False, "warp_ok": False, "rom": rom_id,
           "map_name": "nesemu", "level": level, "quest": quest,
           "rooms_requested": len(rooms), "results": []}
    if not boot_to_overworld(nes):
        out["fatal_error"] = "boot_failed"
        return out
    out["boot_ok"] = True
    force_quest(nes, quest)
    if not warp_to_level(nes, level):
        out["fatal_error"] = "warp_failed"
        return out
    out["warp_ok"] = True
    ganon = level == 9 and quest == 1 and rom_id == "orig"
    for rid in rooms:
        force_room(nes, rid)
        frames, nt, attr, pal = settle_and_capture(nes, rid, ganon and rid == 0x42)
        if nt is None:
            out["results"].append({"room_id": rid, "code": "settle_timeout", "settle_frames": frames})
            continue
        out["results"].append({"room_id": rid, "cur_level": nes.bus(CUR_LEVEL),
                               "game_mode": nes.bus(GAME_MODE), "game_sub": nes.bus(GAME_SUB),
                               "quest": quest, "ppu_mask": nes.bus(CUR_PPU_MASK),
                               "settle_frames": frames, "nt": nt, "attr": attr, "palram": pal})
    out["rooms_captured"] = sum(1 for r in out["results"] if "nt" in r)
    out["rooms_failed"] = len(out["results"]) - out["rooms_captured"]
    return out


def parse_range(s, lo, hi):
    vals = set()
    for part in s.split(","):
        if "-" in part:
            a, b = part.split("-")
            vals.update(range(int(a), int(b) + 1))
        elif part:
            vals.add(int(part))
    return sorted(v for v in vals if lo <= v <= hi)


def _job(job):
    rom_path, rom_id, level, quest, out_dir = job
    res = dump_level(Path(rom_path).read_bytes(), rom_id, level, quest)
    path = Path(out_dir) / f"nes_uw_level{level}_quest{quest}_{rom_id}.json"
    path.write_text(json.dumps(res) + "\n", encoding="utf-8")
    return level, quest, res


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--rom-id", default="orig", choices=("orig", "redux"))
    ap.add_argument("--levels", default="1-9")
    ap.add_argument("--quests", default="1,2")
    ap.add_argument("--out-dir", type=Path, default=ROOT / "engine" / "out")
    ap.add_argument("--jobs", type=int, default=0, help="worker processes (0 = CPU count)")
    args = ap.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    jobs = [(str(args.rom), args.rom_id, lv, q, str(args.out_dir))
            for lv in parse_range(args.levels, 1, 9) for q in parse_range(args.quests, 1, 2)]
    # Each level/quest boots its own machine (as each probe run launched a
    # fresh emulator), so the jobs are independent.
    from concurrent.futures import ProcessPoolExecutor  # noqa: PLC0415
    rc = 0
    with ProcessPoolExecutor(max_workers=args.jobs or None) as ex:
        for level, quest, res in ex.map(_job, jobs):
            print(f"L{level}Q{quest} {args.rom_id}: boot={res['boot_ok']} warp={res['warp_ok']} "
                  f"captured={res.get('rooms_captured', 0)}/{res['rooms_requested']}")
            if res.get("fatal_error") or res.get("rooms_failed"):
                rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
