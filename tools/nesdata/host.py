"""Host operations that extractor steps cannot do inside Pyodide.

Pyodide cannot start processes. Steps call these functions instead of
subprocess: natively they run the real tool; in the browser
(sys.platform == "emscripten") they call the builder page's JS bridge
`zeldaHost` (xgmtool.wasm via wasi_mini.js; Music Studio's render.js).

T-419 Task 9. Spec: docs/superpowers/specs/2026-10-31-T419-one-file-rom-maker-design.md
"""
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import threading
from contextlib import contextmanager
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IN_BROWSER = sys.platform == "emscripten"
_state = threading.local()


@contextmanager
def allow_processes():
    """Native host calls may start processes even while assemble.py forbids them for steps."""
    previous = getattr(_state, "allowed", False)
    _state.allowed = True
    try:
        yield
    finally:
        _state.allowed = previous


def processes_allowed() -> bool:
    return getattr(_state, "allowed", False)


def xgmtool(vgm: bytes) -> bytes:
    """SGDK xgmtool VGM -> compiled XGM (.bin), NTSC, silent: `xgmtool in out.bin -n -s`."""
    if IN_BROWSER:
        import js  # noqa: PLC0415  (Pyodide)
        from pyodide.ffi import to_js  # noqa: PLC0415
        return bytes(js.zeldaHost.xgmtool(to_js(vgm)).to_py())
    sys.path.insert(0, str(ROOT / "tools" / "converter"))
    from platform_tools import tool_command  # noqa: PLC0415
    with tempfile.TemporaryDirectory() as td:
        src, out = Path(td) / "in.vgm", Path(td) / "out.bin"
        src.write_bytes(vgm)
        with allow_processes():
            r = subprocess.run(tool_command("xgmtool", ROOT) + [str(src), str(out), "-n", "-s"],
                               capture_output=True, text=True, stdin=subprocess.DEVNULL)
        if r.returncode or not out.is_file():
            raise RuntimeError(f"xgmtool failed: {(r.stdout + r.stderr).strip()[-400:]}")
        return out.read_bytes()


def uw_dump(rom: bytes, rom_id: str, rom_sha256: str, manifests: dict[str, list[str]],
            clean: bool = True) -> dict[str, str]:
    """Dungeon room dump on the JS NES (builder_page/uw_dump.js): {filename: JSON text}.
    clean (default): each room from the post-warp state, the Mesen-verified definition;
    clean=False: zelda_uw_dump.py's sequential procedure (emulator oracle tests only)."""
    if IN_BROWSER:
        import js  # noqa: PLC0415
        from pyodide.ffi import to_js  # noqa: PLC0415
        return dict(js.zeldaHost.uwDump(to_js(rom), rom_id, rom_sha256, json.dumps(manifests), clean).to_py())
    sys.path.insert(0, str(ROOT / "tools" / "converter"))
    from platform_tools import tool_path  # noqa: PLC0415
    with tempfile.TemporaryDirectory() as td:
        rom_path = Path(td) / "rom.nes"
        rom_path.write_bytes(rom)
        with allow_processes():
            r = subprocess.run([str(tool_path("node", root=ROOT)),
                            str(ROOT / "tools/release/builder_page/run_uw_dump.mjs"), str(rom_path), rom_id,
                            rom_sha256, *([] if clean else ["--sequential"])], input=json.dumps(manifests), text=True, capture_output=True,
                           timeout=600)
        if r.returncode:
            raise RuntimeError(f"uw_dump failed: {r.stderr.strip()[-1600:]}")
        return json.loads(r.stdout)


def studio_render(payload: dict) -> dict[str, bytes]:
    """Music Studio renderer (tools/music_studio/web/render.js): {song id: VGM bytes}."""
    if IN_BROWSER:
        import js  # noqa: PLC0415
        result = js.zeldaHost.studioRender(json.dumps(payload)).to_py()
        return {k: bytes(v) for k, v in result.items()}
    sys.path.insert(0, str(ROOT / "tools" / "converter"))
    from platform_tools import tool_path  # noqa: PLC0415
    with tempfile.TemporaryDirectory() as td:
        with allow_processes():
            r = subprocess.run([str(tool_path("node", root=ROOT)), str(ROOT / "tools/converter/studio_render.js"), td],
                           input=json.dumps(payload), text=True, capture_output=True, timeout=120)
        if r.returncode:
            raise ValueError(f"Studio music renderer failed: {r.stderr.strip()[-1600:]}")
        return {p.stem: p.read_bytes() for p in Path(td).glob("*.vgm")}
