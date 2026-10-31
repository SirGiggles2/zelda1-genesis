"""ROM-free rendering controls; optional local ROM/export reproduction checks."""
import copy
import importlib
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class StudioMusicTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(importlib.util.find_spec("studio_music"),
                             "ROM-local Studio renderer has not been implemented")
        self.music = importlib.import_module("studio_music")

    def song(self, *, loop=None):
        # Authored test tone; contains no extracted game notes.
        return {"id": "title", "frames": 8, "loop_frame": loop,
                "voices": {"melody": [[0, "on", 69, 15], [4, "off"]],
                           "harmony": [], "bass": [], "drums": []}}

    def test_mix_changes_sound_without_changing_duration_or_loop(self):
        settings = self.music.load_settings()
        changed = copy.deepcopy(settings)
        changed["mixes"]["title"]["melody"]["octave"] += 1
        with tempfile.TemporaryDirectory() as folder:
            a = self.music.render_songs([self.song(loop=2)], Path(folder)/"a", settings)["title"]
            b = self.music.render_songs([self.song(loop=2)], Path(folder)/"b", changed)["title"]
            first, second = Path(a["path"]).read_bytes(), Path(b["path"]).read_bytes()
        self.assertNotEqual(first, second)
        self.assertEqual(struct.unpack_from("<I", first, 0x18)[0], 8*735)
        self.assertEqual(struct.unpack_from("<I", first, 0x20)[0], 6*735)
        self.assertEqual(first[0x18:0x24], second[0x18:0x24])

    def test_one_shot_release_tail_does_not_extend_gameplay_timer(self):
        with tempfile.TemporaryDirectory() as folder:
            row = self.music.render_songs([self.song()], Path(folder))["title"]
            data = Path(row["path"]).read_bytes()
        self.assertEqual(row["frames"], 8)
        self.assertIsNone(row["loop_frame"])
        self.assertEqual(struct.unpack_from("<I", data, 0x18)[0], (8+45)*735)
        self.assertEqual(struct.unpack_from("<I", data, 0x1c)[0], 0)

    def test_repeated_render_is_byte_identical(self):
        with tempfile.TemporaryDirectory() as folder:
            a = self.music.render_songs([self.song()], Path(folder)/"a")["title"]
            b = self.music.render_songs([self.song()], Path(folder)/"b")["title"]
            self.assertEqual(Path(a["path"]).read_bytes(), Path(b["path"]).read_bytes())

    def test_settings_contain_only_mix_and_instrument_controls(self):
        settings = self.music.load_settings()
        self.assertEqual(set(settings), {"version", "instruments", "mixes"})
        self.assertEqual(set(settings["mixes"]),
                         {"title", "overworld", "dungeon", "level9", "ganon",
                          "triforce", "zelda", "item", "ending"})
        forbidden = {"notes", "voices", "samples", "midiSongs", "workspace", "stats"}
        def inspect(value):
            if isinstance(value, dict):
                self.assertFalse(forbidden.intersection(value))
                for item in value.values():
                    inspect(item)
            elif isinstance(value, list):
                for item in value:
                    inspect(item)
            elif isinstance(value, str):
                self.assertNotIn("C:\\", value)
        inspect(settings)

    def test_unsupported_rom_rejected_before_capture(self):
        with tempfile.TemporaryDirectory() as folder:
            rom = Path(folder)/"bad.nes"
            rom.write_bytes(b"not a game")
            with self.assertRaisesRegex(ValueError, "unsupported.*ROM"):
                self.music.generate_vgms(rom, Path(folder)/"out")
            self.assertFalse((Path(folder)/"out").exists())

    @unittest.skipUnless(os.environ.get("ZELDA_TEST_ROM"), "set ZELDA_TEST_ROM for local reproduction")
    def test_builder_cli_uses_saved_mix_by_default(self):
        exports = Path(os.environ.get("ZELDA_TEST_EXPORTS", ROOT/"build/music_studio/vgm"))
        with tempfile.TemporaryDirectory() as folder:
            result = subprocess.run([sys.executable, str(ROOT/"tools/converter/nes_music.py"),
                                     "--rom", os.environ["ZELDA_TEST_ROM"], "--out", folder,
                                     "--vgm-only"], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual((Path(folder)/"title.vgm").read_bytes(),
                             (exports/"Title _ Intro.vgm").read_bytes())

    @unittest.skipUnless(os.environ.get("ZELDA_TEST_ROM"), "set ZELDA_TEST_ROM for local reproduction")
    def test_user_rom_recreates_oct29_exports_and_timing(self):
        exports = Path(os.environ.get("ZELDA_TEST_EXPORTS", ROOT/"build/music_studio/vgm"))
        with tempfile.TemporaryDirectory() as folder:
            report = self.music.generate_vgms(Path(os.environ["ZELDA_TEST_ROM"]), Path(folder))
            for sid, filename, frames, loop in (
                    ("title", "Title _ Intro.vgm", 4800, 0),
                    ("overworld", "Overworld.vgm", 2304, 384),
                    ("level9", "Death Mountain (Level 9).vgm", 1344, 0),
                    ("ending", "Ending.vgm", 3840, 1920)):
                row = report["songs"][sid]
                self.assertEqual((row["frames"], row["loop_frame"]), (frames, loop))
                self.assertEqual((Path(folder)/row["file"]).read_bytes(),
                                 (exports/filename).read_bytes(), sid)

    @unittest.skipUnless(os.environ.get("ZELDA_TEST_REDUX"), "set ZELDA_TEST_REDUX for native XGM bank check")
    def test_redux_bank_keeps_twelve_selection_indices_and_saved_mix(self):
        spec = importlib.util.spec_from_file_location("test_redux_builder", ROOT/"tools/audio/build_redux_music.py")
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            try:
                builder.generate(rom=Path(os.environ["ZELDA_TEST_REDUX"]), out=out)
            except TypeError as exc:
                self.fail(f"Redux builder does not yet accept the user's ROM and isolated output: {exc}")
            table = (out/"redux_music.c").read_text()
            self.assertIn("redux_music_count = 12u", table)
            for index, sid in enumerate(("overworld", "dungeon_zelda", "dungeon_new_light",
                                         "dungeon_perils_darkness", "dungeon_triforce_power",
                                         "level9", "ganon", "triforce", "zelda", "item", "ending", "title")):
                self.assertIn(f"/* ${index:02X} {sid}:", table)
            self.assertIn("{ 0x09u, redux_music_song_09, 85u, 0u }", table)
            self.assertIn("{ 0x00u, redux_music_song_00, 2304u, 1u }", table)
            exports = Path(os.environ.get("ZELDA_TEST_EXPORTS", ROOT/"build/music_studio/vgm"))
            self.assertEqual((out/"vgm/overworld.vgm").read_bytes(), (exports/"Overworld.vgm").read_bytes())
            # One-shots remain looping choices, while source timing stays finite.
            data = (out/"item_loop.vgm").read_bytes()
            self.assertGreater(struct.unpack_from("<I", data, 0x1c)[0], 0)
            self.assertEqual(struct.unpack_from("<I", data, 0x18)[0], 130*735)


if __name__ == "__main__":
    unittest.main()
