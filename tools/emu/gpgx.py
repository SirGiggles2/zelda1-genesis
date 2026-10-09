"""Headless Genesis runner (Genesis Plus GX libretro core) for Linux hosts.

BizHawk (the project's probe emulator, Windows) runs the same core. This
module runs Zelda.md without a display: step frames with held buttons,
save PNG screenshots, read 68k work RAM, VRAM, CRAM, VSRAM and VDP
registers, and save/load states.

Build the core once with tools/emu/build_gpgx.sh.

    from gpgx import Genesis
    g = Genesis("builds/Zelda.md")
    g.run(120)                      # frames, no input
    g.run(10, "START")              # frames holding Start
    g.screenshot("shot.png")
    ram = g.ram()                   # 64 KB, $FF0000..$FFFFFF
"""
from __future__ import annotations

import ctypes as C
import struct
import subprocess
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / "build" / "emu" / "genesis_plus_gx_libretro.so"

# libretro joypad ids -> Genesis buttons (GPGX input descriptors).
BUTTONS = {"B": 0, "A": 1, "MODE": 2, "START": 3, "UP": 4, "DOWN": 5, "LEFT": 6,
           "RIGHT": 7, "C": 8, "Y": 9, "X": 10, "Z": 11}

ENV_GET_SYSTEM_DIRECTORY = 9
ENV_SET_PIXEL_FORMAT = 10
ENV_GET_VARIABLE = 15
ENV_GET_SAVE_DIRECTORY = 31
ENV_GET_CAN_DUPE = 3
MEMORY_SYSTEM_RAM = 2

ENV_CB = C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)
VIDEO_CB = C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)
AUDIO_CB = C.CFUNCTYPE(None, C.c_int16, C.c_int16)
AUDIO_BATCH_CB = C.CFUNCTYPE(C.c_size_t, C.c_void_p, C.c_size_t)
POLL_CB = C.CFUNCTYPE(None)
STATE_CB = C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)


class GameInfo(C.Structure):
    _fields_ = [("path", C.c_char_p), ("data", C.c_void_p), ("size", C.c_size_t),
                ("meta", C.c_char_p)]


class Genesis:
    def __init__(self, rom: str | Path, core: str | Path = CORE):
        core = Path(core)
        if not core.exists():
            raise FileNotFoundError(f"{core}: run tools/emu/build_gpgx.sh")
        self.lib = C.CDLL(str(core))
        self.held: set[str] = set()
        self.frame = None            # (bytes, width, height, pitch)
        self.pixfmt = 0                          # RETRO_PIXEL_FORMAT_0RGB1555 until the core sets it
        self._sysdir = C.c_char_p(str(ROOT / "build" / "emu").encode())
        # Audio: set .audio = bytearray() to collect interleaved s16 stereo
        # samples (None = discard).
        self.audio: bytearray | None = None
        self._cbs = (ENV_CB(self._env), VIDEO_CB(self._video), AUDIO_CB(lambda l, r: None),
                     AUDIO_BATCH_CB(self._audio_batch), POLL_CB(lambda: None),
                     STATE_CB(self._input))
        L = self.lib
        L.retro_set_environment(self._cbs[0])
        L.retro_set_video_refresh(self._cbs[1])
        L.retro_set_audio_sample(self._cbs[2])
        L.retro_set_audio_sample_batch(self._cbs[3])
        L.retro_set_input_poll(self._cbs[4])
        L.retro_set_input_state(self._cbs[5])
        L.retro_init()
        self._rom = Path(rom).read_bytes()
        self._buf = C.create_string_buffer(self._rom, len(self._rom))
        info = GameInfo(str(rom).encode(), C.cast(self._buf, C.c_void_p), len(self._rom), None)
        L.retro_load_game.restype = C.c_bool
        if not L.retro_load_game(C.byref(info)):
            raise RuntimeError("retro_load_game failed")
        L.retro_get_memory_data.restype = C.c_void_p
        L.retro_get_memory_size.restype = C.c_size_t
        L.retro_serialize_size.restype = C.c_size_t
        L.retro_serialize.restype = C.c_bool
        L.retro_unserialize.restype = C.c_bool
        self._syms = self._local_symbols(core)

    # ---- libretro callbacks ----
    def _audio_batch(self, data, frames):
        if self.audio is not None:
            self.audio += C.string_at(data, frames * 4)
        return frames

    def _env(self, cmd, data):
        cmd &= 0xFFFF
        if cmd == ENV_SET_PIXEL_FORMAT:
            self.pixfmt = C.cast(data, C.POINTER(C.c_uint))[0]
            return True
        if cmd in (ENV_GET_SYSTEM_DIRECTORY, ENV_GET_SAVE_DIRECTORY):
            C.cast(data, C.POINTER(C.c_char_p))[0] = self._sysdir.value
            return True
        if cmd == ENV_GET_CAN_DUPE:
            C.cast(data, C.POINTER(C.c_bool))[0] = True
            return True
        return False

    def _video(self, data, w, h, pitch):
        if data:
            self.frame = (C.string_at(data, pitch * h), w, h, pitch)

    def _input(self, port, device, index, ident):
        if port != 0 or device != 1:
            return 0
        return int(any(BUTTONS[b] == ident for b in self.held))

    # ---- control ----
    def run(self, frames: int = 1, *buttons: str):
        self.held = {b.upper() for b in buttons}
        for _ in range(frames):
            self.lib.retro_run()
        self.held = set()

    def save_state(self) -> bytes:
        n = self.lib.retro_serialize_size()
        buf = C.create_string_buffer(n)
        if not self.lib.retro_serialize(buf, n):
            raise RuntimeError("serialize failed")
        return buf.raw

    def load_state(self, state: bytes):
        buf = C.create_string_buffer(state, len(state))
        if not self.lib.retro_unserialize(buf, len(state)):
            raise RuntimeError("unserialize failed")

    # ---- memory ----
    def ram(self) -> bytes:
        """68k work RAM $FF0000..$FFFFFF, as the 68k sees it (big-endian)."""
        p = self.lib.retro_get_memory_data(MEMORY_SYSTEM_RAM)
        n = self.lib.retro_get_memory_size(MEMORY_SYSTEM_RAM)
        raw = C.string_at(p, n)
        # GPGX stores work RAM byte-swapped per 16-bit word on little-endian hosts.
        return _swap16(raw)

    def write_ram(self, addr: int, value: int):
        """Write one byte at 68k address $FFxxxx."""
        p = self.lib.retro_get_memory_data(MEMORY_SYSTEM_RAM)
        off = (addr & 0xFFFF) ^ 1
        C.cast(p, C.POINTER(C.c_ubyte))[off] = value & 0xFF

    def _local(self, name: str, size: int) -> bytes:
        return C.string_at(self._base + self._syms[name], size)

    def vram(self) -> bytes:
        return _swap16(self._local("vram", 0x10000))

    def cram(self) -> list[int]:
        """64 colours as Genesis 9-bit BGR words (0000 BBB0 GGG0 RRR0)."""
        raw = self._local("cram", 128)
        words = struct.unpack("<64H", raw)
        # GPGX keeps CRAM in a packed internal format: 0bbb0ggg0rrr (9 bits).
        return [((w & 0x1C0) << 3) | ((w & 0x038) << 2) | ((w & 0x007) << 1) for w in words]

    def vsram(self) -> bytes:
        return self._local("vsram", 0x80)

    def regs(self) -> bytes:
        return self._local("reg", 0x20)

    def _local_symbols(self, core: Path) -> dict[str, int]:
        out = subprocess.run(["nm", str(core)], capture_output=True, text=True).stdout
        syms = {}
        for line in out.splitlines():
            parts = line.split()
            if len(parts) == 3:
                syms[parts[2]] = int(parts[0], 16)
        self._base = C.cast(self.lib.retro_run, C.c_void_p).value - syms["retro_run"]
        return syms

    # ---- screenshots ----
    def rgb(self) -> tuple[bytes, int, int]:
        data, w, h, pitch = self.frame
        out = bytearray()
        for y in range(h):
            row = data[y * pitch:(y * pitch) + pitch]
            if self.pixfmt == 1:                      # XRGB8888
                for x in range(w):
                    b, g, r = row[x * 4], row[x * 4 + 1], row[x * 4 + 2]
                    out += bytes((r, g, b))
            else:                                     # RGB565 (2) / 0RGB1555 (0)
                for x in range(w):
                    v = row[x * 2] | row[x * 2 + 1] << 8
                    if self.pixfmt == 2:
                        r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
                        out += bytes((r * 255 // 31, g * 255 // 63, b * 255 // 31))
                    else:
                        r, g, b = (v >> 10) & 31, (v >> 5) & 31, v & 31
                        out += bytes((r * 255 // 31, g * 255 // 31, b * 255 // 31))
        return bytes(out), w, h

    def screenshot(self, path: str | Path, scale: int = 2):
        pix, w, h = self.rgb()
        rows = []
        for y in range(h):
            line = pix[y * w * 3:(y + 1) * w * 3]
            wide = b"".join(line[x * 3:x * 3 + 3] * scale for x in range(w))
            rows += [b"\0" + wide] * scale
        _write_png(Path(path), w * scale, h * scale, b"".join(rows))


def _swap16(raw: bytes) -> bytes:
    b = bytearray(raw)
    b[0::2], b[1::2] = raw[1::2], raw[0::2]
    return bytes(b)


def _write_png(path: Path, w: int, h: int, raw: bytes):
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    path.write_bytes(png)
