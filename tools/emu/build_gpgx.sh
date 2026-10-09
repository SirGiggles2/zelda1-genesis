#!/bin/sh
# Build the Genesis Plus GX libretro core used by tools/emu/gpgx.py
# (headless Genesis runs on Linux hosts, where BizHawk is unavailable).
# Output: build/emu/genesis_plus_gx_libretro.so (not stripped: gpgx.py
# reads VRAM/CRAM/VSRAM/registers through the symbol table).
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DEST="$ROOT/build/emu"
REV=58c341487e5bfcf979ea68413c7987633adb0c56
mkdir -p "$DEST"
if [ ! -d "$DEST/gpgx" ]; then
    git clone -q https://github.com/libretro/Genesis-Plus-GX.git "$DEST/gpgx"
fi
cd "$DEST/gpgx"
git fetch -q origin "$REV" 2>/dev/null || true
git checkout -q "$REV"
make -f Makefile.libretro -j"$(nproc)"
cp genesis_plus_gx_libretro.so "$DEST/"
echo "built $DEST/genesis_plus_gx_libretro.so"
