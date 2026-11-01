#!/bin/sh
# Source launcher. Packaged applications already include Python.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
for PYTHON in python3 python; do
    if command -v "$PYTHON" >/dev/null 2>&1 && "$PYTHON" -c 'import sys; sys.exit(sys.version_info < (3, 11))' 2>/dev/null; then
        exec "$PYTHON" "$HERE/tools/converter/launch.py" "$@"
    fi
done
printf '%s\n' 'Python 3.11 or newer is required for this source package.' 'Install Python from https://www.python.org/downloads/ or use the packaged Zelda Genesis Builder app.' >&2
exit 1
