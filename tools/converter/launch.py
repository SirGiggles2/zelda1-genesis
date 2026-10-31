"""Source/app entry point, including frozen Python child-script dispatch."""
from pathlib import Path
import os
import importlib
import json
import runpy
import sys


def main():
    # PyInstaller's sys.executable points at this launcher. Existing pipeline
    # subprocesses pass a Python script; execute it using the bundled runtime.
    if len(sys.argv) > 1 and sys.argv[1].lower().endswith('.py'):
        script = Path(sys.argv[1]).resolve()
        sys.argv = sys.argv[1:]
        sys.path.insert(0, str(script.parent))
        runpy.run_path(str(script), run_name='__main__')
        return 0
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    if '--self-test' in sys.argv[1:]:
        from package_workspace import prepare_workspace
        root = prepare_workspace()
        modules = {name: getattr(importlib.import_module(name), '__version__', 'available')
                   for name in ('numpy', 'scipy', 'py65')}
        print(json.dumps({'status': 'ready', 'python': sys.version.split()[0],
                          'workspace': str(root), 'modules': modules}))
        return 0
    if getattr(sys, 'frozen', False):
        from package_workspace import prepare_workspace
        from web_builder import main as serve
        return serve(root=prepare_workspace())
    from package_workspace import prepare_runtime
    interpreter = prepare_runtime()
    if interpreter:
        os.execv(str(interpreter), [str(interpreter), str(Path(__file__).resolve()), *sys.argv[1:]])
    from web_builder import main as serve
    return serve()


if __name__ == '__main__':
    raise SystemExit(main())
