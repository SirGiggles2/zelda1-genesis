"""Build a self-contained native builder app from the sanitized public source.

Run on each target OS after prepare_native_tools.py. No user's ROM or generated
music is copied into the app. PyInstaller bundles Python/numpy/scipy/py65; the
same executable also hosts the converter's child Python scripts.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import platform
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def copy_tree(source: Path, target: Path) -> None:
    shutil.copytree(source, target, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns(".git", "__pycache__", "*.pyc"))


def make_payload(root: Path, payload: Path, native: Path) -> dict:
    if payload.exists() and any(payload.iterdir()):
        raise ValueError("The app payload directory must be empty")
    # Use the canonical exporter, so packaging has the same no-game-data rules
    # as public source publication. Child scripts use the public path layout.
    converter = root / "tools" / ("converter" if (root / "engine").exists() else "converter")
    subprocess.run([sys.executable, str(converter / "export_repo.py"), str(payload)], cwd=root, check=True)
    pin = json.loads((root / "tools/sgdk_pin.json").read_text())
    sgdk = root / "sgdk"
    actual = subprocess.check_output(["git", "-C", str(sgdk), "rev-parse", "HEAD"], text=True).strip()
    if actual != pin["pinned_sha"]:
        raise ValueError("Cannot package a different SGDK revision")
    archive = subprocess.check_output(["git", "-C", str(sgdk), "archive", "--format=tar", actual])
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        tar.extractall(payload / "sgdk", filter="data")
    if native.exists():
        copy_tree(native, payload / "toolchains/native")
    # A source snapshot has no Git metadata, so record the digest of its exact
    # pinned files (line endings included) using the existing package verifier.
    sys.path.insert(0, str(root / "tools"))
    from check_sgdk_pin import content_sha256
    pin["content_sha256"] = content_sha256(payload / "sgdk")
    (payload / "tools/sgdk_pin.json").write_text(json.dumps(pin, indent=2) + "\n")
    files = {p.relative_to(payload).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in sorted(payload.rglob("*")) if p.is_file()}
    release = hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()[:20]
    manifest = {"release": release, "host": platform.system(), "arch": platform.machine(), "files": files}
    (payload / ".builder-release.json").write_text(json.dumps(manifest, sort_keys=True) + "\n")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--native-prefix", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--payload-only", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    out = (args.out or root / "build/builder-app").resolve()
    out.mkdir(parents=True, exist_ok=True)
    payload = out / "payload"
    native = (args.native_prefix or root / "toolchains/native").resolve()
    manifest = make_payload(root, payload, native)
    print(f"App payload {manifest['release']}: {len(manifest['files'])} source/tool files", flush=True)
    if args.payload_only:
        return 0
    entry = payload / "tools/converter/launch.py"
    name = "Zelda Genesis Builder"
    command = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--onedir", "--name", name,
               "--distpath", str(out / "dist"), "--workpath", str(out / "freeze"),
               "--specpath", str(out), "--paths", str(entry.parent),
               "--add-data", f"{payload}{os.pathsep}payload",
               "--add-data", f"{entry.parent / 'web'}{os.pathsep}web",
               "--collect-all", "numpy", "--collect-all", "scipy", "--collect-all", "py65",
               "--hidden-import", "py65.devices.mpu6502", "--hidden-import", "py65.memory"]
    if platform.system() == "Darwin":
        command += ["--windowed", "--osx-bundle-identifier", "org.sirgiggles.zelda-genesis-builder"]
    # Keep Python stdout available in Windows child-script mode. The launcher
    # hides its console when opening the browser; build children use hidden
    # console flags and retain their progress pipes.
    command.append(str(entry))
    subprocess.run(command, cwd=root, check=True)
    print(f"Native app: {out / 'dist' / name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
