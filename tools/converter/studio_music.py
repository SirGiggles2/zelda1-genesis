"""Recreate the saved Zelda Studio arrangements from the user's local ROM.

Public inputs contain only mix controls and selected FM instrument parameters.
The real NES sound driver supplies every note. No extracted songs are bundled.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SETTINGS = ROOT / "data/music/builder_mix.json"
SONGS = (("title", 0x80, 0), ("overworld", 0x01, 0),
         ("dungeon", 0x40, 1), ("level9", 0x20, 9),
         ("ganon", 0x02, 0), ("triforce", 0x04, 0),
         ("item", 0x08, 0), ("ending", 0x10, 0), ("zelda", 0x06, 0))


def load_settings(path: Path = SETTINGS):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def render_songs(songs: list[dict], out: Path, settings: dict | None = None):
    """Render transient voice events; return source timing, never release-tail timing."""
    from platform_tools import tool_path
    settings = load_settings() if settings is None else settings
    process = subprocess.run(
        [str(tool_path("node", root=ROOT)), str(ROOT / "tools/converter/studio_render.js"), str(out.resolve())],
        input=json.dumps({"songs": songs, "settings": settings}),
        text=True, capture_output=True, timeout=120)
    if process.returncode:
        raise ValueError(f"Studio music renderer failed: {process.stderr.strip()[-1600:]}")
    return {song["id"]: {"path": str(out / (song["id"] + ".vgm")),
                          "frames": song["frames"], "loop_frame": song["loop_frame"]}
            for song in songs}


def voice_song(sid, frames, loop, *, mix_id=None):
    from nes_music import apu_states, events
    states = apu_states(frames)
    drums = []
    for frame, row in enumerate(states):
        period, volume, trigger = row[3]
        if trigger and volume:
            length = 1
            while frame + length < len(states) and states[frame + length][3][1] and not states[frame + length][3][2]:
                length += 1
            drums.append((frame, "hit", period & 15, volume, length, (period >> 7) & 1))
    return {"id": sid, "mix_id": mix_id or sid,
            "frames": len(frames), "loop_frame": loop,
            "voices": {"melody": events(states, 1), "harmony": events(states, 0),
                       "bass": events(states, 2), "drums": drums}}


def generate_vgms(rom: Path, out: Path, *, profile="original", settings: Path = SETTINGS):
    from build import SUPPORTED_ROMS, SUPPORTED_REDUX_ROMS
    from nes_music import capture
    raw = rom.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    supported = SUPPORTED_REDUX_ROMS if profile == "redux" else SUPPORTED_ROMS
    if profile not in ("original", "redux") or digest not in supported:
        raise ValueError(f"unsupported {profile} music input ROM sha256 {digest}")
    selected = list(SONGS)
    if profile == "redux":
        selected[2:3] = [("dungeon_zelda", 0x40, 1), ("dungeon_new_light", 0x40, 2),
                         ("dungeon_perils_darkness", 0x40, 3), ("dungeon_triforce_power", 0x40, 4)]
    songs = []
    report = {"rom_sha256": digest, "profile": profile,
              "settings_sha256": hashlib.sha256(settings.read_bytes()).hexdigest(), "songs": {}}
    for sid, request, level in selected:
        frames, loop = capture(raw, request, level=level)
        mix_id = "dungeon" if sid.startswith("dungeon_") else sid
        songs.append(voice_song(sid, frames, loop, mix_id=mix_id))
        report["songs"][sid] = {"request": request, "level": level,
                                 "frames": len(frames), "loop_frame": loop, "file": sid + ".vgm"}
        print(f"  Studio {sid}: {len(frames)} NES frames, loop {loop}", flush=True)
    render_songs(songs, out, load_settings(settings))
    for row in report["songs"].values():
        row["vgm_sha256"] = hashlib.sha256((out / row["file"]).read_bytes()).hexdigest()
    (out / "capture.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def generate(rom: Path, out: Path, *, compile_xgm=True):
    report = generate_vgms(rom, out)
    if compile_xgm:
        from local_music import emit_table
        # Dungeon cover choices retain their existing runtime selection route.
        selected = {sid: row for sid, row in report["songs"].items() if sid != "dungeon"}
        emit_table({row["request"]: (sid, out / row["file"]) for sid, row in selected.items()},
                   out / "nes_music.c", "nes_music",
                   {row["request"]: (row["frames"], row["loop_frame"] is not None)
                    for row in selected.values()})
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=ROOT / "build/nes_music")
    parser.add_argument("--vgm-only", action="store_true")
    args = parser.parse_args()
    try:
        generate(args.rom, args.out, compile_xgm=not args.vgm_only)
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        parser.exit(1, f"ERROR: music from your ROM: {exc}\n")


if __name__ == "__main__":
    main()
