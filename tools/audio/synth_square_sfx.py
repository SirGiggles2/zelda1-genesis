#!/usr/bin/env python3
"""synth_square_sfx.py - pre-render the NES Z1 square-channel "tunes" (SFX).

NES Zelda plays short square-wave tunes from Tune0Request ($0604, square 0)
and Tune1Request ($0602, square 1): Z_00.asm DriveTune0 / DriveTune1. These
are the monster hit, parry, item / key / heart taken, secret found, flute,
Link dying, game over ... sounds. Every table and register rule below is
parsed from or transcribed from reference/aldonunez/Z_00.asm (RULE ZERO):

DriveTune0 (one script step per frame):
  change: Tune0 = request; bit index Y (1..8, lowest set bit);
          TunePtr0 = TuneScripts0[Y-1]
  step:   byte = TuneScripts0[TunePtr0++]
          < 0 : $4000 (duty/volume) = byte, then byte = next script byte
          = 0 : end: $4000=$90, $4003=$18, $4002=0, Tune0=0
          note: EmitSquareNote0 (lo = NotePeriodTable[n+1]; lo == 0 -> no
                write; else $4002 = lo, $4003 = NotePeriodTable[n] | 8),
                $4001 = $7F
DriveTune1 (note lengths):
  change: Tune1 = request, TunePtr1 = TuneScripts1[Y-1], NoteCounter = 1
  frame:  DEC NoteCounter; at 0 fetch: < 0 -> NoteLength = byte & $7F,
          next byte; = 0 end (Game Over $40 restarts) ; note: EmitSquareNote1,
          $4005 = $7F, $4004 = $86, NoteCounter = NoteLength,
          CustomEnvelopeOffset = $1F
  vibrate (Tune1 & $90: flute $10, Link dying $80, every frame):
          if offset: offset--;  $4004 = CustomEnvelopeTune1[offset before dec]
          VibratePitch: counter >= $10 -> $4006 = lo +1 (counter bit 2 = 0)
          or lo -1 (bit 2 = 1)

APU pulse: freq = CPU / (16 * (t + 1)); duty sequences 12.5/25/50/75 %;
$4000/$4004 = DD L C VVVV (constant volume or envelope, L = loop + length
halt); a $4003/$4007 write restarts the envelope and the sequencer phase and
loads the length counter (LengthTable[value >> 3]); envelope clocked 4x and
length 2x per frame. Sweep $7F is disabled (E = 0).

Output: data/audio/sfx_pcm_tunes.{c,h} (8-bit signed PCM stored as u8,
14000 Hz, 256-byte padded, same scaling as the noise effects).
Run from repo root:  python tools/audio/synth_square_sfx.py
"""
from __future__ import annotations
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
ASM = REPO / "reference" / "aldonunez" / "Z_00.asm"
OUT_C = REPO / "data" / "audio" / "sfx_pcm_tunes.c"
OUT_H = REPO / "data" / "audio" / "sfx_pcm_tunes.h"

CPU_HZ = 1789773.0
OUT_HZ = 14000
FPS = 60.0
LENGTH_TABLE = [10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
                12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30]
DUTY_SEQ = [[0, 1, 0, 0, 0, 0, 0, 0], [0, 1, 1, 0, 0, 0, 0, 0],
            [0, 1, 1, 1, 1, 0, 0, 0], [1, 0, 0, 1, 1, 1, 1, 1]]
FIRST_ID = 77          # after the noise effects (72..76)


def table(label: str) -> list[int]:
    """All .BYTE values from `label:` up to the next label."""
    lines = ASM.read_text(encoding="utf-8").splitlines()
    i = next(k for k, l in enumerate(lines) if l.strip() == label + ":")
    out = []
    for l in lines[i + 1:]:
        s = l.strip()
        if re.match(r"^[A-Za-z_@][A-Za-z0-9_]*:", s):
            break
        m = re.match(r"\.BYTE\s+(.*)", s)
        if m:
            out += [int(x.strip().lstrip("$"), 16) for x in m.group(1).split(";")[0].split(",")]
    return out


NOTE_PERIOD = table("NotePeriodTable")
SCRIPTS0 = table("TuneScripts0")
SCRIPTS1 = table("TuneScripts1")
ENVELOPE1 = table("CustomEnvelopeTune1")


class Pulse:
    def __init__(self):
        self.reg0 = 0x30; self.lo = 0; self.hi = 0
        self.length = 0; self.decay = 0; self.div = 0; self.start = False
        self.phase = 0; self.t_acc = 0.0

    def write0(self, v): self.reg0 = v
    def write2(self, v): self.lo = v

    def write3(self, v):
        self.hi = v & 7
        self.length = LENGTH_TABLE[v >> 3]
        self.start = True
        self.phase = 0

    def quarter(self):
        v = self.reg0 & 0x0F
        if self.start:
            self.start, self.decay, self.div = False, 15, v
        elif self.div == 0:
            self.div = v
            if self.decay: self.decay -= 1
            elif self.reg0 & 0x20: self.decay = 15
        else:
            self.div -= 1

    def half(self):
        if not (self.reg0 & 0x20) and self.length: self.length -= 1

    def level(self) -> int:
        t = (self.hi << 8) | self.lo
        if self.length == 0 or t < 8:
            return 0
        vol = (self.reg0 & 0x0F) if (self.reg0 & 0x10) else self.decay
        return vol if DUTY_SEQ[self.reg0 >> 6][self.phase] else 0

    def advance(self, cpu_cycles: float):
        t = (self.hi << 8) | self.lo
        step = 2.0 * (t + 1)
        self.t_acc += cpu_cycles
        while self.t_acc >= step:
            self.t_acc -= step
            self.phase = (self.phase + 1) & 7   # sequencer steps down on NES; symmetric for timbre


def emit_note(ch: Pulse, n: int) -> int | None:
    lo = NOTE_PERIOD[n + 1]
    if lo == 0:
        return None
    ch.write2(lo)
    ch.write3(NOTE_PERIOD[n] | 0x08)
    return lo


def frames_tune0(bit: int) -> list[list]:
    """Per frame: list of (reg, value) writes, driven like DriveTune0."""
    ptr = SCRIPTS0[bit]
    frames = []
    while True:
        w = []
        b = SCRIPTS0[ptr]; ptr += 1
        if b & 0x80:
            w.append((0, b)); b = SCRIPTS0[ptr]; ptr += 1
        if b == 0:
            w += [(0, 0x90), (3, 0x18), (2, 0x00)]
            frames.append(w); return frames
        w.append(("note", b))
        frames.append(w)


def frames_tune1(bit: int) -> list[list]:
    """Per frame writes like DriveTune1 (Game Over renders one pass)."""
    tune = 1 << bit
    ptr = SCRIPTS1[bit]
    counter, length, env_off = 1, 0, 0
    frames = []
    for _ in range(4000):
        w = []
        counter = (counter - 1) & 0xFF
        if counter == 0:
            b = SCRIPTS1[ptr]; ptr += 1
            if b & 0x80:
                length = b & 0x7F
                b = SCRIPTS1[ptr]; ptr += 1
            if b == 0:
                w += [(0, 0x90), (3, 0x18), (2, 0x00)]
                frames.append(w); return frames
            w.append(("note", b))
            w.append((0, 0x86))
            counter = length
            env_off = 0x1F
        if tune & 0x90:
            y = env_off
            if env_off: env_off -= 1
            w.append((0, ENVELOPE1[y]))
            w.append(("vib", counter))
        frames.append(w)
    raise SystemExit(f"tune1 bit {bit} did not end")


def render(frames: list[list]) -> bytearray:
    ch = Pulse()
    cur_lo = 0
    per_frame = CPU_HZ / FPS
    per_out = CPU_HZ / OUT_HZ
    pcm = bytearray()
    for w in frames:
        for reg, v in w:
            if reg == "note":
                lo = emit_note(ch, v)
                if lo is not None: cur_lo = lo
            elif reg == "vib":
                if v >= 0x10:
                    ch.write2((cur_lo - 1) & 0xFF if (v >> 2) & 1 else (cur_lo + 1) & 0xFF)
            elif reg == 0: ch.write0(v)
            elif reg == 2: ch.write2(v)
            elif reg == 3: ch.write3(v)
        n = int(round(OUT_HZ / FPS))
        qpoints = {(k * n) // 4: k for k in range(4)}   # 4-step frame counter
        for s in range(n):
            if s in qpoints:
                ch.quarter()
                if qpoints[s] in (1, 3): ch.half()
            ch.advance(per_out)
            out = ch.level()
            pcm.append(int(round(out / 15.0 * 100.0)) & 0xFF)
    while len(pcm) % 256:
        pcm.append(0)
    return pcm


def main() -> None:
    tunes = []
    for bit in range(7):                     # Tune0 bits 0..6 ($80 = silence)
        tunes.append((f"tune0_{bit}", frames_tune0(bit)))
    for bit in range(8):                     # Tune1 bits 0..7
        tunes.append((f"tune1_{bit}", frames_tune1(bit)))
    h = ["/* AUTO-GENERATED by tools/audio/synth_square_sfx.py - do not edit. */",
         "#ifndef SFX_PCM_TUNES_H", "#define SFX_PCM_TUNES_H", '#include "sfx_pcm.h"',
         f"#define SFX_PCM_TUNE_FIRST_ID {FIRST_ID}",
         "/* Index: Tune0 bits 0..6 = 0..6, Tune1 bits 0..7 = 7..14. */",
         f"#define SFX_PCM_TUNE_COUNT {len(tunes)}"]
    c = ["/* AUTO-GENERATED by tools/audio/synth_square_sfx.py - do not edit.",
         " * NES Z1 square-channel tunes (Z_00.asm DriveTune0/DriveTune1). */",
         '#include "sfx_pcm_tunes.h"']
    lens, frames_n = [], []
    for i, (name, frames) in enumerate(tunes):
        pcm = render(frames)
        lens.append(len(pcm)); frames_n.append(len(frames))
        rows = ["    " + ", ".join(f"0x{b:02X}" for b in pcm[k:k + 16]) + ","
                for k in range(0, len(pcm), 16)]
        c.append(f"static const u8 __attribute__((aligned(256))) sfx_pcm_{name}[{len(pcm)}] = {{")
        c += rows
        c.append("};")
        print(f"{name}: {len(frames)} frames, {len(pcm)} bytes, id {FIRST_ID + i}")
    h.append("extern const u8 *const sfx_pcm_tune_data[SFX_PCM_TUNE_COUNT];")
    h.append("extern const u32 sfx_pcm_tune_len[SFX_PCM_TUNE_COUNT];")
    h.append("extern const u16 sfx_pcm_tune_frames[SFX_PCM_TUNE_COUNT];")
    h.append("#endif")
    c.append("const u8 *const sfx_pcm_tune_data[SFX_PCM_TUNE_COUNT] = {")
    c += [f"    sfx_pcm_{name}," for name, _ in tunes]
    c.append("};")
    c.append("const u32 sfx_pcm_tune_len[SFX_PCM_TUNE_COUNT] = { " + ", ".join(map(str, lens)) + " };")
    c.append("const u16 sfx_pcm_tune_frames[SFX_PCM_TUNE_COUNT] = { " + ", ".join(map(str, frames_n)) + " };")
    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.write_text("\n".join(h) + "\n", encoding="utf-8")
    OUT_C.write_text("\n".join(c) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
