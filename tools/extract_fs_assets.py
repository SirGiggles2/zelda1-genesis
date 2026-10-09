"""Extract Zelda Redux File Select assets from live CHR-RAM dump + nametable dump.

Emits src/gen/fs_static_tilemap.c, fs_palette.c, fs_font_chr.c,
fs_link_sprite_chr.c, fs_heart_cursor_chr.c, fs_border_chr.c.
Writes src/gen/fs_asset_hashes.txt with SHA256 of every output.

CHR source: tools/file_select_test/ref/fs_chr.bin — 8KB live CHR-RAM dump
            captured by dump_fs_chr.lua at the FS screen (BizHawk PPU Bus $0000-$1FFF).

NES PPU CHR-RAM layout at File Select frame:
  $0000-$0FFF (lower half, tiles   0-255): sprite patterns
  $1000-$1FFF (upper half, tiles 256-511): BG patterns (PPUCTRL bit 4 = 1)

The static .dat files (CommonBackgroundPatterns, DemoBackgroundPatterns, etc.)
contain Mode 0 (demo/title) CHR-RAM content — NOT the FS-mode patterns. Using
them produced ZELDA title art instead of the File Select layout. The live dump
captures the CHR state after Mode 1 has uploaded FS-specific tile bitmaps.

To regenerate fs_chr.bin: run tools/file_select_test/dump_fs_chr.lua in BizHawk
with Zelda1-Redux/Zelda Redux.nes, copy C:\\tmp\\fs_chr.bin here.
"""
from __future__ import annotations

import hashlib
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
REF_DIR = REPO / "reference" / "aldonunez" / "dat"
NT_DUMP = REPO / "tools" / "file_select_test" / "ref" / "fs_nt.bin"
FS_CHR_DUMP = REPO / "tools" / "file_select_test" / "ref" / "fs_chr.bin"
OUT_DIR = REPO / "data" / "fs"

# Import shared helpers from extract_intro_assets.py (same repo, same tools/ dir).
sys.path.insert(0, str(REPO / "tools"))
from extract_intro_assets import nes_color_to_gen_cram, nes_tile_to_gen_tile


# ---------------------------------------------------------------------------
# CHR data loaders (live CHR-RAM dump from BizHawk FS frame capture)
# ---------------------------------------------------------------------------

def _load_fs_chr() -> bytes:
    """Load 8KB live CHR-RAM dump captured at FS screen.

    Returns 8192 bytes: [0:4096] = sprite patterns ($0000-$0FFF),
                        [4096:8192] = BG patterns ($1000-$1FFF).
    Run tools/file_select_test/dump_fs_chr.lua to regenerate.
    """
    if not FS_CHR_DUMP.exists():
        sys.stderr.write(
            f"ERROR: {FS_CHR_DUMP} missing — run tools/file_select_test/dump_fs_chr.lua\n"
        )
        sys.exit(1)
    data = FS_CHR_DUMP.read_bytes()
    if len(data) != 8192:
        sys.stderr.write(
            f"ERROR: expected 8192-byte CHR dump, got {len(data)} bytes\n"
        )
        sys.exit(1)
    return data


def _load_bg_chr() -> bytes:
    """Return the BG pattern table half from the live CHR dump ($1000-$1FFF).

    PPUCTRL bit 4 = 1 at FS screen → BG tiles at $1000. The returned 4096 bytes
    cover NES BG tile indices 0x00-0xFF (256 tiles), indexed by tile_idx * 16.
    """
    return _load_fs_chr()[4096:]  # upper half: $1000-$1FFF


def _load_sprite_chr() -> bytes:
    """Return the sprite pattern table half from the live CHR dump ($0000-$0FFF).

    PPUCTRL bit 3 = 0 at FS screen → 8x8 sprites at $0000. The returned 4096 bytes
    cover NES sprite tile indices 0x00-0xFF (256 tiles), indexed by tile_idx * 16.
    """
    return _load_fs_chr()[:4096]  # lower half: $0000-$0FFF


def _read_bg_tile(tile_idx: int, bg_chr: bytes) -> bytes:
    """Return the 16-byte NES tile for BG tile index tile_idx."""
    off = tile_idx * 16
    if off + 16 > len(bg_chr):
        raise IndexError(
            f"BG tile 0x{tile_idx:02X} out of range (CHR has {len(bg_chr)//16} tiles)"
        )
    return bg_chr[off:off + 16]


def _read_sprite_tile(tile_idx: int, sprite_chr: bytes) -> bytes:
    """Return the 16-byte NES tile for sprite tile index tile_idx."""
    off = tile_idx * 16
    if off + 16 > len(sprite_chr):
        raise IndexError(
            f"Sprite tile 0x{tile_idx:02X} out of range (CHR has {len(sprite_chr)//16} tiles)"
        )
    return sprite_chr[off:off + 16]


# ---------------------------------------------------------------------------
# v1.1 — Palette
# ---------------------------------------------------------------------------

def emit_palette() -> None:
    """Emit src/gen/fs_palette.c — 4 Genesis CRAM palettes for FS render.

    Genesis has 4 CRAM palettes total (BG + sprites share). NES File Select
    nametable attribute table uses BG pal 0 + BG pal 1; sprites need green Link
    + heart cursor. Map:

        Gen pal 0 = NES BG pal 0 ($0F,$30,$00,$12)        attr=0 cells
        Gen pal 1 = NES BG pal 1 ($0F,$16,$27,$36)        attr=1 cells (LIFE/heart area)
        Gen pal 2 = NES sprite pal 0 ($0F,$29,$27,$17)    Link slots (green; Redux override)
        Gen pal 3 = NES sprite pal 3 ($0F,$15,$27,$30)    heart cursor

    Source: aldonunez/Z_06.asm:444-449 MenuPalettesTransferBuf (BG pal 1 + cursor pal),
            Zelda1-Redux/src/code/menus/file_select.asm:75-78 (Link palette overrides).

    v1.fix2 limitation: all 3 Link sprite slots use Gen pal 2 (green). Per-slot
    color tinting (blue slot 1, red slot 2) deferred — Gen 4-palette budget is
    spent on BG 0/1 + Link + cursor. v3 SRAM may revisit.
    """
    PALETTES = [
        # Gen pal 0 — BG pal 0: black bg, white text, black, blue (Z_06.asm:445)
        (0x0F, 0x30, 0x00, 0x12),
        # Gen pal 1 — BG pal 1: black, red, beige, light-yellow (Z_06.asm:446)
        # Used by attr=1 region: LIFE column header + heart-count icons. Heart
        # cursor sprite (8x8, pal 1) shares this palette: cursor pixel values
        # 1+3 land on red/light-yellow — close enough to NES sprite pal 3
        # (light-red/white) given the Gen 4-palette ceiling.
        (0x0F, 0x16, 0x27, 0x36),
        # Gen pal 2 — bright Link palette: black, green, beige, brown
        # Redux override (file_select.asm:75) of sprite pal 0; OCCUPIED slot tint.
        (0x0F, 0x29, 0x27, 0x17),
        # Gen pal 3 — faded/empty Link palette: black, dark-olive, dark-brown,
        # very-dark-red. Source: Zelda1-Redux/menu_tweaks.asm:397-404
        # main_menu_palette writes $19,$17,$07 to sprite palette buf for any
        # slot where $0633,y == 0 (no save). EMPTY slot tint.
        (0x0F, 0x19, 0x17, 0x07),
    ]

    out = []
    out.append("/* AUTO-GENERATED — see tools/extract_fs_assets.py */")
    out.append("/* fs_palettes[4][4]: 4 Genesis CRAM palettes × 4 colors each.")
    out.append(" *   pal 0 = NES BG pal 0 (attr=0 cells)")
    out.append(" *   pal 1 = NES BG pal 1 (attr=1 cells: LIFE/hearts) + heart cursor")
    out.append(" *   pal 2 = bright Link  (occupied save slot)")
    out.append(" *   pal 3 = faded Link   (empty save slot — Redux dark tint)")
    out.append(" * Source: aldonunez/Z_06.asm:444-449 MenuPalettesTransferBuf,")
    out.append(" *         Zelda1-Redux/src/code/menus/file_select.asm:75-78,")
    out.append(" *         Zelda1-Redux/src/code/menus/menu_tweaks.asm:397-404 */")
    out.append("#include <stdint.h>")
    out.append("const uint16_t fs_palettes[4][4] = {")
    for pal in PALETTES:
        cram = [nes_color_to_gen_cram(c) for c in pal]
        out.append("    { " + ", ".join(f"0x{w:04X}" for w in cram) + " },")
    out.append("};")

    (OUT_DIR / "fs_palette.c").write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
    print("[fs] emitted fs_palette.c")


# ---------------------------------------------------------------------------
# v1.fix2 — Static attribute table
# ---------------------------------------------------------------------------

def emit_static_attr() -> None:
    """Emit src/gen/fs_static_attr.c — 64-byte NES attribute table.

    Bytes 960..1024 of fs_nt.bin. NES PPU attribute layout:
      8 cols × 8 rows = 64 bytes; each byte covers a 4×4 tile-cell region
      (32×32 px). Bits per 2×2 quadrant:
        bits 1:0 = top-left palette (0..3)
        bits 3:2 = top-right palette
        bits 5:4 = bottom-left palette
        bits 7:6 = bottom-right palette
    fs_render decodes per cell at write time."""
    if not NT_DUMP.exists():
        sys.stderr.write(f"ERROR: nametable dump missing: {NT_DUMP}\n")
        sys.exit(1)
    raw = NT_DUMP.read_bytes()
    attr = raw[960:1024]
    out = []
    out.append("/* AUTO-GENERATED — see tools/extract_fs_assets.py */")
    out.append("/* Source: tools/file_select_test/ref/fs_nt.bin[960:1024] */")
    out.append("#include <stdint.h>")
    out.append("const uint8_t fs_static_attr[64] = {")
    for r in range(8):
        line = "    " + ", ".join(f"0x{attr[r * 8 + c]:02X}" for c in range(8)) + ","
        out.append(line)
    out.append("};")
    (OUT_DIR / "fs_static_attr.c").write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
    print("[fs] emitted fs_static_attr.c")


# ---------------------------------------------------------------------------
# v1.1 — Static tilemap
# ---------------------------------------------------------------------------

def emit_static_tilemap() -> None:
    """Emit src/gen/fs_static_tilemap.c from the 1024-byte nametable dump.

    The dump is a raw 32x30=960-byte nametable + 64-byte attribute table.
    We emit only the first 960 bytes (tile indices); attribute data is excluded.
    Callers apply the Genesis CHR base offset at VDP upload time.
    """
    if not NT_DUMP.exists():
        sys.stderr.write(
            f"ERROR: nametable dump missing: {NT_DUMP}\n"
            "Run tools/file_select_test/dump_fs_nametable.lua in BizHawk first.\n"
        )
        sys.exit(1)
    raw = NT_DUMP.read_bytes()
    if len(raw) != 1024:
        sys.stderr.write(
            f"ERROR: expected 1024-byte NT dump, got {len(raw)} bytes\n"
        )
        sys.exit(1)
    out = []
    out.append("/* AUTO-GENERATED — see tools/extract_fs_assets.py */")
    out.append("/* Source: tools/file_select_test/ref/fs_nt.bin (BizHawk PPUNT capture) */")
    out.append("#include <stdint.h>")
    out.append("/* 30 rows x 32 cols = 960 tile bytes; attribute table (last 64) excluded.")
    out.append(" * Tile indices are raw NES BG PT indices; caller adds Genesis CHR base. */")
    out.append("const uint8_t fs_static_tilemap[960] = {")
    for row in range(30):
        line = "    " + ", ".join(f"0x{raw[row * 32 + col]:02X}" for col in range(32)) + ","
        out.append(line)
    out.append("};")
    (OUT_DIR / "fs_static_tilemap.c").write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
    print("[fs] emitted fs_static_tilemap.c")


# ---------------------------------------------------------------------------
# v1.2 — CHR extraction helpers
# ---------------------------------------------------------------------------

# CHR tile index constants documented from NES OAM/nametable evidence.
#
# Heart cursor: 0xF3 — locked by task v1.R8 (NES ROM at PRG addr 0xA589 / file 0xA599
#   in bank 2). Confirmed: sprite tile byte at OAM+1 for main-menu cursor sprite.
#
# Link sprite: tiles 0x08-0x0B confirmed from aldonunez/Z_02.asm Mode1_WriteLinkSprites.
#   NES 8x16 sprite mode: left column tile index $08 (top half=row $08, bot half=row $09),
#   right column tile index $0A (top half=row $0A, bot half=row $0B).
#   Mode1_WriteLinkSprites (Z_02.asm:2698-2700): LDA #$08→[$02]=left, LDA #$0A→[$03]=right.
#   These are sprite CHR tiles from CommonSpritePatterns.dat (tiles 0x00-0x6F).
#
# Font: BG tiles 0x00-0x09 = digits 0-9, 0x0A-0x23 = letters A-Z (Zelda 1 standard).
#   Space = 0x24. Wider range 0x00-0x63 covers all text glyphs in nametable.
#
# Border: tiles 0xD4-0xE1 + 0xED + 0xEE identified from nametable col 0/1/30/31 analysis.
#   These are all BG tiles (color_shift=0).

LINK_SPRITE_TILES  = [0x08, 0x09, 0x0A, 0x0B]  # NES OAM evidence: Mode1_WriteLinkSprites sets
                                                # left tile=$08, right tile=$0A; NES 8x16 sprite
                                                # mode means $08→rows $08/$09 (left col top/bot),
                                                # $0A→rows $0A/$0B (right col top/bot).
                                                # Source: aldonunez/Z_02.asm:2698-2700
HEART_CURSOR_TILE  = 0xF2                        # 8x16 sprite mode: OAM tile-byte $F3
                                                 # → pattern table 1 (LSB), tile pair = $F2.
                                                 # Top half of the 8x16 cursor lives at BG-
                                                 # half tile $F2 (visible heart shape); the
                                                 # bottom half is blank. Source: live FS CHR
                                                 # dump (fs_chr.bin[0x1F20..0x1F30]) + Z_06
                                                 # Mode1CursorSpriteTriplet OAM evidence.
FONT_TILES_START   = 0x00
FONT_TILES_END     = 0x64   # exclusive; covers 0x00-0x63 (100 tiles)
BORDER_TILES       = [
    0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB,
    0xDC, 0xDD, 0xDE, 0xDF, 0xE0, 0xE1, 0xED, 0xEE,
]


def _emit_chr_array(
    tiles_nes: list[bytes],
    symbol: str,
    color_shift: int = 0,
) -> None:
    """Convert list of 16-byte NES tiles to Genesis 4bpp and write to src/gen/{symbol}.c."""
    out = []
    out.append("/* AUTO-GENERATED — see tools/extract_fs_assets.py */")
    out.append("#include <stdint.h>")
    n_tiles = len(tiles_nes)
    out.append(f"const uint8_t {symbol}[{n_tiles * 32}] = {{")
    for nes_tile in tiles_nes:
        gen_tile = nes_tile_to_gen_tile(nes_tile, color_shift=color_shift)
        out.append("    " + ", ".join(f"0x{b:02X}" for b in gen_tile) + ",")
    out.append("};")
    (OUT_DIR / f"{symbol}.c").write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
    print(f"[fs] emitted {symbol}.c  ({n_tiles} tiles)")


def emit_link_sprite_chr() -> None:
    """Emit src/gen/fs_link_sprite_chr.c from 4 sprite CHR tiles (NES OAM evidence).

    Tiles 0x08-0x0B: left-col top/bot + right-col top/bot for front-facing Link
    in 8x16 sprite mode (Mode1_WriteLinkSprites, aldonunez/Z_02.asm:2698).

    color_shift=0: NES color indices 0-3 map directly to Gen pixel values 0-3.
    Genesis palette 2 (Link) has 4 colors loaded at CRAM byte offsets 0..7 of pal 2.
    """
    sp = _load_sprite_chr()
    tiles = [_read_sprite_tile(ti, sp) for ti in LINK_SPRITE_TILES]
    _emit_chr_array(tiles, "fs_link_sprite_chr", color_shift=0)


def emit_heart_cursor_chr() -> None:
    """Emit src/gen/fs_heart_cursor_chr.c — top half of NES 8x16 cursor sprite.

    Cursor uses NES 8x16 sprite mode at File Select. OAM tile-byte $F3 selects
    pattern table 1 + tile pair starting at $F2; the visible heart pixels live
    in BG-half tile $F2 (rows 0-7), with $F3 blank. We render that single tile
    as a Genesis 8x8 sprite (HEART_CHR_BASE in fs_render.c).

    color_shift=0: NES color indices 0-3 map directly to Gen pixel values 0-3
    of palette 3 (PAL_BG_CURSOR)."""
    bg = _load_bg_chr()
    tiles = [_read_bg_tile(HEART_CURSOR_TILE, bg)]
    _emit_chr_array(tiles, "fs_heart_cursor_chr", color_shift=0)


def emit_font_chr() -> None:
    """Emit src/gen/fs_font_chr.c — BG tiles 0x00-0x63 (digits, letters, glyphs).

    Tile layout:
      0x00-0x09  digits 0-9
      0x0A-0x23  letters A-Z (standard Zelda 1 encoding)
      0x24       space (blank)
      0x25-0x63  additional glyphs present in the FS nametable
    """
    bg = _load_bg_chr()
    tiles = [_read_bg_tile(ti, bg) for ti in range(FONT_TILES_START, FONT_TILES_END)]
    _emit_chr_array(tiles, "fs_font_chr", color_shift=0)


def emit_border_chr() -> None:
    """Emit src/gen/fs_border_chr.c — BG border/frame decoration tiles.

    Tile indices identified from nametable column 0/1/30/31 analysis:
      0xD4-0xE1  primary border pieces (corners + edges + vine decorations)
      0xED, 0xEE  additional border detail tiles
    """
    bg = _load_bg_chr()
    tiles = [_read_bg_tile(ti, bg) for ti in BORDER_TILES]
    _emit_chr_array(tiles, "fs_border_chr", color_shift=0)


def emit_bg_chr_full() -> None:
    """Emit src/gen/fs_bg_chr_full.c — full BG CHR block covering all 256 NES BG tiles.

    CommonBackgroundPatterns.dat (tiles 0x00-0x6F, 112 tiles) +
    DemoBackgroundPatterns.dat  (tiles 0x70-0xFF, 144 tiles) = 256 tiles total.

    Uploaded to VRAM tile 0x00 (256 tiles × 32 bytes = 8192 bytes). This maps every
    NES BG tile index 0x00-0xFF to the correct Genesis VRAM tile, so the nametable
    (which references tiles like 0x71-0xD3 for hearts/name/life display) resolves
    correctly without any gaps.

    Link sprite CHR is uploaded ABOVE this block (at VRAM tile 0x100+) to avoid
    collision with BG tiles that the nametable also references at 0x80-0x84.
    """
    # Combined BG CHR: Common (112 tiles 0x00-0x6F) + Demo (130 tiles 0x70-0xF1) = 242 tiles.
    # Nametable uses tile indices up to 0xF0 (confirmed from static tilemap analysis).
    TOTAL = 242   # tiles 0x00..0xF1 = 242 tiles
    bg = _load_bg_chr()

    tiles_nes = [_read_bg_tile(ti, bg) for ti in range(TOTAL)]

    out = []
    out.append("/* AUTO-GENERATED — see tools/extract_fs_assets.py */")
    out.append("/* Full BG CHR block: tiles 0x00-0xF1 (242 tiles × 32 bytes = 7744 bytes).")
    out.append(" * CommonBackgroundPatterns(112) + DemoBackgroundPatterns(130) = 242 tiles.")
    out.append(" * Upload to VRAM tile 0x00. Covers font, content(0x70-0xD3), border, misc.")
    out.append(" * All NES BG tile refs in nametable (max 0xF0) resolve correctly. */")
    out.append("#include <stdint.h>")
    n = len(tiles_nes)
    out.append(f"const uint8_t fs_bg_chr_full[{n * 32}] = {{")
    for nes_tile in tiles_nes:
        gen_tile = nes_tile_to_gen_tile(nes_tile, color_shift=0)
        out.append("    " + ", ".join(f"0x{b:02X}" for b in gen_tile) + ",")
    out.append("};")
    (OUT_DIR / "fs_bg_chr_full.c").write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
    print(f"[fs] emitted fs_bg_chr_full.c  ({n} tiles, {n*32} bytes)")


def emit_chr_blocks() -> None:
    """Emit all CHR block files."""
    emit_link_sprite_chr()
    emit_heart_cursor_chr()
    emit_font_chr()
    emit_border_chr()
    emit_bg_chr_full()


# ---------------------------------------------------------------------------
# Hash file
# ---------------------------------------------------------------------------

def hash_outputs() -> None:
    """Write src/gen/fs_asset_hashes.txt — SHA256 of every fs_*.c output."""
    entries = []
    for p in sorted(OUT_DIR.glob("fs_*.c")):
        digest = hashlib.sha256(p.read_bytes()).hexdigest()
        entries.append(f"{digest}  {p.name}")
    (OUT_DIR / "fs_asset_hashes.txt").write_text("\n".join(entries) + "\n", encoding="utf-8", newline="\n")
    print(f"[fs] wrote fs_asset_hashes.txt  ({len(entries)} files hashed)")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    # v1.1 — scaffold
    emit_palette()
    emit_static_tilemap()
    emit_static_attr()

    # v1.2 — CHR extraction
    emit_chr_blocks()

    hash_outputs()
    print(f"[fs] all assets emitted to {OUT_DIR}")
