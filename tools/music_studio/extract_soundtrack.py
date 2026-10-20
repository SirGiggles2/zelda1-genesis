"""Extract supported user-ROM songs as Genesis VGMs for the studio library.

Runs the real NES sound driver through the existing builder capture/translation
pipeline. No ROM or derived music is distributed with this tool.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools" / "converter"))
import nes_music
from build import SUPPORTED_ROMS, SUPPORTED_REDUX_ROMS
from convert_nes_music import SONGS


def extract_soundtrack(rom: Path, out: Path):
    raw = rom.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest not in SUPPORTED_ROMS and digest not in SUPPORTED_REDUX_ROMS:
        raise ValueError(f"Unsupported music ROM SHA256 {digest}")
    profile = "Redux" if digest in SUPPORTED_REDUX_ROMS else "Zelda"
    report = {"rom_sha256": digest, "profile": profile,
              "pipeline": "builder.nes_music.capture + translate", "songs": []}
    pending = []
    songs = [(sid, request, title, 9 if sid == "level9" else 0) for sid, request, title in SONGS]
    if profile == "Redux":
        # Authenticated Redux source: code/music/dungeon_music.asm .songs maps
        # levels 1/5, 2/6, 3/7, 4/8 to these four themes, in both quests.
        songs = [entry for entry in songs if entry[0] != "dungeon"]
        songs[2:2] = [("dungeon_zelda", 0x40, "Dungeon - Zelda 1", 1),
                      ("dungeon_new_light", 0x40, "Dungeon - A New Light", 2),
                      ("dungeon_perils_darkness", 0x40, "Dungeon - Perils of Darkness", 3),
                      ("dungeon_triforce_power", 0x40, "Dungeon - Triforce Power", 4)]
    for sid, request, title, level in songs:
        frames, loop = nes_music.capture(raw, request, level=level)
        data = nes_music.translate(frames, loop)
        filename = f"{profile} - {title.replace('/', '-')}.vgm"
        target = out / filename
        if target.exists() and target.read_bytes() != data:
            raise FileExistsError(f"Preserving existing music: {target}. Choose another output folder.")
        report["songs"].append({"id": sid, "title": title, "request": request,
            "frames": len(frames), "rendered_frames": len(frames) + (45 if loop is None else 0), "loop_frame": loop, "level": level, "file": filename,
            "vgm_sha256": hashlib.sha256(data).hexdigest(),
            "apu_writes_sha256": hashlib.sha256(json.dumps(frames,separators=(",",":")).encode()).hexdigest()})
        pending.append((target, data))
        print(f"{filename}: {len(frames)} NES frames, loop {loop}", flush=True)
    out.mkdir(parents=True, exist_ok=True)
    for target, data in pending:
        temp = target.with_suffix(".vgm.tmp"); temp.write_bytes(data); temp.replace(target)
    (out / "extraction.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(f"Extracted {len(pending)} Genesis VGMs to {out}", flush=True)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=REPO / "VGMS/Redux")
    args = parser.parse_args()
    try:
        extract_soundtrack(args.rom,args.out)
    except (OSError, ValueError) as exc:
        parser.exit(1, f"Music extraction failed: {exc}\n")


if __name__ == "__main__":
    main()
