"""Sort the files a player drops into the builder: NES ROM + Redux (patch, zip or ROM).

Runs in the builder page (Pyodide) and natively (tests). `known` comes from the
release recipe (tools/release/make_builder.py known_inputs()): supported ROM and
patch hashes and the PRG0 header for headerless dumps. Every refusal is one
plain sentence for the player.
T-419 Task 13.
"""
from __future__ import annotations

import hashlib
import io
import zipfile

from ips import apply_patch

ADD_ROM = "Add your Zelda NES ROM (USA, PRG0)."
NOT_PRG0 = "This is not the USA PRG0 ROM."
ADD_REDUX = "Add the Zelda Redux patch (.ips or the Redux .zip)."
NOT_REDUX = "This is not the Zelda Redux v3.3.3 patch."


class InputError(ValueError):
    pass


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def classify(name: str, data: bytes, known: dict) -> tuple[str, bytes | list[bytes]]:
    """(kind, payload): rom | redux_rom | patches | nes_other | unknown."""
    if data[:2] == b"PK":
        try:
            with zipfile.ZipFile(io.BytesIO(data)) as z:
                patches = [z.read(n) for n in z.namelist() if n.lower().endswith((".ips", ".bps"))]
        except zipfile.BadZipFile:
            return "unknown", data
        return ("patches", patches) if patches else ("unknown", data)
    if data[:5] == b"PATCH" or data[:4] == b"BPS1":
        return "patches", [data]
    if data[:4] == b"NES\x1a":
        h = _sha(data)
        if h in known["roms"]:
            return "rom", data
        if h in known["redux_roms"]:
            return "redux_rom", data
        return "nes_other", data
    if len(data) == known["prg0_body_size"] and _sha(data) == known["prg0_body_sha256"]:
        return "rom", bytes.fromhex(known["prg0_header_hex"]) + data
    return "unknown", data


def prepare(files: list[tuple[str, bytes]], known: dict) -> tuple[bytes, bytes]:
    """(NES ROM, Redux ROM) or InputError with the sentence to show."""
    rom = redux = None
    patches: list[bytes] = []
    wrong_rom = False
    for name, data in files:
        kind, payload = classify(name, data, known)
        if kind == "rom":
            rom = payload
        elif kind == "redux_rom":
            redux = payload
        elif kind == "patches":
            patches += payload
        elif kind == "nes_other" or (kind == "unknown" and name.lower().endswith(".nes")):
            wrong_rom = True
    if rom is None:
        raise InputError(NOT_PRG0 if wrong_rom else ADD_ROM)
    if redux is None:
        if not patches:
            raise InputError(ADD_REDUX)
        for patch in patches:  # a release zip can hold add-on patches too: keep the Redux one
            try:
                out = apply_patch(rom, patch)
            except ValueError:
                continue
            if _sha(out) in known["redux_roms"]:
                redux = out
                break
        if redux is None:
            raise InputError(NOT_REDUX)
    return rom, redux
