"""Headless GPGX playback/one-shot boundary checks for a converted ROM.

Requires tools/emu/build_gpgx.sh's core and nm output from the exact linked
Debug.out supplied with --symbols. Probes stage only music mailboxes and
option state; they do not establish connected gameplay/quest acceptance.
"""
from __future__ import annotations
import argparse
import ctypes as C
import hashlib
import json
import math
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"tools/emu"))
sys.path.insert(0,str(ROOT/"tools/lockstep"))
from gpgx import Genesis
from preset_run import Session
import presets


class YmWrites:
    """Count the core's real FM data-port writes, preserving the handler."""
    TYPE = C.CFUNCTYPE(None,C.c_uint,C.c_uint,C.c_uint)
    def __init__(self,g):
        self.cell = C.c_void_p.from_address(g._base+g._syms["fm_write"])
        self.original = self.TYPE(self.cell.value)
        self.count = 0
        self.key_ons = 0
        self.frequency_writes = 0
        self.latch = [0,0]
        def record(cycles,addr,value):
            if addr & 1:
                self.count += 1
                reg = self.latch[(addr>>1)&1]
                if reg == 0x28 and value & 0xF0:
                    self.key_ons += 1
                if reg in (0xA0,0xA1,0xA2,0xA4,0xA5,0xA6):
                    self.frequency_writes += 1
            else:
                self.latch[(addr>>1)&1] = value
            self.original(cycles,addr,value)
        self.callback = self.TYPE(record)
        self.cell.value = C.cast(self.callback,C.c_void_p).value
    def restore(self):
        self.cell.value = C.cast(self.original,C.c_void_p).value


def rms(data):
    samples = struct.unpack(f"<{len(data)//2}h",data)
    return math.sqrt(sum(x*x for x in samples)/len(samples)) if samples else 0


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--genesis",type=Path,default=ROOT/"builds/Debug.md")
    p.add_argument("--symbols",type=Path,required=True)
    p.add_argument("--report",type=Path,default=ROOT/"build/nes_music_runtime.json")
    a = p.parse_args()
    symbols = {row[2]:int(row[0],16)&0xFFFFFF for line in a.symbols.read_text().splitlines()
               if len(row:=line.split()) == 3}
    def byte(g,name,offset=0):
        return g.ram()[(symbols[name]+offset)&65535]
    def word(g,name):
        at = symbols[name]&65535
        return int.from_bytes(g.ram()[at:at+2],"big")
    checks = {}
    rom = a.genesis.read_bytes()
    def music_pointer(g,theme):
        base = symbols[theme]
        expected = base+0x104+(rom[base+0xFC]<<8)+(rom[base+0xFD]<<16)
        actual = int.from_bytes(g._local("zram",8192)[0x104:0x108],"little")
        assert actual == expected,(theme,hex(actual),hex(expected))
    g = Genesis(a.genesis)
    hook = YmWrites(g)
    g.audio = bytearray()
    g.run(140)
    title = {"song":byte(g,"s_current_xgm_song"),"owner":byte(g,"audio_music_state",0x2C),
             "ym_writes":hook.count,"rms":rms(g.audio)}
    title.update(key_ons=hook.key_ons,frequency_writes=hook.frequency_writes)
    assert title["song"] == 128 and title["owner"] == 1 and hook.key_ons and hook.frequency_writes and title["rms"] > 0,title
    music_pointer(g,"nes_music_song_80")
    checks["natural_title"] = title
    print(f"natural title: PASS {title}")
    hook.restore()
    base = g.save_state()
    for name,request in (("title",128),("item",8),("level9",32),("ganon",2),
                         ("triforce",4),("zelda",6),("ending",16)):
        g.load_state(base)
        hook = YmWrites(g)
        g.audio = bytearray()
        g.write_ram(0xFF8600,request)
        g.run(70)
        row = {"song":byte(g,"s_current_xgm_song"),"owner":byte(g,"audio_music_state",0x2C),
               "ym_writes":hook.count,"key_ons":hook.key_ons,
               "frequency_writes":hook.frequency_writes,"rms":rms(g.audio)}
        hook.restore()
        assert row["song"] == request and row["owner"] == 1 and row["key_ons"] and row["frequency_writes"] and row["rms"] > 0,(name,row)
        music_pointer(g,f"nes_music_song_{request:02x}")
        checks[name] = row
        print(f"{name}: PASS XGM owner=1 YM notes={row['key_ons']} pitch writes={row['frequency_writes']} RMS={row['rms']:.2f}")
    # Enter actual normal play through the existing new-game save fixture.
    session = Session(a.genesis)
    spec = json.loads((ROOT/"tools/lockstep/presets/newgame.json").read_text())
    session.write_save(presets.build(spec)["gen_slot0"])
    session.enter_fs()
    # File Select now uses mode 3/4 before play. Let InitMode5 and the
    # first play audio dispatch finish before staging a pickup request.
    session.press('', 2)
    g = session.g
    baseline = g.save_state()
    timing = json.loads((ROOT/"build/nes_music/capture.json").read_text())["songs"]
    for name,request in (("item",8),("triforce",4)):
        g.load_state(baseline)
        g.write_ram(0xFF8600,request)
        duration = timing[name]["frames"]
        g.run(duration-1)
        assert byte(g,"s_current_xgm_song") == request and word(g,"s_nes_song_frames") == 1,name
        g.run(1)
        assert byte(g,"s_current_xgm_song") == 1 and byte(g,"audio_music_state",0x2C) == 1,name
        checks[name+"_resume"] = {"nes_frames":duration,"resume_song":1}
        print(f"{name}: PASS OW resumes at NES frame {duration}")
    # Same-song explicit requests must restart, including a completed tune.
    g.load_state(base)
    g.write_ram(0xFF8600,2)
    g.run(200)
    g.write_ram(0xFF8600,2)
    g.run(1)
    assert word(g,"s_nes_song_frames") == timing["ganon"]["frames"]-1
    checks["repeat_ganon"] = "PASS"
    print("same-song restart: PASS")
    # Check the native option's default and the selected UW blob via XGM's
    # ROM pointer mailbox, while preserving generated L9 routing.
    for setting,theme in ((0,"uw_theme_xgm"),(1,"uw_theme_cyberdeous_xgm")):
        g.load_state(baseline)
        g.write_ram(symbols["g_options"]+12,setting)
        g.write_ram(0xFF8010,1)
        g.write_ram(0xFF8600,0x40)
        hook = YmWrites(g)
        g.audio = bytearray()
        g.run(70)
        hook.restore()
        assert byte(g,"s_current_xgm_song") == 0x40 and hook.key_ons and rms(g.audio)>0
        music_pointer(g,theme)
        checks[theme] = {"option":setting,"ym_writes":hook.count,"rms":rms(g.audio)}
        print(f"UW option {setting}: PASS YM={hook.count} RMS={rms(g.audio):.2f}")
    g.load_state(baseline)
    g.write_ram(0xFF8010,9)
    g.write_ram(0xFF8600,0x40)
    g.run(30)
    music_pointer(g,"nes_music_song_20")
    checks["level9_dungeon_request"] = "PASS"
    print("level 9 dungeon request: PASS generated level9 blob")
    # A new area request cancels a pending pickup's old-area continuation.
    g.load_state(baseline)
    g.write_ram(0xFF8600,8)
    g.run(20)
    g.write_ram(0xFF8010,1)
    g.write_ram(0xFF8600,0x40)
    g.run(100)
    assert byte(g,"s_current_xgm_song") == 0x40 and word(g,"s_nes_song_frames") == 0
    checks["pickup_area_change"] = "PASS"
    print("pickup area change: PASS old area does not resume")
    a.report.parent.mkdir(parents=True,exist_ok=True)
    a.report.write_text(json.dumps({"rom_sha256":hashlib.sha256(a.genesis.read_bytes()).hexdigest(),
                                  "checks":checks,"status":"PASS"},indent=2)+"\n")
    print("nes_music_runtime: PASS")


if __name__ == "__main__":
    main()
