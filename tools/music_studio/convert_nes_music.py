#!/usr/bin/env python3
"""
convert_nes_music.py - NES Zelda songs -> Genesis song files for Music Studio.

Step 1 of the music pipeline (step 2 = tools/music_studio/ studio page,
step 3 = bake into the ROM).

How it extracts (no hand-transcription, no guessed engine behavior):
  * Runs the REAL NES sound driver (DriveAudio, PRG bank 0) from the user's
    ROM inside py65, one call per video frame, exactly like the NMI does.
  * Records every APU register write ($4000-$4017) per frame.
  * Interprets those writes with an NES APU model (length counters,
    triangle linear counter, constant volume, sweep mute, $4015) to get what
    is audible each frame: pitch + volume per channel.
  * Converts that into Genesis voices: note-on / pitch / volume / note-off
    events (melody = Square 2, harmony = Square 1, bass = Triangle,
    drums = Noise).  The studio maps voices onto YM2612 FM + PSG channels.

Output is derived from the user's ROM, so it goes to build/music_studio/
(never committed, never distributed).

Usage:
  python tools/music_studio/convert_nes_music.py [--rom PATH] [--out DIR]
"""

import argparse
import hashlib
import json
import math
import sys
from pathlib import Path

from py65.devices.mpu6502 import MPU
from py65.memory import ObservableMemory

REPO = Path(__file__).resolve().parents[2]
DEFAULT_ROM = REPO / "roms" / "Legend of Zelda, The (USA).nes"
DEFAULT_OUT = REPO / "build" / "music_studio"
NES_ROM_SHA256 = "8f72dc2e98572eb4ba7c3a902bca5f69c448fc4391837e5f8f0d4556280440ac"

INES = 16
BANK = 0x4000
CPU_HZ = 1789773.0  # NTSC 2A03

# RAM cells from reference/aldonunez/Variables.inc
SONG_REQUEST = 0x600
SONG = 0x609
SONG_PHRASE_INDEX = 0x6C
SONG_PTR_LO = 0x66
SONG_PTR_HI = 0x67
NOTE_OFFSET_SQ1 = 0x60A

# DriveAudio entry is located by its first opcodes (same signature
# tools/extract_audio.py uses), never by a hardcoded address.
DRIVE_AUDIO_SIG = bytes([0xA5, 0xE0, 0xF0, 0x0C, 0xA9, 0x00, 0x8D, 0x15, 0x40])

# Song request values: DriveSong in Z_00.asm (bit tests + SongTable lookups).
SONGS = [
    ("title",      0x80, "Title / Intro"),
    ("overworld",  0x01, "Overworld"),
    ("dungeon",    0x40, "Dungeon"),
    ("level9",     0x20, "Death Mountain (Level 9)"),
    ("ganon",      0x02, "Ganon"),
    ("triforce",   0x04, "Triforce Get"),
    ("item",       0x08, "Item Get"),
    ("ending",     0x10, "Ending"),
    ("zelda",      0x06, "Zelda Rescued"),
]

MAX_FRAMES = 12000
SENTINEL = 0x07F0  # RTS target; never executed (RAM, outside audio vars)

# NES APU constants (2A03 hardware tables).
LENGTH_TABLE = [10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
                12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30]
NOISE_PERIODS = [4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762,
                 1016, 2034, 4068]


def load_rom(path):
    data = Path(path).read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    if sha != NES_ROM_SHA256:
        sys.exit(f"ROM sha256 {sha} is not the supported Zelda (USA) ROM")
    prg_banks = data[4]
    prg = data[INES:INES + prg_banks * BANK]
    return prg, prg_banks, sha


# ---------------------------------------------------------------------------
# Step A: run the real NES driver, record APU writes per frame
# ---------------------------------------------------------------------------

def run_driver(prg, prg_banks, request):
    mem = ObservableMemory()
    bank0 = prg[0:BANK]
    fixed = prg[(prg_banks - 1) * BANK: prg_banks * BANK]
    for i, b in enumerate(bank0):
        mem[0x8000 + i] = b
    for i, b in enumerate(fixed):
        mem[0xC000 + i] = b
    drive = 0x8000 + bank0.find(DRIVE_AUDIO_SIG)
    if drive < 0x8000:
        sys.exit("DriveAudio signature not found in PRG bank 0")

    writes = []

    def on_apu(addr, value):
        writes.append((addr, value))
        return None

    mem.subscribe_to_write(range(0x4000, 0x4018), on_apu)

    cpu = MPU(memory=mem)
    mem[SONG_REQUEST] = request

    frames = []
    phrase_starts = {}   # (phrase index, script ptr) -> first frame
    loop_frame = None
    prev_off = None
    for frame in range(MAX_FRAMES):
        writes.clear()
        # JSR DriveAudio from a sentinel, like the NMI does once per frame.
        cpu.sp = 0xFF
        ret = SENTINEL - 1
        mem[0x1FF] = (ret >> 8) & 0xFF
        mem[0x1FE] = ret & 0xFF
        cpu.sp = 0xFD
        cpu.pc = drive
        steps = 0
        while cpu.pc != SENTINEL:
            cpu.step()
            steps += 1
            if steps > 200000:
                sys.exit(f"driver runaway, request {request:#x} frame {frame}")
        frames.append(list(writes))

        # A phrase (re)starts when PrepPhrase rewinds the Sq1 script offset
        # (also catches a single-phrase song looping onto itself).
        sq1_off = mem[NOTE_OFFSET_SQ1]
        if prev_off is None or sq1_off < prev_off:
            ptr = (mem[SONG_PTR_HI] << 8) | mem[SONG_PTR_LO]
            key = (mem[SONG_PHRASE_INDEX], ptr)
            if key in phrase_starts:
                loop_frame = phrase_starts[key]
                frames.pop()          # this frame starts the repeat
                break
            phrase_starts[key] = frame
        prev_off = sq1_off
        if mem[SONG] == 0 and frame > 0:
            break                       # one-shot song finished (SilenceSong)
    return frames, loop_frame, drive


# ---------------------------------------------------------------------------
# Step B: NES APU model -> audible state per frame
# ---------------------------------------------------------------------------

class Pulse:
    def __init__(self):
        self.reg0 = 0
        self.sweep = 0
        self.period = 0
        self.length = 0
        self.enabled = False

    def write(self, r, v):
        if r == 0:
            self.reg0 = v
        elif r == 1:
            self.sweep = v
        elif r == 2:
            self.period = (self.period & 0x700) | v
        elif r == 3:
            self.period = (self.period & 0xFF) | ((v & 7) << 8)
            if self.enabled:
                self.length = LENGTH_TABLE[v >> 3]
            return True  # trigger
        return False

    def half_clock(self):
        if not (self.reg0 & 0x20) and self.length > 0:
            self.length -= 1

    def volume(self):
        if not self.enabled or self.length == 0 or self.period < 8:
            return 0
        if not (self.reg0 & 0x10):
            return 15  # hardware envelope path; Zelda songs always use const
        shift = self.sweep & 7
        target = self.period + (self.period >> shift) if not (self.sweep & 8) else 0
        if target > 0x7FF:
            return 0
        return self.reg0 & 0x0F

    def freq(self):
        return CPU_HZ / (16.0 * (self.period + 1))


class Triangle:
    def __init__(self):
        self.ctrl = 0
        self.period = 0
        self.length = 0
        self.linear = 0
        self.reload = False
        self.enabled = False

    def write(self, r, v):
        if r == 0:
            self.ctrl = v
        elif r == 2:
            self.period = (self.period & 0x700) | v
        elif r == 3:
            self.period = (self.period & 0xFF) | ((v & 7) << 8)
            if self.enabled:
                self.length = LENGTH_TABLE[v >> 3]
            self.reload = True
            return True
        return False

    def quarter_clock(self):
        if self.reload:
            self.linear = self.ctrl & 0x7F
        elif self.linear > 0:
            self.linear -= 1
        if not (self.ctrl & 0x80):
            self.reload = False

    def half_clock(self):
        if not (self.ctrl & 0x80) and self.length > 0:
            self.length -= 1

    def audible(self):
        return self.enabled and self.length > 0 and self.linear > 0 and self.period >= 2

    def freq(self):
        return CPU_HZ / (32.0 * (self.period + 1))


class Noise:
    def __init__(self):
        self.reg0 = 0
        self.mode_period = 0
        self.length = 0
        self.enabled = False

    def write(self, r, v):
        if r == 0:
            self.reg0 = v
        elif r == 2:
            self.mode_period = v
        elif r == 3:
            if self.enabled:
                self.length = LENGTH_TABLE[v >> 3]
            return True
        return False

    def half_clock(self):
        if not (self.reg0 & 0x20) and self.length > 0:
            self.length -= 1

    def volume(self):
        if not self.enabled or self.length == 0:
            return 0
        return self.reg0 & 0x0F if self.reg0 & 0x10 else 15


def apu_states(frames):
    """Per frame: audible state of each channel after the frame's writes.

    Frame sequencing: DriveAudio writes $4017=$FF (5-step mode, bit 7 set),
    which clocks quarter+half immediately; then the 5-step sequence runs
    quarter @3729, quarter+half @7457, quarter @11186 APU cycles before the
    next frame's $4017 reset (frame = 14890 APU cycles).  The music writes
    follow the $4017 write inside the same DriveAudio call.
    """
    p1, p2, tri, noi = Pulse(), Pulse(), Triangle(), Noise()
    chans = [p1, p2, tri, noi]
    # In-game steady state: every audio path leaves $4015 = $0F (Z_00.asm
    # DriveAudio pause path, SilenceSong, sample/effect paths).
    for c in chans:
        c.enabled = True
    out = []
    for writes in frames:
        trig = [False] * 4
        for addr, v in writes:
            if addr == 0x4017:
                if v & 0x80:
                    for c in (tri,):
                        c.quarter_clock()
                    for c in chans:
                        c.half_clock()
                continue
            if addr == 0x4015:
                for i, c in enumerate(chans):
                    c.enabled = bool(v & (1 << i))
                    if not c.enabled:
                        c.length = 0
                continue
            idx = (addr - 0x4000) >> 2
            if idx < 4 and chans[idx].write(addr & 3, v):
                trig[idx] = True
        # Sample what is heard during the frame (right after the writes),
        # then run the rest of this frame's sequencer clocks.
        state = {
            "sq0": (p1.freq(), p1.volume(), trig[0]),
            "sq1": (p2.freq(), p2.volume(), trig[1]),
            "tri": (tri.freq(), 15 if tri.audible() else 0, trig[2]),
            "noise": (noi.mode_period, noi.volume(), trig[3]),
        }
        tri.quarter_clock()
        tri.quarter_clock()
        for c in chans:
            c.half_clock()
        tri.quarter_clock()
        out.append(state)
    return out


# ---------------------------------------------------------------------------
# Step C: audible state -> Genesis voice events
# ---------------------------------------------------------------------------

def to_midi(freq):
    return round(69.0 + 12.0 * math.log2(freq / 440.0), 3)


def tone_events(states, key):
    """Events: [frame, "on", midi, vol] / [frame, "pitch", midi] /
    [frame, "vol", vol] / [frame, "off"].  midi is fractional (vibrato)."""
    ev = []
    sounding = False
    last_midi = None
    last_vol = None
    for f, st in enumerate(states):
        freq, vol, trig = st[key]
        if vol > 0:
            midi = to_midi(freq)
            if not sounding or trig:
                ev.append([f, "on", midi, vol])
                sounding = True
            else:
                if midi != last_midi:
                    ev.append([f, "pitch", midi])
                if vol != last_vol:
                    ev.append([f, "vol", vol])
            last_midi, last_vol = midi, vol
        elif sounding:
            ev.append([f, "off"])
            sounding = False
            last_midi = last_vol = None
    return ev


def drum_events(states):
    """[frame, "hit", kind, vol, frames]: kind = NES noise period index
    ($400E low nibble), mode bit kept separately."""
    ev = []
    for f, st in enumerate(states):
        mp, vol, trig = st["noise"]
        if trig and vol > 0:
            length = 1
            while f + length < len(states) and states[f + length]["noise"][1] > 0 \
                    and not states[f + length]["noise"][2]:
                length += 1
            ev.append([f, "hit", mp & 0x0F, vol, length, (mp >> 7) & 1])
    return ev


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--rom", default=str(DEFAULT_ROM))
    ap.add_argument("--out", default=str(DEFAULT_OUT))
    ap.add_argument("--dump-writes", action="store_true",
                    help="also write raw per-frame APU writes (verification)")
    args = ap.parse_args()

    prg, banks, sha = load_rom(args.rom)
    out = Path(args.out)
    (out / "songs").mkdir(parents=True, exist_ok=True)
    index = []
    for sid, req, title in SONGS:
        frames, loop, drive = run_driver(prg, banks, req)
        states = apu_states(frames)
        song = {
            "id": sid,
            "title": title,
            "nes_request": req,
            "frames": len(frames),
            "loop_frame": loop,
            "voices": {
                "melody": tone_events(states, "sq1"),
                "harmony": tone_events(states, "sq0"),
                "bass": tone_events(states, "tri"),
                "drums": drum_events(states),
            },
        }
        (out / "songs" / f"{sid}.json").write_text(json.dumps(song, separators=(",", ":")))
        if args.dump_writes:
            (out / "songs" / f"{sid}.apu.json").write_text(json.dumps(frames))
        counts = {k: sum(1 for e in v if e[1] in ("on", "hit")) for k, v in song["voices"].items()}
        print(f"{sid:10s} req ${req:02X} frames {len(frames):5d} loop {loop}  notes {counts}")
        index.append({"id": sid, "title": title, "frames": len(frames), "loop_frame": loop})
    (out / "songs" / "index.json").write_text(json.dumps(
        {"rom_sha256": sha, "drive_audio": f"${drive:04X}", "songs": index}, indent=1))
    print(f"wrote {out / 'songs'}")


if __name__ == "__main__":
    main()
