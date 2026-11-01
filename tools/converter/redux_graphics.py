"""Extract core Redux sprite banks from the authenticated user ROM.

Bank seams are shared by the pinned Original/Redux images and verified
against Z_02/Z_03 and Redux graphics.asm. No ROM bytes ship in this tool.
The common sprite block is also checked against live T-269 NES CHR.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
REDUX_SHA = "bcc19820e7101d2a3a74a05713c1d523c4020016768bc545f39c730cf6f7bbe6"
# PRG-relative seams from Redux code/gfx/graphics.asm (same as Original).
BLOCKS = {
    "common": (0x807F, 0xEE0),
    "demo": (0x4DB4, 0x1120),
    "enemy_owsp": (0xD15B, 0x720),
    "enemy_uwsp358": (0xD87B, 0x220),
    "enemy_uwsp469": (0xDA9B, 0x220),
    "enemy_uwsp_base": (0xDCBB, 0x100),
    "enemy_uwsp127": (0xDDBB, 0x220),
    "boss_uwspboss1257": (0xDFDB, 0x400),
    "boss_uwspboss3468": (0xE3DB, 0x400),
    "boss_uwspboss9": (0xE7DB, 0x400),
}


# Fixed 110-tile VRAM layout: gen_atlas.build_legacy_variant_blob.
# This contract contains identities/packing only, never extracted ROM bytes.
ITEM_LAYOUT = [
    {'name': 'sword_vert', 'tiles': [32, 33], 'rule': 'narrow_8x16'},
    {'name': 'sword_horz', 'tiles': [130, 131, 132, 133], 'rule': 'wide_16x16_hflippable'},
    {'name': 'boomerang', 'tiles': [54, 55, 56, 57, 58, 59, 60, 61], 'rule': 'narrow_8x16_phase_cycle'},
    {'name': 'arrow_vert', 'tiles': [40, 41], 'rule': 'narrow_8x16'},
    {'name': 'arrow_horz', 'tiles': [134, 135, 136, 137], 'rule': 'wide_16x16_hflippable'},
    {'name': 'bomb', 'tiles': [52, 53], 'rule': 'narrow_8x16'},
    {'name': 'explosion', 'tiles': [112, 113, 114, 115, 116, 117], 'rule': 'wide_16x16_mirrored_8x16_phase_cycle'},
    {'name': 'sword_diag', 'tiles': [72, 73, 74, 75], 'rule': 'wide_16x16_hflippable'},
    {'name': 'candle_fire_f0', 'tiles': [92, 93, 94, 95], 'rule': 'wide_16x16_pair'},
    {'name': 'candle_fire_f1', 'tiles': [158, 159, 160, 161], 'rule': 'wide_16x16_pair'},
    {'name': 'candle_fire_f2', 'tiles': [68, 69, 70, 71], 'rule': 'wide_16x16_pair'},
    {'name': 'candle_fire_f3', 'tiles': [206, 207, 208, 209], 'rule': 'wide_16x16_pair'},
    {'name': 'compass', 'tiles': [46, 47], 'rule': 'narrow_8x16'},
    {'name': 'map', 'tiles': [50, 51], 'rule': 'narrow_8x16'},
    {'name': 'heart_container', 'tiles': [104, 105], 'rule': 'wide_16x16_mirrored_8x16_slim'},
    {'name': 'big_key', 'tiles': [104, 105], 'rule': 'narrow_8x16'},
    {'name': 'book_of_magic', 'tiles': [70, 71], 'rule': 'narrow_8x16'},
    {'name': 'raft', 'tiles': [66, 67], 'rule': 'narrow_8x16'},
    {'name': 'ladder', 'tiles': [44, 45], 'rule': 'narrow_8x16'},
    {'name': 'ring', 'tiles': [118, 119], 'rule': 'narrow_8x16'},
    {'name': 'magic_key', 'tiles': [78, 79], 'rule': 'narrow_8x16'},
    {'name': 'bracelet', 'tiles': [76, 77], 'rule': 'narrow_8x16'},
    {'name': 'bow', 'tiles': [42, 43], 'rule': 'narrow_8x16'},
    {'name': 'recorder', 'tiles': [34, 35], 'rule': 'narrow_8x16'},
    {'name': 'food', 'tiles': [64, 65], 'rule': 'narrow_8x16'},
    {'name': 'potion', 'tiles': [74, 75], 'rule': 'narrow_8x16'},
    {'name': 'triforce_piece', 'tiles': [110, 111, 112, 113], 'rule': 'wide_16x16_pair'},
    {'name': 'magic_shot_v', 'tiles': [122, 123], 'rule': 'wide_16x16_mirrored_8x16_phase_cycle'},
    {'name': 'magic_shot_h', 'tiles': [124, 125, 126, 127], 'rule': 'wide_16x16_pair'},
    {'name': 'fairy_spark_f0', 'tiles': [240, 241], 'rule': 'narrow_8x16'},
    {'name': 'fairy_spark_f1', 'tiles': [242, 243], 'rule': 'narrow_8x16'},
    {'name': 'drop_heart', 'tiles': [224, 225], 'rule': 'narrow_8x16'},
    {'name': 'drop_clock', 'tiles': [102, 103], 'rule': 'narrow_8x16'},
    {'name': 'rod_horz', 'tiles': [138, 139, 140, 141], 'rule': 'wide_16x16_hflippable'},
    {'name': 'sword_shot_spread', 'tiles': [48, 49], 'rule': 'narrow_8x16'},
]

def convert(raw: bytes, bias: int = 0) -> bytes:
    if len(raw) % 16:
        raise ValueError("incomplete NES tile")
    result = bytearray()
    for off in range(0, len(raw), 16):
        for row in range(8):
            for col in range(0, 8, 2):
                pixels = []
                for x in (col, col + 1):
                    bit = 7 - x
                    pixel = ((raw[off + row] >> bit) & 1) | (((raw[off + row + 8] >> bit) & 1) << 1)
                    pixels.append(pixel + bias if pixel else 0)
                result.append(pixels[0] << 4 | pixels[1])
    return bytes(result)



def item_blob(image: bytes) -> tuple[bytes, list[dict]]:
    data = bytearray(); parts = []
    aliases = {0xF0:0x50,0xF1:0x51,0xF2:0x52,0xF3:0x53}
    def tile(t: int) -> bytes:
        if t in (0xE0,0xE1):
            # Odd OAM $F3 selects PT1 $1F20/$1F30, common misc.
            offset = 0x8E7F + (t-0xE0)*16
            ppu = 0x1F20 + (t-0xE0)*16
        else:
            t = aliases.get(t,t); ppu = t*16
            offset = (0x807F+t*16 if t<0x70 else
                      0x4DB4+(t-0x70)*16 if t<0x8E else
                      0xD15B+(t-0x8E)*16)
        raw = image[16+offset:16+offset+16]
        parts.append({"prg_offset":offset,"ppu_address":ppu,"nes_bytes":16,
                      "source_sha256":hashlib.sha256(raw).hexdigest()})
        return convert(raw)
    def mirror(v: bytes, slim: bool=False) -> bytes:
        rows = bytearray()
        for y in range(8):
            pixels = [n for b in v[y*4:y*4+4] for n in (b>>4,b&15)][::-1]
            if slim: pixels = pixels[1:]+[0]
            rows.extend(pixels[i]<<4|pixels[i+1] for i in range(0,8,2))
        return bytes(rows)
    for entry in ITEM_LAYOUT:
        tiles, rule = entry["tiles"], entry["rule"]
        if rule.startswith("wide_16x16_mirrored_8x16"):
            for i in range(0,len(tiles),2):
                top,bot=tile(tiles[i]),tile(tiles[i+1])
                data.extend(top+bot+mirror(top,rule.endswith("_slim"))+mirror(bot,rule.endswith("_slim")))
        else:
            for t in tiles:
                v=tile(t); data.extend(v)
                if rule.startswith("mirrored_"): data.extend(mirror(v))
    if len(data)!=3520: raise ValueError("core item VRAM layout changed")
    return bytes(data), parts


def shared_intro(image: bytes, source: list[str], manifest: dict) -> None:
    """One Redux attract presentation, independent of the selected save profile.

    Native title/story transfer programs are checked against live T-294 PPU.
    Retain the approved Genesis scroll/item-icon composition; replace all29
    item text records from their actual CPU pointers, including blank lines.
    """
    from tools.extract_intro_assets import nes_color_to_gen_cram
    from tools.intro_demo.compose_treasures_tilemap import build_tilemap, ITEM_PAIRS

    def span(offset, size):
        raw = image[16 + offset:16 + offset + size]
        if len(raw) != size:
            raise ValueError("incomplete intro source")
        return raw

    def part(offset, size):
        return {"prg_offset": offset, "nes_bytes": size,
                "source_sha256": hashlib.sha256(span(offset, size)).hexdigest()}

    def emit(name, data, parts, words=False, **details):
        width = 2 if words else 1
        packed = b"".join(v.to_bytes(width, "big") for v in data) if words else bytes(data)
        source.append(f"const unsigned {'short' if words else 'char'} {name}[{len(data)}] = {{")
        source.extend("    " + ",".join(f"0x{v:0{width * 2}X}" for v in data[i:i+16]) + ","
                      for i in range(0, len(data), 16))
        source.append("};")
        manifest["blocks"].append({"symbol": name, "parts": parts,
            "nes_bytes": sum(p["nes_bytes"] for p in parts), "converted_bytes": len(packed),
            "converted_sha256": hashlib.sha256(packed).hexdigest(), **details})

    def nametable(start):
        nt = bytearray([0x24] * 960 + [0] * 64)
        cursor = start
        while image[16 + cursor] != 0xFF:
            hi, lo, ctrl = span(cursor, 3); cursor += 3
            count = ctrl & 63 or 64
            addr = hi * 256 + lo
            for i in range(count):
                value = image[16 + cursor + (0 if ctrl & 64 else i)]
                pos = addr - 0x2000
                if 0 <= pos < 1024:
                    nt[pos] = value
                elif not 0x2800 <= addr < 0x2C00:
                    raise ValueError("unsupported intro transfer address")
                addr += 32 if ctrl & 128 else 1
            cursor += 1 if ctrl & 64 else count
            if cursor - start > 2048:
                raise ValueError("unterminated intro transfer")
        words = []
        for i, tile in enumerate(nt[:960]):
            row, col = divmod(i, 32)
            attr = nt[960 + row // 4 * 8 + col // 4]
            pal = (attr >> ((row % 4 // 2 * 2 + col % 4 // 2) * 2)) & 3
            words.append(tile | pal << 13)
        return words, [part(start, cursor - start + 1)]

    for name, start in (("story", 0x1A3FE), ("title", 0x1A869)):
        words, parts = nametable(start)
        emit(f"core_redux_intro_{name}_map", words, parts, True, encoding="native_nt_words_be16")

    records = []; parts = [part(0x94AD, 58)]
    for index in range(29):
        ptr = int.from_bytes(span(0x94AD + index * 2, 2), "little")
        if not 0x8000 <= ptr < 0xC000:
            raise ValueError("invalid intro text pointer")
        end = ptr + 1
        while image[16 + end] != 255 and end - ptr < 34:
            end += 1
        raw = span(ptr, end - ptr + 1)
        if raw[-1] != 255 or raw[0] + len(raw) - 2 > 32:
            raise ValueError("invalid intro text record")
        records.append(raw); parts.append(part(ptr, len(raw)))
    cells, rows = build_tilemap()
    groups = [(1,2),(3,),(4,),(5,),(6,),(7,8),(9,10),(11,12),(13,),
              (14,15),(16,17),(18,19),(20,21),(22,),(23,24),(25,26),(27,)]
    def write_text(row, index):
        raw = records[index]
        cells[row * 32:(row + 1) * 32] = [0x24] * 32
        for col, tile in enumerate(raw[1:-1], raw[0]):
            cells[row * 32 + col] = tile | (3 << 13 if 0xE2 <= tile <= 0xE6 else 0)
    write_text(3, 0)
    row = 5
    for pair, group in zip(ITEM_PAIRS, groups):
        count = max(len(pair[1]), len(pair[3]))
        if len(group) != count:
            raise ValueError("intro item composition changed")
        for line, index in enumerate(group):
            write_text(row + 5 + line, index)
        row += 7 + count
    write_text(row + 5, 28)
    if rows != 176:
        raise ValueError("intro item height differs from runtime")
    emit("core_redux_intro_items_map", cells, parts, True,
         encoding="native_text_approved_genesis_item_layout_be16", rows=rows)

    sprite = span(0x807F, 0x700) + span(0x4DB4, 0x900)
    emit("core_redux_intro_sprite_chr", convert(sprite, 4),
         [part(0x807F, 0x700), part(0x4DB4, 0x900)], pixel_bias=4)
    emit("core_redux_intro_misc_chr", convert(span(0x8E7F, 0xE0), 4),
         [part(0x8E7F, 0xE0)], pixel_bias=4)
    blink_offsets = [0x8E7F, 0x8E8F] + [0x807F + tile * 16 for tile in (0x68,0x69,0x6E,0x6F,0x32,0x33)]
    emit("core_redux_intro_blink_chr", b"".join(convert(span(o, 16), 8) for o in blink_offsets),
         [part(o, 16) for o in blink_offsets], pixel_bias=8)

    def palette(raw, story=False):
        result = [0] * 64
        for p in range(4):
            for c in range(4):
                result[p * 16 + c] = nes_color_to_gen_cram(raw[p * 4 + c])
                if not story or p < 3:
                    result[((p + 1) if story else p) * 16 + 4 + c] = nes_color_to_gen_cram(raw[16 + p * 4 + c])
        return result
    emit("core_redux_intro_story_palette", palette(span(0x9552, 32), True),
         [part(0x9552, 32)], True, encoding="combined_cram_be16")
    title_pal = bytearray(span(0x94F1, 32))
    title_pal[4:8] = span(0x985E, 4)
    emit("core_redux_intro_title_palette", palette(title_pal),
         [part(0x94F1, 32), part(0x985E, 4)], True, encoding="combined_cram_be16")
    fade = [word for cycle in range(14) for word in palette(span(0x9969 + cycle * 32, 32))]
    emit("core_redux_intro_title_fade", fade, [part(0x9969, 448)], True, encoding="14x64_cram_be16")
    emit("core_redux_intro_title_glow", [nes_color_to_gen_cram(v) for v in span(0x9863, 8)],
         [part(0x9863, 8)], True, encoding="cram_be16")
    manifest["intro_dependencies"] = {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest()
        for path in ("tools/extract_intro_assets.py", "tools/intro_demo/compose_treasures_tilemap.py")}


def generate(rom: Path, output: Path) -> dict:
    image = rom.read_bytes()
    digest = hashlib.sha256(image).hexdigest()
    if digest != REDUX_SHA:
        raise ValueError(f"unsupported core Redux ROM: {digest}")
    if image[:4] != b"NES\x1a" or len(image) != 16 + 8 * 0x4000:
        raise ValueError("unexpected authenticated iNES layout")
    source = ["/* Generated from the user's pinned Redux ROM. Do not distribute. */"]
    manifest = {"schema": 1, "rom_sha256": digest, "generator": "tools/converter/redux_graphics.py",
                "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), "blocks": []}
    for name, (offset, size) in BLOCKS.items():
        raw = image[16 + offset:16 + offset + size]
        if len(raw) != size:
            raise ValueError(f"incomplete {name}")
        for bias in ((0, 12) if name.startswith("boss_") else (0,)):
            data = convert(raw, bias)
            symbol = "core_redux_" + name + "_chr" + ("_subpal3" if bias else "")
            source.append(f"const unsigned char {symbol}[{len(data)}] = {{")
            source += ["    " + ",".join(f"0x{v:02X}" for v in data[i:i+16]) + "," for i in range(0, len(data), 16)]
            source.append("};")
            manifest["blocks"].append({"symbol": symbol, "prg_offset": offset, "nes_bytes": size,
                "pixel_bias": bias, "source_sha256": hashlib.sha256(raw).hexdigest(),
                "converted_sha256": hashlib.sha256(data).hexdigest()})
    data,parts=item_blob(image)
    symbol="core_redux_items_chr"
    source.append(f"const unsigned char {symbol}[{len(data)}] = {{")
    source += ["    " + ",".join(f"0x{v:02X}" for v in data[i:i+16]) + "," for i in range(0,len(data),16)]
    source.append("};")
    manifest["blocks"].append({"symbol":symbol,"nes_bytes":len(data)//2,"parts":parts,
                              "layout":ITEM_LAYOUT,"converted_sha256":hashlib.sha256(data).hexdigest()})
    # Existing OAM-backed inventory_sprite_chr order. Most pairs are in
    # common sprites; ladder $76/$77 is the always-resident demo tail.
    inventory = [(0x24,0),(0x25,0),(0x26,0),(0x27,0),(0x6C,0),(0x6D,0),
                 (0x76,0),(0x77,0),(0x3E,0),(0x3F,0),(0x6A,0),(0x6B,0),
                 (0x4C,0),(0x4D,0),(0x3E,8),(0x3F,8)]
    data = bytearray(); parts = []
    for tile, bias in inventory:
        offset = 0x807F + tile*16 if tile < 0x70 else 0x4DB4 + (tile-0x70)*16
        raw = image[16+offset:16+offset+16]
        data.extend(convert(raw,bias))
        parts.append({"prg_offset":offset,"nes_bytes":16,"pixel_bias":bias,
                      "source_sha256":hashlib.sha256(raw).hexdigest()})
    symbol = "core_redux_inventory_chr"
    source.append(f"const unsigned char {symbol}[{len(data)}] = {{")
    source += ["    " + ",".join(f"0x{v:02X}" for v in data[i:i+16]) + "," for i in range(0,len(data),16)]
    source.append("};")
    manifest["blocks"].append({"symbol":symbol,"nes_bytes":256,"parts":parts,
                              "converted_sha256":hashlib.sha256(data).hexdigest()})
    # Z_02 common BG/misc + Z_03 OW BG, then visible_secrets.asm's
    # native $1540 overlay. Live NES verifies every secret byte.
    bg = bytearray(4096); parts = []
    for offset,size,dest in [(0x877F,0x700,0),(0xC93B,0x820,0x700),
                             (0x8E7F,0xE0,0xF20),(0xEC60,0xC0,0x540)]:
        raw=image[16+offset:16+offset+size];bg[dest:dest+size]=raw
        parts.append({"prg_offset":offset,"nes_bytes":size,"destination":dest,
                      "source_sha256":hashlib.sha256(raw).hexdigest()})
    data=convert(bg);symbol="core_redux_bg_ow_chr"
    source.append(f"const unsigned char {symbol}[{len(data)}] = {{")
    source += ["    " + ",".join(f"0x{v:02X}" for v in data[i:i+16]) + "," for i in range(0,len(data),16)]
    source.append("};")
    manifest["blocks"].append({"symbol":symbol,"nes_bytes":4096,"parts":parts,
                              "source_sha256":hashlib.sha256(bg).hexdigest(),
                              "converted_sha256":hashlib.sha256(data).hexdigest()})
    # All-room live NES census: combinations absent from the legacy LUT.
    # Six static secrets, then four gray animated rock tiles. Physical
    # slots are separately reserved; no Original sparse indices move.
    extra_pairs = [(0x54,2),(0x56,2),(0x58,0),(0x59,0),(0x5A,0),(0x5B,0),
                   (0x8E,0),(0x90,0),(0x93,0),(0x95,0)]
    pair_data = bytes(v for pair in extra_pairs for v in pair)
    symbol="core_redux_ow_extra_pairs"
    source.append(f"const unsigned char {symbol}[20] = {{"+",".join(f"0x{v:02X}" for v in pair_data)+"};")
    manifest["blocks"].append({"symbol":symbol,"nes_bytes":0,"converted_bytes":20,
                              "encoding":"tile_palette_identities","vram_tile_base":1172,
                              "converted_sha256":hashlib.sha256(pair_data).hexdigest()})
    data=b"".join(convert(bg[t*16:(t+1)*16],p*4) for t,p in extra_pairs[:6])
    symbol="core_redux_ow_extra_static_chr"
    source.append(f"const unsigned char {symbol}[192] = {{")
    source += ["    " + ",".join(f"0x{v:02X}" for v in data[i:i+16]) + "," for i in range(0,len(data),16)]
    source.append("};")
    manifest["blocks"].append({"symbol":symbol,"nes_bytes":96,"converted_bytes":192,
                              "parts":parts,"tile_palette_order":extra_pairs[:6],"vram_tile_base":1172,
                              "converted_sha256":hashlib.sha256(data).hexdigest()})
    # Dungeon patterns use the same common/misc seams, the native UW
    # scene bank and TileTransfer's DungeonAssets overlay at PPU $1540.
    bg=bytearray(4096);parts=[]
    for offset,size,dest in [(0x877F,0x700,0),(0xC11B,0x820,0x700),
                             (0x8E7F,0xE0,0xF20),(0xED20,0xC0,0x540)]:
        raw=image[16+offset:16+offset+size];bg[dest:dest+size]=raw
        parts.append({"prg_offset":offset,"nes_bytes":size,"destination":dest,
                      "source_sha256":hashlib.sha256(raw).hexdigest()})
    data=convert(bg);symbol="core_redux_bg_uw_chr"
    source.append(f"const unsigned char {symbol}[{len(data)}] = {{")
    source += ["    " + ",".join(f"0x{v:02X}" for v in data[i:i+16]) + "," for i in range(0,len(data),16)]
    source.append("};")
    manifest["blocks"].append({"symbol":symbol,"nes_bytes":4096,"parts":parts,
                              "source_sha256":hashlib.sha256(bg).hexdigest(),
                              "converted_sha256":hashlib.sha256(data).hexdigest()})
    # Z_05 DoorFaceTilesE/W/S/N: include every state so the pinned top
    # wall patch's boundary byte is authenticated as well as the cracks.
    data=bytearray();parts=[]
    for offset in (0x15FEE,0x1602A,0x16066,0x160A2):
        raw=image[16+offset:16+offset+60];data.extend(raw)
        parts.append({"prg_offset":offset,"nes_bytes":60,
                      "source_sha256":hashlib.sha256(raw).hexdigest()})
    symbol="core_redux_door_faces"
    source.append(f"const unsigned char {symbol}[{len(data)}] = {{")
    source += ["    " + ",".join(f"0x{v:02X}" for v in data[i:i+16]) + "," for i in range(0,len(data),16)]
    source.append("};")
    manifest["blocks"].append({"symbol":symbol,"nes_bytes":240,"converted_bytes":240,
                              "encoding":"native_tile_ids","parts":parts,
                              "converted_sha256":hashlib.sha256(data).hexdigest()})
    # Z_06 LevelBlockOW, bank6:$8400. Live all-room SRAM census
    # authenticates all six attribute tables, including graveyard palettes.
    offset=0x18400;data=image[16+offset:16+offset+768]
    symbol="core_redux_ow_attributes"
    source.append(f"const unsigned char {symbol}[768] = {{")
    source += ["    " + ",".join(f"0x{v:02X}" for v in data[i:i+16]) + "," for i in range(0,len(data),16)]
    source.append("};")
    manifest["blocks"].append({"symbol":symbol,"nes_bytes":768,"converted_bytes":768,
                              "encoding":"native_attributes","parts":[{"prg_offset":offset,"nes_bytes":768,
                              "source_sha256":hashlib.sha256(data).hexdigest()}],
                              "converted_sha256":hashlib.sha256(data).hexdigest()})
    # hud_and_subscreen.asm bank6:$9307, white heart outline in OW.
    # Extract raw NES palette codes so room patches cannot restore Original.
    offset=0x19307;data=image[16+offset:16+offset+4]
    symbol="core_redux_ow_heart_palette"
    source.append(f"const unsigned char {symbol}[4] = {{"+",".join(f"0x{v:02X}" for v in data)+"};")
    manifest["blocks"].append({"symbol":symbol,"nes_bytes":4,"converted_bytes":4,
                              "encoding":"nes_palette","parts":[{"prg_offset":offset,"nes_bytes":4,
                              "source_sha256":hashlib.sha256(data).hexdigest()}],
                              "converted_sha256":hashlib.sha256(data).hexdigest()})
    # animate.asm: PPU $1780 (12 tiles) and $1880 (18 tiles).
    # All four seams match the live 32 KiB CHR-RAM reference census.
    for frame, offsets in enumerate(((0xC9BB,0xCABB),(0xEEA0,0xEF60),
                                      (0xF080,0xF140),(0xF260,0xF320))):
        raw=bytearray(); parts=[]
        for offset,size in zip(offsets,(0xC0,0x120)):
            chunk=image[16+offset:16+offset+size];raw.extend(chunk)
            parts.append({"prg_offset":offset,"nes_bytes":size,
                          "source_sha256":hashlib.sha256(chunk).hexdigest()})
        # Sparse OW contract: palette 2/3 per tile, except $99 uses 2.
        # Emit ready-to-transfer contiguous ranges, with no runtime repack.
        data=b"".join(convert(raw[t*16:(t+1)*16],pal*4)
                      for t in range(30) for pal in ((2,) if t==29 else (2,3)))
        symbol=f"core_redux_water{frame}_chr"
        source.append(f"const unsigned char {symbol}[{len(data)}] = {{")
        source += ["    " + ",".join(f"0x{v:02X}" for v in data[i:i+16]) + "," for i in range(0,len(data),16)]
        source.append("};")
        manifest["blocks"].append({"symbol":symbol,"nes_bytes":len(raw),"parts":parts,
                                  "converted_bytes":len(data),"tile_palette_order":"78-83,88-98:2/3;99:2",
                                  "source_sha256":hashlib.sha256(raw).hexdigest(),
                                  "converted_sha256":hashlib.sha256(data).hexdigest()})
        data=b"".join(convert(raw[(12+t-0x88)*16:(13+t-0x88)*16]) for t,_ in extra_pairs[6:])
        symbol=f"core_redux_gray_water{frame}_chr"
        source.append(f"const unsigned char {symbol}[128] = {{")
        source += ["    " + ",".join(f"0x{v:02X}" for v in data[i:i+16]) + "," for i in range(0,len(data),16)]
        source.append("};")
        manifest["blocks"].append({"symbol":symbol,"nes_bytes":64,"converted_bytes":128,
                                  "parts":parts,"tile_palette_order":extra_pairs[6:],"vram_tile_base":1178,
                                  "converted_sha256":hashlib.sha256(data).hexdigest()})
    # arrows.asm bank4:$B042: actual native drop table, including ID$55.
    # Keep this ROM-backed alongside the other profile-specific tables.
    offset=0x13042;data=image[16+offset:16+offset+40]
    symbol="core_redux_drop_items"
    source.append(f"const unsigned char {symbol}[40] = {{"+",".join(f"0x{v:02X}" for v in data)+"};")
    manifest["blocks"].append({"symbol":symbol,"prg_offset":offset,"nes_bytes":40,
                              "converted_bytes":40,"encoding":"native_item_ids",
                              "source_sha256":hashlib.sha256(data).hexdigest(),
                              "converted_sha256":hashlib.sha256(data).hexdigest()})
    # automap.asm bank5:$AD00: unfiltered 8x4 map, four rooms per tile.
    offset=0x16D00;raw=image[16+offset:16+offset+512]
    data=convert(raw);symbol="core_redux_automap_chr"
    source.append(f"const unsigned char {symbol}[1024] = {{")
    source += ["    " + ",".join(f"0x{v:02X}" for v in data[i:i+16]) + "," for i in range(0,len(data),16)]
    source.append("};")
    manifest["blocks"].append({"symbol":symbol,"prg_offset":offset,"nes_bytes":512,
                              "converted_bytes":1024,"vram_tile_base":1320,
                              "source_sha256":hashlib.sha256(raw).hexdigest(),
                              "converted_sha256":hashlib.sha256(data).hexdigest()})
    # move_maps.asm and actual bank6 Q2 replacement pointers, not README order.
    records=bytearray();parts=[]
    for quest in range(2):
        for level in range(9):
            if quest:
                ptr=int.from_bytes(image[16+0x183A4+level*2:16+0x183A6+level*2],"little")
                start=0x18000+ptr-0x8000
                xoffset=start+5;macro=start+0x26
            else:
                start=0x193FC+level*0xFC
                xoffset=start+0x2E;macro=start+0x4F
            cursor=macro
            while image[16+cursor]!=0xFF:
                spec=image[16+cursor+2];cursor+=3+(1 if spec&0x40 else spec&0x3F)
            size=cursor-macro+1
            if size>45: raise ValueError("dungeon HUD macro exceeds native field")
            raw=image[16+macro:16+macro+size]
            records+=bytes([image[16+xoffset],size])+raw+bytes([255])*(46-size)
            parts.append({"quest":quest+1,"level":level+1,"x_prg_offset":xoffset,
                          "macro_prg_offset":macro,"macro_bytes":size})
    symbol="core_redux_dungeon_hud"
    source.append(f"const unsigned char {symbol}[864] = {{")
    source += ["    " + ",".join(f"0x{v:02X}" for v in records[i:i+16]) + "," for i in range(0,len(records),16)]
    source.append("};")
    manifest["blocks"].append({"symbol":symbol,"nes_bytes":864,"converted_bytes":864,
                              "parts":parts,"encoding":"xoffset_length_native_macro_padded48",
                              "converted_sha256":hashlib.sha256(records).hexdigest()})
    # script.asm bank1:$8000 contains42 CPU pointers. Follow each through
    # its actual end flag, including relocated text33..41 in free space.
    pointer_bytes=image[16+0x4000:16+0x4000+84]
    packed=bytearray();offsets=bytearray();parts=[]
    for entry in range(42):
        ptr=int.from_bytes(pointer_bytes[entry*2:entry*2+2],"little")
        if not 0x8000 <= ptr < 0xC000: raise ValueError("invalid Redux script pointer")
        start=0x4000+ptr-0x8000;cursor=start
        while cursor-start<256 and image[16+cursor]&0xC0!=0xC0: cursor+=1
        if cursor-start>=256: raise ValueError("unterminated Redux script entry")
        raw=image[16+start:16+cursor+1]
        offsets.extend(len(packed).to_bytes(2,"little"));packed.extend(raw)
        parts.append({"entry":entry,"prg_offset":start,"nes_bytes":len(raw),
                      "source_sha256":hashlib.sha256(raw).hexdigest()})
    for symbol,data,encoding,details in (
        ("core_redux_text_offsets",offsets,"packed_offsets_le16",
         {"prg_offset":0x4000,"source_sha256":hashlib.sha256(pointer_bytes).hexdigest()}),
        ("core_redux_text",packed,"native_script_control_codes",{"parts":parts}),
        ("core_redux_text_selectors",image[16+0x45A2:16+0x45A2+20],"native_selectors",
         {"prg_offset":0x45A2})):
        source.append(f"const unsigned char {symbol}[{len(data)}] = {{")
        source += ["    " + ",".join(f"0x{v:02X}" for v in data[i:i+16]) + "," for i in range(0,len(data),16)]
        source.append("};")
        manifest["blocks"].append({"symbol":symbol,"nes_bytes":len(data),"converted_bytes":len(data),
            "encoding":encoding,"converted_sha256":hashlib.sha256(data).hexdigest(),**details})
    # caution_screen.asm bank6:$ACD0: decode its real transfer program.
    # The exclusive mode8 screen borrows the map/dash range1320..1375.
    tiles=bytearray([0x24])*960;attrs=bytearray(64);cursor=0x1ACD0
    while image[16+cursor]!=0xFF:
        hi,lo,ctrl=image[16+cursor:16+cursor+3];cursor+=3
        count=ctrl&63 or 64;addr=(hi<<8)|lo
        if cursor-0x1ACD0>512: raise ValueError("unterminated caution program")
        for i in range(count):
            val=image[16+cursor+(0 if ctrl&64 else i)];pos=addr-0x2000
            if 0<=pos<960: tiles[pos]=val
            elif 960<=pos<1024: attrs[pos-960]=val
            # Redux 2025 placed its subscreen "TRIFORCE OF WISDOM" records at $ADA0,
            # where this program's $FF used to be, so the NES also writes them to
            # NT2 ($2B6A, $2BB0) here. They never show on the caution screen, and the
            # pause screen draws that label itself (inventory_render.c). Skip them.
            elif 0x0800<=pos<0x0C00: pass
            else: raise ValueError("unsupported caution transfer")
            addr+=32 if ctrl&128 else 1
        cursor+=1 if ctrl&64 else count
    def caution_pal(pos):
        row,col=divmod(pos,32)
        return (attrs[row//4*8+col//4]>>((row%4//2*2+col%4//2)*2))&3
    labels=(tiles[5*32+12:5*32+20],tiles[7*32+12:7*32+16],tiles[9*32+12:9*32+17])
    pairs={(tile,caution_pal(i)) for i,tile in enumerate(tiles) if tile!=0x24}
    pairs|={(tile,pal) for label in labels for tile in label for pal in (0,1)}
    pairs=[(0x24,0)]+sorted(pairs)
    if len(pairs)!=54: raise ValueError("caution glyph count differs from runtime linkage")
    lookup={pair:i for i,pair in enumerate(pairs)}
    caution_chr=b"".join(convert(image[16+0x877F+t*16:16+0x878F+t*16],pal*4) for t,pal in pairs)
    caution_map=bytes(0 if t==0x24 else lookup[t,caution_pal(i)] for i,t in enumerate(tiles))
    select=bytearray()
    for label in labels:
        for pal in (0,1): select.extend([lookup[t,pal] for t in label]+[0]*(8-len(label)))
    params=image[16+0x14AE5:16+0x14AE9]+image[16+0x14AF1:16+0x14AF4]+image[16+0x17F4B:16+0x17F4E]
    for symbol,data,encoding in (("core_redux_caution_chr",caution_chr,"biased_nes_4bpp"),
                                 ("core_redux_caution_map",caution_map,"glyph_indices_30x32"),
                                 ("core_redux_caution_select",select,"selection_indices_3x2x8"),
                                 ("core_redux_caution_params",params,"cursor_x_y3_flash_addresses3_attrs3")):
        source.append(f"const unsigned char {symbol}[{len(data)}] = {{")
        source += ["    " + ",".join(f"0x{v:02X}" for v in data[i:i+16]) + "," for i in range(0,len(data),16)]
        source.append("};")
        manifest["blocks"].append({"symbol":symbol,"nes_bytes":len(data),"converted_bytes":len(data),
            "encoding":encoding,"converted_sha256":hashlib.sha256(data).hexdigest(),
            "vram_tile_base":1320,"glyph_pairs":pairs,
            "parts":[{"prg_offset":0x877F+t*16,"nes_bytes":16,
                      "source_sha256":hashlib.sha256(image[16+0x877F+t*16:16+0x878F+t*16]).hexdigest()}
                     for t,pal in pairs],
            "source_program_prg_offset":0x1ACD0,"source_program_bytes":cursor-0x1ACD0+1,
            "source_program_sha256":hashlib.sha256(image[16+0x1ACD0:16+cursor+1]).hexdigest()})
    shared_intro(image, source, manifest)
    output.mkdir(parents=True, exist_ok=True)
    (output / "core_redux_chr.c").write_text("\n".join(source) + "\n", encoding="ascii")
    (output / "core_redux_chr.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="ascii")
    return manifest


def verify(output: Path) -> None:
    manifest = json.loads((output / "core_redux_chr.json").read_text())
    if manifest["rom_sha256"] != REDUX_SHA:
        raise ValueError("generated banks have an unsupported ROM owner")
    if manifest.get("generator_sha256") != hashlib.sha256(Path(__file__).read_bytes()).hexdigest():
        raise ValueError("Redux generator changed: rerun the user-ROM conversion")
    source = (output / "core_redux_chr.c").read_text()
    arrays = re.findall(r"const unsigned (char|short) (\w+)\[(\d+)\] = \{(.*?)\};", source, re.S)
    actual = {}
    for kind, name, size, body in arrays:
        width = 2 if kind == "short" else 1
        values = re.findall(r"0x([0-9A-F]{%d})" % (width * 2), body)
        actual[name] = (int(size) * width,
                        b"".join(int(v, 16).to_bytes(width, "big") for v in values))
    if len(actual) != 50 or len(manifest["blocks"]) != 50:
        raise ValueError("incomplete Redux bank manifest")
    for path, digest in manifest.get("intro_dependencies", {}).items():
        if hashlib.sha256((ROOT / path).read_bytes()).hexdigest() != digest:
            raise ValueError("intro dependency changed: rerun the user-ROM conversion")
    for block in manifest["blocks"]:
        size, data = actual[block["symbol"]]
        if size != len(data) or size != block.get("converted_bytes",block["nes_bytes"] * 2):
            raise ValueError(f"wrong bank size: {block['symbol']}")
        if hashlib.sha256(data).hexdigest() != block["converted_sha256"]:
            raise ValueError(f"stale generated bank: {block['symbol']}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--redux", type=Path)
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--out", type=Path, default=ROOT / "build/generated")
    args = parser.parse_args()
    try:
        if args.verify:
            verify(args.out)
            print("core Redux graphics: generated bank hashes verified")
            return 0
        if args.redux is None:
            parser.error("--redux is required for conversion")
        manifest = generate(args.redux, args.out)
    except (OSError, ValueError) as error:
        parser.exit(1, f"ERROR: {error}\n")
    print(f"core Redux graphics: {len(manifest['blocks'])} authenticated banks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
