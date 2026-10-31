"""Archive a ready app without losing macOS bundle or Unix executable bits."""
import argparse
import hashlib
import platform
import shutil
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    name = "Zelda-Genesis-Builder-" + args.label
    folder = args.source / "Zelda Genesis Builder"
    if platform.system() == "Darwin":
        app = args.source / "Zelda Genesis Builder.app"
        archive = args.out / (name + ".zip")
        subprocess.run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(app), str(archive)], check=True)
    else:
        extension = "zip" if platform.system() == "Windows" else "gztar"
        archive = Path(shutil.make_archive(str(args.out / name), extension, folder.parent, folder.name))
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(archive.suffix + ".sha256").write_text(f"{digest}  {archive.name}\n")
    print(archive)


if __name__ == "__main__":
    main()
