"""Writable packaged workspace and safe source-runtime setup controls."""
import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import package_workspace as workspace
import package_app


class WorkspaceTests(unittest.TestCase):
    def test_packager_includes_stdlib_used_only_by_child_scripts(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "extract.py").write_text("import wave\nfrom xml.etree import ElementTree\nimport scipy.signal\n")
            modules = package_app.script_stdlib_imports(root)
            self.assertIn("wave", modules)
            self.assertIn("xml.etree.ElementTree", modules)
            self.assertNotIn("scipy.signal", modules)

    def test_payload_copies_and_reuses_a_writable_release(self):
        with tempfile.TemporaryDirectory(prefix="builder paths ") as td:
            base = Path(td)
            payload = base / "read only payload"
            payload.mkdir()
            script = payload / "script.py"
            script.write_text("print('ready')")
            manifest = {"release": "unit-release", "files": {
                "script.py": hashlib.sha256(script.read_bytes()).hexdigest()}}
            (payload / ".builder-release.json").write_text(json.dumps(manifest))
            result = workspace.install_payload(payload, base / "user cache")
            self.assertEqual((result / "script.py").read_bytes(), script.read_bytes())
            (result / "build").mkdir()
            self.assertEqual(workspace.install_payload(payload, base / "user cache"), result)
            self.assertTrue((result / "build").is_dir())

    def test_corrupted_payload_is_not_installed(self):
        with tempfile.TemporaryDirectory() as td:
            base = Path(td)
            payload = base / "payload"
            payload.mkdir()
            (payload / "x").write_text("changed")
            (payload / ".builder-release.json").write_text(json.dumps({
                "release": "bad", "files": {"x": "0" * 64}}))
            with self.assertRaisesRegex(ValueError, "damaged"):
                workspace.install_payload(payload, base / "cache")
            self.assertFalse((base / "cache" / "bad").exists())

    def test_manifest_cannot_escape_payload(self):
        with tempfile.TemporaryDirectory() as td:
            base = Path(td)
            (base / ".builder-release.json").write_text(json.dumps({
                "release": "bad", "files": {"../outside": "0" * 64}}))
            with self.assertRaises(ValueError):
                workspace.install_payload(base, base / "cache")

    def test_source_checkout_is_used_without_copying(self):
        with patch.object(workspace.sys, "frozen", False, create=True):
            self.assertEqual(workspace.prepare_workspace(), workspace.ROOT)


if __name__ == "__main__":
    unittest.main()
