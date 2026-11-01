"""Downloaded native build inputs must be verified and safely unpacked."""
import io
import tarfile
import tempfile
import unittest
from pathlib import Path

import prepare_native_tools as native


class NativeSetupTests(unittest.TestCase):
    def test_platform_names_cover_intel_and_apple_silicon(self):
        self.assertEqual(native.node_archive("Darwin", "arm64"), "node-v22.16.0-darwin-arm64.tar.gz")
        self.assertEqual(native.node_archive("Linux", "x86_64"), "node-v22.16.0-linux-x64.tar.xz")
        self.assertEqual(native.node_archive("Windows", "AMD64"), "node-v22.16.0-win-x64.zip")
        with self.assertRaises(ValueError):
            native.node_archive("Windows", "arm64")

    def test_extract_rejects_parent_traversal(self):
        with tempfile.TemporaryDirectory() as td:
            base = Path(td)
            archive = base / "bad.tar"
            with tarfile.open(archive, "w") as tar:
                member = tarfile.TarInfo("../outside")
                member.size = 4
                tar.addfile(member, io.BytesIO(b"evil"))
            with self.assertRaises(ValueError):
                native.unpack(archive, base / "out")
            self.assertFalse((base / "outside").exists())

    def test_mismatch_removes_only_the_failed_download(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "archive"
            path.write_bytes(b"wrong")
            with self.assertRaisesRegex(ValueError, "checksum"):
                native.verify_download(path, "0" * 64)
            self.assertFalse(path.exists())


if __name__ == "__main__":
    unittest.main()
