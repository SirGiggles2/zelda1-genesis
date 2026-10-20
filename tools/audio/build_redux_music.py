"""Build the local Redux music bank from authenticated studio extractions.

Missing extractions are reproduced from the user's supported Redux ROM.
ROM-derived VGMs and C arrays stay in build/ and are never distributed.
"""
import hashlib
import importlib.util
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools' / 'converter'), str(ROOT / 'tools/music_studio')]

# Stable order: these indices are saved in per-file options. Never reorder.
IDS = ('overworld', 'dungeon_zelda', 'dungeon_new_light',
       'dungeon_perils_darkness', 'dungeon_triforce_power', 'level9',
       'ganon', 'triforce', 'zelda', 'item', 'ending', 'title')


def generate():
    folder = ROOT / 'VGMS/Redux'
    manifest = folder / 'extraction.json'
    if not manifest.is_file():
        from extract_soundtrack import extract_soundtrack
        from build import SUPPORTED_REDUX_ROMS
        candidates = [ROOT / 'build/redux/Zelda Redux.nes', *ROOT.glob('*.nes'),
                      *ROOT.glob('roms/*.nes')]
        rom = next((p for p in candidates if p.is_file() and
                    hashlib.sha256(p.read_bytes()).hexdigest() in SUPPORTED_REDUX_ROMS), None)
        if not rom:
            raise ValueError('Redux music needs the supported Redux ROM or VGMS/Redux/extraction.json')
        extract_soundtrack(rom, folder)
    report = json.loads(manifest.read_text())
    from build import SUPPORTED_REDUX_ROMS
    if report['rom_sha256'] not in SUPPORTED_REDUX_ROMS:
        raise ValueError('Redux music extraction has an unsupported source ROM')
    rows = {s['id']: s for s in report['songs']}
    out = ROOT / 'build/redux_music'
    out.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location('local_music', ROOT / 'tools/converter/local_music.py')
    compiler = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(compiler)
    sources = [folder / rows[s]['file'] for s in IDS]
    for sid, path in zip(IDS, sources):
        if hashlib.sha256(path.read_bytes()).hexdigest() != rows[sid]['vgm_sha256']:
            raise ValueError(f'Redux music extraction changed: {path.name}')
    table = out / 'redux_music.c'
    fingerprint = hashlib.sha256(Path(__file__).read_bytes() + manifest.read_bytes() +
                                (ROOT / 'tools/converter/local_music.py').read_bytes()).hexdigest()
    stamp = out / 'fingerprint.txt'
    if table.is_file() and (out / 'redux_music.h').is_file() and stamp.exists() and stamp.read_text() == fingerprint:
        return
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
    generate()
