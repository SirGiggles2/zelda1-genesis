"""Native tool selection and its build/music consumers; no game data required."""
from __future__ import annotations

import contextlib
import importlib
import importlib.util
import io
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))


class PlatformToolTests(unittest.TestCase):
    def setUp(self):
        try:
            self.tools = importlib.import_module("platform_tools")
        except ModuleNotFoundError:
            self.fail("native platform_tools resolver is missing")
        scratch = Path(__file__).resolve().parents[2] / "build" / "t413" / "native-tool-tests"
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="native tools with spaces ", dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.env = patch.dict(os.environ, {"PATH": ""}, clear=True)
        self.env.start()
        self.addCleanup(self.env.stop)
        if os.name == "nt":
            # Windows has no POSIX executable mode for extensionless fixtures.
            access = patch.object(self.tools.os, "access", return_value=True)
            access.start()
            self.addCleanup(access.stop)

    def executable(self, rel, content=b"#!/bin/sh\nexit 0\n"):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        path.chmod(0o755)
        return path

    def native_set(self):
        return {name: self.executable("toolchains/native/bin/" + filename) for name, filename in (
            ("gcc", "m68k-elf-gcc"), ("objcopy", "m68k-elf-objcopy"),
            ("nm", "m68k-elf-nm"), ("xgmtool", "xgmtool"), ("node", "node"))}

    def load_debug(self):
        path = Path(__file__).resolve().parents[2] / "tools/build/build_rom.py"
        spec = importlib.util.spec_from_file_location("native_test_build_rom", path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_windows_prefers_pinned_sgdk_over_path(self):
        with patch.object(sys, "platform", "win32"):
            for name in ("gcc", "objcopy", "nm", "xgmtool"):
                expected = self.executable(f"sgdk/bin/{name}.exe")
                self.assertEqual(self.tools.tool_path(name, self.root), expected)
                self.assertEqual(self.tools.tool_command(name, self.root), [str(expected)])

    def test_windows_path_host_compiler_is_not_a_cross_toolchain_fallback(self):
        host = self.executable("host tools/gcc.exe", b"MZhost compiler")
        os.environ["PATH"] = str(host.parent)
        with patch.object(sys, "platform", "win32"):
            with self.assertRaises(self.tools.ToolNotFoundError):
                self.tools.tool_path("gcc", self.root)

    def test_linux_and_macos_use_bundled_native_tools(self):
        expected = self.native_set()
        for platform in ("linux", "darwin"):
            with self.subTest(platform=platform), patch.object(sys, "platform", platform):
                for name, path in expected.items():
                    self.assertEqual(self.tools.tool_command(name, self.root), [str(path)])

    def test_unix_can_find_target_prefixed_tools_on_path(self):
        gcc = self.executable("system bin/m68k-elf-gcc")
        os.environ["PATH"] = str(gcc.parent)
        with patch.object(sys, "platform", "linux"):
            self.assertEqual(self.tools.tool_path("gcc", self.root), gcc)

    def test_host_gcc_and_windows_tools_are_never_unix_fallbacks(self):
        host = self.executable("system bin/gcc")
        self.executable("sgdk/bin/gcc.exe", b"MZwindows")
        self.executable("system bin/m68k-elf-gcc.exe", b"MZwindows")
        os.environ["PATH"] = str(host.parent)
        with patch.object(sys, "platform", "linux"):
            with self.assertRaises(self.tools.ToolNotFoundError) as failure:
                self.tools.tool_command("gcc", self.root)
            self.assertIn("m68k-elf-gcc", str(failure.exception))
            self.assertIn("ZELDA_TOOLCHAIN_BIN", str(failure.exception))

    def test_override_takes_priority_and_preserves_spaces(self):
        self.native_set()
        expected = self.executable("custom compiler/bin/m68k-elf-gcc")
        os.environ["ZELDA_TOOLCHAIN_BIN"] = str(expected.parent)
        with patch.object(sys, "platform", "darwin"):
            self.assertEqual(self.tools.tool_command("gcc", self.root), [str(expected)])

    def test_missing_override_does_not_silently_use_bundled_tools(self):
        self.native_set()
        os.environ["ZELDA_TOOLCHAIN_BIN"] = str(self.root / "missing override")
        with patch.object(sys, "platform", "linux"):
            with self.assertRaises(self.tools.ToolNotFoundError) as failure:
                self.tools.tool_path("gcc", self.root)
            self.assertIn("ZELDA_TOOLCHAIN_BIN", str(failure.exception))

    def test_renamed_windows_executable_is_rejected_on_unix(self):
        self.executable("toolchains/native/bin/xgmtool", b"MZwindows")
        with patch.object(sys, "platform", "darwin"):
            with self.assertRaises(self.tools.ToolNotFoundError):
                self.tools.tool_command("xgmtool", self.root)

    def test_nonexecutable_native_tool_is_rejected(self):
        self.native_set()
        with patch.object(sys, "platform", "linux"), patch.object(self.tools.os, "access", return_value=False):
            with self.assertRaises(self.tools.ToolNotFoundError):
                self.tools.tool_path("gcc", self.root)

    def test_node_can_use_path_without_cross_compiler_override(self):
        node = self.executable("runtime bin/node")
        os.environ.update(PATH=str(node.parent), ZELDA_TOOLCHAIN_BIN=str(self.root / "compiler only"))
        with patch.object(sys, "platform", "linux"):
            self.assertEqual(self.tools.tool_path("node", self.root), node)

    def test_unknown_tool_is_rejected(self):
        with self.assertRaises(ValueError):
            self.tools.tool_path("wine", self.root)

    def test_debug_import_is_safe_before_native_tools_are_installed(self):
        with patch.object(sys, "platform", "linux"):
            debug = self.load_debug()
        self.assertTrue(callable(debug.compile_c))

    def test_debug_gcc_prefix_uses_native_bin_and_unix_separator(self):
        paths = self.native_set()
        debug = self.load_debug()
        with patch.object(debug, "ROOT", self.root), patch.object(sys, "platform", "linux"):
            self.assertEqual(debug.gcc_prefix(), [str(paths["gcc"]), "-B", str(paths["gcc"].parent) + "/"])

    def test_debug_missing_tools_returns_error_before_generating_files(self):
        debug = self.load_debug()
        with patch.object(debug, "ROOT", self.root), patch.object(sys, "platform", "linux"), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(debug.main(), 1)
        self.assertEqual(list(self.root.iterdir()), [])

    def test_studio_sources_and_saved_mix_invalidate_generated_music(self):
        debug = self.load_debug()
        music_dir = self.root / "build/nes_music"
        music_dir.mkdir(parents=True)
        table = music_dir / "nes_music.c"
        (self.root / "Legend of Zelda, The (USA).nes").write_bytes(b"abc")
        (music_dir / "capture.json").write_text('{"rom_sha256":"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"}')
        changes = ("tools/converter/studio_music.py", "tools/converter/studio_render.js",
                   "tools/music_studio/web/render.js", "data/music/builder_mix.json")
        sources = changes + ("tools/converter/nes_music.py", "tools/converter/local_music.py")
        for rel in sources:
            source = self.root / rel
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_text("generator fixture")

        def regenerate(argv, **kwargs):
            table.write_text("regenerated music")
            return subprocess.CompletedProcess(argv, 0)

        with patch.object(debug, "ROOT", self.root), patch.object(debug, "NES_MUSIC_DIR", music_dir), patch.object(debug.subprocess, "run", regenerate), contextlib.redirect_stdout(io.StringIO()):
            for changed in changes:
                with self.subTest(changed=changed):
                    table.write_text("stale music")
                    os.utime(table, (20, 20))
                    for rel in sources:
                        os.utime(self.root / rel, (10, 10))
                    os.utime(self.root / changed, (30, 30))
                    self.assertTrue(debug.ensure_nes_music())
                    self.assertEqual(table.read_text(), "regenerated music")

    def test_builder_check_accepts_native_tools_and_requires_pinned_library(self):
        import build
        self.native_set()
        with patch.object(build, "ROOT", self.root), patch.object(sys, "platform", "linux"), contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(build.check_toolchain(), 2)
            for name in ("libmd.a", "libgcc.a"):
                library = self.root / "sgdk" / "lib" / name
                library.parent.mkdir(parents=True, exist_ok=True)
                library.touch()
            self.assertEqual(build.check_toolchain(), 0)
            (self.root / "toolchains/native/bin/m68k-elf-nm").unlink()
            self.assertEqual(build.check_toolchain(), 2)

    def test_music_compiler_runs_native_command_with_spaced_arguments(self):
        import local_music
        self.assertTrue(hasattr(local_music, "tool_command"), "music compiler must use the common native resolver")
        vgm = self.root / "title music.vgm"
        vgm.write_bytes(b"private music fixture")
        output = self.root / "compiled song.bin"
        compiler = self.root / "native compiler fixture.py"
        compiler.write_text("from pathlib import Path\nimport sys\n"
                            "assert sys.argv[3:] == ['-n', '-s']\n"
                            "Path(sys.argv[2]).write_bytes(b'compiled:' + Path(sys.argv[1]).read_bytes())\n")
        with patch.object(sys, "platform", "linux"), patch.object(local_music, "tool_command", return_value=[sys.executable, str(compiler)]):
            local_music.xgm_compile(vgm, output)
        self.assertEqual(output.read_bytes(), b"compiled:private music fixture")

    def test_external_redux_path_reaches_extraction_children(self):
        import build
        rom = self.root / "original.nes"
        redux = self.root / "external Redux ROM.nes"
        rom.write_bytes(b"ROM fixture")
        redux.write_bytes(b"Redux fixture")
        extractor = self.root / "probe child.py"
        extractor.write_text("from pathlib import Path\nimport os\n"
                             "Path('observed-redux.txt').write_text(os.environ.get('ZELDA_REDUX_ROM', 'missing'))\n")
        with patch.object(build, "ROOT", self.root), patch.object(sys, "argv", ["build.py", str(rom), "--redux", str(redux), "--extract-only"]), patch.object(build, "validate_rom", return_value=0), patch.object(build, "check_python_deps", return_value=0), patch.object(build, "plan_steps", return_value=[("extract fixture", [sys.executable, str(extractor)])]), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(build.main(), 0)
        self.assertEqual((self.root / "observed-redux.txt").read_text(), str(redux))


if __name__ == "__main__":
    unittest.main(verbosity=2)
