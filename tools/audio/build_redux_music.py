"""Build the local Redux music bank from the user's ROM and saved Studio mix.

ROM-derived VGMs and C arrays stay in build/ and are never distributed.
"""
import argparse
import hashlib
import os
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools' / 'converter'), str(ROOT / 'tools/music_studio')]

# Stable order: these indices are saved in per-file options. Never reorder.
IDS = ('overworld', 'dungeon_zelda', 'dungeon_new_light',
       'dungeon_perils_darkness', 'dungeon_triforce_power', 'level9',
       'ganon', 'triforce', 'zelda', 'item', 'ending', 'title')


def generate(rom: Path | None = None, out: Path | None = None):
    from studio_music import generate_vgms
    from build import SUPPORTED_REDUX_ROMS
    candidates = ([rom] if rom else []) + ([Path(os.environ['ZELDA_REDUX_ROM'])]
                  if os.environ.get('ZELDA_REDUX_ROM') else []) + [
                  ROOT / 'build/redux/Zelda Redux.nes', *ROOT.glob('*.nes'), *ROOT.glob('roms/*.nes')]
    if rom is None:
        rom = next((p for p in candidates if p.is_file() and
                    hashlib.sha256(p.read_bytes()).hexdigest() in SUPPORTED_REDUX_ROMS), None)
    if rom is None or hashlib.sha256(rom.read_bytes()).hexdigest() not in SUPPORTED_REDUX_ROMS:
        raise ValueError('Redux Studio music needs the supported Redux ROM; set ZELDA_REDUX_ROM')
    out = out or ROOT / 'build/redux_music'
    out.mkdir(parents=True, exist_ok=True)
    import local_music as compiler
    table = out / 'redux_music.c'
    dependencies = [Path(__file__), ROOT / 'data/music/builder_mix.json',
                    ROOT / 'tools/music_studio/web/render.js',
                    *(ROOT / 'tools/converter' / name for name in
                      ('studio_music.py', 'studio_render.js', 'nes_music.py', 'local_music.py'))]
    fingerprint = hashlib.sha256(rom.read_bytes() + b''.join(p.read_bytes() for p in dependencies)).hexdigest()
    stamp = out / 'fingerprint.txt'
    if table.is_file() and (out / 'redux_music.h').is_file() and stamp.exists() and stamp.read_text() == fingerprint:
        return
    folder = out / 'vgm'
    report = generate_vgms(rom, folder, profile='redux')
    rows = report['songs']
    sources = [folder / rows[s]['file'] for s in IDS]
    picked = {}
    for i, (sid, p) in enumerate(zip(IDS, sources)):
        data = bytearray(p.read_bytes())
        if rows[sid]['loop_frame'] is None:
            # A fanfare can be selected as the all-music/area track too.
            # Loop this selectable version; source request timers still
            # control pickup/rescue/ending progression independently.
            start = 0x34 + struct.unpack_from('<I', data, 0x34)[0]
            if start == 0x34:
                start = 0x40
            struct.pack_into('<I', data, 0x1C, start - 0x1C)
            struct.pack_into('<I', data, 0x20, struct.unpack_from('<I', data, 0x18)[0])
            p = out / f'{sid}_loop.vgm'
            p.write_bytes(data)
        picked[i] = (sid, p)
    # Reuse the compiler's stable generic table schema; rename this bank only.
    compiler.emit_table(picked, out / 'local_music.c', timing={
        i: (rows[s]['frames'], rows[s]['loop_frame'] is not None) for i, s in enumerate(IDS)})
    table.write_text((out / 'local_music.c').read_text().replace('local_music', 'redux_music'), encoding='utf-8')
    (out / 'redux_music.h').write_text((out / 'local_music.h').read_text().replace('local_music', 'redux_music')
                                   .replace('LOCAL_MUSIC_H', 'REDUX_MUSIC_H'), encoding='utf-8')
    stamp.write_text(fingerprint)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--rom', type=Path)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    generate(args.rom, args.out)
