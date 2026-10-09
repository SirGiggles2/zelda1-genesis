#!/usr/bin/env python
"""
extract_dmc_samples.py — Pull the 7 DMC samples used by Zelda 1 out of the
stock NES ROM and produce assets consumable by the Genesis port.

Outputs:
  data/dmc_samples.bin        — concatenated raw DMC delta-PCM bytes
  data/dmc_samples_pcm.bin    — concatenated 8-bit unsigned linear PCM, ready
                                 to stream directly to YM2612 reg $2A (DAC).
                                 Each delta byte decodes to 8 PCM bytes, so the
                                 PCM blob is ~8x the delta blob.
  src/dmc_samples.inc         — 68K-assembler include: labels, offsets, sizes,
                                 NES $4010/$4012/$4013 values. Emits both
                                 delta and PCM offset tables and incbin labels.
  data/dmc_samples_wav/*.wav  — decoded PCM for ear-checking (optional)

Sample-table source is `src/zelda_translated/z_00.asm`:

    SampleAddrs:   dc.b $00, $4C, $80, $1D, $20, $28, $4C   ; $4012 values
    SampleLengths: dc.b $75, $C0, $40, $0A, $B0, $90, $D0   ; $4013 values
    SampleRates:   dc.b $0F, $0F, $0D, $0F, $0E, $0F, $0E   ; $4010 values

NES DMC encoding (per https://www.nesdev.org/wiki/APU_DMC):
  $4012 byte N  → sample start address = $C000 + N*64
  $4013 byte N  → sample length        = N*16 + 1   bytes
  $4010 low 4   → playback rate (period table below)

Zelda uses MMC1 with a 128 KB PRG. The last 16 KB bank is fixed at
$C000-$FFFF, so any address in that range is at file offset
  0x10  +  7 * 0x4000  +  (addr - 0xC000)
  = 0x1C010 + (addr - 0xC000)

Usage:
  python tools/extract_dmc_samples.py [path-to-rom]

If no path given, looks for "Legend of Zelda, The (USA).nes" in worktree root.
"""

from __future__ import annotations

import math
import os
import struct
import sys
import wave
from fractions import Fraction
from pathlib import Path

import numpy as np
from scipy.signal import resample_poly

# Measured empirically via bizhawk_audio_probe.lua with music active:
# ~8751 HBlank interrupts per second reach the DMC streamer (YM ym_write
# SR=6 lockouts mask ~40% of the theoretical 15700 Hz per-line rate).
# Samples are bandlimited to this rate so playback sounds clean without
# aliasing / jitter artifacts.
GENESIS_HINT_HZ = 8751

# SGDK XGM (Doppler) driver fixed PCM rate. Z80 mixes 4 channels at this
# rate; sample data must arrive bandlimited to this rate, 8-bit unsigned,
# $80-centered, 256-byte aligned in length.
XGM_PCM_HZ = 14000

# XGM SFX sample IDs must be >= 64 (1..63 reserved for music). Map our
# 1-based dmc_trigger index to 64..70.
XGM_SFX_ID_BASE = 64

# --- sample table -----------------------------------------------------------

SAMPLE_NAMES = [
    "SFX_01",   # 1
    "SFX_02",   # 2
    "SFX_03",   # 3
    "SFX_04",   # 4
    "SFX_05",   # 5
    "SFX_06",   # 6
    "SFX_07",   # 7
]
SAMPLE_ADDR_BYTES   = [0x00, 0x4C, 0x80, 0x1D, 0x20, 0x28, 0x4C]  # $4012
SAMPLE_LENGTH_BYTES = [0x75, 0xC0, 0x40, 0x0A, 0xB0, 0x90, 0xD0]  # $4013
SAMPLE_RATE_BYTES   = [0x0F, 0x0F, 0x0D, 0x0F, 0x0E, 0x0F, 0x0E]  # $4010

# NES NTSC DMC period table (CPU cycles per output bit)
DMC_PERIOD_NTSC = [428, 380, 340, 320, 286, 254, 226, 214,
                   190, 160, 142, 128, 106,  84,  72,  54]
CPU_HZ_NTSC = 1789773


def dmc_rate_hz(rate_idx: int) -> float:
    return CPU_HZ_NTSC / DMC_PERIOD_NTSC[rate_idx & 0xF]


# --- ROM IO -----------------------------------------------------------------

def read_ines(rom_path: Path):
    data = rom_path.read_bytes()
    if data[:4] != b"NES\x1A":
        raise SystemExit(f"{rom_path}: not an iNES file")
    prg_banks = data[4]  # 16 KB units
    chr_banks = data[5]  # 8 KB units
    prg_size = prg_banks * 0x4000
    header = 0x10
    # Trainer?
    if data[6] & 0x04:
        header += 0x200
    prg = data[header:header + prg_size]
    print(f"  iNES: PRG={prg_banks}*16KB ({prg_size} B), CHR={chr_banks}*8KB, "
          f"mapper={((data[6]>>4) | (data[7]&0xF0))}")
    if prg_size != 0x20000:
        raise SystemExit(f"expected 128 KB PRG, got {prg_size}")
    return prg  # 128 KB


def nes_addr_to_prg_offset(addr: int, prg: bytes) -> int:
    # MMC1, last bank fixed at $C000-$FFFF.
    assert 0xC000 <= addr <= 0xFFFF, f"DMC addr {addr:04X} outside last bank"
    return (len(prg) - 0x4000) + (addr - 0xC000)


# --- DMC decoder (delta-PCM → uint8 signed-7) -------------------------------

def decode_dmc(sample_bytes: bytes) -> list[int]:
    """Decode NES DMC delta modulation to a list of 7-bit DAC levels (0..127)."""
    level = 64  # reset value in the Zelda driver is 64 (mid)
    out: list[int] = []
    for byte in sample_bytes:
        for bit in range(8):
            if byte & (1 << bit):
                if level <= 125:
                    level += 2
            else:
                if level >= 2:
                    level -= 2
            out.append(level)
    return out


def write_wav(path: Path, samples: list[int], rate_hz: float) -> None:
    # Convert 0..127 → signed int16 centered at 0, ~1/2 full scale
    frames = bytearray()
    for s in samples:
        v = (s - 64) * 256  # roughly ±16k
        frames += struct.pack("<h", max(-32768, min(32767, v)))
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(int(round(rate_hz)))
        w.writeframes(bytes(frames))


# --- main -------------------------------------------------------------------

def main(argv: list[str]) -> int:
    here = Path(__file__).resolve().parent
    worktree = here.parent
    default_rom = worktree / "Legend of Zelda, The (USA).nes"
    rom_path = Path(argv[1]) if len(argv) > 1 else default_rom
    print(f"ROM: {rom_path}")
    prg = read_ines(rom_path)

    # Blobs live under src/data/ so vasm's `include` directive (run with cwd
    # = src/) can pull them in with a short "data/..." path matching the
    # other audio-include conventions (music_blob.dat, pcm_samples.inc).
    src_dir = worktree / "src"
    data_dir = src_dir / "data"
    wav_dir = worktree / "data" / "dmc_samples_wav"
    data_dir.mkdir(parents=True, exist_ok=True)
    wav_dir.mkdir(parents=True, exist_ok=True)

    bin_path = data_dir / "dmc_samples.bin"
    pcm_path = data_dir / "dmc_samples_pcm.bin"
    inc_path = data_dir / "dmc_samples.inc"

    blob = bytearray()       # raw DMC delta
    pcm_blob = bytearray()   # 8-bit unsigned linear PCM, ready for YM $2A
    offsets: list[int] = []
    lengths: list[int] = []
    pcm_offsets: list[int] = []
    pcm_lengths: list[int] = []
    nes_addrs: list[int] = []

    # XGM-rate (14 kHz) per-sample bytes. Each entry is the resampled and
    # 256-byte-padded PCM ready for SGDK XGM_setPCM(). Indexed 0..6.
    xgm_samples: list[bytes] = []

    for i, (name, a12, a13, a10) in enumerate(
            zip(SAMPLE_NAMES, SAMPLE_ADDR_BYTES, SAMPLE_LENGTH_BYTES, SAMPLE_RATE_BYTES)):
        nes_addr = 0xC000 + a12 * 64
        length   = a13 * 16 + 1
        rate_idx = a10 & 0x0F
        rate_hz  = dmc_rate_hz(rate_idx)

        prg_off = nes_addr_to_prg_offset(nes_addr, prg)
        sample  = bytes(prg[prg_off:prg_off + length])
        if len(sample) != length:
            raise SystemExit(f"{name}: short read ({len(sample)}/{length})")

        offsets.append(len(blob))
        lengths.append(length)
        nes_addrs.append(nes_addr)
        blob.extend(sample)

        # Decode delta -> 7-bit DAC levels (0..127) at native NES rate.
        decoded = decode_dmc(sample)

        # Bandlimited resample from native NES rate to Genesis HINT rate.
        # Use scipy.signal.resample_poly with a rational up/down ratio so
        # the built-in Kaiser-window FIR prefilter eliminates aliasing
        # (the cause of the "crunchy" sound in earlier linear-interp
        # builds). Convert 0..127 -> -64..+63 so filtering is centered
        # at zero, then re-bias back to 8-bit unsigned $80-centered.
        frac = Fraction(GENESIS_HINT_HZ, int(round(rate_hz))).limit_denominator(1000)
        up, down = frac.numerator, frac.denominator
        centered = np.asarray(decoded, dtype=np.float64) - 64.0
        resampled_f = resample_poly(centered, up, down)
        # 7-bit DAC levels scale to 0..127; shift <<1 to 8-bit unsigned.
        resampled_u7 = np.clip(np.round(resampled_f + 64.0), 0, 127).astype(np.uint8)
        resampled_u8 = ((resampled_u7.astype(np.uint16) << 1) & 0xFF).astype(np.uint8)

        pcm_offsets.append(len(pcm_blob))
        pcm_lengths.append(len(resampled_u8))
        pcm_blob.extend(resampled_u8.tobytes())

        # XGM-rate resample (independent rate from the legacy HINT path).
        #
        # Format: SGDK XGM driver expects 8-bit SIGNED two's-complement PCM
        # at 14 kHz, silence = 0x00. Verified from sgdk/bin/xgm.txt:7
        # ("8 bits signed at 14 Khz") and Z80 mixer drv_xgm.s80:405-409
        # ("ADD (HL); JP PO,.ok" — signed-overflow check on each add,
        # clamps to $7F/$80 via "LD A,C; ADC $FF"). Earlier builds emitted
        # 8-bit unsigned $80-centered; XGM read silence ($80) as signed
        # -128 = max negative DC bias = harsh clipping + offset hum.
        #
        # Conversion: `centered` is float in range ~-64..+63 (7-bit DAC
        # delta-decoded, biased to zero). Scale x2 to fill 8-bit signed
        # range -128..+127, clamp, cast to int8.
        xgm_frac = Fraction(XGM_PCM_HZ, int(round(rate_hz))).limit_denominator(1000)
        xgm_up, xgm_down = xgm_frac.numerator, xgm_frac.denominator
        xgm_resampled_f = resample_poly(centered, xgm_up, xgm_down)
        xgm_s8 = np.clip(np.round(xgm_resampled_f * 2.0), -128, 127).astype(np.int8)
        # XGM requires sample length to be a multiple of 256; pad with 0
        # (signed silence). SGDK auto-aligns the address; we control
        # length here so the C array is the right shape.
        pad_target = ((len(xgm_s8) + 255) // 256) * 256
        if pad_target > len(xgm_s8):
            pad_bytes = np.zeros(pad_target - len(xgm_s8), dtype=np.int8)
            xgm_s8 = np.concatenate([xgm_s8, pad_bytes])
        # XGM_setPCM takes const u8*, but the byte values are interpreted
        # signed by the Z80 mixer. tobytes() preserves the bit pattern.
        xgm_samples.append(xgm_s8.tobytes())

        wav_path = wav_dir / f"{i+1:02d}_{name}.wav"
        write_wav(wav_path, decoded, rate_hz)

        print(f"  [{i+1}] {name}: NES ${nes_addr:04X} "
              f"(PRG ${prg_off:05X})  {length} B delta / "
              f"{len(decoded)} B pcm @ {rate_hz:.0f} Hz -> "
              f"HINT {len(resampled_u8)} B @ {GENESIS_HINT_HZ} Hz "
              f"({up}/{down}); XGM {len(xgm_s8)} B @ {XGM_PCM_HZ} Hz "
              f"({xgm_up}/{xgm_down}) [signed]")

    # align blobs to even byte for M68K rept loads
    if len(blob) & 1:
        blob.append(0x00)
    if len(pcm_blob) & 1:
        pcm_blob.append(0x80)  # center value = silence for PCM

    bin_path.write_bytes(bytes(blob))
    pcm_path.write_bytes(bytes(pcm_blob))
    print(f"\nWrote {len(blob)} B delta -> {bin_path}")
    print(f"Wrote {len(pcm_blob)} B pcm   -> {pcm_path}")

    # --- emit assembler include --------------------------------------------
    lines = []
    lines.append("; dmc_samples.inc - auto-generated by tools/extract_dmc_samples.py")
    lines.append("; DO NOT EDIT BY HAND. Regenerate from Legend of Zelda, The (USA).nes.")
    lines.append(";")
    lines.append("; 7 DMC samples ripped from the stock NES ROM, available in two forms:")
    lines.append(";")
    lines.append(";   data/dmc_samples.bin     — raw DMC delta-PCM bytes. Kept for a")
    lines.append(";                              possible future true-NES-DMC emulation")
    lines.append(";                              path. The current Phase-C scaffold does")
    lines.append(";                              NOT consume this.")
    lines.append(";   data/dmc_samples_pcm.bin — decoded 8-bit unsigned linear PCM,")
    lines.append(";                              ready to stream byte-by-byte to YM2612")
    lines.append(";                              register $2A (DAC). Each delta byte")
    lines.append(";                              expands to 8 PCM bytes.")
    lines.append(";")
    lines.append("; Each sample's metadata matches the NES $4010/$4012/$4013 values so the")
    lines.append("; APU register stubs in nes_io.asm can look them up by 1-based index.")
    lines.append("")
    lines.append("DMC_SAMPLE_COUNT    equ     " + str(len(SAMPLE_NAMES)))
    lines.append("")
    lines.append("; --- Delta form (raw DMC bytes) ----------------------------------------")
    for i, name in enumerate(SAMPLE_NAMES):
        lines.append(f"DMC_{name}_DELTA_OFF equ     ${offsets[i]:04X}")
        lines.append(f"DMC_{name}_DELTA_LEN equ     ${lengths[i]:04X}")
    lines.append("")
    lines.append("; --- PCM form (decoded, ready for YM reg $2A) --------------------------")
    for i, name in enumerate(SAMPLE_NAMES):
        lines.append(f"DMC_{name}_PCM_OFF   equ     ${pcm_offsets[i]:06X}")
        lines.append(f"DMC_{name}_PCM_LEN   equ     ${pcm_lengths[i]:06X}")
    lines.append("")
    lines.append("; --- Shared metadata ---------------------------------------------------")
    for i, name in enumerate(SAMPLE_NAMES):
        lines.append(f"DMC_{name}_RATE      equ     ${SAMPLE_RATE_BYTES[i]:02X}"
                     f"   ; $4010 raw")
        lines.append(f"DMC_{name}_NESADDR   equ     ${nes_addrs[i]:04X}"
                     f"   ; $4012 -> NES addr")
    lines.append("")
    lines.append("; --- Parallel lookup tables, 1-based --------------------------------")
    lines.append("; Note: DMC_SAMPLE_PCM_OFFS/LENS are long-word because decoded")
    lines.append("; samples are up to ~26 KB each (larger than a 16-bit offset).")
    lines.append("    even")
    lines.append("DMC_SAMPLE_PCM_OFFS:")
    for i, name in enumerate(SAMPLE_NAMES):
        lines.append(f"    dc.l    DMC_{name}_PCM_OFF       ; {i+1}")
    lines.append("")
    lines.append("    even")
    lines.append("DMC_SAMPLE_PCM_LENS:")
    for i, name in enumerate(SAMPLE_NAMES):
        lines.append(f"    dc.l    DMC_{name}_PCM_LEN       ; {i+1}")
    lines.append("")
    lines.append("DMC_SAMPLE_RATES:")
    for i, name in enumerate(SAMPLE_NAMES):
        lines.append(f"    dc.b    DMC_{name}_RATE          ; {i+1}")
    lines.append("    even")
    lines.append("")
    lines.append("; Per-frame burst size at ~60 Hz VBlank.  Computed as")
    lines.append("; round(dmc_rate_hz / 60) so playback speed matches the NES")
    lines.append("; original.  Used by dmc_feed to drain exactly one frame's")
    lines.append("; worth of PCM bytes per music_tick.")
    lines.append("    even")
    lines.append("DMC_SAMPLE_BURST60:")
    for i, name in enumerate(SAMPLE_NAMES):
        burst60 = int(round(dmc_rate_hz(SAMPLE_RATE_BYTES[i]) / 60.0))
        lines.append(f"    dc.w    {burst60}                     ; {i+1} "
                     f"({dmc_rate_hz(SAMPLE_RATE_BYTES[i]):.0f} Hz)")
    lines.append("")
    lines.append("; dbra-count per sample for dmc_trigger's synchronous cycle-paced")
    lines.append("; streamer.  M68K runs at 7.67 MHz NTSC; a sample at rate_hz needs")
    lines.append("; one DAC write every (7670000 / rate_hz) cycles.  Fixed overhead")
    lines.append("; per byte in the streamer inner loop is ~50 cycles; the dbra spin")
    lines.append("; loop contributes 10*N + 4 cycles for dbra count N.  So:")
    lines.append(";     N = round((cycles_per_byte - 54) / 10)")
    lines.append("; Clamp to zero so a too-slow-CPU assumption never goes negative.")
    lines.append("    even")
    lines.append("DMC_SAMPLE_SPIN:")
    M68K_HZ = 7670000
    FIXED_OVERHEAD = 54  # cycles of non-spin work per byte in streamer inner loop
    for i, name in enumerate(SAMPLE_NAMES):
        hz = dmc_rate_hz(SAMPLE_RATE_BYTES[i])
        cyc = M68K_HZ / hz
        spin = max(0, int(round((cyc - FIXED_OVERHEAD) / 10.0)))
        lines.append(f"    dc.w    {spin:<5}                  ; {i+1} "
                     f"({hz:.0f} Hz, {cyc:.0f} cyc/byte)")
    lines.append("")
    lines.append("; $4015-stub lookup by (addr, rate) tuple. Used to recover the 1-based")
    lines.append("; sample index when Zelda's SelectDMC writes $4010 (rate) + $4012 (addr)")
    lines.append("; + $4015 ($1F). 4 bytes per entry so scan step = addq.l #4,A0.")
    lines.append("    even")
    lines.append("DMC_SAMPLE_LOOKUP:")
    for i, name in enumerate(SAMPLE_NAMES):
        lines.append(f"    dc.b    ${SAMPLE_ADDR_BYTES[i]:02X},${SAMPLE_RATE_BYTES[i]:02X},"
                     f" {i+1},0   ; {i+1} {name}")
    lines.append("DMC_SAMPLE_LOOKUP_END:")
    lines.append("")
    lines.append("; --- Blobs ------------------------------------------------------------")
    lines.append("    even")
    lines.append("DMC_SAMPLE_BLOB:")
    lines.append('    incbin  "data/dmc_samples.bin"')
    lines.append("DMC_SAMPLE_BLOB_END:")
    lines.append("")
    lines.append("    even")
    lines.append("DMC_SAMPLE_PCM_BLOB:")
    lines.append('    incbin  "data/dmc_samples_pcm.bin"')
    lines.append("DMC_SAMPLE_PCM_BLOB_END:")
    lines.append("")

    # vasm accepts UTF-8 comments; switch off ASCII enforcement so that em-dashes
    # and arrows in the generated include don't blow up the writer on Windows.
    inc_path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"Wrote asm include -> {inc_path}")
    print(f"Wrote {len(SAMPLE_NAMES)} WAVs -> {wav_dir}")

    emit_xgm_bank(worktree, xgm_samples)
    return 0


def emit_xgm_bank(worktree: Path, xgm_samples: list[bytes]) -> None:
    """Write data/audio/sfx_pcm.{c,h} for the SGDK XGM PCM driver.

    Each sample is emitted as a `static const u8 sfx_pcm_NN[len]` array
    aligned to 256 bytes. A `sfx_pcm_table` of {ptr, len} pairs lets
    audio_adapter.c iterate registrations at boot."""
    audio_dir = worktree / "data" / "audio"
    audio_dir.mkdir(parents=True, exist_ok=True)
    c_path = audio_dir / "sfx_pcm.c"
    h_path = audio_dir / "sfx_pcm.h"

    # --- header --------------------------------------------------------
    h_lines: list[str] = []
    h_lines.append("/* sfx_pcm.h - auto-generated by tools/extract_dmc_samples.py")
    h_lines.append(" * DO NOT EDIT BY HAND. Regenerate from Legend of Zelda, The (USA).nes.")
    h_lines.append(" *")
    h_lines.append(" * Seven NES DMC samples ripped from the stock ROM, decoded and bandlimited")
    h_lines.append(f" * to {XGM_PCM_HZ} Hz for the SGDK XGM (Doppler) driver. Format is 8-bit")
    h_lines.append(" * SIGNED two's-complement, 0x00 = silence, length padded to 256-byte")
    h_lines.append(" * boundary with 0x00 silence. (Stored as u8 because XGM_setPCM takes u8*,")
    h_lines.append(" * but the Z80 mixer interprets each byte as int8 — see drv_xgm.s80:405.)")
    h_lines.append(" *")
    h_lines.append(f" * Sample IDs start at {XGM_SFX_ID_BASE} (XGM reserves 1..63 for music).")
    h_lines.append(" */")
    h_lines.append("#ifndef SFX_PCM_H")
    h_lines.append("#define SFX_PCM_H")
    h_lines.append("")
    h_lines.append('#include "types.h"')
    h_lines.append("")
    h_lines.append(f"#define SFX_PCM_COUNT       {len(xgm_samples)}")
    h_lines.append(f"#define SFX_PCM_RATE_HZ     {XGM_PCM_HZ}")
    h_lines.append(f"#define SFX_PCM_ID_BASE     {XGM_SFX_ID_BASE}")
    h_lines.append("")
    for i, name in enumerate(SAMPLE_NAMES):
        h_lines.append(f"#define SFX_PCM_{name}_ID    {XGM_SFX_ID_BASE + i}")
        h_lines.append(f"#define SFX_PCM_{name}_LEN   {len(xgm_samples[i])}")
        h_lines.append(f"extern const u8 sfx_pcm_{name.lower()}[{len(xgm_samples[i])}];")
        h_lines.append("")
    h_lines.append("typedef struct {")
    h_lines.append("    const u8 *data;")
    h_lines.append("    u32       len;")
    h_lines.append("    u8        id;")
    h_lines.append("} sfx_pcm_entry_t;")
    h_lines.append("")
    h_lines.append("extern const sfx_pcm_entry_t sfx_pcm_table[SFX_PCM_COUNT];")
    h_lines.append("")
    h_lines.append("#endif")
    h_lines.append("")
    h_path.write_text("\n".join(h_lines), encoding="utf-8", newline="\n")
    print(f"Wrote XGM header  -> {h_path}")

    # --- C body --------------------------------------------------------
    c_lines: list[str] = []
    c_lines.append("/* sfx_pcm.c - auto-generated by tools/extract_dmc_samples.py")
    c_lines.append(" * DO NOT EDIT BY HAND. */")
    c_lines.append('#include "sfx_pcm.h"')
    c_lines.append("")
    for i, name in enumerate(SAMPLE_NAMES):
        data = xgm_samples[i]
        c_lines.append(f"const u8 __attribute__((aligned(256))) sfx_pcm_{name.lower()}[{len(data)}] = {{")
        # 16 bytes per row for compactness
        for row_start in range(0, len(data), 16):
            row = data[row_start:row_start + 16]
            row_str = ", ".join(f"0x{b:02X}" for b in row)
            c_lines.append(f"    {row_str},")
        c_lines.append("};")
        c_lines.append("")
    c_lines.append("const sfx_pcm_entry_t sfx_pcm_table[SFX_PCM_COUNT] = {")
    for i, name in enumerate(SAMPLE_NAMES):
        c_lines.append(f"    {{ sfx_pcm_{name.lower()}, "
                       f"SFX_PCM_{name}_LEN, SFX_PCM_{name}_ID }},")
    c_lines.append("};")
    c_lines.append("")
    c_path.write_text("\n".join(c_lines), encoding="utf-8", newline="\n")
    total = sum(len(s) for s in xgm_samples)
    print(f"Wrote XGM C bank  -> {c_path}  ({total} B over {len(xgm_samples)} samples)")


if __name__ == "__main__":
    sys.exit(main(sys.argv))
