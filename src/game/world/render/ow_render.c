#include "ow_render.h"
#include "render_abi.h"
#include "../../../../engine/src/vram_layout.h"
#include "../bg_palette.h"  /* Phase 12.2 promoted */
#include "../ow_palette.h"  /* Phase 12.2 promoted */
#include "../../../../engine/src/bg_sparse_chr.h"  /* Phase J: sparse atlas + LUT */
#include "platform_abi.h"  /* nes_ram, NES_PLAY_AREA_BASE, NES_TILE_COL_STRIDE */
#include "../world_dispatch.h"  /* world_get_shortcut_or_item_xy_for_room */

extern const unsigned char rooms_overworld[];
extern const unsigned char rooms_dungeons[];
extern const unsigned char common_chr[];
extern const unsigned char rooms_overworld_redux[];
extern const unsigned short rooms_overworld_redux_heap_offsets[16];

#define LEVEL_INFO_OW_OFFSET 768
#define OW_ATTRS_A_OFFSET    0
#define OW_ATTRS_B_OFFSET    128
#define OW_ATTRS_D_OFFSET    384
#define OW_LAYOUTS_OFFSET    1166
#define OW_HEAP_BLOB_OFFSET  3150

/* Phase 4: legacy LEVEL_INFO_PALETTE_OFFSET path replaced by live NES
 * PALRAM data (g_roomrom_ow_palram). Define kept for any out-of-tree
 * consumer; not used internally. */
#define LEVEL_INFO_PALETTE_OFFSET (LEVEL_INFO_OW_OFFSET + 3)

#define COMMON_BG_TILE_COUNT      112u
#define OW_BG_TILE_COUNT          130u
#define COMMON_MISC_TILE_COUNT    14u
#define REDUX_AUTOMAP_TILE_COUNT  32u
#define COMMON_BG_CHR_OFFSET      (112u * 32u)
#define COMMON_MISC_CHR_OFFSET    (224u * 32u)

/* PrimarySquaresOW from Z_05.asm line 5731 (56 entries).
 * NES accesses this table with the raw sq_idx (0-63); indices 56-63 fall into
 * SecondarySquaresOW[0..7] due to ROM table adjacency, so we replicate that here. */
static const unsigned char s_primary_squares[64] = {
    0x24,0x6F,0xF3,0xFA,0x98,0x90,0x8F,0x95,
    0x8E,0x90,0x74,0x76,0xF3,0x24,0x26,0x89,
    0x03,0x04,0x70,0xC8,0xBC,0x8D,0x8F,0x93,
    0x95,0xC4,0xCE,0xD8,0xB0,0xB4,0xAA,0xAC,
    0xB8,0x9C,0xA6,0x9A,0xA2,0xA0,0xE5,0xE6,
    0xE7,0xE8,0xE9,0xEA,0xC0,0xE0,0x78,0x7A,
    0x7E,0x80,0xCC,0xD0,0xD4,0xDC,0x89,0x84,
    0x24,0x24,0x24,0x24,0x6F,0x6F,0x6F,0x6F  /* SecondarySquaresOW[0..7] */
};

/* SecondarySquaresOW from Z_05.asm line 5740 */
static const unsigned char s_secondary_squares[64] = {
    0x24,0x24,0x24,0x24,0x6F,0x6F,0x6F,0x6F,
    0xF3,0xF3,0xF3,0xF3,0xFA,0xFA,0xFA,0xFA,
    0x98,0x95,0x26,0x26,0x90,0x95,0x90,0x95,
    0x8F,0x90,0x8F,0x90,0x95,0x96,0x95,0x96,
    0x8E,0x93,0x90,0x95,0x90,0x95,0x92,0x97,
    0x74,0x74,0x75,0x75,0x76,0x77,0x76,0x77,
    0xF3,0x24,0xF3,0x24,0x24,0x24,0x24,0x24,
    0x26,0x26,0x26,0x26,0x89,0x88,0x8B,0x88
};

static const unsigned char s_tile_object_primary_ow[6] = {
    0xC8,0xD8,0xC4,0xBC,0xC0,0xC0
};

static const unsigned char s_tile_object_primary_ow_redux[6] = {
    0xC8,0x58,0x5C,0xBC,0xC0,0xC0
};

static const unsigned char s_pal_to_attr[4] = {
    0x00,0x55,0xAA,0xFF
};

/* Heap byte offsets from data/rooms/MANIFEST.json "ow_heap_offsets" */
static const unsigned short s_heap_offsets[16] = {
    0,53,102,168,236,286,346,405,464,526,591,660,721,775,841,893
};

static unsigned char s_roomrom_map_id = ROOMROM_MAP_ORIGINAL;

/* S5 collision: walkable metatile grid for the current OW room. Filled
 * during fill_plane_a; queried by main loop pre-step.
 * 16 cols x 11 rows, 1 = walkable, 0 = blocking. */
static unsigned char s_walkable[16][11];

/* Task 5.4: raw NES BG tile id cache for the active 32x22 playfield.
 *
 * Populated as a side effect of fill_plane_a / fill_one_col_at. Cells are
 * indexed [tile_col][tile_row] in NES BG tile units (not metatiles). The
 * warp coordinator queries this through roomrom_ow_room_render_raw_tile_at
 * after Link's foot pixel is mapped to (col, row).
 *
 * SLICE-1 SEMANTICS:
 *   - "stable" = the cache reflects a complete fill_plane_a for the
 *     currently rendered room AND no partial column writes have happened
 *     since.
 *   - Single-column writes (scroll seam) flip the stable flag off; the
 *     coordinator must wait for the next full fill before checking warps.
 *   - Tile mutations from secrets, bombed rocks, burnt bushes, pushed
 *     blocks are NOT republished here. Those visual changes are out of
 *     scope for slice 1; future ticket adds a publish hook.
 */
#define ROOMROM_OW_RAW_TILE_COLS  32u  /* 16 metatiles x 2 tiles per metatile */
#define ROOMROM_OW_RAW_TILE_ROWS  22u  /* 11 metatiles x 2 tiles per metatile */
static unsigned char s_raw_tiles[ROOMROM_OW_RAW_TILE_COLS][ROOMROM_OW_RAW_TILE_ROWS];
static unsigned char s_raw_tiles_stable = 0u;

/* OW walkable NES tile IDs. Sourced from
 *   reference/aldonunez/Z_07.asm WalkableTiles ($8D,$91,$9C,$AC,$AD,$CC,$D2,$D5,$DF)
 * plus paths/sand/stairs/shore/redux variants observed in s_primary_squares
 * and s_secondary_squares_redux.
 *
 * Task 5.4 addition: $F3 (sand). NES OW $37 L1 entrance is metatile (7,4)
 * sq=$0C with BG tiles [$F3,$24,$F3,$24] — secondary metatile whose
 * tile_tl=$F3 is the renderer's primary_for_walk key. NES treats $F3 as
 * walkable (it's the sand path tile); without this, Link is blocked
 * before ever reaching the entrance. Verified against NES extraction
 * of LevelBlockOW.dat + RoomLayoutsOW.dat. */
static unsigned char ow_walkable_primary(unsigned char primary)
{
    switch (primary) {
        case 0x03: case 0x04:
        case 0x24: case 0x26:
        case 0x54: case 0x56: case 0x58: case 0x5C:
        case 0x6F: case 0x70:
        case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x84:
        case 0x8D:
        case 0x91:
        case 0x9C:
        case 0xAC: case 0xAD:
        case 0xCC:
        case 0xD2: case 0xD5: case 0xDF:
        case 0xF3:                          /* NES sand path (Z1 OW) */
            return 1u;
        default:
            return 0u;
    }
}

unsigned char roomrom_ow_room_render_walkable_at(unsigned char col,
                                                 unsigned char row)
{
    if (col >= 16u || row >= 11u) return 0u;
    return s_walkable[col][row];
}

/* NES Z1 OW BG-tile-id walkable check. Mirrors GetCollidableTile +
 * WalkableTiles list, plus path/sand additions seen in renderer
 * (primary_squares + secondary_squares cover lake/road/sand/forest-edge
 * tiles that NES accepts as walkable substrate). */
static unsigned char nes_ow_walkable_tile(unsigned char tile)
{
    switch (tile) {
        /* NES Z_07.asm WalkableTiles ($8D,$91,$9C,$AC,$AD,$CC,$D2,$D5,$DF) */
        case 0x8D: case 0x91: case 0x9C:
        case 0xAC: case 0xAD:
        case 0xCC:
        case 0xD2: case 0xD5: case 0xDF:
        /* Path/sand sub-tiles produced by primary_squares ($26 + neighbors,
         * sand $74-$77, $F3 path, $24 sand corner). */
        case 0x03: case 0x04:
        case 0x24: case 0x25: case 0x26: case 0x27:
        case 0x54: case 0x55: case 0x56: case 0x57:
        case 0x58: case 0x59: case 0x5A: case 0x5B:
        case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        case 0x6F: case 0x70: case 0x71:
        case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x84: case 0x85: case 0x86: case 0x87:
        case 0xF3:
            return 1u;
        default:
            return 0u;
    }
}

unsigned char roomrom_ow_room_render_walkable_tile_at(unsigned char tile_col,
                                                      unsigned char tile_row)
{
    if (tile_col >= ROOMROM_OW_RAW_TILE_COLS ||
        tile_row >= ROOMROM_OW_RAW_TILE_ROWS) {
        return 0u;
    }
    if (!s_raw_tiles_stable) return s_walkable[tile_col >> 1u][tile_row >> 1u];
    return nes_ow_walkable_tile(s_raw_tiles[tile_col][tile_row]);
}

static const unsigned char s_secondary_squares_redux[64] = {
    0x24,0x24,0x24,0x24,0x6F,0x6F,0x6F,0x6F,
    0xF3,0xF3,0xF3,0xF3,0xFA,0xFA,0xFA,0xFA,
    0xF3,0x24,0xF3,0x24,0x90,0x95,0x90,0x95,
    0x8F,0x90,0x8F,0x90,0x95,0x96,0x95,0x96,
    0x8E,0x93,0x90,0x95,0x90,0x95,0x92,0x97,
    0x74,0x74,0x75,0x75,0x76,0x77,0x76,0x77,
    0x54,0x24,0x56,0x24,0x24,0x24,0x24,0x24,
    0x26,0x26,0x26,0x26,0x89,0x88,0x8B,0x88
};

static const unsigned char *roomrom_rooms(void)
{
    return (s_roomrom_map_id == ROOMROM_MAP_REDUX) ? rooms_overworld_redux
                                                   : rooms_overworld;
}

static const unsigned short *roomrom_heap_offsets(void)
{
    return (s_roomrom_map_id == ROOMROM_MAP_REDUX) ? rooms_overworld_redux_heap_offsets
                                                   : s_heap_offsets;
}

static const unsigned char *roomrom_secondary_squares(void)
{
    return (s_roomrom_map_id == ROOMROM_MAP_REDUX) ? s_secondary_squares_redux
                                                   : s_secondary_squares;
}

void roomrom_ow_room_render_set_map(unsigned char map_id)
{
    s_roomrom_map_id = (map_id == ROOMROM_MAP_REDUX) ? ROOMROM_MAP_REDUX
                                                     : ROOMROM_MAP_ORIGINAL;
}

unsigned char roomrom_ow_room_render_get_map(void)
{
    return s_roomrom_map_id;
}

void roomrom_ow_room_render_load_palette(unsigned char room_id)
{
    unsigned char map = (s_roomrom_map_id == ROOMROM_MAP_REDUX) ? 1u : 0u;
    unsigned char palram[32];
    unsigned char i;
    for (i = 0u; i < 32u; ++i) palram[i] = g_roomrom_ow_palram[map][i];
    if (map == 0u) {
        /* NES Z_07.asm:@ChooseTileObjPalette chooses row 7 by tile-object
         * type and installed LevelBlockAttrsB. Z_06.asm transfer buffers
         * give the four exact bytes for each row. The old per-room scan
         * table misses the brown rock in $79; the live NES hurt capture
         * shows $0F/$17/$37/$12, not that table's default red row. */
        static const unsigned char k_ghost[4] = {0x0Fu, 0x30u, 0x00u, 0x12u};
        static const unsigned char k_green[4] = {0x0Fu, 0x1Au, 0x37u, 0x12u};
        static const unsigned char k_brown[4] = {0x0Fu, 0x17u, 0x37u, 0x12u};
        static const unsigned char k_red[4]   = {0x0Fu, 0x0Fu, 0x1Cu, 0x16u};
        const unsigned char *row = k_red;
        unsigned char tile_type, tile_x, tile_y;
        const unsigned char attrs_b = nes_ram[0x68FEu + room_id];
        roomrom_ow_room_tile_object(room_id, &tile_type, &tile_x, &tile_y);
        if (tile_type == 0x65u || (tile_type == 0x66u && (attrs_b & 1u)))
            row = k_ghost;
        else if (tile_type == 0x66u || (tile_type == 0x62u && !(attrs_b & 1u)))
            row = k_green;
        else if (tile_type == 0x62u)
            row = k_brown;
        for (i = 0u; i < 4u; ++i) palram[28u + i] = row[i];
    }
    /* PAL1 packs NES sprite sub-palettes 0..3 into slots 0..15. */
    roomrom_bg_palette_load_palram_full(palram);
    roomrom_ow_palette_patch_bg_per_room(room_id);
}

static unsigned char normalize_primary_tile(unsigned char raw)
{
    if (raw >= 0xE5 && raw <= 0xEA) {
        unsigned char idx = (unsigned char)(raw - 0xE5);
        if (s_roomrom_map_id == ROOMROM_MAP_REDUX)
            return s_tile_object_primary_ow_redux[idx];
        return s_tile_object_primary_ow[idx];
    }
    return raw;
}

void roomrom_ow_room_render_upload_chr(void)
{
    /* Phase J (2026-05-18): single sparse upload replaces 4x legacy bank.
     * bg_sparse_chr_orig_ow / bg_sparse_chr_redux_ow are pre-bias-encoded
     * flat tile arrays (one Genesis tile per used (tile_id, sub_pal)
     * combo). Slot allocation is universal across variants — only the
     * tile content differs. Renderer's tile_word() uses bg_sparse_tile_lut
     * to map NES (tile_id, sub_pal) -> slot. */
    const unsigned char redux = (s_roomrom_map_id == ROOMROM_MAP_REDUX);
    const unsigned char *blob = redux ? bg_sparse_chr_redux_ow
                                      : bg_sparse_chr_orig_ow;
    render_chr_upload((unsigned short)(ROOMROM_BG_TILE_BASE * 32u),
                      blob, BG_SPARSE_BLOB_BYTES);
}

static unsigned char ow_tile_palette(unsigned char tile_col, unsigned char tile_row,
                                     unsigned char outer_pal,
                                     unsigned char inner_pal)
{
    unsigned char attr_index = (unsigned char)(((tile_row >> 2) << 3) + (tile_col >> 2));
    unsigned char attr_col = attr_index & 0x07;
    unsigned char attr = s_pal_to_attr[outer_pal & 0x03];
    unsigned char inner_attr = s_pal_to_attr[inner_pal & 0x03];
    unsigned char shift = 0;

    if (attr_index >= 9 && attr_index < 0x27 && attr_col != 0 && attr_col != 7) {
        if (attr_index >= 0x21)
            attr = (unsigned char)((inner_attr & 0x0F) | (attr & 0xF0));
        else
            attr = inner_attr;
    }

    if (tile_col & 0x02)
        shift += 2;
    if (tile_row & 0x02)
        shift += 4;
    return (unsigned char)((attr >> shift) & 0x03);
}

static unsigned short tile_word(unsigned char raw_tile, unsigned char pal)
{
    /* Phase J (2026-05-18): sparse atlas LUT lookup. Pre-Phase-J this
     * used ROOMROM_BG_TILE_BASE_PAL(pal) + raw_tile (4x bank stride).
     * Post-J: universal LUT maps NES (tile_id, sub_pal) -> sparse slot.
     * Sentinel 0xFFFF (unused combo) -> tile 0 blank fallback per §36.1
     * MF3 (observability hook deferred to Phase Q telemetry probe). */
    unsigned short slot = bg_sparse_tile_lut[raw_tile][pal & 0x03u];
    if (slot == 0xFFFFu) {
        return ROOMROM_BLANK_TILE;
    }
    return (unsigned short)(ROOMROM_BG_TILE_BASE + slot);
}

/* Task 5.4: when set, write_tile_at also records the raw NES BG tile id
 * into s_raw_tiles. fill_plane_a brackets a full-room render with this
 * flag and sets s_raw_tiles_stable on completion. Single-column writers
 * leave it off so partial fills don't poison the cache. */
static unsigned char s_raw_tile_capture_active = 0u;

/* PR-2: target plane for nametable writes. 0=BG_A (default, current room),
 * 1=BG_B (V scroll staging slot for incoming room). Caller toggles via
 * roomrom_ow_room_render_set_target_plane before render_room_into_slot. */
static unsigned char s_target_plane = 0u;

void roomrom_ow_room_render_set_target_plane(unsigned char plane)
{
    s_target_plane = plane ? 1u : 0u;
}

static unsigned short wrapped_plane_row(unsigned short row)
{
    return (unsigned short)(row & (ROOMROM_PLANE_ROWS - 1u));
}

/* Palette uses src tile coords (where the tile semantically lives in its
 * source room). Plane write uses dst tile coords (where the tile actually
 * lands on the BG plane — supports off-room rendering during scroll). */
static void write_tile_at(unsigned char src_tile_col, unsigned char src_tile_row,
                          unsigned char dst_tile_col, unsigned char dst_tile_row,
                          unsigned char dst_row_base,
                          unsigned char raw_tile,
                          unsigned char outer_pal,
                          unsigned char inner_pal)
{
    unsigned char pal = ow_tile_palette(src_tile_col, src_tile_row,
                                        outer_pal, inner_pal);
    unsigned short row_addr = wrapped_plane_row(
        (unsigned short)(dst_row_base + dst_tile_row + ROOMROM_ROOM_FIRST_ROW));
    unsigned short word = tile_word(raw_tile, pal);
    if (s_target_plane) render_set_plane_b_word(dst_tile_col, row_addr, word);
    else                render_set_plane_a_word(dst_tile_col, row_addr, word);
    /* Cache key = SOURCE-room BG tile (0..31, 0..21), not plane dst col.
     * Plane placement varies by scroll slot, but the warp coordinator
     * checks tiles in the source-room coordinate space (link_x >> 3,
     * (link_y - HUD) >> 3). Keying by src_tile_col makes the cache
     * a logical room snapshot that is independent of slot 0 vs 1. */
    if (s_raw_tile_capture_active &&
        src_tile_col < ROOMROM_OW_RAW_TILE_COLS &&
        src_tile_row < ROOMROM_OW_RAW_TILE_ROWS) {
        s_raw_tiles[src_tile_col][src_tile_row] = raw_tile;
    }
}

/* Render a single source metatile column from `room_id` into plane
 * metatile column `dst_col`. `src_col` selects which column of the
 * source room (so palette/attribute logic uses src coords). Updates
 * s_walkable[dst_col] for collision queries. Plane writes wrap at 32
 * plane cols via SGDK's setTileMapXY.
 *
 * If `col_dirs_override` is non-NULL it replaces the OW unique-id
 * lookup (caves use this to substitute RoomLayoutOWCave0/1 — Z_05.asm
 * LayoutCaveAndAvanceSubmode @ 6020). Palette pattern still comes from
 * `room_id`'s OW attrs (NES uses room $44 for caves per Z_05.asm:6628
 * "An OW room that has the same NT attributes as a cave."). */
/* Set only while roomrom_cave_room_render_fill_plane_a is painting a cave
 * room, so render_one_metatile_col can pick the cave wall sub-pal (3)
 * without colliding with real OW room $44. */
static unsigned char s_rendering_cave = 0u;

/* T-050: NES LayoutRoomOrCaveOW column walk (Z_05.asm:5775). Fills the
 * $B square indexes (descriptor & $3F) of source column `src_col`. */
static void decode_column_squares(const unsigned char *heap_ptr,
                                  unsigned char col_in_heap,
                                  unsigned char out[11])
{
    unsigned short y = 0;
    unsigned char cols_found = col_in_heap;
    unsigned char row = 0;
    unsigned char repeat_state = 0;

    while (1) {
        if (heap_ptr[y] & 0x80) {
            if (cols_found == 0) break;
            cols_found--;
        }
        y++;
    }
    heap_ptr += y;

    while (row < 11) {
        unsigned char sq_byte = heap_ptr[0];
        out[row] = (unsigned char)(sq_byte & 0x3F);
        row++;
        if (sq_byte & 0x40) {
            repeat_state ^= 0x40;
            if (repeat_state != 0) continue;
        }
        heap_ptr++;
    }
}

static void column_squares(const unsigned char *col_dirs, unsigned char src_col,
                           unsigned char out[11])
{
    unsigned char desc=col_dirs[src_col];
    const unsigned char *heap=roomrom_rooms()+OW_HEAP_BLOB_OFFSET+
        roomrom_heap_offsets()[desc>>4];
    decode_column_squares(heap, desc&15u, out);
}

/* NES source: Z_05 LayoutCellarAndAdvanceSubmode / LayoutRoomOrCaveOW.
 * Drained C: existing OW column decoder/square writer.
 * Coverage: PARTIAL (UW cellar alternate heap); Stance: EXTEND.
 * Extracted offsets verified against data/rooms/MANIFEST.json. */
unsigned short roomrom_cellar_tile_word(unsigned char tile)
{
    if (tile == 0x6Fu) return ROOMROM_CELLAR_BG_TILE_BASE;
    if (tile == 0xFAu) return ROOMROM_CELLAR_BG_TILE_BASE + 1u;
    return tile_word(tile, 2u);
}

void roomrom_cellar_render(void)
{
    const unsigned char *layout=rooms_dungeons+0x1800u+
        ((nes_ram[0x69FEu+nes_ram[0xEBu]]&1u) ? 16u : 0u);
    unsigned char col,row,squares[11],i;
    static unsigned char patterns[64]; /* DMA source remains live through VBlank */
    for (i=0;i<64;++i) {
        unsigned char v=common_chr[(i<32 ? (112u+0x6Fu)*32u+i :
            (224u+0xFAu-0xF2u)*32u+i-32u)];
        unsigned char hi=v>>4,lo=v&15u;
        patterns[i]=(unsigned char)(((hi ? hi+8u : 0u)<<4)|(lo ? lo+8u : 0u));
    }
    render_chr_upload(ROOMROM_CELLAR_BG_TILE_BASE*32u,patterns,64u);
    for (col=0;col<16;++col) {
        decode_column_squares(rooms_dungeons+0x18FEu, layout[col]&15u, squares);
        for (row=0;row<11;++row) {
            unsigned char j,base=(unsigned char)(squares[row]*4u);
            for (j=0;j<4;++j) {
                unsigned char x=(unsigned char)(col*2u+(j>>1));
                unsigned char y=(unsigned char)(row*2u+(j&1));
                unsigned char t=s_secondary_squares[base+j];
                render_set_plane_a_word(x,(unsigned short)(ROOMROM_ROOM_FIRST_ROW+y),
                                        roomrom_cellar_tile_word(t));
                nes_ram[NES_PLAY_AREA_BASE+x*NES_TILE_COL_STRIDE+y]=t;
            }
        }
    }
}

static unsigned char ow_layout_id(unsigned char room_id)
{
    const unsigned char *rooms = roomrom_rooms();
    /* Z_05.asm:LayoutRoomOW reads the installed LevelBlockAttrsD. Quest 2
     * patches three entries after the base OW block is copied to SRAM. */
    return ((s_roomrom_map_id == ROOMROM_MAP_REDUX)
        ? rooms[OW_ATTRS_D_OFFSET + room_id]
        : nes_ram[0x69FEu + room_id]) & 0x7Fu;
}

static const unsigned char *ow_col_dirs(unsigned char room_id)
{
    const unsigned char *rooms = roomrom_rooms();
    return &rooms[OW_LAYOUTS_OFFSET + (unsigned short)ow_layout_id(room_id) * 16];
}

/* NES OW world flags (LevelInfo_WorldFlagsAddr = $067F for the OW):
 * bit $80 = secret found, bit $20 = visited / shortcut seen. */
#define NES_OW_WORLD_FLAGS 0x067Fu
static unsigned char ow_room_flags(unsigned char room_id)
{
    return nes_ram[NES_OW_WORLD_FLAGS + (room_id & 0x7Fu)];
}

/* LayoutRoomOW @LoopSquareOW secret substitution. In: square index `sq`.
 * Out: final square index ($0D) and the primary A that CheckTileObject
 * sees. A secret found in the room turns a tree ($E7) or special armos
 * ($EA) into stairs (square $10, primary $70) and a rock wall ($E6) into
 * a cave entrance: @MakeCave LDA #$0C / STA $0D, then PHA / PLA, so
 * CheckTileObject sees $0C and records no tile object (T-171: A kept $E6
 * made a bombed-open entrance a $63 tile object in slot $B; NES OW $00). */
static void resolve_layout_square(unsigned char flags, unsigned char sq,
                                  unsigned char *out_sq, unsigned char *out_a)
{
    unsigned char a = s_primary_squares[sq];
    if (flags & 0x80u) {
        if (a == 0xE7u || a == 0xEAu) {
            sq = 0x10u;
            a = 0x70u;
        } else if (a == 0xE6u) {
            sq = 0x0Cu;
            a = 0x0Cu;
        }
    }
    *out_sq = sq;
    *out_a = a;
}

/* NES TileObjectTypes (Z_05.asm, CheckTileObject), indexed by A - $E5. */
static const unsigned char s_tile_object_types[6] = {
    0x62u, 0x63u, 0x64u, 0x65u, 0x66u, 0x67u
};

/* Result of LayoutRoomOW + CheckShortcut for one OW room: the tile
 * object CheckTileObject recorded (last $E5-$EA primary in column-major
 * order), and the square CheckShortcut writes, if any. */
typedef struct {
    unsigned char room_id;
    unsigned char valid;
    unsigned char map_id, layout_id, flags;
    unsigned char obj_type, obj_x, obj_y;
    unsigned char shortcut;          /* 1 = CheckShortcut writes a square */
    unsigned char sc_col, sc_row;    /* metatile position */
    unsigned char sc_sq, sc_primary; /* WriteSquareOW $0D / A */
} ow_room_layout_t;
static ow_room_layout_t s_layout_cache;

static unsigned char layout_tile_tl(unsigned char sq, unsigned char a)
{
    if (sq >= 0x10u) return normalize_primary_tile(a);
    return roomrom_secondary_squares()[(unsigned char)(sq * 4u)];
}

static const ow_room_layout_t *ow_room_layout(unsigned char room_id)
{
    const unsigned char *col_dirs;
    unsigned char flags;
    unsigned char layout_id;
    unsigned char col, row;
    unsigned char sqs[11];
    ow_room_layout_t *L = &s_layout_cache;
    flags = ow_room_flags(room_id);
    layout_id = ow_layout_id(room_id);
    if (L->valid && L->room_id == room_id && L->map_id == s_roomrom_map_id &&
        L->flags == flags && L->layout_id == layout_id) return L;
    L->room_id = room_id;
    L->map_id = s_roomrom_map_id;
    L->flags = flags;
    L->layout_id = layout_id;
    L->obj_type = L->obj_x = L->obj_y = 0u;
    L->shortcut = 0u;
    col_dirs = ow_col_dirs(room_id);
    for (col = 0u; col < 16u; ++col) {
        column_squares(col_dirs, col, sqs);
        for (row = 0u; row < 11u; ++row) {
            unsigned char sq, a;
            resolve_layout_square(flags, sqs[row], &sq, &a);
            if (a >= 0xE5u && a <= 0xEAu) {
                L->obj_type = s_tile_object_types[a - 0xE5u];
                L->obj_x = (unsigned char)(col << 4);
                L->obj_y = (unsigned char)((row << 4) + 0x40u);
            }
        }
    }
    /* CheckShortcut (Z_05.asm, after LayoutRoomOW): secret not found but
     * the room was visited -> stairs where the shortcut lies, unless a
     * gravestone is there. */
    if (!(flags & 0x80u) && (flags & 0x20u)) {
        unsigned int xy = world_get_shortcut_or_item_xy_for_room(room_id);
        unsigned char sx = (unsigned char)(xy >> 8);
        unsigned char sy = (unsigned char)(xy & 0xFFu);
        unsigned char c = (unsigned char)(sx >> 4);
        unsigned char r = (unsigned char)((unsigned char)(sy - 0x40u) >> 4);
        unsigned char sq, a, tile;
        column_squares(col_dirs, c, sqs);
        resolve_layout_square(flags, sqs[r], &sq, &a);
        tile = layout_tile_tl(sq, a);
        L->sc_col = c;
        L->sc_row = r;
        L->sc_sq = 0x10u;
        L->sc_primary = 0x70u;
        L->shortcut = 1u;
        if (tile == 0xBCu) {
            L->shortcut = 0u;                     /* gravestone stays */
        } else if (tile == 0xD8u && L->obj_type != 0x62u) {
            L->obj_type = 0u;                     /* black square */
            L->sc_sq = 0x0Cu;
            L->sc_primary = a;
        }
    }
    L->valid = 1u;
    return L;
}

void roomrom_ow_room_tile_object(unsigned char room_id, unsigned char *type,
                                 unsigned char *x, unsigned char *y)
{
    const ow_room_layout_t *L;
    /* T-172: the cache key holds the live room flags (secret found /
     * visited), so it is reused here; only a LevelInfo install (other
     * quest's attributes) drops it (roomrom_ow_room_render_layout_drop).
     * Recomputing all 16 columns at every room entry made the entry tick
     * run two frames (t105_horizontal tick clock t221). */
    L = ow_room_layout(room_id);
    *type = L->obj_type;
    *x = L->obj_x;
    *y = L->obj_y;
}

/* Slot mapping of the room the tile cache describes: recorded while a
 * full fill is capturing, committed when the fill is marked stable.
 * roomrom_ow_room_render_set_tile uses it to edit the live room. */
static unsigned char s_fill_dst_col[16];
static unsigned char s_fill_row_base, s_fill_plane, s_fill_cave;
static unsigned char s_act_dst_col[16];
static unsigned char s_act_row_base, s_act_plane, s_act_cave, s_act_valid;
static unsigned char s_fill_outer, s_fill_inner, s_act_outer, s_act_inner;

/* Compute one source metatile column: the two plane-column words, the raw
 * NES tiles (raw[0] / raw[1] = left / right tile column) and the 11
 * metatile walkability bytes. Pure: no plane or cache writes. */
static void compute_metatile_col(unsigned char room_id,
                                 unsigned char src_col,
                                 const unsigned char *col_dirs_override,
                                 unsigned short col0[ROOMROM_ROOM_ROWS],
                                 unsigned short col1[ROOMROM_ROOM_ROWS],
                                 unsigned char raw[2][ROOMROM_ROOM_ROWS],
                                 unsigned char walk[11],
                                 unsigned char *outer_out,
                                 unsigned char *inner_out)
{
    const unsigned char *secondary_squares = roomrom_secondary_squares();
    /* T-115: NES FillPlayAreaAttrs: outer = LevelBlockAttrsA & 3, inner =
     * LevelBlockAttrsB & 3 per OW room (NES attribute tables byte-checked:
     * $76/$78 2/2, $79 3/3, $39 3/2). The 2026-05-23 "always $AA" rule
     * came from rooms that happen to be 2/2.
     * CAVE: NES cave attr (byte-verified, nes_6A CIRAM $3C0) puts the
     * border WALL tiles ($D8-$DB) on sub-pal 3 and the interior FLOOR
     * ($24) on sub-pal 2; ow_tile_palette() routes border -> outer_pal and
     * interior -> inner_pal. The cave flag is set by
     * roomrom_cave_room_render_fill_plane_a (CAVE_PALETTE_ROOM_ID $44
     * collides with real OW room $44, so we cannot gate on room_id). */
    const unsigned char *rooms = roomrom_rooms();
    unsigned char outer_pal = s_rendering_cave
        ? 3u : (unsigned char)(((s_roomrom_map_id == ROOMROM_MAP_REDUX)
            ? rooms[OW_ATTRS_A_OFFSET + room_id]
            : nes_ram[0x687Eu + room_id]) & 0x03u);
    unsigned char inner_pal = s_rendering_cave
        ? 2u : (unsigned char)(((s_roomrom_map_id == ROOMROM_MAP_REDUX)
            ? rooms[OW_ATTRS_B_OFFSET + room_id]
            : nes_ram[0x68FEu + room_id]) & 0x03u);
    const unsigned char *col_dirs = col_dirs_override
        ? col_dirs_override
        : ow_col_dirs(room_id);
    /* Secrets and shortcuts are OW-room layout only (caves have none). */
    const ow_room_layout_t *L = col_dirs_override ? (const ow_room_layout_t *)0
                                                  : ow_room_layout(room_id);
    unsigned char flags = col_dirs_override ? 0u : ow_room_flags(room_id);
    unsigned char sqs[11];
    unsigned char row;
    /* A square is one attribute quadrant (2x2 tiles, even-aligned), so its
     * palette is looked up once. */
    const unsigned char src_tc = (unsigned char)(src_col << 1);

    *outer_out = outer_pal;
    *inner_out = inner_pal;
    column_squares(col_dirs, src_col, sqs);

    for (row = 0; row < 11; row++) {
        unsigned char sq, a;
        unsigned char tile_tl, tile_bl, tile_tr, tile_br;
        unsigned char primary_for_walk;

        resolve_layout_square(flags, sqs[row], &sq, &a);
        if (L && L->shortcut && L->sc_col == src_col && L->sc_row == row) {
            sq = L->sc_sq;
            a = L->sc_primary;
        }
        if (sq >= 0x10) {
            unsigned char p = normalize_primary_tile(a);
            tile_tl = p;
            tile_bl = p + 1;
            tile_tr = p + 2;
            tile_br = p + 3;
            primary_for_walk = p;
        } else {
            unsigned char b = (unsigned char)(sq * 4);
            tile_tl = secondary_squares[b];
            tile_bl = secondary_squares[b + 1];
            tile_tr = secondary_squares[b + 2];
            tile_br = secondary_squares[b + 3];
            primary_for_walk = tile_tl;
        }

        {
            const unsigned char tr = (unsigned char)(row << 1);
            const unsigned char pal = ow_tile_palette(src_tc, tr,
                                                      outer_pal, inner_pal);
            col0[tr]      = tile_word(tile_tl, pal);
            col0[tr + 1u] = tile_word(tile_bl, pal);
            col1[tr]      = tile_word(tile_tr, pal);
            col1[tr + 1u] = tile_word(tile_br, pal);
            raw[0][tr]      = tile_tl;
            raw[0][tr + 1u] = tile_bl;
            raw[1][tr]      = tile_tr;
            raw[1][tr + 1u] = tile_br;
        }
        walk[row] = ow_walkable_primary(primary_for_walk);
    }
}

/* Record a computed column in the fill mapping, the raw-tile cache (keyed
 * by SOURCE-room tile, see write_tile_at) and walkability. */
static void store_metatile_col(unsigned char src_col, unsigned char dst_col,
                               unsigned char dst_row_base,
                               unsigned char raw[2][ROOMROM_ROOM_ROWS],
                               const unsigned char walk[11],
                               unsigned char outer_pal, unsigned char inner_pal)
{
    const unsigned char src_tc = (unsigned char)((src_col & 0x0Fu) << 1);
    unsigned char row;
    if (s_raw_tile_capture_active) {
        s_fill_dst_col[src_col & 0x0F] = dst_col;
        s_fill_row_base = dst_row_base;
        s_fill_plane = s_target_plane;
        s_fill_cave = s_rendering_cave;
        s_fill_outer = outer_pal;
        s_fill_inner = inner_pal;
        for (row = 0; row < ROOMROM_ROOM_ROWS; row++) {
            s_raw_tiles[src_tc][row]      = raw[0][row];
            s_raw_tiles[src_tc + 1u][row] = raw[1][row];
        }
    }
    for (row = 0; row < 11u; row++) s_walkable[dst_col & 0x0F][row] = walk[row];
}

/* T-125/T-134: stream both plane columns with one VDP address each (was
 * one per tile via write_tile_at: ~107 instructions a tile). */
static void write_metatile_col(unsigned char dst_col, unsigned char dst_row_base,
                               const unsigned short *col0,
                               const unsigned short *col1)
{
    const unsigned char dst_tc = (unsigned char)(dst_col << 1);
    const unsigned short first = wrapped_plane_row(
        (unsigned short)(dst_row_base + ROOMROM_ROOM_FIRST_ROW));
    unsigned char row;
    if (s_target_plane) {
        for (row = 0; row < ROOMROM_ROOM_ROWS; row++) {
            unsigned short r = wrapped_plane_row((unsigned short)(first + row));
            render_set_plane_b_word(dst_tc, r, col0[row]);
            render_set_plane_b_word((unsigned short)(dst_tc + 1u), r, col1[row]);
        }
    } else {
        render_plane_a_write_col(dst_tc, first, col0, ROOMROM_ROOM_ROWS,
                                 ROOMROM_PLANE_ROWS);
        render_plane_a_write_col((unsigned short)(dst_tc + 1u), first, col1,
                                 ROOMROM_ROOM_ROWS, ROOMROM_PLANE_ROWS);
    }
}

/* T-134/T-135: columns computed ahead of a load while the playfield is
 * black (cave entry: the cave; cave exit: the OW room). One set of buffers,
 * owned by whichever was prepared last. */
static unsigned short s_prep_rows[ROOMROM_ROOM_ROWS][32];
static unsigned char  s_prep_raw[16][2][ROOMROM_ROOM_ROWS];
static unsigned char  s_prep_walk[16][11];
static unsigned char  s_prep_outer = 0u, s_prep_inner = 0u;
static unsigned char  s_cave_prep_id = 0xFFu;   /* cave id, 0xFF = none */
static unsigned char  s_ow_prep_room = 0xFFu;   /* OW room, 0xFF = none */
static unsigned char  s_prep_cols = 0u;

static void prep_store_col(unsigned char col, const unsigned short *col0,
                           const unsigned short *col1)
{
    unsigned char row;
    for (row = 0; row < ROOMROM_ROOM_ROWS; row++) {
        s_prep_rows[row][col << 1]        = col0[row];
        s_prep_rows[row][(col << 1) + 1u] = col1[row];
    }
}

static void render_one_metatile_col(unsigned char room_id,
                                    unsigned char src_col,
                                    unsigned char dst_col,
                                    unsigned char dst_row_base,
                                    const unsigned char *col_dirs_override)
{
    unsigned short col0[ROOMROM_ROOM_ROWS];
    unsigned short col1[ROOMROM_ROOM_ROWS];
    unsigned char raw[2][ROOMROM_ROOM_ROWS];
    unsigned char walk[11];
    unsigned char outer_pal, inner_pal;
    if (col_dirs_override == (const unsigned char *)0 &&
        room_id == s_ow_prep_room && src_col < s_prep_cols) {
        unsigned char row;
        for (row = 0; row < ROOMROM_ROOM_ROWS; row++) {
            col0[row] = s_prep_rows[row][src_col << 1];
            col1[row] = s_prep_rows[row][(src_col << 1) + 1u];
        }
        store_metatile_col(src_col, dst_col, dst_row_base, s_prep_raw[src_col],
                           s_prep_walk[src_col], s_prep_outer, s_prep_inner);
        write_metatile_col(dst_col, dst_row_base, col0, col1);
        return;
    }
    compute_metatile_col(room_id, src_col, col_dirs_override, col0, col1,
                         raw, walk, &outer_pal, &inner_pal);
    store_metatile_col(src_col, dst_col, dst_row_base, raw, walk,
                       outer_pal, inner_pal);
    write_metatile_col(dst_col, dst_row_base, col0, col1);
}

void roomrom_ow_room_render_prepare(unsigned char room_id, unsigned char max_cols)
{
    if (s_ow_prep_room != room_id) {
        s_ow_prep_room = room_id;
        s_cave_prep_id = 0xFFu;
        s_prep_cols = 0u;
    }
    while (max_cols-- && s_prep_cols < 16u) {
        const unsigned char col = s_prep_cols;
        unsigned short col0[ROOMROM_ROOM_ROWS];
        unsigned short col1[ROOMROM_ROOM_ROWS];
        compute_metatile_col(room_id, col, (const unsigned char *)0, col0, col1,
                             s_prep_raw[col], s_prep_walk[col],
                             &s_prep_outer, &s_prep_inner);
        prep_store_col(col, col0, col1);
        ++s_prep_cols;
    }
}

void roomrom_ow_room_render_layout_drop(void)
{
    s_layout_cache.valid = 0u;
}

/* Compute the room's layout summary ahead of InitMode_EnterRoom (on a
 * scroll tick with spare time). */
void roomrom_ow_room_render_prepare_layout(unsigned char room_id)
{
    (void)ow_room_layout(room_id);
}

/* T-172: the whole room from a complete precompute (s_prep_rows is row
 * major): the same caches as 16 fill_one_col_at calls for plane metatile
 * columns dst_col0..+15, written a plane row at a time. 0 = not available. */
unsigned char roomrom_ow_room_render_fill_room_prepared(unsigned char room_id,
                                                        unsigned char dst_col0,
                                                        unsigned char dst_row_base)
{
    unsigned char c, row;
    unsigned short first;
    if (s_target_plane || room_id != s_ow_prep_room || s_prep_cols < 16u ||
        dst_col0 > 16u)
        return 0u;
    s_raw_tiles_stable = 0u;
    for (c = 0u; c < 16u; ++c)
        store_metatile_col(c, (unsigned char)((dst_col0 + c) & 0x1Fu), dst_row_base,
                           s_prep_raw[c], s_prep_walk[c], s_prep_outer, s_prep_inner);
    first = wrapped_plane_row((unsigned short)(dst_row_base + ROOMROM_ROOM_FIRST_ROW));
    for (row = 0u; row < ROOMROM_ROOM_ROWS; ++row)
        render_plane_a_write_run((unsigned short)(dst_col0 << 1),
                                 wrapped_plane_row((unsigned short)(first + row)),
                                 s_prep_rows[row], 32u);
    return 1u;
}

void roomrom_ow_room_render_prepare_drop(void)
{
    s_ow_prep_room = 0xFFu;
    s_cave_prep_id = 0xFFu;
    s_prep_cols = 0u;
}

static void commit_fill_mapping(void)
{
    unsigned char c;
    for (c = 0u; c < 16u; ++c) s_act_dst_col[c] = s_fill_dst_col[c];
    s_act_row_base = s_fill_row_base;
    s_act_plane = s_fill_plane;
    s_act_cave = s_fill_cave;
    s_act_outer = s_fill_outer;
    s_act_inner = s_fill_inner;
    s_act_valid = 1u;
}

unsigned char roomrom_ow_room_render_set_tile(unsigned char tile_col,
                                              unsigned char tile_row,
                                              unsigned char raw_tile)
{
    unsigned char dst_col, saved_plane;
    if (!s_act_valid || tile_col >= ROOMROM_OW_RAW_TILE_COLS ||
        tile_row >= ROOMROM_OW_RAW_TILE_ROWS) {
        return 0u;
    }
    dst_col = s_act_dst_col[tile_col >> 1];
    saved_plane = s_target_plane;
    s_target_plane = s_act_plane;
    write_tile_at(tile_col, tile_row,
                  (unsigned char)((dst_col << 1) | (tile_col & 1u)), tile_row,
                  s_act_row_base, raw_tile, s_act_outer, s_act_inner);
    s_target_plane = saved_plane;
    s_raw_tiles[tile_col][tile_row] = raw_tile;
    if (((tile_col | tile_row) & 1u) == 0u) {
        s_walkable[dst_col & 0x0F][tile_row >> 1] = ow_walkable_primary(raw_tile);
    }
    return 1u;
}

void roomrom_ow_room_render_fill_one_col(unsigned char room_id,
                                         unsigned char src_col,
                                         unsigned char dst_col)
{
    roomrom_ow_room_render_fill_one_col_at(room_id, src_col, dst_col, 0);
}

void roomrom_ow_room_render_fill_one_col_at(unsigned char room_id,
                                            unsigned char src_col,
                                            unsigned char dst_col,
                                            unsigned char dst_row_base)
{
    /* Single-column write: cache is no longer a coherent snapshot of
     * one room; flip stability off until the next full fill_plane_a. */
    s_raw_tiles_stable = 0u;
    render_one_metatile_col(room_id, src_col & 0x0F, dst_col & 0x1F,
                            dst_row_base, (const unsigned char *)0);
}

void roomrom_ow_room_render_fill_plane_a(unsigned char room_id)
{
    unsigned char col;
    s_raw_tiles_stable = 0u;
    s_raw_tile_capture_active = 1u;
    for (col = 0; col < 16; col++) {
        render_one_metatile_col(room_id, col, col, 0, (const unsigned char *)0);
    }
    s_raw_tile_capture_active = 0u;
    s_raw_tiles_stable = 1u;
    commit_fill_mapping();
}

/* NES cave column-desc tables (Z_05.asm RoomLayoutOWCave0/1 @ 4198/4202).
 *
 * Each byte is a column descriptor that indexes ColumnHeapOW (the same
 * heap OW rooms use): high nibble = heap_idx, low nibble = col_in_heap.
 *
 *   Cave0 (regular cave  — Z_05.asm LayoutCaveAndAvanceSubmode X=0):
 *     boundary | floor wall | floor wall | door | door | floor | boundary
 *   Cave1 (shortcut cave — Z_05.asm LayoutShortcutAndAdvanceSubmode X=2):
 *     same with stairs/secret variations
 *
 * Palette pattern comes from OW room $44 ("An OW room that has the same
 * NT attributes as a cave." — Z_05.asm:6628 FillPlayAreaAttrs). */
static const unsigned char k_cave_layout_regular[16] = {
    0x00u, 0x00u, 0x95u, 0x95u, 0x95u, 0x95u, 0x95u, 0xC2u,
    0xC2u, 0x95u, 0x95u, 0x95u, 0x95u, 0x95u, 0x00u, 0x00u
};

static const unsigned char k_cave_layout_shortcut[16] = {
    0x00u, 0x00u, 0x95u, 0x95u, 0x95u, 0xF8u, 0x95u, 0xC2u,
    0xF8u, 0x95u, 0x95u, 0xF8u, 0x95u, 0x95u, 0x00u, 0x00u
};

/* Cave palette source room: NES uses OW room $44 attrs for cave NT
 * pattern (Z_05.asm:6628). */
#define CAVE_PALETTE_ROOM_ID 0x44u

/* HandleWarpOW selects Mode $0C only for selector $50. Its cave index
 * is ($50-$40)/4=4, hence cave id $6E. Cave ids $7B-$7D are ordinary
 * one-time rupee rooms and use the regular Mode $0B layout. */
static const unsigned char *cave_layout_for(unsigned char cave_id)
{
    if (cave_id == 0x6Eu) return k_cave_layout_shortcut;
    return k_cave_layout_regular;
}

/* T-134: NES lays the cave out while its playfield is black (mode $0B
 * submodes 2-7) and shows it at once. The cave's words / raw tiles /
 * walkability are computed during the load hold (prepare), and the swap
 * stores them and queues the plane rows for the next VBlank DMA, so the
 * cave appears in one frame. */
void roomrom_cave_room_render_prepare(unsigned char cave_id,
                                      unsigned char max_cols)
{
    const unsigned char *layout = cave_layout_for(cave_id);
    if (s_cave_prep_id != cave_id) {
        s_cave_prep_id = cave_id;
        s_ow_prep_room = 0xFFu;
        s_prep_cols = 0u;
    }
    s_rendering_cave = 1u;   /* cave wall cells -> sub-pal 3 (orange/brown) */
    while (max_cols-- && s_prep_cols < 16u) {
        const unsigned char col = s_prep_cols;
        unsigned short col0[ROOMROM_ROOM_ROWS];
        unsigned short col1[ROOMROM_ROOM_ROWS];
        compute_metatile_col(CAVE_PALETTE_ROOM_ID, col, layout, col0, col1,
                             s_prep_raw[col], s_prep_walk[col],
                             &s_prep_outer, &s_prep_inner);
        prep_store_col(col, col0, col1);
        ++s_prep_cols;
    }
    s_rendering_cave = 0u;
}

void roomrom_cave_room_render_fill_plane_a(unsigned char cave_id)
{
    unsigned char col, row;
    roomrom_cave_room_render_prepare(cave_id, 16u);   /* any columns left */
    s_raw_tiles_stable = 0u;
    s_raw_tile_capture_active = 1u;
    s_rendering_cave = 1u;
    for (col = 0; col < 16; col++) {
        store_metatile_col(col, col, 0u, s_prep_raw[col], s_prep_walk[col],
                           3u, 2u);
    }
    s_rendering_cave = 0u;
    s_raw_tile_capture_active = 0u;
    s_raw_tiles_stable = 1u;
    commit_fill_mapping();
    for (row = 0; row < ROOMROM_ROOM_ROWS; row++) {
        const unsigned short r = wrapped_plane_row(
            (unsigned short)(ROOMROM_ROOM_FIRST_ROW + row));
        if (s_target_plane || !render_plane_a_queue_row(r, 0u, s_prep_rows[row], 32u)) {
            for (col = 0; col < 32u; col++) {
                if (s_target_plane) render_set_plane_b_word(col, r, s_prep_rows[row][col]);
                else                render_set_plane_a_word(col, r, s_prep_rows[row][col]);
            }
        }
    }
    /* Rows stay valid until the VBlank DMA; the next prepare reclaims. */
    s_cave_prep_id = 0xFFu;
    s_prep_cols = 0u;
}

unsigned char roomrom_ow_room_render_raw_tile_at(unsigned char tile_col,
                                                 unsigned char tile_row)
{
    if (tile_col >= ROOMROM_OW_RAW_TILE_COLS ||
        tile_row >= ROOMROM_OW_RAW_TILE_ROWS) {
        return 0u;
    }
    return s_raw_tiles[tile_col][tile_row];
}

unsigned char roomrom_ow_room_render_is_stable(void)
{
    return s_raw_tiles_stable;
}

/* Task 5.4: callers using fill_one_col_at for a full 16-column room
 * paint (e.g. main.c::render_room_into_slot in load_room) bracket the
 * loop with begin/mark to declare the cache stable on the active slot.
 *
 *   begin: clears cache + stable flag and turns capture on so each
 *          fill_one_col_at populates the raw-tile cache.
 *   mark:  turns capture off and asserts stable. */
void roomrom_ow_room_render_begin_full_fill(void)
{
    s_raw_tiles_stable = 0u;
    s_raw_tile_capture_active = 1u;
    /* The layout cache stays: its key holds the room's live world flags
     * (and LevelInfo installs drop it), so a full fill right after the
     * palette's lookup reuses it; the 16-column decode is ~1.4 frames
     * (cave exit InitMode4 lag, T-171). */
}

void roomrom_ow_room_render_mark_stable(void)
{
    s_raw_tile_capture_active = 0u;
    s_raw_tiles_stable = 1u;
    commit_fill_mapping();
}

/* Task 5.4 cache export for BizHawk Lua probes.
 * Layout @ $FF7400:
 *   off 0..1: magic 'T','C'
 *   off 2:    cols (32)
 *   off 3:    rows (22)
 *   off 4..:  raw tile bytes, column-major: cache[col*22 + row]
 * Total: 4 + 704 = 708 bytes. */
#define ROOMROM_OW_RAW_TILE_PROBE_BASE 0x00FF7400UL

void roomrom_ow_room_render_publish_cache(void)
{
    volatile unsigned char *p =
        (volatile unsigned char *)ROOMROM_OW_RAW_TILE_PROBE_BASE;
    unsigned char col, row;

    p[0] = 0x54u;  /* 'T' */
    p[1] = 0x43u;  /* 'C' */
    p[2] = (unsigned char)ROOMROM_OW_RAW_TILE_COLS;
    p[3] = (unsigned char)ROOMROM_OW_RAW_TILE_ROWS;

    for (col = 0; col < ROOMROM_OW_RAW_TILE_COLS; col++) {
        for (row = 0; row < ROOMROM_OW_RAW_TILE_ROWS; row++) {
            p[4u + (unsigned short)col * ROOMROM_OW_RAW_TILE_ROWS + row] =
                s_raw_tiles[col][row];
        }
    }
}

/* T0.1 cave entry: mirror raw-tile cache into nes_ram PlayAreaTiles.
 *
 * NES Z_07.asm PlayAreaTiles ($6530, NES SRAM). Column-major layout:
 *   col 0 = $6530..$6545 (22 rows)
 *   col c = $6530 + c*$16 + row
 *
 * collision_get_collidable_tile_still (drain GetCollidableTile) reads
 * nes_ram[col_addr + row_idx]; without this publisher, that memory is
 * always zero so HandleWarpOW never sees cave/stairs tiles ($24/$88/
 * $70..$73). After this runs, cave_entrance_check fires correctly when
 * Link stands on an entrance tile. */
extern void sgdk_memcpy(void *to, const void *from, unsigned short len) __asm__("memcpy");
void roomrom_ow_room_render_publish_play_area_tiles(void)
{
    /* PlayAreaTiles is column-major with a $16-byte column, the layout of
     * s_raw_tiles[32][22]: one block copy. T-172: the byte loop cost
     * 4.2k instructions in the room-entry tick (two frames). */
    _Static_assert(NES_TILE_COL_STRIDE == ROOMROM_OW_RAW_TILE_ROWS,
                   "PlayAreaTiles column stride");
    sgdk_memcpy(&nes_ram[NES_PLAY_AREA_BASE], s_raw_tiles,
                (unsigned short)sizeof s_raw_tiles);
}

/* Old monolithic body kept for reference until verified equivalent; now dead. */
#if 0
void roomrom_ow_room_render_fill_plane_a_OLD(unsigned char room_id)
{
    unsigned char unique_id;
    unsigned char outer_pal;
    unsigned char inner_pal;
    const unsigned char *col_dirs;
    const unsigned char *rooms = roomrom_rooms();
    const unsigned short *heap_offsets = roomrom_heap_offsets();
    const unsigned char *secondary_squares = roomrom_secondary_squares();
    unsigned char col;

    outer_pal = rooms[OW_ATTRS_A_OFFSET + room_id] & 0x03;
    inner_pal = rooms[OW_ATTRS_B_OFFSET + room_id] & 0x03;
    unique_id = rooms[OW_ATTRS_D_OFFSET + room_id] & 0x7F;
    col_dirs  = &rooms[OW_LAYOUTS_OFFSET + (unsigned short)unique_id * 16];

    for (col = 0; col < 16; col++) {
        unsigned char desc       = col_dirs[col];
        unsigned char heap_idx   = (desc >> 4) & 0x0F;
        unsigned char col_in_heap = desc & 0x0F;
        const unsigned char *heap_ptr;
        unsigned short y;
        unsigned char cols_found;
        unsigned char row;
        unsigned char repeat_state;

        heap_ptr = &rooms[OW_HEAP_BLOB_OFFSET + heap_offsets[heap_idx]];

        /* Each column block opens with a bit7-set byte that doubles as the first tile row */
        y = 0;
        cols_found = col_in_heap;
        while (1) {
            if (heap_ptr[y] & 0x80) {
                if (cols_found == 0) break;
                cols_found--;
            }
            y++;
        }
        heap_ptr += y;

        row = 0;
        repeat_state = 0;
        while (row < 11) {
            unsigned char sq_byte = heap_ptr[0];
            unsigned char sq_idx  = sq_byte & 0x3F;
            unsigned char tile_tl, tile_bl, tile_tr, tile_br;
            unsigned char primary_for_walk;

            if (sq_idx >= 0x10) {
                unsigned char p = normalize_primary_tile(s_primary_squares[sq_idx]);
                tile_tl = p;
                tile_bl = p + 1;
                tile_tr = p + 2;
                tile_br = p + 3;
                primary_for_walk = p;
            } else {
                unsigned char b = (unsigned char)(sq_idx * 4);
                tile_tl = secondary_squares[b];
                tile_bl = secondary_squares[b + 1];
                tile_tr = secondary_squares[b + 2];
                tile_br = secondary_squares[b + 3];
                /* Classify secondary squares by their TL tile id (close enough
                 * for v1; secondary squares are rare and mostly path/edge). */
                primary_for_walk = tile_tl;
            }

            write_square(col, row, tile_tl, tile_bl, tile_tr, tile_br,
                         outer_pal, inner_pal);
            s_walkable[col][row] = ow_walkable_primary(primary_for_walk);
            row++;

            if (sq_byte & 0x40) {
                repeat_state ^= 0x40;
                if (repeat_state != 0) continue;
            }
            heap_ptr++;
        }
    }
}
#endif
