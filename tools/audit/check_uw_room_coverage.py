"""Fail closed when a required native dungeon room has no linked payload.

Requirements are derived from installed source records, not a room whitelist.
The converter supplies its fresh captures; developer builds use the independent
native both-quest inventory receipt. Existing verified layout aliases remain valid.
"""
from pathlib import Path
import argparse
import hashlib
import json
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
from tools.audit.check_redux_q2_l1_coverage import binding, read_blob
from nesemu.zelda_uw_dump import normal_room_inventory


def complete_sets(requirements):
    assert requirements, 'empty dungeon inventory'
    for mid in {r['map_id'] for r in requirements}:
        pairs = [(r['quest'], r['level']) for r in requirements if r['map_id'] == mid]
        assert len(pairs) == 18 and set(pairs) == {
            (q, level) for q in (1, 2) for level in range(1, 10)
        }, 'incomplete or duplicated quest/level capture set'
    return requirements


def installed_requirements(path):
    receipt = json.loads(path.read_text(encoding='utf-8'))
    requirements = complete_sets(receipt['requirements'])
    for r in requirements:
        attrs = bytes.fromhex(r['installed_level_block_hex'])
        info = bytes.fromhex(r['installed_level_info_hex'])
        assert len(attrs) == 768 and len(info) == 256
        assert hashlib.sha256(attrs + info).hexdigest() == r['installed_records_sha256']
        assert normal_room_inventory(attrs, info, r['level']) == r['required_rooms']
        assert info[0x33] == r['display_level']
    assert hashlib.sha256((ROOT / 'roms/Zelda Redux.nes').read_bytes()).hexdigest() == receipt['source_rom_sha256']
    return requirements


def check(blob, requirements, aliases):
    cases = []
    for requirement in requirements:
        level, quest, mid = (requirement[k] for k in ('level', 'quest', 'map_id'))
        found, missing = {}, []
        for room in requirement['required_rooms']:
            siblings = [level] + [s for s in (range(1, 7) if level <= 6 else range(7, 10)) if s != level]
            for sibling in siblings:
                index = binding(blob, (mid, quest, sibling, room), aliases)
                if index is not None:
                    found[f'{room:02X}'] = blob['index'][index]
                    break
            else:
                missing.append(f'{room:02X}')
        cases.append({'level': level, 'quest': quest, 'map_id': mid,
                      'display_level': requirement.get('display_level'),
                      'required': len(requirement['required_rooms']),
                      'covered': found, 'missing': missing})
    return {'pass': bool(cases) and not any(c['missing'] for c in cases), 'cases': cases}


def fresh_requirements(directory):
    requirements = []
    for path in sorted(directory.glob('nes_uw_level*_quest*_*.json')):
        capture = json.loads(path.read_text())
        assert capture.get('boot_ok') and capture.get('warp_ok'), path
        level, quest = capture['level'], capture['quest']
        assert 1 <= level <= 9 and quest in (1, 2)
        attrs = bytes(capture['installed_level_block'])
        info = bytes(capture['installed_level_info'])
        rooms = normal_room_inventory(attrs, info, level)
        assert capture['required_rooms'] == rooms, path
        requirements.append({'level': level, 'quest': quest,
                             'map_id': 1 if capture['rom'] == 'redux' else 0,
                             'display_level': info[0x33], 'required_rooms': rooms})
    return complete_sets(requirements)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--blob', type=Path, default=ROOT / 'engine/src/uw_room_blob.c')
    p.add_argument('--requirements', type=Path,
                   default=ROOT / 'docs/audit/2026-10-18_T352_room_inventory.json')
    p.add_argument('--captures', type=Path)
    p.add_argument('--out', type=Path)
    a = p.parse_args()
    requirements = fresh_requirements(a.captures) if a.captures else installed_requirements(a.requirements)
    result = check(read_blob(a.blob), requirements,
                   json.loads((ROOT / 'data/graphics/verified_uw_layout_aliases.json').read_text()))
    if a.out:
        a.out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'pass': result['pass'], 'rooms': sum(c['required'] for c in result['cases']),
                      'missing': [{'level': c['level'], 'quest': c['quest'],
                                   'map_id': c['map_id'], 'rooms': c['missing']}
                                  for c in result['cases'] if c['missing']]}))
    raise SystemExit(0 if result['pass'] else 1)
