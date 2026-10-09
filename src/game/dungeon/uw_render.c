/* Phase 12.2 SGDK-1 cleanup: dropped <genesis.h>; route VRAM-read
 * via render_vram_read_word adapter. */
#include "uw_render.h"
#include "../../../RoomRom/src/uw_room_blob.h"
#include "render_abi.h"
#include "platform_abi.h"  /* nes_ram, NES_PLAY_AREA_BASE (T-117) */
#include "../../../RoomRom/src/roomrom_vram_map.h"
#include "../world/bg_palette.h"  /* Phase 12.2 promoted */
#include "../../../RoomRom/src/bg_sparse_chr.h"  /* Phase J: sparse atlas + LUT */
#include "../../../RoomRom/src/uw_collision_data.h"

extern const unsigned char rooms_dungeons[];

#define UW_LEVELBLOCK_SIZE       768u
#define UW_LEVELINFO_SIZE        256u
#define UW_LEVELINFO_BASE        3072u
#define UW_LEVELINFO_PAL_OFFSET  3u

#define COMMON_BG_TILE_COUNT     112u
#define UW_BG_TILE_COUNT         130u
#define COMMON_MISC_TILE_COUNT   14u
#define COMMON_BG_CHR_OFFSET     (112u * 32u)
#define COMMON_MISC_CHR_OFFSET   (224u * 32u)
#define ROOMROM_SHARED_PLANE_BASE 0xC000u
#define ROOMROM_SHARED_PLANE_COLS 64u
#define UW_DOOR_PRIORITY_CACHE_SLOTS 2u
#define UW_DOOR_PRIORITY_CACHE_MAX   128u
#define UW_DOOR_PRIORITY_CACHE_NONE  0xFFu
/* Phase 12.2 SGDK-1 cleanup: local VDP MMIO macros dropped; VRAM
 * reads now go through render_vram_read_word adapter. */

static unsigned char s_uw_map_id = ROOMROM_MAP_ORIGINAL;
static unsigned char s_uw_level  = 1u;
static unsigned char s_uw_quest  = 1u;

/* Cached AT pointer for the current room's blob; set by blit_blob,
 * cleared by draw_placeholder. Used by roomrom_uw_room_render_palette_at. */
static const unsigned char *s_cur_attr = (const unsigned char *)0;

/* S5.5 collision: walkable metatile grid for the current UW room.
 * Filled during blit_blob; queried by main loop. 16 cols x 11 rows. */
static unsigned char s_uw_walkable[16][11];

/* NES collision samples the live UW play area at 8px tile granularity.
 * Keep this beside the metatile grid so door patches and false-wall state
 * affect both collision paths. */
static unsigned char s_uw_tile_walkable[32][22];

/* Vertical scroll priority patching only needs known door-art cells. Caching
 * them as each room is rendered avoids scanning the full live nametable during
 * transition setup/finalize. */
static unsigned char s_door_priority_cache_valid[UW_DOOR_PRIORITY_CACHE_SLOTS];
static unsigned char s_door_priority_cache_slot_x[UW_DOOR_PRIORITY_CACHE_SLOTS];
static unsigned char s_door_priority_cache_row_base[UW_DOOR_PRIORITY_CACHE_SLOTS];
static unsigned char s_door_priority_cache_count[UW_DOOR_PRIORITY_CACHE_SLOTS];
static unsigned char s_door_priority_cache_col[UW_DOOR_PRIORITY_CACHE_SLOTS]
                                                 [UW_DOOR_PRIORITY_CACHE_MAX];
static unsigned char s_door_priority_cache_row[UW_DOOR_PRIORITY_CACHE_SLOTS]
                                                 [UW_DOOR_PRIORITY_CACHE_MAX];
static unsigned char s_door_priority_cache_build = UW_DOOR_PRIORITY_CACHE_NONE;
static unsigned char s_door_priority_cache_next = 0u;

/* NES UW tile classifier. Captured blob nt[] stores NES tile IDs from the
 * visible play area, and live NES PlayAreaTiles for room $73 match the
 * direct 8x8 rendered tile threshold exactly: tile < $78 walks.
 * ObjectFirstUnwalkableTile lives at NES RAM $034A. */
static unsigned char uw_walkable_tile_id(unsigned char t)
{
    return (t < 0x78u) ? 1u : 0u;
}

/* T-117: NES PlayAreaTiles ($6530, column-major, 22 rows per column).
 * NES LayoutRoomUW fills it with the room's 32x22 play-area tiles and
 * LayOutDoors / door updates write door faces into it; the drained
 * collision code (GetCollidableTile) reads it. Every raw UW tile write
 * for the current room's source cell mirrors here. */
static void play_area_set(unsigned char col, unsigned char row,
                          unsigned char raw)
{
    if (col >= 32u || row >= 22u) return;
    nes_ram[NES_PLAY_AREA_BASE + (unsigned short)col * NES_TILE_COL_STRIDE + row] = raw;
}

static void set_collision_metatile(unsigned char col,
                                   unsigned char row,
                                   unsigned char walk)
{
    unsigned char tile_col;
    unsigned char tile_row;

    if (col >= 16u || row >= 11u) return;
    walk = walk ? 1u : 0u;
    tile_col = (unsigned char)(col * 2u);
    tile_row = (unsigned char)(row * 2u);
    s_uw_walkable[col][row] = walk;
    s_uw_tile_walkable[tile_col][tile_row] = walk;
    s_uw_tile_walkable[(unsigned char)(tile_col + 1u)][tile_row] = walk;
    s_uw_tile_walkable[tile_col][(unsigned char)(tile_row + 1u)] = walk;
    s_uw_tile_walkable[(unsigned char)(tile_col + 1u)]
                      [(unsigned char)(tile_row + 1u)] = walk;
}

static void set_walkable_metatile_only(unsigned char col,
                                       unsigned char row,
                                       unsigned char walk)
{
    if (col >= 16u || row >= 11u) return;
    s_uw_walkable[col][row] = walk ? 1u : 0u;
}

unsigned char roomrom_uw_room_render_walkable_at(unsigned char col,
                                                 unsigned char row)
{
    if (col >= 16u || row >= 11u) return 0u;
    return s_uw_walkable[col][row];
}

unsigned char roomrom_uw_room_render_walkable_tile_at(unsigned char col,
                                                      unsigned char row)
{
    if (col >= 32u || row >= 22u) return 0u;
    return s_uw_tile_walkable[col][row];
}

void roomrom_uw_room_render_set_map(unsigned char map_id)
{
    s_uw_map_id = (map_id == ROOMROM_MAP_REDUX) ? ROOMROM_MAP_REDUX
                                                 : ROOMROM_MAP_ORIGINAL;
}

unsigned char roomrom_uw_room_render_get_map(void)
{
    return s_uw_map_id;
}

void roomrom_uw_room_render_set_level(unsigned char level)
{
    if (level < ROOMROM_UW_LEVEL_MIN) level = ROOMROM_UW_LEVEL_MIN;
    if (level > ROOMROM_UW_LEVEL_MAX) level = ROOMROM_UW_LEVEL_MAX;
    s_uw_level = level;
}

unsigned char roomrom_uw_room_render_get_level(void)
{
    return s_uw_level;
}

void roomrom_uw_room_render_set_quest(unsigned char quest)
{
    if (quest < ROOMROM_UW_QUEST_MIN) quest = ROOMROM_UW_QUEST_MIN;
    if (quest > ROOMROM_UW_QUEST_MAX) quest = ROOMROM_UW_QUEST_MAX;
    s_uw_quest = quest;
}

unsigned char roomrom_uw_room_render_get_quest(void)
{
    return s_uw_quest;
}

static int find_blob_entry(unsigned char level, unsigned char room_id)
{
    unsigned char want_map = (s_uw_map_id == ROOMROM_MAP_REDUX) ? 1u : 0u;
    unsigned short idx_plus_one;
    if (s_uw_quest >= 3u || level >= 10u || room_id >= 128u) return -1;
    idx_plus_one = g_uw_room_lookup[want_map][s_uw_quest][level][room_id];
    if (idx_plus_one != 0u) return (int)(idx_plus_one - 1u);
    /* Same-block LAYOUT fallback (2026-05-31). NES UW room layouts are
     * per-LevelBlock: levels 1-6 share LevelBlockUW1, 7-9 share UW2 (per
     * quest). So a room captured under ANY sibling level in this block has
     * the byte-identical layout. The Phase-B per-level capture mis-assigned
     * some rooms across levels (e.g. L1's Aquamentus boss room got captured
     * under L3), leaving render holes -> black BG + no boss when live play
     * reaches them. Fill the hole from a same-block sibling so every
     * reachable room (incl. boss rooms) renders its correct layout. Palette
     * is handled separately (per-level) by the caller — do NOT use the
     * sibling entry's palette. */
    {
        unsigned char lo  = (level <= 6u) ? 1u : 7u;
        unsigned char hi  = (level <= 6u) ? 6u : 9u;
        unsigned char sib;
        for (sib = lo; sib <= hi; ++sib) {
            if (sib == level) continue;
            idx_plus_one = g_uw_room_lookup[want_map][s_uw_quest][sib][room_id];
            if (idx_plus_one != 0u) return (int)(idx_plus_one - 1u);
        }
    }
    return -1;
}

static int find_blob_entry_explicit(unsigned char level,
                                    unsigned char quest,
                                    unsigned char room_id)
{
    unsigned char want_map = (s_uw_map_id == ROOMROM_MAP_REDUX) ? 1u : 0u;
    unsigned short idx_plus_one;
    if (quest >= 3u || level >= 10u || room_id >= 128u) return -1;
    idx_plus_one = g_uw_room_lookup[want_map][quest][level][room_id];
    return (idx_plus_one == 0u) ? -1 : (int)(idx_plus_one - 1u);
}

static void load_palette_from_blob(int idx)
{
    /* g_uw_room_palette[idx][32] = full per-room NES PALRAM captured from
     * BizHawk runtime; bytes 0..15 = NES BG sub-pals 0..3, 16..31 = SPR. */
    roomrom_bg_palette_load_palram_full(g_uw_room_palette[idx]);
}

static void load_palette_from_levelinfo(void)
{
    /* Plan v6-C2 fix (2026-05-24): rooms_dungeons levelinfo block has
     * variable per-level palette offset that depends on FoeCounts anchor
     * shift (L1=$20 .. L9=$00 per ROOMROM_UW_LEVELINFO_FOE_COUNTS_OFFSET).
     * Earlier static UW_LEVELINFO_PAL_OFFSET=3 was correct only for L1..L4;
     * L5+ read shifted bytes producing brown PAL0[0]=$11 instead of $0F
     * black backdrop (probed L9 Ganon r$42).
     *
     * Cheap correct fallback: BG palette is per-LEVEL constant on NES Z1.
     * Walk g_uw_room_index for any blob entry matching current level/quest
     * and reuse its PAL[0..15] — guaranteed correct since blob was captured
     * live from NES PALRAM. Only falls through to the default palette if level has
     * no captured rooms at all (defensive). */
    unsigned short i;
    int found_idx = -1;
    unsigned char want_map = (s_uw_map_id == ROOMROM_MAP_REDUX) ? 1u : 0u;
    for (i = 0; i < g_uw_room_count; i++) {
        const unsigned char *idxe = g_uw_room_index[i];
        if (idxe[0] == want_map && idxe[1] == s_uw_quest &&
            idxe[2] == s_uw_level) {
            found_idx = (int)i;
            break;
        }
    }
    /* Phase I0 (2026-05-27, debate 058 BUG 1): load BG + SPR halves
     * together via palram_full. Previously bg_only left PAL1 sprite half
     * stale from prior OW load → enemies/bosses rendered with OW colors
     * on first dungeon entry. If we have a sibling blob entry for this
     * level/quest, pass its full 32-byte capture (bytes 0-15 BG + 16-31
     * SPR per g_uw_room_palette layout at uw_render.c:170 comment). */
    if (found_idx >= 0) {
        roomrom_bg_palette_load_palram_full(g_uw_room_palette[found_idx]);
        return;
    }
    /* No captured rooms for this level/quest combo. Build full 32-byte
     * palette: BG defaults (black backdrop + grays) + NES Z1 default
     * sprite palette ($3F10-$3F1F): Link tunic, items, projectiles. */
    unsigned char full[32];
    full[0]  = 0x0Fu; full[1]  = 0x10u; full[2]  = 0x20u; full[3]  = 0x30u;
    full[4]  = 0x0Fu; full[5]  = 0x10u; full[6]  = 0x20u; full[7]  = 0x30u;
    full[8]  = 0x0Fu; full[9]  = 0x10u; full[10] = 0x20u; full[11] = 0x30u;
    full[12] = 0x0Fu; full[13] = 0x10u; full[14] = 0x20u; full[15] = 0x30u;
    /* NES default SPR palette (typical OW/UW used outside Z_06 patches). */
    full[16] = 0x0Fu; full[17] = 0x29u; full[18] = 0x1Au; full[19] = 0x0Fu;
    full[20] = 0x0Fu; full[21] = 0x02u; full[22] = 0x22u; full[23] = 0x30u;
    full[24] = 0x0Fu; full[25] = 0x16u; full[26] = 0x27u; full[27] = 0x30u;
    full[28] = 0x0Fu; full[29] = 0x0Cu; full[30] = 0x1Cu; full[31] = 0x2Cu;
    roomrom_bg_palette_load_palram_full(full);
}

void roomrom_uw_room_render_load_palette(unsigned char room_id)
{
    /* Palette is per-LEVEL (not per-block), so use the EXACT-level lookup
     * here, NOT find_blob_entry's same-block layout fallback. A room filled
     * from a sibling level (layout-correct) must still take THIS level's
     * palette via load_palette_from_levelinfo (which reuses any same-level
     * room's captured PALRAM — BG palette is a per-level constant on NES). */
    int idx = find_blob_entry_explicit(s_uw_level, s_uw_quest, room_id);
    if (idx >= 0) {
        load_palette_from_blob(idx);
    } else {
        load_palette_from_levelinfo();
    }

    /* T-171: the 2026-05-31 boss-room override of PAL2 (NES sprite
     * sub-palette 1) with the level's sub-palette 3 is gone. Sub-palette 3
     * boss tiles draw from the boss bank's biased copy in PAL1 13..15
     * (enemy_render ROOMROM_BOSS_SUBPAL3_TILE_BASE), which InitMode5Play's
     * row-7 cue now sets; the override turned the sub-palette 1 sprites in
     * the room (Gleeok's fireball, t171_boss_l4) teal. */
}

void roomrom_uw_room_render_upload_chr(void)
{
    /* Phase J.2 (2026-05-18): unified sparse path for orig + redux UW.
     * redux_uw_bg_chr (8192 B / 256 tile Genesis 4bpp source) flows
     * through bg_sparse_chr_redux_uw with unified_source semantics
     * (single-bank replaces full BG bank). LUT shared with orig path. */
    const unsigned char *blob = (s_uw_map_id == ROOMROM_MAP_REDUX)
        ? bg_sparse_chr_redux_uw
        : bg_sparse_chr_orig_uw;
    render_chr_upload((unsigned short)(ROOMROM_BG_TILE_BASE * 32u),
                      blob, BG_SPARSE_BLOB_BYTES);
}

/* Stub renderer.
 *
 * The NES dungeon room render path (z_05.asm LayoutUWFloor + WriteSquareUW
 * + ColumnDirectoryUW) interprets per-room column directories, level-specific
 * column heaps, and 8-entry primary square table. Faithfully porting that
 * pipeline takes more time than this round budgets, so for now this stub:
 *   - Clears plane A play area to a flat dungeon-floor square (PrimarySquaresUW[4] = $70).
 *   - Writes the level number and room id as glyphs for diagnostic visibility.
 *
 * Real renderer ports the LayoutUWFloor logic in a follow-up commit. */
/* Door art tiles that render IN FRONT of Link as he walks through.
 * Verified by dumping L1Q1 R73 blob NT:
 *   N doorway (rows 1-3): $78,$79,$7A,$7B,$7C arches + $24 interior
 *   E doorway (rows 9-12): $88,$89,$8A,$8B posts
 *   S doorway (rows 18-20): $7D,$7E,$7F,$80,$81 bottom arches
 * Walkable floor / thresholds ($74,$75,$76,$77) stay LOW priority so
 * Link renders normally on them. Locked-art ($98..$AF) also blocks. */
static unsigned char uw_is_door_tile(unsigned char t)
{
    if (t >= 0x78u && t <= 0x81u) return 1u;
    if (t >= 0x88u && t <= 0x8Bu) return 1u;
    if (t >= 0x98u && t <= 0xAFu) return 1u;
    return 0u;
}

static unsigned char door_priority_cache_find(unsigned char slot_x,
                                              unsigned char row_base)
{
    unsigned char i;
    for (i = 0u; i < UW_DOOR_PRIORITY_CACHE_SLOTS; i++) {
        if (s_door_priority_cache_valid[i] &&
            s_door_priority_cache_slot_x[i] == slot_x &&
            s_door_priority_cache_row_base[i] == row_base) {
            return i;
        }
    }
    return UW_DOOR_PRIORITY_CACHE_NONE;
}

static unsigned char door_priority_cache_alloc(unsigned char slot_x,
                                               unsigned char row_base)
{
    unsigned char idx = door_priority_cache_find(slot_x, row_base);
    if (idx == UW_DOOR_PRIORITY_CACHE_NONE) {
        unsigned char i;
        for (i = 0u; i < UW_DOOR_PRIORITY_CACHE_SLOTS; i++) {
            if (!s_door_priority_cache_valid[i]) {
                idx = i;
                break;
            }
        }
    }
    if (idx == UW_DOOR_PRIORITY_CACHE_NONE) {
        idx = s_door_priority_cache_next;
        s_door_priority_cache_next++;
        if (s_door_priority_cache_next >= UW_DOOR_PRIORITY_CACHE_SLOTS)
            s_door_priority_cache_next = 0u;
    }

    s_door_priority_cache_valid[idx] = 1u;
    s_door_priority_cache_slot_x[idx] = slot_x ? 1u : 0u;
    s_door_priority_cache_row_base[idx] = row_base;
    s_door_priority_cache_count[idx] = 0u;
    return idx;
}

static void door_priority_cache_begin(unsigned char dst_col,
                                      unsigned char row_base)
{
    unsigned char slot_x = (dst_col >= 16u) ? 1u : 0u;
    s_door_priority_cache_build =
        door_priority_cache_alloc(slot_x, row_base);
}

static void door_priority_cache_record(unsigned char plane_col,
                                       unsigned char row)
{
    unsigned char idx = s_door_priority_cache_build;
    unsigned char count;
    if (idx == UW_DOOR_PRIORITY_CACHE_NONE) return;
    count = s_door_priority_cache_count[idx];
    if (count >= UW_DOOR_PRIORITY_CACHE_MAX) return;
    s_door_priority_cache_col[idx][count] = plane_col;
    s_door_priority_cache_row[idx][count] = row;
    s_door_priority_cache_count[idx] = (unsigned char)(count + 1u);
}

/* PR-2: target plane for nametable writes. 0=BG_A (default, current room),
 * 1=BG_B (V scroll staging slot for incoming room). */
static unsigned char s_target_plane = 0u;

void roomrom_uw_room_render_set_target_plane(unsigned char plane)
{
    s_target_plane = plane ? 1u : 0u;
}

static unsigned short wrapped_plane_row(unsigned short row)
{
    return (unsigned short)(row & (ROOMROM_PLANE_ROWS - 1u));
}

static void plane_write(unsigned short col, unsigned short row,
                        unsigned short word)
{
    if (s_target_plane) render_set_plane_b_word(col, row, word);
    else                render_set_plane_a_word(col, row, word);
}

static unsigned short shared_plane_addr(unsigned short col, unsigned short row)
{
    return (unsigned short)(ROOMROM_SHARED_PLANE_BASE +
        ((((row & (ROOMROM_PLANE_ROWS - 1u)) * ROOMROM_SHARED_PLANE_COLS) +
          (col & (ROOMROM_SHARED_PLANE_COLS - 1u))) << 1));
}

static unsigned short plane_read_live_word(unsigned short col,
                                           unsigned short row)
{
    unsigned short addr = shared_plane_addr(col, row);
    return render_vram_read_word(addr);
}

static void plane_write_live_word(unsigned short col, unsigned short row,
                                  unsigned short word)
{
    plane_write(col, row, word);
}

/* T-118: per-palette plane word and tile class for every NES tile,
 * built once from bg_sparse_tile_lut (tile_word below). Room column
 * fills look words up instead of re-deriving them per tile. */
#define UW_CLS_DOOR 0x01u
#define UW_CLS_WALK 0x02u
static unsigned short s_uw_word[4][256];
static unsigned char s_uw_cls[256];
static unsigned char s_uw_tables_built;
static unsigned short tile_word(unsigned char raw_tile, unsigned char pal);

static void uw_tables_build(void)
{
    unsigned int t, pal;
    for (t = 0u; t < 256u; ++t) {
        s_uw_cls[t] = (unsigned char)((uw_is_door_tile((unsigned char)t) ? UW_CLS_DOOR : 0u) |
                                      (uw_walkable_tile_id((unsigned char)t) ? UW_CLS_WALK : 0u));
        for (pal = 0u; pal < 4u; ++pal)
            s_uw_word[pal][t] = tile_word((unsigned char)t, (unsigned char)pal);
    }
    s_uw_tables_built = 1u;
}

static unsigned short tile_word(unsigned char raw_tile, unsigned char pal)
{
    /* Phase 4: NES sub-pal selector lives in the tile index (pixel-biased
     * sub-pal copy); Gen pal-slot bits stay 0 (PAL0 owns NES BG).
     * Priority bit (0x8000) preserved for door art. */
    /* Phase J.2 (2026-05-18): unified sparse LUT lookup for orig + redux
     * UW (redux UW migrated off legacy 4x bank via unified_source sparse
     * variant). Same path as orig — variant-specific data difference is
     * in the upload blob, LUT mapping is universal. */
    unsigned short pri = uw_is_door_tile(raw_tile) ? 0x8000u : 0u;
    unsigned short slot = bg_sparse_tile_lut[raw_tile][pal & 0x03u];
    unsigned short tile_index = (slot == 0xFFFFu)
        ? ROOMROM_BLANK_TILE
        : (unsigned short)(ROOMROM_BG_TILE_BASE + slot);
    return (unsigned short)(pri | tile_index);
}

/* T-131: NES ShowLinkSpritesBehindHorizontalDoors puts a Link half whose
 * X is below $10 or at $E9 and above behind the background, so it shows
 * only over BG colour 0 there (lockstep t131_uw_doors f2145-2151: Link
 * hidden by the W wall bricks, visible in the black door opening). Such a
 * half covers play columns 0..2 or 29..31; those BG tiles are high
 * priority, the Link half low priority (sprite_render.c link_half_prio).
 * Wallmaster's behind-BG hand also reaches column 3 (x=$18) as it exits
 * the W wall; make the fourth border column high priority so opaque wall
 * pixels cover it as they do on NES (t171_wallmaster_grab t7855/t7950). */
static unsigned short edge_priority(unsigned char src_col)
{
    return (src_col < 4u || src_col > 27u) ? 0x8000u : 0u;
}

/* T-114: plane placement of the live (last rendered) room. Rooms are
 * painted into scroll slots (plane col offset per metatile column, row
 * base) by roomrom_uw_room_render_fill_one_col_at; every later single-tile
 * edit (door patches, dark fill) must land in that same slot. A full
 * fill_plane_a resets it to the identity slot. */
static unsigned char s_live_dst_col[16] = {
    0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u
};
static unsigned char s_live_row_base = 0u;
static unsigned char s_live_plane = 0u;

static void live_slot_identity(void)
{
    unsigned char c;
    for (c = 0u; c < 16u; ++c) s_live_dst_col[c] = c;
    s_live_row_base = 0u;
    s_live_plane = s_target_plane;
}

static void write_tile_raw(unsigned char col, unsigned char row,
                           unsigned char raw_tile, unsigned char pal)
{
    unsigned char saved = s_target_plane;
    unsigned char dst = (unsigned char)((s_live_dst_col[(col >> 1) & 0x0Fu] << 1) |
                                        (col & 1u));
    s_target_plane = s_live_plane;
    if (!s_uw_tables_built) uw_tables_build();   /* s_uw_word = tile_word */
    plane_write(dst, wrapped_plane_row((unsigned short)(s_live_row_base + row +
                                                        ROOMROM_ROOM_FIRST_ROW)),
                (unsigned short)(s_uw_word[pal & 0x03u][raw_tile] | edge_priority(col)));
    s_target_plane = saved;
}

static unsigned char digit_tile(unsigned char d)
{
    return (unsigned char)(d & 0x0F);
}

static unsigned char attr_palette_for(const unsigned char *attr,
                                       unsigned char nt_col,
                                       unsigned char nt_row)
{
    /* AT covers NT in 8x8 grid of 32x32 quads. Each AT byte holds 4
     * 2-bit palette quads laid out: bits 0-1=TL, 2-3=TR, 4-5=BL, 6-7=BR.
     * AT byte index: (nt_row/4)*8 + (nt_col/4). */
    unsigned char at_idx = (unsigned char)(((nt_row >> 2) << 3) | (nt_col >> 2));
    unsigned char byte = attr[at_idx & 0x3F];
    unsigned char shift = (unsigned char)((((nt_row >> 1) & 1u) << 2) |
                                          (((nt_col >> 1) & 1u) << 1));
    return (unsigned char)((byte >> shift) & 0x03u);
}

/* Z_05.asm FillPlayAreaAttrs: the play-area attribute bytes (NT attribute
 * table $10..$3F) from the installed level block: LevelBlockAttrsA & 3 =
 * outer palette for all of it, LevelBlockAttrsB & 3 = inner palette for
 * offsets 9..$26 except the left/right edge columns, the bottom inner
 * row ($21+) only in its top half. T-171: the captured g_uw_room_attr
 * differs from this in 4 of 331 rooms (L7 $2A Aquamentus: inner $FF,
 * NES $AA, blocks drawn blue); computing it is NES-exact for all. */
#define UW_LBA_A(room) nes_ram[0x687Eu + (room)]
#define UW_LBA_B(room) nes_ram[0x68FEu + (room)]
static unsigned char s_lba_attr[2][64];
static unsigned char s_lba_attr_room[2] = { 0xFFu, 0xFFu };
static unsigned char s_lba_attr_lvl[2];         /* CurLevel | quest << 4 */
static unsigned char s_lba_attr_next;

static const unsigned char *uw_room_attr(unsigned char room_id)
{
    static const unsigned char k_sel[4] = { 0x00u, 0x55u, 0xAAu, 0xFFu };
    unsigned char i, y, outer, inner;
    unsigned char *pa;
    const unsigned char lvl = (unsigned char)(nes_ram[0x0010u] | (s_uw_quest << 4));
    room_id = (unsigned char)(room_id & 0x7Fu);
    for (i = 0u; i < 2u; ++i)
        if (s_lba_attr_room[i] == room_id && s_lba_attr_lvl[i] == lvl) return s_lba_attr[i];
    i = (unsigned char)(s_lba_attr_next++ & 1u);
    pa = &s_lba_attr[i][0x10u];
    outer = k_sel[UW_LBA_A(room_id) & 3u];
    inner = k_sel[UW_LBA_B(room_id) & 3u];
    for (y = 0u; y < 0x10u; ++y) s_lba_attr[i][y] = 0u;   /* status bar */
    for (y = 0u; y < 0x30u; ++y) pa[y] = outer;
    for (y = 0x09u; y < 0x27u; ++y) {
        if ((y & 7u) == 0u || (y & 7u) == 7u) continue;
        pa[y] = (y >= 0x21u) ? (unsigned char)((inner & 0x0Fu) | (pa[y] & 0xF0u))
                             : inner;
    }
    s_lba_attr_room[i] = room_id;
    s_lba_attr_lvl[i] = lvl;
    return s_lba_attr[i];
}

static void blit_blob(int idx, unsigned char room_id)
{
    const unsigned char *nt = g_uw_room_nt[idx];
    const unsigned char *attr = uw_room_attr(room_id);
    s_cur_attr = attr;
    unsigned char row, col;
    unsigned char mt_col, mt_row;
    for (row = 0; row < ROOMROM_UW_BLOB_ROWS; row++) {
        for (col = 0; col < ROOMROM_UW_BLOB_COLS; col++) {
            unsigned char raw = nt[row * ROOMROM_UW_BLOB_COLS + col];
            /* Play area starts at NT row 8 (HUD occupies rows 0..7). */
            unsigned char nt_row = (unsigned char)(row + 8u);
            unsigned char pal = attr_palette_for(attr, col, nt_row);
            write_tile_raw(col, row, raw, pal);
            play_area_set(col, row, raw);
            s_uw_tile_walkable[col][row] = uw_walkable_tile_id(raw);
        }
    }
    /* Legacy 16x11 summary only. Link collision samples s_uw_tile_walkable,
     * which preserves NES 8x8 PlayAreaTiles behavior. */
    for (mt_row = 0; mt_row < 11; mt_row++) {
        for (mt_col = 0; mt_col < 16; mt_col++) {
            unsigned char tl =
                nt[(mt_row * 2u) * ROOMROM_UW_BLOB_COLS + (mt_col * 2u)];
            set_walkable_metatile_only(mt_col, mt_row, uw_walkable_tile_id(tl));
        }
    }
}

/* T-172: InitMode3_Sub8's LayOutRoom ran ~5 frames on the Genesis (NES 4;
 * t171_patra_sword t300). The tile words of a room's metatile columns are
 * a pure function of the blob entry and the room's two attribute selectors
 * (LevelBlockAttrsA/B & 3), so main.c computes them on the mode-3 Sub2-7
 * ticks into its idle curtain buffer (22 x 32 words, plane-column order).
 * The fill uses them only when blob entry and selectors still match. */
static unsigned short *s_uwp_buf;
static int s_uwp_idx = -1;
static unsigned char s_uwp_room = 0xFFu;
static unsigned char s_uwp_sel;
static unsigned char s_uwp_cols;

static unsigned char uw_attr_sel(unsigned char room_id)
{
    return (unsigned char)((UW_LBA_A(room_id) & 3u) | ((UW_LBA_B(room_id) & 3u) << 2));
}

/* Tile words of metatile column src_col: rows at col0[r * stride] and
 * col1[r * stride]. */
static void uw_metacol_words(const unsigned char *nt, const unsigned char *attr,
                             unsigned char src_col, unsigned short *col0,
                             unsigned short *col1, unsigned char stride)
{
    const unsigned char src_p0 = (unsigned char)(src_col << 1);
    const unsigned short ep = edge_priority(src_p0);   /* same for src_p0 + 1 */
    const unsigned char *n = nt + src_p0;
    const unsigned short *words = s_uw_word[0];
    unsigned char row;
    if (!s_uw_tables_built) uw_tables_build();
    for (row = 0; row < ROOMROM_UW_BLOB_ROWS; row++) {
        /* Play rows start at NT row 8: row pairs share an attribute quad. */
        if ((row & 1u) == 0u)
            words = s_uw_word[attr_palette_for(attr, src_p0, (unsigned char)(row + 8u))];
        *col0 = (unsigned short)(words[n[0]] | ep);
        *col1 = (unsigned short)(words[n[1]] | ep);
        col0 += stride;
        col1 += stride;
        n += ROOMROM_UW_BLOB_COLS;
    }
}

void roomrom_uw_room_render_prepare(unsigned char room_id, unsigned short *buf,
                                    unsigned char max_cols)
{
    int idx;
    unsigned char sel;
    room_id = (unsigned char)(room_id & 0x7Fu);
    idx = find_blob_entry(s_uw_level, room_id);
    sel = uw_attr_sel(room_id);
    if (buf != s_uwp_buf || room_id != s_uwp_room || idx != s_uwp_idx ||
        sel != s_uwp_sel) {
        s_uwp_buf = buf;
        s_uwp_room = room_id;
        s_uwp_idx = idx;
        s_uwp_sel = sel;
        s_uwp_cols = 0u;
    }
    if (idx < 0) return;
    while (max_cols-- && s_uwp_cols < 16u) {
        const unsigned char c = s_uwp_cols;
        uw_metacol_words(g_uw_room_nt[idx], uw_room_attr(room_id), c,
                         buf + 2u * c, buf + 2u * c + 1u, ROOMROM_UW_BLOB_COLS);
        ++s_uwp_cols;
    }
}

unsigned char roomrom_uw_room_render_has_layout(unsigned char room_id)
{
    return (unsigned char)(find_blob_entry(s_uw_level, (unsigned char)(room_id & 0x7Fu)) >= 0);
}

void roomrom_uw_room_render_prepare_drop(void)
{
    s_uwp_buf = (unsigned short *)0;
    s_uwp_room = 0xFFu;
    s_uwp_cols = 0u;
}

/* Tile classes of metatile column src_col: door priority cache (plane
 * column dst_p0 / dst_p0 + 1), walkability (keyed by SOURCE col: Link's
 * probe samples source-room space whatever the plane slot, Task 5.5),
 * PlayAreaTiles (column-major, 22 rows), and the legacy 16x11 metatile
 * summary (top-left tile = even rows of plane column src_p0). */
static void uw_metacol_classes(const unsigned char *nt, unsigned char src_col,
                               unsigned char dst_p0)
{
    const unsigned char src_p0 = (unsigned char)(src_col << 1);
    const unsigned char src_p1 = (unsigned char)(src_p0 + 1u);
    const unsigned char dst_p1 = (unsigned char)(dst_p0 + 1u);
    const unsigned char *n = nt + src_p0;
    unsigned char *w0 = s_uw_tile_walkable[src_p0];
    unsigned char *w1 = s_uw_tile_walkable[src_p1];
    unsigned char *pa0 = (unsigned char *)(unsigned long)
        &nes_ram[NES_PLAY_AREA_BASE + (unsigned short)src_p0 * NES_TILE_COL_STRIDE];
    unsigned char *pa1 = pa0 + NES_TILE_COL_STRIDE;
    unsigned char *mt = s_uw_walkable[src_col & 0x0Fu];
    unsigned char row;
    for (row = 0; row < ROOMROM_UW_BLOB_ROWS; row = (unsigned char)(row + 2u)) {
        const unsigned char a0 = n[0], a1 = n[1];
        const unsigned char b0 = n[ROOMROM_UW_BLOB_COLS];
        const unsigned char b1 = n[ROOMROM_UW_BLOB_COLS + 1u];
        const unsigned char ca0 = s_uw_cls[a0], ca1 = s_uw_cls[a1];
        const unsigned char cb0 = s_uw_cls[b0], cb1 = s_uw_cls[b1];
        if ((ca0 | ca1 | cb0 | cb1) & UW_CLS_DOOR) {
            if (ca0 & UW_CLS_DOOR) door_priority_cache_record(dst_p0, row);
            if (ca1 & UW_CLS_DOOR) door_priority_cache_record(dst_p1, row);
            if (cb0 & UW_CLS_DOOR) door_priority_cache_record(dst_p0, (unsigned char)(row + 1u));
            if (cb1 & UW_CLS_DOOR) door_priority_cache_record(dst_p1, (unsigned char)(row + 1u));
        }
        w0[row] = (unsigned char)(ca0 >> 1);       /* UW_CLS_WALK */
        w1[row] = (unsigned char)(ca1 >> 1);
        w0[row + 1u] = (unsigned char)(cb0 >> 1);
        w1[row + 1u] = (unsigned char)(cb1 >> 1);
        mt[row >> 1] = (unsigned char)(ca0 >> 1);
        pa0[row] = a0;
        pa1[row] = a1;
        pa0[row + 1u] = b0;
        pa1[row + 1u] = b1;
        n += 2u * ROOMROM_UW_BLOB_COLS;
    }
}

/* S6.5 scroll: render two plane cols (one metatile col) of `room_id` from
 * blob src_col into plane dst_col. src_col / dst_col are metatile cols (0..15).
 * Falls back to no-op if room not found in blob (stays as previous content). */
static void blit_blob_one_metacol_at(int idx, unsigned char room_id,
                                     unsigned char src_col,
                                     unsigned char dst_col,
                                     unsigned char dst_row_base)
{
    const unsigned char *nt   = g_uw_room_nt[idx];
    const unsigned char *attr = uw_room_attr(room_id);
    s_cur_attr = attr; /* keep current so palette_at is valid for door patches */
    unsigned char row;
    unsigned char src_p0 = (unsigned char)(src_col << 1);
    unsigned char src_p1 = (unsigned char)(src_p0 + 1);
    unsigned char dst_p0 = (unsigned char)(dst_col << 1);
    unsigned char dst_p1 = (unsigned char)(dst_p0 + 1);
    /* T-118: build both plane columns, then stream each with one VDP
     * address set (render_plane_a_write_col) instead of one per tile. */
    unsigned short col0[ROOMROM_UW_BLOB_ROWS];
    unsigned short col1[ROOMROM_UW_BLOB_ROWS];
    const unsigned short *c0 = col0;
    const unsigned short *c1 = col1;
    unsigned char cstride = 1u;
    if (!s_uw_tables_built) uw_tables_build();
    if (s_uwp_buf && (room_id & 0x7Fu) == s_uwp_room && idx == s_uwp_idx &&
        src_col < s_uwp_cols && uw_attr_sel((unsigned char)(room_id & 0x7Fu)) == s_uwp_sel) {
        c0 = s_uwp_buf + src_p0;                 /* prepared on mode-3 Sub2-7 */
        c1 = c0 + 1;
        cstride = ROOMROM_UW_BLOB_COLS;
    } else {
        uw_metacol_words(nt, attr, src_col, col0, col1, 1u);
    }
    uw_metacol_classes(nt, src_col, dst_p0);
    {
        unsigned short first = wrapped_plane_row(
            (unsigned short)(dst_row_base + ROOMROM_ROOM_FIRST_ROW));
        if (s_target_plane) {
            for (row = 0; row < ROOMROM_UW_BLOB_ROWS; row++) {
                unsigned short r = wrapped_plane_row((unsigned short)(first + row));
                plane_write(dst_p0, r, c0[(unsigned short)row * cstride]);
                plane_write(dst_p1, r, c1[(unsigned short)row * cstride]);
            }
        } else {
            render_plane_a_write_col_strided(dst_p0, first, c0, ROOMROM_UW_BLOB_ROWS,
                                             ROOMROM_PLANE_ROWS, cstride);
            render_plane_a_write_col_strided(dst_p1, first, c1, ROOMROM_UW_BLOB_ROWS,
                                             ROOMROM_PLANE_ROWS, cstride);
        }
    }
}

static void draw_placeholder(unsigned char room_id)
{
    unsigned char col, row;
    unsigned char mt_col, mt_row;
    unsigned char floor_tile = 0x70;
    s_cur_attr = (const unsigned char *)0;
    for (row = 0; row < ROOMROM_ROOM_ROWS; row++) {
        for (col = 0; col < ROOMROM_ROOM_COLS; col++) {
            unsigned char t = (unsigned char)(floor_tile + ((col + row) & 1));
            write_tile_raw(col, row, t, 1);
        }
    }
    /* Load precomputed NES collision grid for this room. Slice-1 (Task
     * 5.6 P0-2): cellars not yet in blob; uw_room_walkable returns 0
     * for unknown rooms → Link gets locked in placeholder. Force all
     * placeholder rooms walkable so the coordinator's stair-exit
     * branch can fire. Real cellar rendering lands in a substrate
     * Phase 1 follow-up (cellar blob extraction). */
    for (mt_row = 0; mt_row < 11; mt_row++) {
        for (mt_col = 0; mt_col < 16; mt_col++) {
            unsigned char walk =
                uw_room_walkable(s_uw_level, s_uw_quest, room_id,
                                 (unsigned char)mt_col, (unsigned char)mt_row);
            if (walk == 0u) walk = 1u;  /* placeholder: open floor */
            set_collision_metatile(mt_col, mt_row, walk);
        }
    }
    write_tile_raw(2, 1, 0x15, 0);
    write_tile_raw(3, 1, digit_tile(s_uw_level), 0);
    write_tile_raw(6, 1, 0x1B, 0);
    write_tile_raw(7, 1, digit_tile((unsigned char)(room_id >> 4)), 0);
    write_tile_raw(8, 1, digit_tile(room_id), 0);
}

void roomrom_uw_room_render_fill_plane_a(unsigned char room_id)
{
    int idx = find_blob_entry(s_uw_level, room_id);
    live_slot_identity();
    if (idx >= 0) {
        blit_blob(idx, room_id);
    } else {
        draw_placeholder(room_id);
    }
}

/* Task 5.8: dark-room render. Paints all 32x22 playfield cells with
 * VDP word 0x0000 (tile $00, PAL0). HUD on WINDOW plane is unaffected
 * — a global PAL swap would have darkened HUD too (G2-fix justification).
 * Walkability cache untouched: Link still walks the same cells; the
 * visual is just black until candle-lit. */
void roomrom_uw_room_render_fill_plane_a_dark(void)
{
    unsigned char col, row;
    unsigned char saved = s_target_plane;
    s_cur_attr = (const unsigned char *)0;
    s_target_plane = s_live_plane;
    for (row = 0; row < ROOMROM_ROOM_ROWS; row++) {
        for (col = 0; col < ROOMROM_ROOM_COLS; col++) {
            /* T-114: the live room's slot, not plane row/col 0. */
            unsigned char dst = (unsigned char)((s_live_dst_col[(col >> 1) & 0x0Fu] << 1) |
                                                (col & 1u));
            plane_write(dst,
                wrapped_plane_row((unsigned short)(s_live_row_base + row +
                                                   ROOMROM_ROOM_FIRST_ROW)),
                0x0000u);
        }
    }
    s_target_plane = saved;
}

void roomrom_uw_room_render_fill_one_col(unsigned char room_id,
                                         unsigned char src_col,
                                         unsigned char dst_col)
{
    roomrom_uw_room_render_fill_one_col_at(room_id, src_col, dst_col, 0);
}

void roomrom_uw_room_render_fill_one_col_at(unsigned char room_id,
                                            unsigned char src_col,
                                            unsigned char dst_col,
                                            unsigned char dst_row_base)
{
    unsigned char src = (unsigned char)(src_col & 0x0Fu);
    unsigned char dst = (unsigned char)(dst_col & 0x1Fu);
    int idx = find_blob_entry(s_uw_level, room_id);
    s_live_dst_col[src] = dst;
    s_live_row_base = dst_row_base;
    s_live_plane = s_target_plane;
    if (src == 0u) {
        door_priority_cache_begin(dst, dst_row_base);
    }
    if (idx >= 0) {
        blit_blob_one_metacol_at(idx, room_id, src, dst, dst_row_base);
    } else {
        /* Non-blob room: plane tiles left unchanged; populate s_uw_walkable
         * from precomputed NES grid so collision is valid for all rooms.
         * Guard dst_col: s_uw_walkable is sized [16][11] for the active slot
         * only; slot-1 prefetch (dst_col 16-31) is out-of-bounds — skip. */
        unsigned char mt_row;
        if (dst < 16u) {
            for (mt_row = 0u; mt_row < 11u; mt_row++) {
                set_collision_metatile(
                    dst, mt_row,
                    uw_room_walkable(s_uw_level, s_uw_quest, room_id,
                                     src, mt_row));
            }
        }
    }
    if (src == 15u) {
        s_door_priority_cache_build = UW_DOOR_PRIORITY_CACHE_NONE;
    }
}

/* T-172: the whole room from the mode-3 precompute (s_uwp_buf = the 22 x 32
 * plane words in row order): the same plane cells, tile classes, live-slot
 * and door-priority state as 16 fill_one_col_at calls for plane metatile
 * columns dst_col0..dst_col0 + 15, written a plane row at a time. Returns
 * 0 (nothing done) unless the precompute is complete and still valid. */
unsigned char roomrom_uw_room_render_fill_room_prepared(unsigned char room_id,
                                                        unsigned char dst_col0,
                                                        unsigned char dst_row_base)
{
    int idx;
    const unsigned char *nt;
    unsigned char c, row;
    unsigned short first;
    room_id = (unsigned char)(room_id & 0x7Fu);
    if (s_target_plane || !s_uwp_buf || s_uwp_cols < 16u || room_id != s_uwp_room ||
        dst_col0 > 16u)
        return 0u;
    idx = find_blob_entry(s_uw_level, room_id);
    if (idx < 0 || idx != s_uwp_idx || uw_attr_sel(room_id) != s_uwp_sel) return 0u;
    if (!s_uw_tables_built) uw_tables_build();
    nt = g_uw_room_nt[idx];
    s_cur_attr = uw_room_attr(room_id);
    s_live_row_base = dst_row_base;
    s_live_plane = s_target_plane;
    door_priority_cache_begin(dst_col0, dst_row_base);
    for (c = 0u; c < 16u; ++c) {
        const unsigned char dst = (unsigned char)((dst_col0 + c) & 0x1Fu);
        s_live_dst_col[c] = dst;
        uw_metacol_classes(nt, c, (unsigned char)(dst << 1));
    }
    s_door_priority_cache_build = UW_DOOR_PRIORITY_CACHE_NONE;
    first = wrapped_plane_row((unsigned short)(dst_row_base + ROOMROM_ROOM_FIRST_ROW));
    for (row = 0u; row < ROOMROM_UW_BLOB_ROWS; ++row)
        render_plane_a_write_run((unsigned short)(dst_col0 << 1),
                                 wrapped_plane_row((unsigned short)(first + row)),
                                 s_uwp_buf + (unsigned short)row * ROOMROM_UW_BLOB_COLS,
                                 ROOMROM_UW_BLOB_COLS);
    return 1u;
}

void roomrom_uw_room_render_set_live_door_priority(unsigned char slot_x,
                                                   unsigned char row_base,
                                                   unsigned char enabled)
{
    unsigned char idx = door_priority_cache_find(slot_x ? 1u : 0u, row_base);
    unsigned char i;
    unsigned short col_base = slot_x ? ROOMROM_ROOM_COLS : 0u;
    unsigned short pri_mask = enabled ? 0x8000u : 0u;

    if (idx != UW_DOOR_PRIORITY_CACHE_NONE) {
        unsigned char count = s_door_priority_cache_count[idx];
        for (i = 0u; i < count; i++) {
            unsigned short plane_col = s_door_priority_cache_col[idx][i];
            unsigned short plane_row = wrapped_plane_row(
                (unsigned short)(row_base + s_door_priority_cache_row[idx][i] +
                                 ROOMROM_ROOM_FIRST_ROW));
            unsigned short word = plane_read_live_word(plane_col, plane_row);
            unsigned short tile = (unsigned short)(word & 0x07FFu);
            unsigned char raw = (unsigned char)((tile - ROOMROM_BG_TILE_BASE) & 0x00FFu);
            if (!uw_is_door_tile(raw))
                continue;
            word = (unsigned short)((word & 0x7FFFu) | pri_mask);
            plane_write_live_word(plane_col, plane_row, word);
        }
        return;
    }

    for (i = 0u; i < ROOMROM_ROOM_ROWS; i++) {
        unsigned char col;
        unsigned short plane_row = wrapped_plane_row(
            (unsigned short)(row_base + i + ROOMROM_ROOM_FIRST_ROW));
        for (col = 0u; col < ROOMROM_ROOM_COLS; col++) {
            unsigned short plane_col = (unsigned short)(col_base + col);
            unsigned short word = plane_read_live_word(plane_col, plane_row);
            unsigned short tile = (unsigned short)(word & 0x07FFu);
            unsigned char raw = (unsigned char)((tile - ROOMROM_BG_TILE_BASE) & 0x00FFu);
            if (!uw_is_door_tile(raw))
                continue;
            word = (unsigned short)((word & 0x7FFFu) | pri_mask);
            plane_write_live_word(plane_col, plane_row, word);
        }
    }
}

/* Ph5.3 door-state layer: public write accessors used by uw_door_state.c. */

void roomrom_uw_room_render_write_tile(unsigned char col, unsigned char row,
                                       unsigned char raw_tile, unsigned char pal)
{
    write_tile_raw(col, row, raw_tile, pal);
    play_area_set(col, row, raw_tile);
    if (col < 32u && row < 22u)
        s_uw_tile_walkable[col][row] = uw_walkable_tile_id(raw_tile);
}

/* T-118: PlayAreaTiles + walkability only (no plane write). */
void roomrom_uw_room_render_write_tile_pat(unsigned char col, unsigned char row,
                                           unsigned char raw_tile)
{
    play_area_set(col, row, raw_tile);
    if (col < 32u && row < 22u)
        s_uw_tile_walkable[col][row] = uw_walkable_tile_id(raw_tile);
}

/* T-119: nametable-only write (NES door animation transfer records);
 * PlayAreaTiles and walkability unchanged. */
void roomrom_uw_room_render_write_tile_nt(unsigned char col, unsigned char row,
                                          unsigned char raw_tile, unsigned char pal)
{
    write_tile_raw(col, row, raw_tile, pal);
}

/* A 2x2 square of PlayAreaTiles changed (NES ChangeTileObjTiles: push
 * block, block stairs): Link's collision tables follow the tiles now
 * there, as the room render derives them. col/row: top-left tile. */
void roomrom_uw_room_render_refresh_square(unsigned char col, unsigned char row)
{
    unsigned char dc, dr;
    for (dc = 0u; dc < 2u; ++dc) {
        for (dr = 0u; dr < 2u; ++dr) {
            const unsigned char c = (unsigned char)(col + dc);
            const unsigned char r = (unsigned char)(row + dr);
            if (c >= 32u || r >= 22u) continue;
            s_uw_tile_walkable[c][r] = uw_walkable_tile_id(
                nes_ram[NES_PLAY_AREA_BASE + (unsigned short)c * NES_TILE_COL_STRIDE + r]);
        }
    }
    if (col < 32u && row < 22u)
        set_walkable_metatile_only((unsigned char)(col >> 1), (unsigned char)(row >> 1),
            uw_walkable_tile_id(nes_ram[NES_PLAY_AREA_BASE +
                                        (unsigned short)col * NES_TILE_COL_STRIDE + row]));
}

void roomrom_uw_room_render_set_walkable(unsigned char col, unsigned char row,
                                         unsigned char val)
{
    set_walkable_metatile_only(col, row, val);
}

void roomrom_uw_room_render_set_walkable_tile(unsigned char col,
                                              unsigned char row,
                                              unsigned char val)
{
    if (col >= 32u || row >= 22u) return;
    s_uw_tile_walkable[col][row] = val ? 1u : 0u;
}

void roomrom_uw_room_render_publish_walkable(void)
{
    volatile unsigned char *p =
        (volatile unsigned char *)ROOMROM_DEBUG_UW_WALKABLE_BASE;
    unsigned char col, row;
    p[0] = 0x55u; /* 'U' */
    p[1] = 0x57u; /* 'W' */
    p[2] = 32u;
    p[3] = 22u;
    for (col = 0u; col < 32u; col++) {
        for (row = 0u; row < 22u; row++) {
            p[4u + (unsigned short)col * 22u + row] = s_uw_tile_walkable[col][row];
        }
    }
}

/* Return AT palette for NT coordinates (col 0..31, row 8..29 in full NT space).
 * Falls back to 0 if no blob is loaded (placeholder room). */
unsigned char roomrom_uw_room_render_palette_at(unsigned char nt_col,
                                                unsigned char nt_row)
{
    if (s_cur_attr == (const unsigned char *)0) return 0u;
    return attr_palette_for(s_cur_attr, nt_col, nt_row);
}

/* Task 5.6: explicit (level, quest, room) blob NT lookup. Independent of
 * the cached s_uw_level/s_uw_quest so the warp coordinator can probe the
 * current UW source room while the cellar dispatch hasn't yet applied. */
unsigned char roomrom_uw_room_render_raw_tile_at_room(unsigned char level,
                                                      unsigned char quest,
                                                      unsigned char room_id,
                                                      unsigned char col,
                                                      unsigned char row)
{
    int idx;
    if (col >= ROOMROM_UW_BLOB_COLS) return 0u;
    if (row >= ROOMROM_UW_BLOB_ROWS) return 0u;
    idx = find_blob_entry_explicit(level, quest, room_id);
    if (idx < 0) return 0u;
    return g_uw_room_nt[idx][row * ROOMROM_UW_BLOB_COLS + col];
}
