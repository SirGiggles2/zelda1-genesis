"""Check public renames against the complete converter dependency manifest."""
import ast
import subprocess
import sys
from types import SimpleNamespace
from unittest.mock import patch
import build
from pathlib import Path

import export_repo
import public_layout


def main():
    files = export_repo.repo_files()
    if public_layout.is_public(export_repo.ROOT) and (export_repo.ROOT / '.git').exists():
        tracked = set(subprocess.check_output(['git', '-C', str(export_repo.ROOT), 'ls-files'], text=True).splitlines())
        assert set(files) <= tracked, f'exported sources missing from Git: {sorted(set(files)-tracked)}'
    assert 'Build.bat' in files and 'Debug.bat' not in files
    assert 'engine/src/main.c' in files
    assert 'src/platform/game_main.c' in files
    assert 'tools/build/build_rom.py' in files
    assert 'tools/converter/build.py' in files
    assert public_layout.rename('RoomRom/src/roomrom_vram_map.h') == 'engine/src/vram_layout.h'
    assert public_layout.rename(public_layout.rename('RoomRom/src/main.c')) == 'engine/src/main.c'
    stale = ('RoomRom/', 'tools/builder/', 'tools/debug/', 'src/debug/',
             'a4_probe_main', 'a4_probe_asm', 'roomrom_vram_map.h',
             'Debug.bat', 'Debug.md', 'debug_project')
    exempt = ('public_layout.py', 'test_public_layout.py')
    for dst, src in files.items():
        data = export_repo.export_bytes(src).decode('utf-8' if dst.endswith('.py') else 'latin-1')
        if dst.endswith('.py'):
            ast.parse(data, filename=dst)
        if not dst.endswith(exempt):
            assert not any(name in data for name in stale), dst
    manifest = export_repo.export_bytes(files['tools/converter/package_manifest.txt']).decode('utf-8')
    for line in manifest.splitlines():
        if line and not line.startswith('#'):
            assert line in files, f'missing converter dependency: {line}'
    # Every explicitly compiled implementation must reach the public package.
    from package_check import is_banned
    assert not is_banned("data/graphics/verified_uw_layout_aliases.json")[0]
    assert not is_banned("data/music/builder_mix.json")[0]
    assert is_banned("data/music/studio_project.json")[0]
    assert is_banned("data/graphics/other.json")[0]
    script = export_repo.ROOT / ('tools/build/build_rom.py' if public_layout.is_public(export_repo.ROOT)
                                 else 'tools/debug/build_debug.py')
    def rooted_path(node):
        if isinstance(node, ast.Name) and node.id == 'ROOT':
            return ''
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Div):
            left = rooted_path(node.left)
            if left is not None and isinstance(node.right, ast.Constant) and isinstance(node.right.value, str):
                return '/'.join(p for p in (left, node.right.value) if p)
        return None

    for node in ast.walk(ast.parse(script.read_text(encoding='utf-8'))):
        helper = rooted_path(node)
        if helper and helper.endswith('.py'):
            assert public_layout.rename(helper) in files, f'missing build helper: {helper}'
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            source = node.value
            if source.endswith(('.c', '.s', '.asm', '.py')) and '/' in source:
                if not is_banned(source)[0] and not source.startswith('build/'):
                    assert public_layout.rename(source) in files, f'missing build source: {source}'
    # The GUI and command-line paths must select the same converter build.
    core = export_repo.export_bytes(files['tools/converter/converter_core.py']).decode('utf-8')
    assert '"converter" / "build.py"' in core
    build_text = export_repo.export_bytes(files['tools/converter/build.py']).decode('utf-8')
    assert '"builds" / "Zelda.md"' in build_text
    steps = build.plan_steps(Path('original.nes'), Path('redux.nes'))
    for _, command in steps:
        script = Path(command[1]).resolve().relative_to(build.ROOT.resolve()).as_posix()
        assert public_layout.rename(script) in files, f'missing conversion stage: {script}'
    final = ('generated asset catalog' , 'record generated freshness', 'verify generated freshness')
    assert tuple(label for label, _ in steps[-3:]) == final
    calls = []
    def failed_producer(argv, **kwargs):
        calls.append(argv)
        return SimpleNamespace(returncode=1 if len(calls) == 1 else 0)
    with patch.object(build.subprocess, 'run', failed_producer), patch('builtins.print'):
        assert build.run_extractors(Path('original.nes'), Path('redux.nes')) == 1
    assert not any(argv[1] == command[1] for argv in calls for _, command in steps[-3:])
    print(f'public_layout: PASS ({len(files)} unique paths, Python syntax, manifest closure, launcher/output references)')


if __name__ == '__main__':
    main()
