#!/usr/bin/env python3
"""
extract_audio.py - Extract audio data from NES Zelda Z_00 to Genesis C arrays.

S2 Phase C1: ports the old .inc emission to Genesis-ready C-array output under
data/audio/.  The final audio format choice is deferred to S11 (XGM2 swap);
this pass ships a RAW dump that the S11 transcoder can consume directly.

This pass extracts:
  - song phrase headers
  - raw song-script blobs from the NES ROM
  - tune scripts, note tables, envelopes, and noise params
  - SFX note arrays and sample tables
  - PCM sample blob from PRG bank 7

The Aldonunez disassembly references the song scripts via .INCBIN files, but
those sidecar files are not present in this workspace. We recover the exact
blob boundaries from the header pointers plus the first opcode sequence of
DriveAudio in PRG bank 0, so the extraction stays deterministic.

Primary outputs (C arrays, data/audio/):
  songs.c          -- song/tune table bytes (headers, note tables, envelopes)
  song_scripts.c   -- raw song-script blobs from PRG bank 0
  sfx.c            -- SFX note arrays and sample tables
  pcm_samples.c    -- raw PCM sample blob from PRG bank 7
  MANIFEST.json    -- schema_version + nes_rom_sha256 + per-block metadata

Legacy outputs (--legacy-inc flag only):
  src/data/songs.inc, song_scripts.inc, sfx.inc, pcm_samples.inc,
  music_blob.dat, music_blob.inc  (and reference/aldonunez/dat/*.dat)
"""

import argparse
import hashlib
import json
import os
import re
import sys
from pathlib import Path

# Canonical NES ROM SHA256.
NES_ROM_SHA256 = "8f72dc2e98572eb4ba7c3a902bca5f69c448fc4391837e5f8f0d4556280440ac"

INES_HEADER_SIZE = 16
PRG_BANK_SIZE = 0x4000
BANK0_CPU_BASE = 0x8000

DRIVE_AUDIO_SIGNATURE = bytes(
    [
        0xA5, 0xE0, 0xF0, 0x0C, 0xA9, 0x00, 0x8D, 0x15,
        0x40, 0xA9, 0x0F, 0x8D, 0x15, 0x40, 0xD0, 0x11,
        0xA9, 0xFF, 0x8D, 0x17, 0x40,
    ]
)

LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):$")

SONG_LABELS = {
    "SongTable",
    "SongHeaderDemo0",
    "SongHeaderItemTaken0",
    "SongHeaderEndLevel0",
    "SongHeaderOverworld0",
    "SongHeaderUnderworld0",
    "SongHeaderLastLevel0",
    "SongHeaderGanon0",
    "SongHeaderEnding0",
    "SongHeaderZelda",
    "TuneScripts0",
    "TuneScripts1",
    "NoiseVolumes",
    "NoisePeriods",
    "NoiseLengths",
    "NotePeriodTable",
    "NoteLengthTable0",
    "NoteLengthTable1",
    "NoteLengthTable2",
    "NoteLengthTable3",
    "NoteLengthTable4",
    "CustomEnvelopeSong",
    "CustomEnvelopeTune1",
}

Z07_LABELS = {
    "PlayAreaColumnAddrs",
}

SFX_LABELS = {
    "BombSfxNotes",
    "StairsSfxNotes",
    "SwordSfxNotes",
    "ArrowSfxNotes",
    "FlameSfxNotes",
    "SampleAddrs",
    "SampleLengths",
    "SampleRates",
}

SONG_SCRIPT_HEADER_MAP = [
    ("SongScriptItemTaken0", "SongHeaderItemTaken0", 0),
    ("SongScriptOverworld0", "SongHeaderOverworld0", 0),
    ("SongScriptUnderworld0", "SongHeaderUnderworld0", 0),
    ("SongScriptEndLevel0", "SongHeaderEndLevel0", 0),
    ("SongScriptLastLevel0", "SongHeaderLastLevel0", 0),
    ("SongScriptGanon0", "SongHeaderGanon0", 0),
    ("SongScriptEnding0", "SongHeaderEnding0", 0),
    ("SongScriptDemo0", "SongHeaderDemo0", 0),
    ("SongScriptZelda0", "SongHeaderZelda", 0),
]

SONG_TABLE_SIGNATURE = bytes(
    [
        0x7D, 0xB5, 0x6E, 0x67, 0x7D, 0xAD, 0x64, 0x64,
        0x75, 0x7D, 0x85, 0x95, 0x7D, 0x8D, 0x95, 0x9D,
        0xA5, 0xBD, 0xC5, 0xCD, 0xD5, 0xDD, 0xD5, 0xE5,
        0xED, 0x24, 0x2C, 0x34, 0x3C, 0x44, 0x34, 0x4C,
        0x54, 0x5C, 0x44, 0xF5,
    ]
)


# ---------------------------------------------------------------------------
# Parsing helpers
# ---------------------------------------------------------------------------

def strip_comment(line: str) -> str:
    return line.split(";", 1)[0].strip()


def parse_byte_token(token: str) -> int:
    token = token.strip()
    if not token:
        raise ValueError("Empty .BYTE token")
    if token.startswith("$"):
        return int(token[1:], 16)
    return int(token, 10)


def read_ines_prg(path: str) -> bytes:
    with open(path, "rb") as f:
        header = f.read(INES_HEADER_SIZE)
        if header[:4] != b"NES\x1A":
            raise ValueError(f"Not a valid iNES ROM: {path}")
        prg_banks = header[4]
        prg_data = f.read(prg_banks * PRG_BANK_SIZE)
    return prg_data


def parse_asm_data_blocks(path: str, target_labels: set) -> dict:
    blocks: dict = {}
    current = None

    with open(path, "r", encoding="utf-8") as f:
        for raw_line in f:
            line = strip_comment(raw_line)
            if not line:
                if current is not None and blocks[current]:
                    current = None
                continue

            match = LABEL_RE.match(line)
            if match:
                label = match.group(1)
                current = label if label in target_labels else None
                if current is not None and current not in blocks:
                    blocks[current] = bytearray()
                continue

            if current is None:
                continue

            if line.startswith(".BYTE"):
                items = [item.strip() for item in line[5:].split(",") if item.strip()]
                blocks[current].extend(parse_byte_token(item) for item in items)

    missing = sorted(label for label in target_labels if label not in blocks)
    if missing:
        raise ValueError(f"Missing expected labels in {path}: {', '.join(missing)}")

    return blocks


def extract_script_addr(header_bytes: bytearray, phrase_index: int) -> int:
    offset = phrase_index * 8
    if len(header_bytes) < offset + 3:
        raise ValueError("Song header too short to extract script address")
    lo = header_bytes[offset + 1]
    hi = header_bytes[offset + 2]
    return (hi << 8) | lo


def cpu_addr_to_bank0_offset(cpu_addr: int) -> int:
    if cpu_addr < BANK0_CPU_BASE or cpu_addr >= BANK0_CPU_BASE + PRG_BANK_SIZE:
        raise ValueError(f"CPU address ${cpu_addr:04X} is outside PRG bank 0")
    return cpu_addr - BANK0_CPU_BASE


def find_drive_audio_addr(bank0_data: bytes, min_cpu_addr: int) -> int:
    min_offset = cpu_addr_to_bank0_offset(min_cpu_addr)
    match_offset = bank0_data.find(DRIVE_AUDIO_SIGNATURE, min_offset)
    if match_offset < 0:
        raise ValueError("Could not locate DriveAudio signature in PRG bank 0")
    return BANK0_CPU_BASE + match_offset


def find_unique_pattern(data: bytes, pattern: bytes, description: str) -> int:
    pos = data.find(pattern)
    if pos < 0:
        raise ValueError(f"Could not find {description}")
    second = data.find(pattern, pos + 1)
    if second >= 0:
        raise ValueError(
            f"{description} was not unique (found at {pos:#x} and {second:#x})"
        )
    return pos


def extract_song_scripts(prg_data: bytes, song_blocks: dict) -> tuple:
    bank0_data = prg_data[:PRG_BANK_SIZE]

    script_starts = []
    for script_label, header_label, phrase_index in SONG_SCRIPT_HEADER_MAP:
        start_addr = extract_script_addr(song_blocks[header_label], phrase_index)
        script_starts.append((script_label, start_addr))

    drive_audio_addr = find_drive_audio_addr(bank0_data, script_starts[-1][1])

    scripts = {}
    for index, (script_label, start_addr) in enumerate(script_starts):
        if index + 1 < len(script_starts):
            end_addr = script_starts[index + 1][1]
        else:
            end_addr = drive_audio_addr

        start_offset = cpu_addr_to_bank0_offset(start_addr)
        end_offset = cpu_addr_to_bank0_offset(end_addr)
        if end_offset <= start_offset:
            raise ValueError(
                f"Invalid ROM bounds for {script_label}: "
                f"${start_addr:04X}-${end_addr:04X}"
            )

        scripts[script_label] = {
            "start_addr": start_addr,
            "end_addr": end_addr,
            "bytes": bytes(bank0_data[start_offset:end_offset]),
        }

    return scripts, drive_audio_addr


def extract_music_blob(prg_data: bytes, song_script_blocks: dict) -> tuple:
    """Return (blob_bytes, nes_base_addr) for the contiguous music data.

    The blob is SongTable + all 9 song headers + unknown 3-byte gap +
    9 concatenated song scripts, pulled verbatim from PRG bank 0.  The
    native M68K music player converts any NES CPU pointer found inside a
    song header to a blob offset via (nes_addr - nes_base_addr).
    """
    bank0_data = prg_data[:PRG_BANK_SIZE]
    pos = find_unique_pattern(
        bank0_data, SONG_TABLE_SIGNATURE, "SongTable in PRG bank 0"
    )
    nes_base = BANK0_CPU_BASE + pos
    end_addr = max(info["end_addr"] for info in song_script_blocks.values())
    blob_size = end_addr - nes_base
    if blob_size <= 0:
        raise ValueError("Music blob end address precedes SongTable")
    blob_bytes = bytes(bank0_data[pos : pos + blob_size])
    return blob_bytes, nes_base


def extract_pcm_samples(prg_data: bytes, z07_blocks: dict) -> bytes:
    bank7_data = prg_data[7 * PRG_BANK_SIZE : 8 * PRG_BANK_SIZE]
    play_area_column_addrs = bytes(z07_blocks["PlayAreaColumnAddrs"])
    play_area_pos = find_unique_pattern(
        bank7_data, play_area_column_addrs, "PlayAreaColumnAddrs in PRG bank 7"
    )
    return bytes(bank7_data[:play_area_pos])


# ---------------------------------------------------------------------------
# C-array emission helpers (Phase B pattern)
# ---------------------------------------------------------------------------

def bytes_to_c_array(data: bytes, var_name: str) -> str:
    """Emit a C const-array declaration matching the Phase B convention."""
    n = len(data)
    lines = [
        f"/* Auto-generated by tools/extract_audio.py - do not edit. */",
        f"const unsigned long {var_name}_size = {n}UL;",
        f"const unsigned char {var_name}[{n}] = {{",
    ]
    for i in range(0, n, 16):
        chunk = data[i : i + 16]
        hex_vals = ", ".join(f"0x{b:02x}" for b in chunk)
        comma = "" if (i + 16) >= n else ","
        lines.append(f"    {hex_vals}{comma}")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def write_c_file(path: str, data: bytes, var_name: str) -> None:
    content = bytes_to_c_array(data, var_name)
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write(content)
    print(f"  Wrote {path} ({len(data)} bytes)")


# ---------------------------------------------------------------------------
# MANIFEST.json builder
# ---------------------------------------------------------------------------

def build_manifest(blocks_info: list) -> dict:
    manifest: dict = {
        "schema_version": 1,
        "nes_rom_sha256": NES_ROM_SHA256,
        "songs": [],
        "sfx": [],
        "song_scripts": [],
        "pcm": [],
    }

    for info in blocks_info:
        category = info["category"]
        entry = {
            "name": info["name"],
            "file": info["file"],
            "var_name": info["var_name"],
            "byte_offset": info["byte_offset"],
            "byte_size": info["byte_size"],
        }
        if "nes_start_addr" in info:
            entry["nes_start_addr"] = info["nes_start_addr"]
        manifest[category].append(entry)

    return manifest


def write_manifest(path: str, manifest: dict) -> None:
    with open(path, "w", encoding="ascii", newline="\n") as f:
        json.dump(manifest, f, indent=2, sort_keys=False, ensure_ascii=True)
        f.write("\n")
    total = sum(len(v) for v in manifest.values() if isinstance(v, list))
    print(f"  Wrote {path} ({total} block entries)")


# ---------------------------------------------------------------------------
# Per-file C emission
# ---------------------------------------------------------------------------

def write_songs_c(out_dir: str, song_blocks: dict) -> tuple:
    """Emit songs.c (all song/tune table blocks concatenated) and return block list."""
    ordered_labels = [
        "SongTable",
        "SongHeaderDemo0",
        "SongHeaderItemTaken0",
        "SongHeaderEndLevel0",
        "SongHeaderOverworld0",
        "SongHeaderUnderworld0",
        "SongHeaderLastLevel0",
        "SongHeaderGanon0",
        "SongHeaderEnding0",
        "SongHeaderZelda",
        "TuneScripts0",
        "TuneScripts1",
        "NoiseVolumes",
        "NoisePeriods",
        "NoiseLengths",
        "NotePeriodTable",
        "NoteLengthTable0",
        "NoteLengthTable1",
        "NoteLengthTable2",
        "NoteLengthTable3",
        "NoteLengthTable4",
        "CustomEnvelopeSong",
        "CustomEnvelopeTune1",
    ]

    # Build a single blob from all song-table labels; each label gets its own
    # metadata entry tracking the offset within the blob.
    blob = bytearray()
    blocks_info: list = []
    for label in ordered_labels:
        data = bytes(song_blocks[label])
        offset = len(blob)
        blocks_info.append({
            "category": "songs",
            "name": label,
            "file": "songs.c",
            "var_name": "audio_songs",
            "byte_offset": offset,
            "byte_size": len(data),
        })
        blob.extend(data)

    write_c_file(os.path.join(out_dir, "songs.c"), bytes(blob), "audio_songs")
    return bytes(blob), blocks_info


def write_sfx_c(out_dir: str, sfx_blocks: dict) -> tuple:
    """Emit sfx.c and return (blob, blocks_info)."""
    ordered_labels = [
        "BombSfxNotes",
        "StairsSfxNotes",
        "SwordSfxNotes",
        "ArrowSfxNotes",
        "FlameSfxNotes",
        "SampleAddrs",
        "SampleLengths",
        "SampleRates",
    ]

    blob = bytearray()
    blocks_info: list = []
    for label in ordered_labels:
        data = bytes(sfx_blocks[label])
        offset = len(blob)
        blocks_info.append({
            "category": "sfx",
            "name": label,
            "file": "sfx.c",
            "var_name": "audio_sfx",
            "byte_offset": offset,
            "byte_size": len(data),
        })
        blob.extend(data)

    write_c_file(os.path.join(out_dir, "sfx.c"), bytes(blob), "audio_sfx")
    return bytes(blob), blocks_info


def write_song_scripts_c(
    out_dir: str,
    script_blocks: dict,
    drive_audio_addr: int,
) -> tuple:
    """Emit song_scripts.c and return (blob, blocks_info)."""
    ordered_labels = [label for label, _, _ in SONG_SCRIPT_HEADER_MAP]

    blob = bytearray()
    blocks_info: list = []
    for label in ordered_labels:
        info = script_blocks[label]
        data = info["bytes"]
        offset = len(blob)
        blocks_info.append({
            "category": "song_scripts",
            "name": label,
            "file": "song_scripts.c",
            "var_name": "audio_song_scripts",
            "byte_offset": offset,
            "byte_size": len(data),
            "nes_start_addr": info["start_addr"],
        })
        blob.extend(data)

    write_c_file(
        os.path.join(out_dir, "song_scripts.c"),
        bytes(blob),
        "audio_song_scripts",
    )
    return bytes(blob), blocks_info


def write_pcm_samples_c(out_dir: str, pcm_data: bytes) -> list:
    """Emit pcm_samples.c and return blocks_info entry list."""
    write_c_file(
        os.path.join(out_dir, "pcm_samples.c"),
        pcm_data,
        "audio_pcm_samples",
    )
    return [
        {
            "category": "pcm",
            "name": "PcmSamples",
            "file": "pcm_samples.c",
            "var_name": "audio_pcm_samples",
            "byte_offset": 0,
            "byte_size": len(pcm_data),
        }
    ]


# ---------------------------------------------------------------------------
# Legacy .inc emission helpers
# ---------------------------------------------------------------------------

def data_to_inc_bytes(data: bytes, label: str, bytes_per_line: int = 16) -> str:
    lines = [f"; {label} - {len(data)} bytes", f"{label}:"]
    for i in range(0, len(data), bytes_per_line):
        chunk = data[i : i + bytes_per_line]
        lines.append("    dc.b " + ",".join(f"${byte:02X}" for byte in chunk))
    return "\n".join(lines)


def write_text_file(path: str, lines: list) -> None:
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"  Wrote {path}")


def write_songs_inc(data_dir: str, song_blocks: dict) -> None:
    ordered_labels = [
        "SongTable", "SongHeaderDemo0", "SongHeaderItemTaken0",
        "SongHeaderEndLevel0", "SongHeaderOverworld0", "SongHeaderUnderworld0",
        "SongHeaderLastLevel0", "SongHeaderGanon0", "SongHeaderEnding0",
        "SongHeaderZelda", "TuneScripts0", "TuneScripts1",
        "NoiseVolumes", "NoisePeriods", "NoiseLengths", "NotePeriodTable",
        "NoteLengthTable0", "NoteLengthTable1", "NoteLengthTable2",
        "NoteLengthTable3", "NoteLengthTable4",
        "CustomEnvelopeSong", "CustomEnvelopeTune1",
    ]
    lines = [
        "; Audio song/tune tables extracted from NES Zelda Z_00",
        "; Auto-generated by extract_audio.py - DO NOT EDIT",
        "",
    ]
    for label in ordered_labels:
        lines.append(data_to_inc_bytes(bytes(song_blocks[label]), label))
        lines.append("")
    write_text_file(os.path.join(data_dir, "songs.inc"), lines[:-1])


def write_song_scripts_inc(
    data_dir: str,
    script_blocks: dict,
    drive_audio_addr: int,
) -> None:
    ordered_labels = [label for label, _, _ in SONG_SCRIPT_HEADER_MAP]
    lines = [
        "; Raw song-script blobs extracted from NES Zelda PRG bank 0",
        "; Auto-generated by extract_audio.py - DO NOT EDIT",
        f"; Final script ends at DriveAudio (${drive_audio_addr:04X})",
        "",
    ]
    for label in ordered_labels:
        info = script_blocks[label]
        lines.append(
            "; "
            f"{label} - {len(info['bytes'])} bytes "
            f"(${info['start_addr']:04X}-${info['end_addr'] - 1:04X})"
        )
        lines.append(f"{label}:")
        data = info["bytes"]
        for i in range(0, len(data), 16):
            chunk = data[i : i + 16]
            lines.append("    dc.b " + ",".join(f"${byte:02X}" for byte in chunk))
        lines.append("")
    write_text_file(os.path.join(data_dir, "song_scripts.inc"), lines[:-1])


def write_sfx_inc(data_dir: str, sfx_blocks: dict) -> None:
    ordered_labels = [
        "BombSfxNotes", "StairsSfxNotes", "SwordSfxNotes",
        "ArrowSfxNotes", "FlameSfxNotes",
        "SampleAddrs", "SampleLengths", "SampleRates",
    ]
    lines = [
        "; Audio SFX/sample tables extracted from NES Zelda Z_00",
        "; Auto-generated by extract_audio.py - DO NOT EDIT",
        "",
    ]
    for label in ordered_labels:
        lines.append(data_to_inc_bytes(bytes(sfx_blocks[label]), label))
        lines.append("")
    write_text_file(os.path.join(data_dir, "sfx.inc"), lines[:-1])


def write_pcm_samples_inc(data_dir: str, pcm_samples: bytes) -> None:
    lines = [
        "; Raw PCM sample blob extracted from NES Zelda PRG bank 7",
        "; Auto-generated by extract_audio.py - DO NOT EDIT",
        "",
        data_to_inc_bytes(pcm_samples, "PcmSamples"),
    ]
    write_text_file(os.path.join(data_dir, "pcm_samples.inc"), lines)


def write_music_blob_files(data_dir: str, blob: bytes, nes_base: int) -> None:
    dat_path = os.path.join(data_dir, "music_blob.dat")
    with open(dat_path, "wb") as f:
        f.write(blob)
    print(f"  Wrote {dat_path} ({len(blob)} bytes, NES base ${nes_base:04X})")

    inc_lines = [
        "; Music blob constants - auto-generated by extract_audio.py",
        "; DO NOT EDIT.  Regenerated on every audio extraction.",
        "",
        f"MUSIC_BLOB_NES_BASE equ ${nes_base:04X}",
        f"MUSIC_BLOB_SIZE     equ {len(blob)}",
        "",
    ]
    inc_path = os.path.join(data_dir, "music_blob.inc")
    with open(inc_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(inc_lines))
    print(f"  Wrote {inc_path}")


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Extract NES Zelda audio data and emit Genesis-ready C arrays."
    )
    parser.add_argument(
        "--out-dir",
        default=None,
        help="Output directory for data/audio/ files. Default: <repo>/data/audio/",
    )
    parser.add_argument(
        "--legacy-inc",
        action="store_true",
        default=False,
        help=(
            "Also emit legacy .inc files (vasm format) and .dat sidecars "
            "alongside C arrays."
        ),
    )
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)

    # Resolve output directory.
    if args.out_dir is not None:
        out_dir = os.path.abspath(args.out_dir)
    else:
        out_dir = os.path.join(project_root, "data", "audio")

    # Legacy .inc output goes to the old location regardless of --out-dir.
    legacy_dir = os.path.join(project_root, "src", "data")

    # Locate reference ASM files.
    z00_path = os.path.join(project_root, "reference", "aldonunez", "Z_00.asm")
    z07_path = os.path.join(project_root, "reference", "aldonunez", "Z_07.asm")

    # Locate NES ROM.
    rom_path_env = os.environ.get("ZELDA_NES_ROM", "")
    if rom_path_env and os.path.isfile(rom_path_env):
        rom_path = rom_path_env
    else:
        rom_path = os.path.join(project_root, "Legend of Zelda, The (USA).nes")

    for required_path in [z00_path, z07_path, rom_path]:
        if not os.path.exists(required_path):
            print(f"ERROR: required file not found: {required_path}")
            sys.exit(1)

    # Verify ROM SHA256.
    print(f"Reading NES ROM: {rom_path}")
    h = hashlib.sha256()
    with open(rom_path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    actual_sha256 = h.hexdigest()
    if actual_sha256.lower() != NES_ROM_SHA256.lower():
        print(f"WARNING: ROM SHA256 mismatch!")
        print(f"  Expected: {NES_ROM_SHA256}")
        print(f"  Actual:   {actual_sha256}")

    print(f"Parsing reference data: {z00_path}")
    song_blocks = parse_asm_data_blocks(z00_path, SONG_LABELS)
    sfx_blocks = parse_asm_data_blocks(z00_path, SFX_LABELS)
    z07_blocks = parse_asm_data_blocks(z07_path, Z07_LABELS)
    prg_data = read_ines_prg(rom_path)
    song_script_blocks, drive_audio_addr = extract_song_scripts(prg_data, song_blocks)
    pcm_samples = extract_pcm_samples(prg_data, z07_blocks)
    music_blob, music_blob_nes_base = extract_music_blob(prg_data, song_script_blocks)

    os.makedirs(out_dir, exist_ok=True)

    print("\nWriting C audio data files...")
    _songs_blob, songs_info = write_songs_c(out_dir, song_blocks)
    _sfx_blob, sfx_info = write_sfx_c(out_dir, sfx_blocks)
    _scripts_blob, scripts_info = write_song_scripts_c(
        out_dir, song_script_blocks, drive_audio_addr
    )
    pcm_info = write_pcm_samples_c(out_dir, pcm_samples)

    all_blocks_info = songs_info + sfx_info + scripts_info + pcm_info
    manifest = build_manifest(all_blocks_info)
    write_manifest(os.path.join(out_dir, "MANIFEST.json"), manifest)

    if args.legacy_inc:
        print("\nEmitting legacy .inc files...")
        os.makedirs(legacy_dir, exist_ok=True)
        write_songs_inc(legacy_dir, song_blocks)
        write_song_scripts_inc(legacy_dir, song_script_blocks, drive_audio_addr)
        write_sfx_inc(legacy_dir, sfx_blocks)
        write_pcm_samples_inc(legacy_dir, pcm_samples)
        write_music_blob_files(legacy_dir, music_blob, music_blob_nes_base)

        ref_dat_dir = os.path.join(project_root, "reference", "aldonunez", "dat")
        os.makedirs(ref_dat_dir, exist_ok=True)
        for label, info in song_script_blocks.items():
            dat_path = os.path.join(ref_dat_dir, f"{label}.dat")
            with open(dat_path, "wb") as f:
                f.write(info["bytes"])
            print(f"  Wrote {dat_path} ({len(info['bytes'])} bytes)")

    total_bytes = (
        sum(len(bytes(v)) for v in song_blocks.values())
        + sum(len(info["bytes"]) for info in song_script_blocks.values())
        + sum(len(bytes(v)) for v in sfx_blocks.values())
        + len(pcm_samples)
    )

    print("\n=== Audio extraction complete ===")
    print(f"  Total extracted audio table bytes: {total_bytes}")
    print(f"  Output directory: {out_dir}")


if __name__ == "__main__":
    main()
