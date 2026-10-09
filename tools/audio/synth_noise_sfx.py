#!/usr/bin/env python3
"""synth_noise_sfx.py - pre-render the NES Z1 noise-channel sound effects.

NES Zelda plays these on the APU noise channel from EffectRequest ($0603)
bits (Z_00.asm DriveEffect), so they are not among the 7 DMC samples
(tools/extract_dmc_samples.py). Stairs (bit 3) is rendered by
tools/parity/cave_golden/synth_stairs_sfx.py; this tool renders the others
from the same disassembly tables and frame rules (RULE ZERO: every period,
volume and frame count below is read from reference/aldonunez/Z_00.asm):

  bit 0 sword   PlaySwordSfx: EffectCounter $0A; note = SwordSfxNotes[c-1]
  bit 1 arrow   PlayArrowSfx: EffectCounter $05; note = ArrowSfxNotes[c-1]
                (also the boomerang: PlayBoomerangSfx requests bit 1)
  bit 2 flame   @PlayFlameSfx: EffectCounter $20; Y = c >> 1;
                $400C = FlameSfxNotes[Y-1] (Y = 0 reads the byte before the
                table, ArrowSfxNotes' last byte $3F); period fixed $0E
  bit 4 bomb    @PlayBombSfx: EffectCounter $18; note = BombSfxNotes[c-1]
  bit 5 sea     @PlaySeaSfx: SeaSfxCounter $D0, volume byte [$68] from $10:
                counter >= $BF: INC; else every 8th frame DEC down to $10;
                $400C = [$68], period 3

PlaySfxNote: $400E = note & $0F, $400C = (note >> 4) | $10 (constant volume).
Every frame also writes $400F ($08), restarting the envelope. A $400C value
without bit 4 (the sea above $1F) runs the APU envelope: decay restarts at 15
each frame and steps down every (V+1) quarter frames.

Noise: 15-bit LFSR, mode 0 (bit0 XOR bit1), clocked every
NoisePeriodTable[idx] CPU cycles (NTSC); output = level when LFSR bit 0 is 0.
PCM format and scaling match sfx_pcm_stairs (8-bit signed stored as u8,
14000 Hz, 256-byte padded).

Output: data/audio/sfx_pcm_noise.{c,h}. Run from repo root:
  python tools/audio/synth_noise_sfx.py
"""
from __future__ import annotations
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
OUT_C = REPO / "data" / "audio" / "sfx_pcm_noise.c"
OUT_H = REPO / "data" / "audio" / "sfx_pcm_noise.h"

CPU_HZ = 1789773.0
OUT_HZ = 14000
FPS = 60.0
NOISE_PERIOD_NTSC = [4, 8, 16, 32, 64, 96, 128, 160,
                     202, 254, 380, 508, 762, 1016, 2034, 4068]

SWORD = [0x47, 0x67, 0x87, 0xA8, 0xB9, 0x9A, 0x8A, 0x5A, 0x9B, 0x8B]
ARROW = [0xFB, 0xF9, 0x9D, 0x6E, 0x3F]
FLAME = [0x1A, 0x1A, 0x1C, 0x1D, 0x1D, 0x1E, 0x1E, 0x1F,
         0x1F, 0x1E, 0x1A, 0x19, 0x16, 0x13, 0x11, 0x11]
BOMB = [0x1F, 0x2F, 0x2E, 0x3F, 0x3F, 0x4C, 0x4E, 0x5F,
        0x6F, 0x6F, 0x7E, 0x8F, 0x9E, 0xAF, 0xBE, 0xCF,
        0xDE, 0xEF, 0xFE, 0xFD, 0xFE, 0xFF, 0xFF, 0xFE]


def note_regs(note: int) -> tuple[int, int]:
    """PlaySfxNote: ($400E period idx, $400C value)."""
    return note & 0x0F, (note >> 4) | 0x10


def frames_counted(table: list[int], count: int) -> list[tuple[int, int]]:
    return [note_regs(table[c - 1]) for c in range(count, 0, -1)]


def frames_flame() -> list[tuple[int, int]]:
    out = []
    for c in range(0x20, 0, -1):
        y = c >> 1
        val = FLAME[y - 1] if y >= 1 else ARROW[-1]   # FlameSfxNotes-1 = $3F
        out.append((0x0E, val))
    return out


def frames_sea() -> list[tuple[int, int]]:
    out = []
    vol = 0x10
    for c in range(0xD0, 0, -1):
        if c >= 0xBF:
            vol = (vol + 1) & 0xFF
        elif (c & 7) == 7 and vol != 0x10:
            vol -= 1
        out.append((0x03, vol))
    return out


def levels_for_frame(reg400c: int) -> list[int]:
    """Output level for each of the 4 quarter frames of one video frame."""
    v = reg400c & 0x0F
    if reg400c & 0x10:
        return [v] * 4
    # Envelope, restarted by the $400F write this frame.
    decay, divider, out = 15, v, []
    for q in range(4):
        if q == 0:
            decay, divider = 15, v
        elif divider == 0:
            divider = v
            if decay > 0:
                decay -= 1
        else:
            divider -= 1
        out.append(decay)
    return out


def render(frames: list[tuple[int, int]]) -> bytearray:
    total = int(round(len(frames) / FPS * OUT_HZ))
    per_frame = OUT_HZ / FPS
    apu_per_out = CPU_HZ / OUT_HZ
    lfsr, t_apu = 1, 0.0
    pcm = bytearray()
    for s in range(total):
        fi = min(int(s / per_frame), len(frames) - 1)
        period_idx, reg = frames[fi]
        q = min(int((s - fi * per_frame) / (per_frame / 4)), 3)
        level = levels_for_frame(reg)[q]
        period = NOISE_PERIOD_NTSC[period_idx]
        target = s * apu_per_out
        while t_apu + period <= target:
            fb = (lfsr ^ (lfsr >> 1)) & 1
            lfsr = ((lfsr >> 1) | (fb << 14)) & 0x7FFF
            t_apu += period
        out = level if (lfsr & 1) == 0 else 0
        s8 = int(round((out / 15.0) * 100.0)) if out > 0 else 0
        pcm.append(max(-128, min(127, s8)) & 0xFF)
    while len(pcm) % 256:
        pcm.append(0)
    return pcm


EFFECTS = [  # (name, XGM PCM id, frames)
    ("sword", 72, frames_counted(SWORD, 0x0A)),
    ("arrow", 73, frames_counted(ARROW, 0x05)),
    ("flame", 74, frames_flame()),
    ("bomb", 75, frames_counted(BOMB, 0x18)),
    ("sea", 76, frames_sea()),
]


def main() -> None:
    h = ["/* AUTO-GENERATED by tools/audio/synth_noise_sfx.py - do not edit. */",
         "#ifndef SFX_PCM_NOISE_H", "#define SFX_PCM_NOISE_H", '#include "sfx_pcm.h"']
    c = ["/* AUTO-GENERATED by tools/audio/synth_noise_sfx.py - do not edit.",
         " * NES Z1 noise-channel effects (Z_00.asm DriveEffect) rendered via the",
         " * NES noise LFSR: 8-bit signed PCM stored as u8 @ 14000 Hz. */",
         '#include "sfx_pcm_noise.h"']
    for name, pid, frames in EFFECTS:
        pcm = render(frames)
        up = name.upper()
        h.append(f"#define SFX_PCM_{up}_ID   {pid}")
        h.append(f"#define SFX_PCM_{up}_LEN  {len(pcm)}")
        h.append(f"extern const u8 sfx_pcm_{name}[SFX_PCM_{up}_LEN];")
        rows = ["    " + ", ".join(f"0x{b:02X}" for b in pcm[i:i + 16]) + ","
                for i in range(0, len(pcm), 16)]
        c.append(f"const u8 __attribute__((aligned(256))) sfx_pcm_{name}[SFX_PCM_{up}_LEN] = {{")
        c.extend(rows)
        c.append("};")
        print(f"{name}: {len(frames)} frames, {len(pcm)} bytes, id {pid}")
    h.append("#endif")
    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.write_text("\n".join(h) + "\n", encoding="utf-8")
    OUT_C.write_text("\n".join(c) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
