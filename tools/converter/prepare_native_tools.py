"""Release engineering: build native tools once, then bundle them with the app.

Users of the packaged app do not run a compiler installer. This script runs on
the corresponding release host (Windows, Linux, Intel Mac or Apple Silicon).
SGDK stays pinned and untouched; Unix tools are installed separately.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NODE_VERSION = "22.16.0"
NODE_SHA256 = {
    "darwin-arm64.tar.gz": "1d7f34ec4c03e12d8b33481e5c4560432d7dc31a0ef3ff5a4d9a8ada7cf6ecc9",
    "darwin-x64.tar.gz": "838d400f7e66c804e5d11e2ecb61d6e9e878611146baff69d6a2def3cc23f4ac",
    "linux-arm64.tar.xz": "eab80cb88f8fda1e65f5e8d0420c9809bdb320b03fd34976ab7161b6e703b910",
    "linux-x64.tar.xz": "f4cb75bb036f0d0eddf6b79d9596df1aaab9ddccd6a20bf489be5abe9467e84e",
    "win-x64.zip": "21c2d9735c80b8f86dab19305aa6a9f6f59bbc808f68de3eef09d5832e3bfbbd",
}
SOURCES = {
    "gcc-13.2.0.tar.xz": ("https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/", "e275e76442a6067341a27f04c5c6b83d8613144004c0413528863dc6b5c743da"),
    "binutils-2.40.tar.xz": ("https://ftp.gnu.org/gnu/binutils/", "0f8a4c272d7f17f369ded10a4aca28b8e304828e95526da482b0ccc4dfc9d8e1"),
}


def node_archive(system: str, machine: str) -> str:
    host = {"Darwin": "darwin", "Linux": "linux", "Windows": "win"}.get(system)
    arch = {"x86_64": "x64", "AMD64": "x64", "arm64": "arm64", "aarch64": "arm64"}.get(machine)
    ext = "zip" if system == "Windows" else "tar.gz" if system == "Darwin" else "tar.xz"
    suffix = f"{host}-{arch}.{ext}"
    if suffix not in NODE_SHA256:
        raise ValueError(f"No packaged native runtime for {system} {machine}")
    return f"node-v{NODE_VERSION}-{suffix}"


def verify_download(path: Path, expected: str) -> None:
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        path.unlink()
        raise ValueError(f"Download checksum failed: {path.name}")


def download(url: str, target: Path, expected: str) -> Path:
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.is_file():
        partial = target.with_suffix(target.suffix + ".partial")
        print(f"Downloading {target.name}", flush=True)
        urllib.request.urlretrieve(url, partial)
        verify_download(partial, expected)
        partial.replace(target)
    verify_download(target, expected)
    return target


def unpack(archive: Path, dest: Path) -> None:
    dest.mkdir(parents=True, exist_ok=True)
    root = dest.resolve()
    if zipfile.is_zipfile(archive):
        with zipfile.ZipFile(archive) as z:
            for item in z.infolist():
                path = Path(item.filename)
                if path.is_absolute() or ".." in path.parts or "\\" in item.filename or ":" in item.filename:
                    raise ValueError("Unsafe archive path")
            z.extractall(dest)
    else:
        with tarfile.open(archive) as tar:
            for member in tar.getmembers():
                path = Path(member.name)
                if path.is_absolute() or ".." in path.parts or not (root / path).resolve().is_relative_to(root):
                    raise ValueError("Unsafe archive path")
                if member.issym() or member.islnk():
                    link = (root / path.parent / member.linkname).resolve()
                    if not link.is_relative_to(root):
                        raise ValueError("Unsafe archive link")
                if member.isdev():
                    raise ValueError("Unsafe archive device")
            tar.extractall(dest, filter="data")


def run(argv: list, cwd: Path, env: dict | None = None) -> None:
    print(" ".join(map(str, argv)), flush=True)
    subprocess.run(list(map(str, argv)), cwd=cwd, env=env, check=True)


def prepare_node(prefix: Path, work: Path) -> None:
    name = node_archive(platform.system(), platform.machine())
    suffix = name.removeprefix(f"node-v{NODE_VERSION}-")
    archive = download(f"https://nodejs.org/dist/v{NODE_VERSION}/{name}", work / "downloads" / name, NODE_SHA256[suffix])
    source = work / "node"
    if not (source / name.split(".tar")[0].removesuffix(".zip")).exists():
        unpack(archive, source)
    folder = source / name.split(".tar")[0].removesuffix(".zip")
    binary = folder / "node.exe" if os.name == "nt" else folder / "bin/node"
    dest = prefix / "bin" / binary.name
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(binary, dest)
    license_file = folder / "LICENSE"
    if license_file.exists():
        shutil.copy2(license_file, prefix / "NODE-LICENSE")


def prepare_compiler(prefix: Path, work: Path, jobs: int) -> None:
    if (prefix / "bin/m68k-elf-gcc").exists():
        return
    if any(c.isspace() for c in str(work)):
        raise ValueError("GNU's build directory must not contain spaces. Use --work /tmp/zelda-native-tools.")
    # GNU Make's install rules do not reliably quote a prefix containing
    # spaces. Build in a plain temporary prefix, then relocate the whole
    # self-contained toolchain into the requested app/cache directory.
    installed = work / "install"
    source_root = work / "src"
    for name, (url, digest) in SOURCES.items():
        if not (source_root / name.removesuffix(".tar.xz")).exists():
            unpack(download(url + name, work / "downloads" / name, digest), source_root)
    env = dict(os.environ, PATH=str(installed / "bin") + os.pathsep + os.environ.get("PATH", ""))
    for component in ("binutils-2.40", "gcc-13.2.0"):
        source = source_root / component
        out = work / ("build-" + component)
        out.mkdir(parents=True, exist_ok=True)
        if component.startswith("gcc"):
            if not (source / "gmp").exists():
                run(["sh", source / "contrib/download_prerequisites"], source, env)
            flags = ["--enable-languages=c", "--enable-lto", "--disable-bootstrap",
                     "--disable-multilib", "--without-headers", "--disable-libssp",
                     "--disable-libquadmath", "--disable-libgomp", "--disable-libatomic",
                     "--disable-libsanitizer", "--disable-threads", "--with-cpu=68000"]
            targets = ["all-gcc", "all-target-libgcc"]
            install = ["install-gcc", "install-target-libgcc"]
        else:
            flags = ["--disable-gprofng", "--disable-werror", "--enable-plugins"]
            targets, install = ["all"], ["install"]
        if not (out / "Makefile").exists():
            run([source / "configure", "--target=m68k-elf", f"--prefix={installed}",
                 "--disable-nls", "--disable-shared", *flags], out, env)
        run(["make", f"-j{jobs}", "MAKEINFO=true", *targets], out, env)
        run(["make", "MAKEINFO=true", *install], out, env)
    shutil.copytree(installed, prefix, dirs_exist_ok=True)
    licenses = prefix / "licenses"
    licenses.mkdir(exist_ok=True)
    for component in ("gcc-13.2.0", "binutils-2.40"):
        for name in ("COPYING", "COPYING3", "COPYING.RUNTIME"):
            source = source_root / component / name
            if source.exists():
                shutil.copy2(source, licenses / (component + "-" + name))
    (licenses / "SOURCES.txt").write_text(
        "Unmodified native compiler sources and complete build instructions:\n"
        "https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/gcc-13.2.0.tar.xz\n"
        "https://ftp.gnu.org/gnu/binutils/binutils-2.40.tar.xz\n"
        "tools/converter/prepare_native_tools.py in this release's source repository.\n")


def prepare_xgmtool(root: Path, prefix: Path) -> None:
    binary = prefix / "bin/xgmtool"
    sources = sorted((root / "sgdk/tools/xgmtool/src").glob("*.c"))
    if not sources:
        raise RuntimeError("The pinned SGDK xgmtool source is missing")
    if not binary.exists():
        run(["cc", "-O2", "-fcommon", *sources, "-lm", "-o", binary], root)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--prefix", type=Path)
    parser.add_argument("--work", type=Path)
    parser.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 2))
    parser.add_argument("--node-only", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    prefix = (args.prefix or root / "toolchains/native").resolve()
    default_work = root / "build/native-tools" if os.name == "nt" else Path(tempfile.gettempdir()) / "zelda-native-tools"
    work = (args.work or default_work).resolve()
    prefix.mkdir(parents=True, exist_ok=True)
    work.mkdir(parents=True, exist_ok=True)
    prepare_node(prefix, work)
    if os.name != "nt" and not args.node_only:
        prepare_compiler(prefix, work, args.jobs)
        prepare_xgmtool(root, prefix)
    (prefix / "native-release.json").write_text(json.dumps({
        "host": platform.system(), "arch": platform.machine(), "gcc": "13.2.0",
        "binutils": "2.40", "node": NODE_VERSION}, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
