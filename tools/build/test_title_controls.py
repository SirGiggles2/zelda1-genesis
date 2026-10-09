"""Verify retired title shortcuts and retained ABC+Start on a built ROM."""
from __future__ import annotations
import argparse
import ctypes
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/emu'))
from gpgx import Genesis


def sample(rom, core, buttons):
    g = Genesis(rom, core)
    try:
        # GPGX libretro.c: MDPAD_6B = SUBCLASS(JOYPAD,1) = 513.
        g.lib.retro_set_controller_port_device.argtypes = [ctypes.c_uint, ctypes.c_uint]
        g.lib.retro_set_controller_port_device(0, 513)
        g.run(200)
        g.run(8, *buttons)
        g.run(60)
        ram = g.ram()
        result = {'state': ram[0x700d], 'vram': hashlib.sha256(g.vram()).hexdigest(),
                  'width': g.frame[1]}
        if buttons == ('A','B','C','START'):
            assert result['width'] == 320, 'diagnostic dump did not switch to H40'
            a = g.vram()
            g.run(20)
            assert a == g.vram(), 'diagnostic freeze unexpectedly resumed'
        return result
    finally:
        g.lib.retro_unload_game()
        g.lib.retro_deinit()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom',type=Path,required=True)
    ap.add_argument('--core',type=Path,required=True)
    ap.add_argument('--before',type=Path)
    ap.add_argument('--report',type=Path,required=True)
    a = ap.parse_args()
    chords = {'idle': (), 'abc': ('A','B','C'), 'xyz': ('X','Y','Z'),
              'mode': ('MODE',), 'start': ('START',), 'c_start': ('C','START'),
              'dump': ('A','B','C','START')}
    fixed={k: sample(a.rom,a.core,v) for k,v in chords.items()}
    for k in ('abc','xyz','mode'):
        assert fixed[k] == fixed['idle'], (k,fixed[k],fixed['idle'])
    assert fixed['start']['vram'] == fixed['c_start']['vram'] != fixed['idle']['vram']
    assert fixed['dump']['width']==320
    before={k: sample(a.before,a.core,v) for k,v in chords.items()} if a.before else None
    if before:
        assert before['abc']['state']==before['xyz']['state']==1
        assert before['mode']['vram'] != before['idle']['vram']
        assert before['c_start']['vram'] != before['start']['vram']
    a.report.parent.mkdir(parents=True,exist_ok=True)
    a.report.write_text(json.dumps({'rom_sha256':hashlib.sha256(a.rom.read_bytes()).hexdigest(),
                                 'fixed':fixed,'before':before},indent=2)+'\n')
    print('title_controls: PASS (ABC/XYZ/Mode ignored, Start/C+Start file select, ABC+Start diagnostic freeze)')


if __name__ == '__main__':
    main()
