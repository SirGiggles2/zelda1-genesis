"""Make the one-file ROM maker release from the player's point of view.

    python tools/release/make_builder.py --rom <zelda.nes> (--redux-patch <ips> | --redux <redux.nes>)

1. Stage a clean copy of this tree (tools/nesdata/inventory.stage).
2. In it, rebuild every NES/Redux-derived input from the ROMs exactly as the
   browser will (tools/nesdata/assemble.py, traced: every file it opens is recorded),
   then build Zelda.md there. That ROM is the release.
3. Blank its .nesdata range (range from __nesdata_start/__nesdata_end).
4. Write builds/release/{base.bin, recipe.json, bundle.json}: the blanked ROM, the
   piece list + hashes + final SHA-256, and the source files the browser needs
   (only files that existed before extraction and are not derived inputs).
5. Refuse to finish if the bundle or the blanked ROM contains NES/Redux bytes.
Task 13 packs these into the HTML; Task 14 adds the browser self-test.

T-419 Task 12. Spec: docs/superpowers/specs/2026-10-31-T419-one-file-rom-maker-design.md
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Iterable

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools" / "converter"))
from tools.nesdata.pieces import Piece  # noqa: E402

OUT = ROOT / "builds" / "release"
PYODIDE = "0.27.7"


PAGE = ROOT / "tools" / "release" / "builder_page"
PYODIDE_URL = f"https://cdn.jsdelivr.net/pyodide/v{PYODIDE}/full/"
HTML_OUT = OUT / "Zelda-Genesis-Builder.html"
# Worker libraries in load order (ES module syntax stripped), then the worker body.
WORKER_PARTS = (PAGE / "wasi_mini.js", PAGE / "nesemu.js", PAGE / "uw_dump.js",
                ROOT / "tools" / "music_studio" / "web" / "render.js", PAGE / "studio_host.js", PAGE / "worker.js")
# Python the page itself runs (besides what the traced extraction read).
PAGE_PYTHON = ("tools/release/builder_page/inputs.py", "tools/release/builder_page/splice.py",
               "tools/converter/ips.py")


JS_IMPORT = re.compile(r"^import .* from [\"'].*[\"'];?$")


def worker_source() -> str:
    parts = []
    for path in WORKER_PARTS:
        lines = path.read_text(encoding="utf-8").splitlines()
        lines = [l for l in lines if not JS_IMPORT.match(l)]  # Python imports in template strings stay
        lines = [l[len("export "):] if l.startswith("export ") else l for l in lines]
        lines = [l for l in lines if not l.startswith("{ ") or "};" not in l]  # `export { OPS };` remnant
        parts.append(f"// ---- {path.relative_to(ROOT).as_posix()} ----\n" + "\n".join(lines))
    return "\n".join(parts).replace("__PYODIDE_URL__", PYODIDE_URL)


def _embed(text: str, what: str) -> str:
    if "</script" in text.lower():
        raise SystemExit(f"ERROR: {what} contains '</script'; cannot embed")
    return text


def pack(base: bytes, recipe: dict, bundle: dict[str, str], out: Path = HTML_OUT) -> Path:
    html = (PAGE / "template.html").read_text(encoding="utf-8")
    fills = {
        "__RECIPE__": _embed(json.dumps(recipe), "recipe"),
        "__BASE__": base64.b64encode(base).decode(),
        "__XGMTOOL__": base64.b64encode((PAGE / "xgmtool.wasm").read_bytes()).decode(),
        "__BUNDLE__": _embed(json.dumps(bundle), "bundle"),
        "__WORKER__": _embed(worker_source(), "worker"),
        "__APP__": _embed((PAGE / "app.js").read_text(encoding="utf-8"), "app"),
    }
    for key, value in fills.items():
        if html.count(key) != 1:
            raise SystemExit(f"ERROR: template placeholder {key} missing or repeated")
        html = html.replace(key, value)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(html, encoding="utf-8", newline="\n")
    return out


def known_inputs() -> dict:
    """What the page accepts (tools/release/builder_page/inputs.py): build.py's pins,
    plus the PRG0 header/body hash so a headerless dump can be completed."""
    import build  # noqa: PLC0415
    nes = ROOT / "roms" / "Legend of Zelda, The (USA).nes"
    data = nes.read_bytes()
    if hashlib.sha256(data).hexdigest() not in build.SUPPORTED_ROMS:
        raise SystemExit("ERROR: roms/ does not hold the supported PRG0 ROM")
    return {"roms": build.SUPPORTED_ROMS, "redux_roms": build.SUPPORTED_REDUX_ROMS,
            "old_patches": build.REDUX_WRONG_PATCHES,
            "prg0_header_hex": data[:16].hex(), "prg0_body_size": len(data) - 16,
            "prg0_body_sha256": hashlib.sha256(data[16:]).hexdigest()}


def blank(rom: bytes, start: int, size: int) -> bytes:
    return rom[:start] + bytes(size) + rom[start + size:]


def recipe(rom: bytes, start: int, size: int, pieces: Iterable[Piece], root: Path, commit: str) -> dict:
    return {
        "schema": 1, "commit": commit, "pyodide": PYODIDE,
        "rom_sha256": hashlib.sha256(rom).hexdigest(), "rom_size": len(rom),
        "nesdata": {"offset": start, "size": size},
        "pieces": [{"name": p.name, "size": p.size,
                    "sha256": hashlib.sha256((root / p.path).read_bytes()).hexdigest()} for p in pieces],
    }


def python_bundle(root: Path, reads: Iterable[str], existed: set[str], derived: set[str],
                  written: set[str] = frozenset()) -> dict[str, str]:
    """{relative path: base64 bytes} for files the run read that are sources: present before
    extraction, not rewritten by it (those come from the ROM), not derived inputs."""
    out = {}
    for rel in reads:
        if rel in existed and rel not in derived and rel not in written and (root / rel).is_file():
            out[rel] = base64.b64encode((root / rel).read_bytes()).decode()
    return out


def rom_hits(bundle: dict[str, str], nes_roms: list[bytes]) -> list[str]:
    from rom_bytes_scan import WINDOW, windows  # noqa: PLC0415
    wins: set[bytes] = set()
    for nes in nes_roms:
        wins |= windows(nes)
    hits = []
    for rel, b64 in bundle.items():
        data = base64.b64decode(b64)
        if any(data[i:i + WINDOW] in wins for i in range(len(data) - WINDOW + 1)):
            hits.append(rel)
    return hits


def site_package(name: str) -> dict[str, str]:
    """A pure-Python dependency (py65) as bundle entries under site/."""
    import importlib  # noqa: PLC0415
    pkg = Path(importlib.import_module(name).__file__).parent
    return {f"site/{name}/{p.relative_to(pkg).as_posix()}": base64.b64encode(p.read_bytes()).decode()
            for p in sorted(pkg.rglob("*.py")) if "tests" not in p.parts}


def redux_rom(rom: Path, patch: Path | None, redux: Path | None, work: Path) -> Path:
    import build  # noqa: PLC0415
    from ips import apply_patch  # noqa: PLC0415
    if redux is None:
        data = apply_patch(rom.read_bytes(), patch.read_bytes())
        redux = work / "Zelda Redux.nes"
        redux.write_bytes(data)
    if hashlib.sha256(redux.read_bytes()).hexdigest() not in build.SUPPORTED_REDUX_ROMS:
        raise SystemExit("ERROR: not the supported Zelda Redux v3.3.3 ROM/patch")
    return redux


def run(cmd: list[str], cwd: Path, log: Path) -> None:
    with log.open("a", encoding="utf-8") as stream:
        stream.write(f"$ {cmd}\n")
        stream.flush()
        rc = subprocess.run(cmd, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT,
                            stdin=subprocess.DEVNULL).returncode
    if rc:
        raise SystemExit(f"ERROR: {Path(cmd[1]).name} failed (exit {rc}); see {log}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rom", type=Path)
    ap.add_argument("--redux-patch", type=Path)
    ap.add_argument("--redux", type=Path)
    ap.add_argument("--keep", type=Path, help="keep the staged tree here (debugging)")
    ap.add_argument("--repack", action="store_true", help="only rebuild the HTML from builds/release/")
    a = ap.parse_args()
    if a.repack:
        html = repack()
        print(f"wrote {html.relative_to(ROOT)} ({html.stat().st_size // 1024} KB)")
        return 0
    if not a.rom or not (a.redux_patch or a.redux):
        ap.error("give --rom and --redux-patch or --redux")
    from tools.nesdata.inventory import stage  # noqa: PLC0415
    from tools.nesdata.check_clean import nesdata_range  # noqa: PLC0415
    OUT.mkdir(parents=True, exist_ok=True)
    log = OUT / "make_builder.log"
    log.write_text("", encoding="utf-8")
    t0 = time.time()
    with tempfile.TemporaryDirectory(prefix="t419-release-") as tmp:
        work = Path(a.keep or tmp).resolve()
        ws = work / "ws"
        rom = a.rom.resolve()
        redux = redux_rom(rom, a.redux_patch, a.redux and a.redux.resolve(), work)
        stage(ROOT, ws)
        existed = {p.relative_to(ws).as_posix() for p in ws.rglob("*") if p.is_file()}
        print(f"[1/5] staged {len(existed)} files", flush=True)
        time.sleep(2)  # mtime granularity: anything the extraction writes is newer than this
        t_extract = time.time()
        reads_json = work / "reads.json"
        run([sys.executable, str(ws / "tools/release/trace_assemble.py"), "--rom", str(rom), "--redux", str(redux),
             "--start", "0", "--out", str(work / "nesdata0.bin"), "--reads", str(reads_json)], ws, log)
        written = {p.relative_to(ws).as_posix() for p in ws.rglob("*")
                   if p.is_file() and p.stat().st_mtime >= t_extract}
        print(f"[2/5] extracted in the browser's way ({time.time() - t0:.0f} s)", flush=True)
        # The Debug build's freshness gate checks recorded sentinels; this tree was just
        # regenerated from the ROMs (and the public layout rewrites hashed files), so record
        # them here as build.py's extraction does. The browser never runs this gate.
        run([sys.executable, str(ws / "tools/probes/check_generated_freshness.py"), "--gen"], ws, log)
        run([sys.executable, str(ws / "tools/build/build_rom.py")], ws, log)
        release = (ws / "builds" / "Zelda.md").read_bytes()
        start, size = nesdata_range(ws / "build/rom_project/out/Zelda.out")
        sys.path.insert(0, str(ws))
        from tools.nesdata import assemble as ws_assemble  # noqa: PLC0415
        from tools.nesdata import pieces as ws_pieces  # noqa: PLC0415
        if ws_assemble.section(ws_pieces.PIECES, ws, start) != release[start:start + size]:
            raise SystemExit("ERROR: assembled .nesdata differs from the built ROM")
        print(f"[3/5] release ROM {hashlib.sha256(release).hexdigest()[:16]} nesdata ${start:06X}+{size}", flush=True)
        commit = subprocess.run(["git", "rev-parse", "--short=9", "HEAD"], cwd=ROOT, capture_output=True,
                                text=True).stdout.strip()
        base = blank(release, start, size)
        rec = recipe(release, start, size, ws_pieces.PIECES, ws, commit)
        derived = set(json.loads((ws / "tools/nesdata/inventory.json").read_text(encoding="utf-8"))["files"])
        bundle = python_bundle(ws, json.loads(reads_json.read_text(encoding="utf-8")), existed, derived, written)
        bundle.update(site_package("py65"))
        nes = [rom.read_bytes()[16:], redux.read_bytes()[16:]]
        hits = rom_hits(bundle, nes)
        if hits:
            raise SystemExit(f"ERROR: NES/Redux bytes in bundled files: {hits[:10]}")
        if any(base[start:start + size]):
            raise SystemExit("ERROR: blanked range is not all zero")
        print(f"[4/5] bundle {len(bundle)} files, {sum(len(v) for v in bundle.values()) // 1024} KB base64; "
              f"no NES/Redux bytes", flush=True)
        (OUT / "base.bin").write_bytes(base)
        (OUT / "recipe.json").write_text(json.dumps(rec, indent=1) + "\n", encoding="utf-8")
        (OUT / "bundle.json").write_text(json.dumps(bundle) + "\n", encoding="utf-8")
        (OUT / "release.md").write_bytes(release)  # local reference copy; never published
    html = repack()
    print(f"[5/5] wrote {html.relative_to(ROOT)} ({html.stat().st_size // 1024} KB) in {time.time() - t0:.0f} s")
    return 0


def repack() -> Path:
    """HTML from the last release's base.bin/recipe.json/bundle.json + the current page files."""
    base = (OUT / "base.bin").read_bytes()
    rec = json.loads((OUT / "recipe.json").read_text(encoding="utf-8"))
    bundle = json.loads((OUT / "bundle.json").read_text(encoding="utf-8"))
    rec["inputs"] = known_inputs()
    for rel in PAGE_PYTHON:
        bundle[rel] = base64.b64encode((ROOT / rel).read_bytes()).decode()
    return pack(base, rec, bundle)


if __name__ == "__main__":
    sys.exit(main())
