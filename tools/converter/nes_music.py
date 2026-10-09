"""Convert seven songs using the sound driver in the user's NES ROM.

No song data or private fixtures are shipped. The NES boots in Nes, then
its bank-zero DriveAudio routine is isolated from gameplay (which otherwise
requests unrelated tunes), called once per frame, and its APU writes logged.
RAM labels come from the ROM-restored disassembly. YM patches and mix settings
are port instruments fitted to the private reference VGMs, not song scripts.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from nesemu.nes import Nes

SONGS = (("title", 0x80), ("item", 0x08), ("level9", 0x20),
         ("ganon", 0x02), ("triforce", 0x04), ("zelda", 0x06), ("ending", 0x10))
CPU_HZ = 1789773
FM_CLOCK = 7670453
PSG_CLOCK = 3579545
MAX_FRAMES = 12000
LENGTHS = (10,254,20,2,40,4,80,6,160,8,60,10,14,12,26,14,
           12,16,24,18,48,20,96,22,192,24,72,26,16,28,32,30)


def labels(root=ROOT):
    text = (root / "reference/aldonunez/Variables.inc").read_text()
    found = {k: int(v, 16) for k, v in re.findall(r"(?m)^(\w+)\s*:=\s*\$([\da-fA-F]+)", text)}
    for k in ("SongRequest", "Song", "SongPhraseIndex", "SongScriptPtrLo",
              "SongScriptPtrHi", "NoteOffsetSongSq1"):
        if k not in found:
            raise ValueError(f"missing restored NES label {k}")
    return found


def capture(rom: bytes, request: int):
    """One real driver call per frame, starting with an empty audio context.

    This is an offline JSR harness, not a CPU/PPU timing or audio renderer.
    Boot is real; clearing RAM isolates the audio context from title/gameplay.
    A bounded call fails loudly on runaway or unsupported driver signatures.
    """
    lab = labels()
    n = Nes(rom)
    n.run_frames(5)
    n.ram[:] = bytes(len(n.ram))
    n.mmc1_prg = 0
    n._map_prg()
    signature = bytes.fromhex("a5e0f00ca9008d1540")
    bank = n.prg[0]
    offset = bank.find(signature)
    if offset < 0 or bank.find(signature, offset + 1) >= 0:
        raise ValueError("DriveAudio signature missing or ambiguous")
    drive = 0x8000 + offset
    n.apu_write_log = []
    n.poke(lab["SongRequest"], request)
    frames, phrases = [], {}
    previous_offset = None
    cpu = n.cpu
    # RTS sentinel lives in unused RAM; it is a control target, never executed.
    sentinel = 0x07F0
    for frame in range(MAX_FRAMES):
        n.frame = frame
        n.apu_write_log.clear()
        cpu.sp = 0xFD
        n.poke(0x1FF, (sentinel - 1) >> 8)
        n.poke(0x1FE, (sentinel - 1) & 255)
        cpu.pc = drive
        for _ in range(200000):
            if cpu.pc == sentinel:
                break
            cpu.step()
        else:
            raise ValueError(f"DriveAudio runaway: request ${request:02X}, frame {frame}")
        writes = [(reg, val) for _, reg, val in n.apu_write_log]
        offset = n.bus(lab["NoteOffsetSongSq1"])
        if previous_offset is None or offset < previous_offset:
            # PrepPhrase rewinds the script. The phrase descriptor is the RAM
            # state identifying its position in SongTable (including intro).
            state = tuple(n.bus(lab[k]) for k in
                          ("SongPhraseIndex", "SongScriptPtrLo", "SongScriptPtrHi"))
            if state in phrases:
                return frames, phrases[state]
            phrases[state] = frame
        frames.append(writes)
        previous_offset = offset
        if frame and n.bus(lab["Song"]) == 0:
            return frames, None
    raise ValueError(f"song ${request:02X} neither ended nor looped in {MAX_FRAMES} frames")


@dataclass
class ApuChannel:
    kind: int
    control: int = 0
    sweep: int = 0
    period: int = 0
    length: int = 0
    linear: int = 0
    reload: bool = False
    enabled: bool = True

    def write(self, reg, value):
        if reg == 0:
            self.control = value
        elif reg == 1:
            self.sweep = value
        elif reg == 2:
            self.period = value if self.kind == 3 else (self.period & 0x700) | value
        elif reg == 3:
            if self.kind != 3:
                self.period = (self.period & 255) | ((value & 7) << 8)
            self.length = LENGTHS[value >> 3] if self.enabled else 0
            self.reload = self.kind == 2
            return True
        return False

    def quarter(self):
        if self.kind == 2:
            self.linear = (self.control & 127) if self.reload else max(0, self.linear - 1)
            if not self.control & 128:
                self.reload = False

    def half(self):
        halt = self.control & (128 if self.kind == 2 else 32)
        if not halt:
            self.length = max(0, self.length - 1)

    def volume(self):
        if not self.enabled or not self.length:
            return 0
        if self.kind == 2:
            return 15 if self.linear and self.period >= 2 else 0
        if self.kind < 2:
            target = 0 if self.sweep & 8 else self.period + (self.period >> (self.sweep & 7))
            if self.period < 8 or target > 2047:
                return 0
        return self.control & 15 if self.control & 16 else 15


def apu_states(frames):
    """Zelda's constant-volume channels plus length/triangle sequencers.

    DriveAudio resets the five-step frame counter every video frame. Sample
    after the call, then clock the remaining three quarter/one half events.
    Hardware-envelope songs are rejected rather than approximated.
    """
    channels = [ApuChannel(i) for i in range(4)]
    out = []
    for writes in frames:
        trigger = [False] * 4
        for reg, value in writes:
            if reg == 0x4017 and value & 128:
                for c in channels:
                    c.quarter()
                    c.half()
            elif reg == 0x4015:
                for i, c in enumerate(channels):
                    c.enabled = bool(value & (1 << i))
                    if not c.enabled:
                        c.length = 0
            elif 0x4000 <= reg < 0x4010:
                i = (reg - 0x4000) // 4
                trigger[i] |= channels[i].write(reg & 3, value)
        row = []
        for i, c in enumerate(channels):
            volume = c.volume()
            if volume and i != 2 and not c.control & 16:
                raise ValueError("unexpected hardware envelope in Zelda song")
            pitch = c.period if i == 3 else CPU_HZ / ((32 if i == 2 else 16) * (c.period + 1))
            row.append((pitch, volume, trigger[i]))
        out.append(row)
        for c in channels:
            c.quarter()
            c.quarter()
            c.half()
            c.quarter()
    return out


def events(states, channel):
    out, sounding, last_pitch, last_volume = [], False, None, None
    for f, state in enumerate(states):
        hz, vol, trigger = state[channel]
        midi = round(69 + 12 * math.log2(hz / 440), 3)
        if vol:
            if trigger or not sounding:
                out.append((f, "on", midi, vol))
            else:
                if midi != last_pitch:
                    out.append((f, "pitch", midi))
                if vol != last_volume:
                    out.append((f, "vol", vol))
            sounding, last_pitch, last_volume = True, midi, vol
        elif sounding:
            out.append((f, "off"))
            sounding, last_pitch, last_volume = False, None, None
    return out


# Operators are in YM register order (0,8,4,12), with DTMUL, base TL, AR,
# D1R, D2R, SL/RR, SSG. These are the shared hand-made Genesis instruments.
PATCHES = (
    (0x2C, ((0x01,28,31,6,0,0x28,0), (0x01,0,31,3,2,0x27,0),
            (0x32,36,31,8,0,0x38,0), (0x71,2,31,3,2,0x27,0))),
    (0x34, ((0x02,30,31,0,0,0x0A,0), (0x01,0,31,2,1,0x18,0),
            (0x02,127,31,0,0,0x0F,0), (0x01,127,31,0,0,0x0F,0))),
    (0x30, ((0x01,30,31,10,0,0x4C,0), (0x00,32,31,12,0,0x6C,0),
            (0x01,22,31,8,0,0x3C,0), (0x01,0,31,5,3,0x39,0))),
)
# (APU channel, patch, level %, stereo, echo delay, follow NES volume)
TRACKS = ((1,0,85,0xC0,0,True), (0,1,65,0xC0,0,True),
          (2,2,90,0xC0,0,False), (1,0,35,0x80,10,True),
          (0,1,30,0x40,10,True))
SLOTS = (0,8,4,12)


def rounded(value):
    return math.floor(value + 0.5)


def attenuation(ratio, step=0.75):
    return rounded(-20 * math.log10(ratio) / step) if ratio > 0 else 999


def frequency(midi):
    hz = 440 * 2 ** ((midi - 69) / 12)
    for block in range(8):
        fnum = rounded(hz * 2 ** (21 - block) / (FM_CLOCK / 144))
        if fnum < 2048:
            return fnum, block
    return 2047, 7


class Vgm:
    def __init__(self):
        self.data = bytearray()
        self.samples = 0
        self.loop = None

    def fm(self, channel, reg, value):
        self.data.extend((0x52 if channel < 3 else 0x53, reg, value))

    def psg(self, value):
        self.data.extend((0x50, value))

    def frame(self):
        self.data.append(0x62)
        self.samples += 735

    def bytes(self):
        header = bytearray(64)
        header[:4] = b"Vgm "
        fields = {4:64+len(self.data)+1-4, 8:0x150, 12:PSG_CLOCK,
                  24:self.samples, 36:60, 44:FM_CLOCK, 52:12}
        if self.loop is not None:
            offset, samples = self.loop
            fields.update({28:64+offset-28, 32:self.samples-samples})
        for offset, value in fields.items():
            struct.pack_into("<I", header, offset, value)
        struct.pack_into("<H", header, 40, 9)
        header[42] = 16
        return bytes(header + self.data + b"\x66")


def translate(frames, loop):
    states = apu_states(frames)
    voice_events = [events(states, i) for i in range(3)]
    w = Vgm()
    for reg, val in ((0x22,0), (0x27,0), (0x2B,0)):
        w.fm(0, reg, val)
    for ch in (0,1,2,4,5,6):
        w.fm(0, 0x28, ch)
    for val in (0x9F,0xBF,0xDF,0xFF):
        w.psg(val)
    tracks = []
    for ch, (source, patch, level, pan, delay, follow) in enumerate(TRACKS):
        algorithm, ops = PATCHES[patch]
        for slot, op in zip(SLOTS, ops):
            for reg, val in zip((0x30,0x40,0x50,0x60,0x70,0x80,0x90),
                                (op[0],127,*op[2:])):
                w.fm(ch, reg+slot+ch%3, val)
        w.fm(ch, 0xB0+ch%3, algorithm)
        w.fm(ch, 0xB4+ch%3, pan)
        schedule = {}
        peak = max([1] + [e[3] if e[1] == "on" else e[2]
                          for e in voice_events[source] if e[1] in ("on","vol")])
        for e in voice_events[source]:
            f = e[0]+delay
            if f < len(frames):
                schedule.setdefault(f, []).append(e)
        tracks.append([schedule, False, peak])
    drum_hits = {}
    for f, state in enumerate(states):
        period, vol, trig = state[3]
        if trig and vol:
            length = 1
            while f+length < len(states) and states[f+length][3][1] and not states[f+length][3][2]:
                length += 1
            drum_hits[f] = (period & 15, length)
    drum_level, drum_step = 15, 0
    for f in range(len(frames) + (45 if loop is None else 0)):
        if f == loop:
            w.loop = (len(w.data), w.samples)
        for ch, (schedule, keyed, peak) in enumerate(tracks):
            source, patch, level, pan, delay, follow = TRACKS[ch]
            for e in schedule.get(f, ()):
                kind = e[1]
                if kind == "off" or (kind == "on" and keyed):
                    if keyed:
                        w.fm(0,0x28,ch if ch < 3 else ch+1)
                    keyed = False
                if kind in ("on","pitch") and (kind == "on" or keyed):
                    fn, block = frequency(e[2])
                    w.fm(ch,0xA4+ch%3,(block<<3)|(fn>>8))
                    w.fm(ch,0xA0+ch%3,fn&255)
                if kind == "on" or (kind == "vol" and keyed and follow):
                    vol = e[3] if kind == "on" else e[2]
                    att = attenuation(level/100) + (attenuation(vol/peak) if follow else 0)
                    for i, (slot, op) in enumerate(zip(SLOTS, PATCHES[patch][1])):
                        carrier = i in ((3,) if patch == 2 else (1,3))
                        tl = min(127, op[1]+att) if carrier else op[1]
                        w.fm(ch,0x40+slot+ch%3,tl)
                if kind == "on":
                    w.fm(0,0x28,0xF0|(ch if ch<3 else ch+1))
                    keyed = True
            if f == len(frames) and keyed:
                w.fm(0,0x28,ch if ch<3 else ch+1)
                keyed = False
            tracks[ch][1] = keyed
        if f in drum_hits:
            index, length = drum_hits[f]
            w.psg(0xE4 | (0 if index <= 4 else 1 if index <= 8 else 2))
            duration = max(1, rounded((3 if length <= 2 else 8)*1.2))
            drum_level = min(15, attenuation(0.7,2))
            drum_step = (15-drum_level)/duration
            w.psg(0xF0|rounded(drum_level))
        elif drum_level < 15:
            drum_level = min(15, drum_level+drum_step)
            w.psg(0xF0|rounded(drum_level))
        w.frame()
    return w.bytes()


def generate(rom: Path, out: Path, *, compile_xgm=True):
    raw = rom.read_bytes()
    from build import SUPPORTED_ROMS, SUPPORTED_REDUX_ROMS
    digest = hashlib.sha256(raw).hexdigest()
    if digest not in SUPPORTED_ROMS and digest not in SUPPORTED_REDUX_ROMS:
        raise ValueError(f"unsupported music input ROM sha256 {digest}")
    out.mkdir(parents=True, exist_ok=True)
    entries, report = {}, {"rom_sha256": digest, "songs": {}}
    for name, request in SONGS:
        frames, loop = capture(raw, request)
        vgm = out / f"{name}.vgm"
        vgm.write_bytes(translate(frames, loop))
        entries[request] = (name, vgm)
        report["songs"][name] = {"request": request, "frames": len(frames), "loop_frame": loop}
        print(f"  {name}: {len(frames)} NES frames, loop {loop}", flush=True)
    (out / "capture.json").write_text(json.dumps(report, indent=2)+"\n")
    if compile_xgm:
        from local_music import emit_table
        timing = {v["request"]: (v["frames"], v["loop_frame"] is not None)
                  for v in report["songs"].values()}
        emit_table(entries, out / "nes_music.c", "nes_music", timing)
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--rom", type=Path, required=True)
    p.add_argument("--out", type=Path, default=ROOT / "build/nes_music")
    p.add_argument("--vgm-only", action="store_true")
    args = p.parse_args()
    try:
        generate(args.rom, args.out, compile_xgm=not args.vgm_only)
    except (OSError, ValueError) as exc:
        p.exit(1, f"ERROR: music from your ROM: {exc}\n")


if __name__ == "__main__":
    main()
