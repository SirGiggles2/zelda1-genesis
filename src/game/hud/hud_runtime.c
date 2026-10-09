/* NES source: Z_01.asm:FormatHeartsInTextBuf / FormatStatusBarText.
 * Drained C: hud_dispatch.c:hud_format_status_bar_text.
 * Coverage: PARTIAL (Original counters/hearts; map/equipment ownership pending).
 * Stance: EXTEND. Render native formatting and observe native changes. */
/* Phase 12.2 SGDK-1 cleanup: route VDP_setTileMapXY/VDP_clearTileMapRect
 * through render_set_window_word / render_clear_window_rect adapter. */
#include "hud_runtime.h"
#include "platform_abi.h"
#include "../world/render/ow_render.h"  /* Phase 12.2 promoted */
#include "render_abi.h"
#include "../../../RoomRom/src/roomrom_vram_map.h"
#include "../../../RoomRom/src/bg_sparse_chr.h"  /* Phase J: sparse atlas + LUT */
#include "../../state/inventory.h"
/* P4c: atlas header included for named constant reference and future
 * ATLAS_ASSERT_SIZE hooks.
 *
 * hud_chr.h provides ROOMROM_ATLAS_HUD_HUD_*_OFFSET byte-offset constants
 * into the roomrom_atlas_hud blob and W_HUD_x/H_HUD_x dispatch defines.
 *
 * Migration gap: this renderer addresses HUD content as raw NES BG tile IDs
 * (e.g., TILE_FULL_HEART = 0xF2u) passed to hud_word() which calls
 * ROOMROM_BG_TILE_BASE_PAL(pal) + raw_tile.  The atlas hud_chr offsets are
 * atlas-blob-local indices (heart_full at byte 0, digit_0 at byte 96, etc.)
 * and do NOT correspond to NES BG tile IDs.  The roomrom_atlas_hud blob is
 * also not currently uploaded to VRAM -- HUD content is sourced from the
 * expanded BG CHR bank which already contains NES BG tiles at their native
 * NES tile-ID positions.
 *
 * Additionally, hud_chr.h W_HUD_x/H_HUD_x dispatch defines describe sprite
 * SPRITE_SIZE widths, but this renderer uses VDP_setTileMapXY (BG tile maps),
 * not VDP_setSpriteFull.  ATLAS_ASSERT_SIZE has nothing to verify here.
 *
 * TODO(Phase-4c / Phase 6): once the HUD CHR upload path is reworked to
 * source tiles from roomrom_atlas_hud rather than the expanded BG bank:
 *   1. Replace raw tile-ID literals with
 *      ROOMROM_ATLAS_HUD_HUD_<NAME>_OFFSET / 32
 *      (after confirming NES tile IDs match atlas byte ordering).
 *   2. Add ATLAS_ASSERT_SIZE-equivalent BG-tile checks (need a new
 *      ATLAS_ASSERT_BG_TILE macro for tile-map rather than sprite use). */
#include "atlas/hud_chr.h"
#include "../room/room_dispatch.h"
#include "hud_dispatch.h"   /* T2.3: hud_format_status_bar_text mirror */
#include "heart_container_anim.h"   /* T2.7: 3-frame scale-up on pickup */
#include "../world/render/sprite_render.h" /* NES $3E position dots */

#define HUD_TILE_SPACE  0x24u
#define TILE_DASH       0x62u
#define TILE_LOW_X      0x21u
#define TILE_FULL_HEART 0xF2u
#define TILE_HALF_HEART 0xF3u
#define TILE_EMPTY_HEART 0xF4u
#define TILE_GRAY_MAP   0xF5u
#define TILE_REDUX_HEART_OUTLINE 0x50u
#define TILE_ORIGINAL_MAP_MARKER 0x51u
#define TILE_REDUX_HEART_FILL    0x52u
#define COMMON_BG_CHR_OFFSET     (112u * 32u)
#define COMMON_MISC_CHR_OFFSET   (224u * 32u)
#define COMMON_MISC_TILE_BASE    0xF2u
#define REDUX_AUTOMAP_TILE_BASE  0x30u
#define REDUX_AUTOMAP_TILE_COUNT 32u

static unsigned char s_hud_pal[ROOMROM_HUD_ROWS][ROOMROM_ROOM_COLS];

/* Phase 6 Task 6.10.6 (Step A): cached HUD identity so the per-frame
 * dynamic refresh knows where to paint counts/hearts without re-running
 * the static transfer macro. Forward-declared here so T6.5 marker
 * refresh (defined above roomrom_hud_draw) can read it. */
static unsigned char s_hud_id_cached = 0xFFu;
static unsigned char s_last_room_id_cached = 0u;
static unsigned char s_last_is_uw_cached = 0u;

static const unsigned char s_hud_custom_chr[96] = {
    0x01,0x10,0x01,0x10,
    0x10,0x01,0x10,0x01,
    0x10,0x00,0x00,0x01,
    0x10,0x00,0x00,0x01,
    0x01,0x00,0x00,0x10,
    0x00,0x10,0x01,0x00,
    0x00,0x01,0x10,0x00,
    0x00,0x00,0x00,0x00,

    0x00,0x00,0x00,0x00,
    0x00,0x01,0x10,0x00,
    0x00,0x13,0x31,0x00,
    0x01,0x33,0x33,0x10,
    0x00,0x13,0x31,0x00,
    0x00,0x01,0x10,0x00,
    0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,

    0x00,0x00,0x00,0x00,
    0x01,0x10,0x01,0x10,
    0x01,0x11,0x11,0x10,
    0x01,0x11,0x11,0x10,
    0x00,0x11,0x11,0x00,
    0x00,0x01,0x10,0x00,
    0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00
};

/* NES Zelda overworld HUD, captured from live NES at room $77.
 * NT cols 2-9 rows 3-6: 8x4 gray map block (tile $F5).
 * NT col 11-13 rows 3,5,6: rupee/key/bomb count (icon, 'X', digit).
 * NT col 15-20 rows 3-6: B/A item slot frames.
 * NT col 23-28 row 3: "-LIFE-" header.
 * NT col 22-24 row 6: 3 full hearts. */
static const unsigned char s_original_hud_macro[] = {
    /* Attribute table $23C0..$23CF - matches NES exactly. */
    0x23,0xC0,0x10,
    0x00,0x00,0x40,0x00,0x00,0x44,0x55,0x55,
    0x00,0x00,0x04,0x00,0x00,0x44,0x55,0x55,

    /* Row 3: gray map row 0, rupee count, B/A box top, -LIFE- */
    0x20,0x62,0x08, TILE_GRAY_MAP,TILE_GRAY_MAP,TILE_GRAY_MAP,TILE_GRAY_MAP,
                    TILE_GRAY_MAP,TILE_GRAY_MAP,TILE_GRAY_MAP,TILE_GRAY_MAP,
    0x20,0x6B,0x03, 0xF7,TILE_LOW_X,0x00,
    0x20,0x6F,0x06, 0x69,0x0B,0x6B,0x69,0x0A,0x6B,
    0x20,0x77,0x06, TILE_DASH,0x15,0x12,0x0F,0x0E,TILE_DASH,

    /* Row 4: gray map row 1, B/A vertical edges */
    0x20,0x82,0x08, TILE_GRAY_MAP,TILE_GRAY_MAP,TILE_GRAY_MAP,TILE_GRAY_MAP,
                    TILE_GRAY_MAP,TILE_GRAY_MAP,TILE_GRAY_MAP,TILE_GRAY_MAP,
    0x20,0x8F,0x01, 0x6C,
    0x20,0x91,0x01, 0x6C,
    0x20,0x92,0x01, 0x6C,
    0x20,0x94,0x01, 0x6C,

    /* Row 5: gray map row 2, key count, B/A vertical edges */
    0x20,0xA2,0x08, TILE_GRAY_MAP,TILE_GRAY_MAP,TILE_GRAY_MAP,TILE_GRAY_MAP,
                    TILE_GRAY_MAP,TILE_GRAY_MAP,TILE_GRAY_MAP,TILE_GRAY_MAP,
    0x20,0xAB,0x03, 0xF9,TILE_LOW_X,0x00,
    0x20,0xAF,0x01, 0x6C,
    0x20,0xB1,0x01, 0x6C,
    0x20,0xB2,0x01, 0x6C,
    0x20,0xB4,0x01, 0x6C,

    /* Row 6: gray map row 3, bomb count, B/A box bottom, hearts */
    0x20,0xC2,0x08, TILE_GRAY_MAP,TILE_GRAY_MAP,TILE_GRAY_MAP,TILE_GRAY_MAP,
                    TILE_GRAY_MAP,TILE_GRAY_MAP,TILE_GRAY_MAP,TILE_GRAY_MAP,
    0x20,0xCB,0x03, 0x61,TILE_LOW_X,0x00,
    0x20,0xCF,0x06, 0x6E,0x6A,0x6D,0x6E,0x6A,0x6D,
    0x20,0xD6,0x03, TILE_FULL_HEART,TILE_FULL_HEART,TILE_FULL_HEART,
    0xFF
};

static const unsigned char s_redux_ow_hud_macro[] = {
    0x23,0xC0,0x10,
    0x44,0x55,0x55,0x00,0x00,0xC0,0xFF,0x70,
    0x44,0x55,0x05,0x00,0x00,0xC0,0xAF,0x3A,

    0x20,0x76,0x08, 0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,
    0x20,0x96,0x08, 0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,
    0x20,0xB6,0x08, 0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,
    0x20,0xD6,0x08, 0x48,0x49,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,

    0x20,0x63,0x12,
    TILE_DASH,0x15,0x12,0x0F,0x0E,TILE_DASH,HUD_TILE_SPACE,HUD_TILE_SPACE,HUD_TILE_SPACE,HUD_TILE_SPACE,HUD_TILE_SPACE,HUD_TILE_SPACE,
    0x69,0x0B,0x6B,0x69,0x0A,0x6B,
    0x20,0xCF,0x06, 0x6E,0x6A,0x6D,0x6E,0x6A,0x6D,
    0x20,0x8F,0xC2, 0x6C,
    0x20,0x91,0xC2, 0x6C,
    0x20,0x92,0xC2, 0x6C,
    0x20,0x94,0xC2, 0x6C,
    0x20,0x6B,0x84, 0xF7,0xF9,0x65,0x61,
    0xFF
};

static const unsigned char s_redux_uw_hud_macro[] = {
    0x23,0xC0,0x10,
    0x44,0x55,0x55,0x00,0x00,0xC0,0xFF,0x70,
    0x44,0x55,0x05,0x00,0x00,0xC0,0xAF,0x3A,

    /* Plan v5c T6.6 — paint UW dungeon automap area (4 rows x 8 cols)
     * at the same top-right position as OW redux HUD. Tiles $30..$4F
     * are the redux automap CHR uploaded by upload_redux_automap_chr().
     * Slice-1: static tile range (placeholder dungeon layout). Dynamic
     * visited-room / compass-flash overlay deferred to T6.6 slice-2
     * (needs roomrom_visited_rooms tracker + DUNGEON_LEVEL_TRIFORCE_ROOM
     * read per-tick). */
    0x20,0x76,0x08, 0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,
    0x20,0x96,0x08, 0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,
    0x20,0xB6,0x08, 0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,
    0x20,0xD6,0x08, 0x48,0x49,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,

    0x20,0x63,0x12,
    TILE_DASH,0x15,0x12,0x0F,0x0E,TILE_DASH,HUD_TILE_SPACE,HUD_TILE_SPACE,HUD_TILE_SPACE,HUD_TILE_SPACE,HUD_TILE_SPACE,HUD_TILE_SPACE,
    0x69,0x0B,0x6B,0x69,0x0A,0x6B,
    0x20,0xCF,0x06, 0x6E,0x6A,0x6D,0x6E,0x6A,0x6D,
    0x20,0x8F,0xC2, 0x6C,
    0x20,0x91,0xC2, 0x6C,
    0x20,0x92,0xC2, 0x6C,
    0x20,0x94,0xC2, 0x6C,
    0x20,0x6B,0x84, 0xF7,0xF9,0x65,0x61,
    0xFF
};

static unsigned short hud_word(unsigned char raw_tile, unsigned char pal)
{
    /* Phase J (2026-05-18): sparse atlas LUT lookup. HUD tiles are
     * force-included in gen_bg_sparse.py per §36.1 MF2 (HUD rows not
     * covered by room nametable audit). Sentinel 0xFFFF (combo not
     * force-included) -> tile 0 blank fallback. */
    unsigned short slot = bg_sparse_tile_lut[raw_tile][pal & 0x03u];
    unsigned short tile = (slot == 0xFFFFu)
        ? ROOMROM_BLANK_TILE
        : (unsigned short)(ROOMROM_BG_TILE_BASE + slot);
    /* Phase 12.2 SGDK-1 cleanup: inline TILE_ATTR_FULL(PAL0, 1, 0, 0, tile).
     * Format: priority<<15 | palette<<13 | vflip<<12 | hflip<<11 | tile_index.
     * PAL0=0, priority=1 -> 0x8000; vflip=hflip=0. */
    return (unsigned short)(0x8000u | tile);
}

/* V2.4k (2026-05-26): HUD position mode toggle.
 *   bottom=0 (default): HUD at TOP (NES gameplay layout).
 *     HUD_WIN_ROW_BASE = 0 + VDP_setWindowOnTop(ROOMROM_HUD_ROWS).
 *   bottom=1 (inventory pause): the 224-line NES capture places the
 *     visible HUD eight pixels lower than the original seven-row window.
 *     Expose six rows at the bottom and draw from tile row 22; the seventh
 *     HUD row is outside the visible 28-row viewport.
 * Toggle via roomrom_hud_set_bottom_mode() — clears old position +
 * redraws at new offset. */
static unsigned char s_hud_bottom = 0u;
#define HUD_PAUSE_WINDOW_ROWS (ROOMROM_HUD_ROWS - 1u)
#define HUD_WIN_ROW_BASE  (s_hud_bottom ? (28u - HUD_PAUSE_WINDOW_ROWS) : 0u)

static void draw_hud_tile(unsigned char col, unsigned char row,
                          unsigned char raw_tile, unsigned char pal)
{
    if (col >= ROOMROM_ROOM_COLS || row >= ROOMROM_HUD_ROWS)
        return;
    render_set_window_word(col,
                           (unsigned short)(HUD_WIN_ROW_BASE + row),
                           hud_word(raw_tile, pal));
}

static void draw_hud_tile_b(unsigned char col, unsigned char row,
                            unsigned char raw_tile, unsigned char pal)
{
    (void)col;
    (void)row;
    (void)raw_tile;
    (void)pal;
    /* PR-2c: BG_B shares BG_A's $C000 scroll surface. HUD lives on WINDOW;
     * writing BG_B HUD shadows would erase wrapped room rows after vertical
     * scrolls. */
}

static void draw_hud_tile_attr(unsigned char col, unsigned char row,
                               unsigned char raw_tile)
{
    draw_hud_tile(col, row, raw_tile, s_hud_pal[row][col]);
}

static void clear_hud_pal(void)
{
    unsigned char row, col;
    for (row = 0; row < ROOMROM_HUD_ROWS; row++) {
        for (col = 0; col < ROOMROM_ROOM_COLS; col++)
            s_hud_pal[row][col] = 0;
    }
}

static void clear_hud_b(void)
{
    /* PR-2c: BG_B mirrors BG_A at $C000, so this must not clear rows 0..6.
     * Those rows are live bottom-room rows after an upward vertical scroll. */
}

/* T-118: set by any window rebuild; see the HUD draw cache below. */
static unsigned char s_hud_force = 1u;
/* T-118: 1 while the window holds a complete HUD for s_hud_id_cached /
 * s_last_is_uw_cached; cleared by every window clear and by
 * roomrom_hud_invalidate (outside window writers). */
static unsigned char s_hud_built = 0u;
static unsigned char s_hud_level_cached = 0xFFu;   /* CurLevel $10 at build */

static void clear_hud_window(void)
{
    s_hud_force = 1u;
    s_hud_built = 0u;
    render_clear_window_rect(0, (unsigned short)HUD_WIN_ROW_BASE,
                              ROOMROM_ROOM_COLS, ROOMROM_HUD_ROWS);
}

static void apply_attr_byte(unsigned char attr_offset, unsigned char attr)
{
    unsigned char nt_row_base = (unsigned char)((attr_offset >> 3) << 2);
    unsigned char col_base = (unsigned char)((attr_offset & 0x07) << 2);
    unsigned char q;

    for (q = 0; q < 4; q++) {
        unsigned char pal = (unsigned char)((attr >> (q << 1)) & 0x03);
        unsigned char q_col = (unsigned char)(col_base + ((q & 1u) << 1));
        unsigned char q_nt_row = (unsigned char)(nt_row_base + ((q >> 1) << 1));
        unsigned char dy, dx;
        for (dy = 0; dy < 2; dy++) {
            unsigned char nt_row = (unsigned char)(q_nt_row + dy);
            if (nt_row == 0 || nt_row > ROOMROM_HUD_ROWS)
                continue;
            for (dx = 0; dx < 2; dx++) {
                unsigned char col = (unsigned char)(q_col + dx);
                if (col < ROOMROM_ROOM_COLS)
                    s_hud_pal[nt_row - 1u][col] = pal;
            }
        }
    }
}

static void apply_transfer_macro(const unsigned char *macro)
{
    unsigned short i = 0;
    while (macro[i] != 0xFF) {
        unsigned short addr = (unsigned short)(((unsigned short)macro[i] << 8) | macro[i + 1]);
        unsigned char spec = macro[i + 2];
        unsigned char len = (unsigned char)(spec & 0x3Fu);
        unsigned char vertical = (unsigned char)(spec & 0x80u);
        unsigned char repeat = (unsigned char)(spec & 0x40u);
        unsigned char n;
        i += 3;

        if (addr >= 0x23C0u && addr < 0x2400u) {
            unsigned char attr_offset = (unsigned char)(addr - 0x23C0u);
            for (n = 0; n < len; n++)
                apply_attr_byte((unsigned char)(attr_offset + n), macro[i + n]);
            i += len;
            continue;
        }

        if (addr >= 0x2000u && addr < 0x23C0u) {
            unsigned char nt_row = (unsigned char)((addr - 0x2000u) >> 5);
            unsigned char col = (unsigned char)((addr - 0x2000u) & 0x1Fu);
            for (n = 0; n < len; n++) {
                unsigned char raw_tile = repeat ? macro[i] : macro[i + n];
                if (nt_row > 0 && nt_row <= ROOMROM_HUD_ROWS)
                    draw_hud_tile_attr(col, (unsigned char)(nt_row - 1u), raw_tile);
                if (vertical)
                    nt_row++;
                else
                    col++;
            }
            i += repeat ? 1u : len;
        } else {
            i += repeat ? 1u : len;
        }
    }
}

/* Phase 6 Task 6.10.6 (Step A): live-read HUD count cell.
 * NES Z_01.asm:2922 FormatDecimalCountByte format: 123 -> "123",
 * 23 -> "X23", 3 -> "X3 ". Three cells at col, col+1, col+2: hundreds /
 * tens / ones, with the 'X' (TILE_LOW_X) replacing the hundreds cell when
 * value < 100, and a trailing space when value < 10 (single low digit
 * shifted left into tens, ones blanked).
 *
 * NES is byte-wide (0..255) for each count. RoomRom widens rupees to
 * 16-bit; clamp display to 999 so the 3-digit window stays legal. */
/* T-118: HUD draw cache. A full HUD draw sets s_hud_force; incremental
 * refreshes (every inventory/heart change during play) then write only the
 * count cells and heart tiles whose value changed. Each VDP tile write here
 * costs ~2 scanlines of 68000 time, and the old full refresh (counts +
 * 16 heart tiles) cost ~60 lines, dropping frames in busy rooms. */
static unsigned short s_count_drawn[4];   /* HUD rows 2..5 at col 12 */
/* T-132: NES fills the counts and hearts only in mode 5
 * (UpdateHeartsAndRupees); level entry (modes 3/4) shows them blank. */
static unsigned char s_hud_counts_hidden;
static unsigned char s_heart_drawn[16];
static unsigned char s_heart_row_valid;
static unsigned char s_heart_row_values, s_heart_row_partial;
static unsigned char s_heart_row_hidden, s_heart_row_col, s_heart_row_row;

static void draw_count_cell(unsigned short value, unsigned char col,
                            unsigned char row, unsigned char pal)
{
    unsigned short v = (value > 999u) ? 999u : value;
    if (col == 12u && row >= 2u && row <= 5u) {
        unsigned short key = (unsigned short)(v | ((unsigned short)pal << 12));
        if (!s_hud_force && s_count_drawn[row - 2u] == key) return;
        s_count_drawn[row - 2u] = key;
    }
    unsigned char hundreds = (unsigned char)(v / 100u);
    unsigned char tens     = (unsigned char)((v / 10u) % 10u);
    unsigned char ones     = (unsigned char)(v % 10u);
    unsigned char tile_h, tile_t, tile_o;

    if (hundreds != 0u) {
        tile_h = hundreds;
        tile_t = tens;
        tile_o = ones;
    } else if (tens != 0u) {
        tile_h = TILE_LOW_X;
        tile_t = tens;
        tile_o = ones;
    } else {
        tile_h = TILE_LOW_X;
        tile_t = ones;
        tile_o = HUD_TILE_SPACE;
    }
    draw_hud_tile(col,                       row, tile_h, pal);
    draw_hud_tile((unsigned char)(col + 1u), row, tile_t, pal);
    draw_hud_tile((unsigned char)(col + 2u), row, tile_o, pal);
}

/* Phase 6 Task 6.11 (Step A): live heart row. NES paints up to 16
 * hearts across NT row 5 (hearts 9..16, overflow) and row 6
 * (hearts 1..8). Cols 22..29 = 8 slots per row. Single 8x8 tile per
 * heart (NES $F2 full / $F3 half / $F4 empty).
 *
 * heart_values: hi=max, lo=cur. heart_partial: 0 -> empty, nonzero ->
 * half. NES Z1 max heart cap = $0F (16). Probe shows $99 (9 max / 9
 * cur) — without the expansion below this clamped at 3 visible. */
static void draw_hearts_row(unsigned char col, unsigned char row,
                            unsigned char hud_id)
{
    unsigned char i;
    const unsigned char hearts = RAM(0x066Fu);
    const unsigned char partial = RAM(0x0670u);
    /* NES source: Z_01 FormatHeartsInTextBuf; drain: hud_heart_tile.
     * Coverage: FULL Original row inputs; Stance: EXTEND draw cache.
     * T-172 busy ladder profile: 338 instructions for an unchanged row.
     * Keep transfer cadence, force redraws and Redux's frame animation;
     * only avoid recalculating the same sixteen Original tile choices. */
    if (hud_id != ROOMROM_MAP_REDUX) {
        if (!s_hud_force && s_heart_row_valid &&
            s_heart_row_values == hearts && s_heart_row_partial == partial &&
            s_heart_row_hidden == s_hud_counts_hidden &&
            s_heart_row_col == col && s_heart_row_row == row) return;
        s_heart_row_valid = 1u;
        s_heart_row_values = hearts;
        s_heart_row_partial = partial;
        s_heart_row_hidden = s_hud_counts_hidden;
        s_heart_row_col = col;
        s_heart_row_row = row;
    } else {
        s_heart_row_valid = 0u;
    }
    /* Shared native tile choice; no transfer-buffer or scratch writes. */
    for (i = 0u; i < 16u; ++i) {
        unsigned char top = (i >= 8u);
        unsigned char x = (unsigned char)(col + (i & 7u));
        unsigned char y = (unsigned char)(row - top);
        unsigned char tile = s_hud_counts_hidden ? HUD_TILE_SPACE :
                             hud_heart_tile(hearts, partial, i);
        if (hud_id == ROOMROM_MAP_REDUX)
            tile = hud_heart_container_anim_override_tile(i, tile);
        if (!s_hud_force && s_heart_drawn[i] == tile) continue;
        s_heart_drawn[i] = tile;
        if (hud_id == ROOMROM_MAP_REDUX) {
            draw_hud_tile_b(x, y, tile == HUD_TILE_SPACE ? HUD_TILE_SPACE :
                            TILE_REDUX_HEART_OUTLINE, 0);
        }
        draw_hud_tile(x, y, tile, 1);
    }
}

/* Redux 4-row count strip at cols 12..14, HUD rows 2..5 (rupee/key/-/bomb).
 * Row 4 currently carries no NES analogue; show heart-count there for now. */
static void draw_status_counts_redux(void)
{
    draw_count_cell(g_inventory.rupees,                      12u, 2u, 0u);
    draw_count_cell((unsigned short)g_inventory.keys,        12u, 3u, 0u);
    draw_count_cell((unsigned short)heart_values_cur(g_inventory.heart_values),
                                                              12u, 4u, 0u);
    draw_count_cell((unsigned short)g_inventory.bombs,       12u, 5u, 0u);
}

/* Original HUD count strip mirrors NES rows 3/5/6 = HUD rows 2/4/5. The
 * static macro paints icon at col 11 + "X 0 " at cols 12..14. We overlay
 * the live 3-digit count at cols 12..14, leaving the icon untouched. */
static void draw_status_counts_original(void)
{
    if (s_hud_counts_hidden) {
        unsigned char r, k;
        static const unsigned char rows[3] = { 2u, 4u, 5u };
        for (r = 0u; r < 3u; ++r) {
            if (!s_hud_force && s_count_drawn[rows[r] - 2u] == 0xFFFFu) continue;
            s_count_drawn[rows[r] - 2u] = 0xFFFFu;
            for (k = 0u; k < 3u; ++k)
                draw_hud_tile((unsigned char)(12u + k), rows[r], HUD_TILE_SPACE, 0u);
        }
        return;
    }
    draw_count_cell(RAM(0x066Du),                12u, 2u, 0u);
    if (RAM(0x0664u)) {
        /* Magic key "XA": cache it as an out-of-range count value. */
        if (s_hud_force || s_count_drawn[2] != 0x0FFFu) {
            s_count_drawn[2] = 0x0FFFu;
            draw_hud_tile(12u, 4u, TILE_LOW_X, 0u);
            draw_hud_tile(13u, 4u, 0x0Au, 0u);
            draw_hud_tile(14u, 4u, HUD_TILE_SPACE, 0u);
        }
    } else {
        draw_count_cell((unsigned short)RAM(0x066Eu), 12u, 4u, 0u);
    }
    draw_count_cell((unsigned short)RAM(0x0658u), 12u, 5u, 0u);
}

/* NES source: Z_05.asm:InitMode3_Sub6/Sub7; Z_06 LevelNumberTransferBuf.
 * Drained C: room_init_mode3_sub6 / room_has_map.
 * Coverage: PARTIAL Original dungeon map background/ownership; Stance: EXTEND.
 * Room visits belong to the pause sheet. HUD outline requires the map. */
static unsigned char s_dungeon_map_owned = 0xFFu;
/* T-171: taking the map in play sets StatusBarMapTrigger; UpdateMode5Play
 * cues selector $44 a tick later and the NMI after that shows it
 * (t013_route: map bit t6986, $44 t6987, on screen t6988). The drain
 * reports the $44 cue here; the map is drawn on the next tick's refresh,
 * the frame the NES shows it. Load paths (status bar rebuilt, not mode 5)
 * draw at once. */
static unsigned char s_map_cue;
void roomrom_hud_status_bar_map_cue(void) { s_map_cue = 1u; }
static void draw_original_dungeon_map(void)
{
    unsigned char owned = room_has_map() ? 1u : 0u;
    unsigned char row, col;
    static const unsigned char label[6] = {0x15u,0x0Eu,0x1Fu,0x0Eu,0x15u,0x62u};
    if (s_map_cue) {
        s_map_cue = 0u;
        if (owned) s_dungeon_map_owned = 0xFEu;      /* redraw now */
    }
    if (owned == s_dungeon_map_owned) return;
    if (owned && s_dungeon_map_owned == 0u && RAM(0x0012u) == 0x05u)
        return;                                       /* wait for $44 */
    s_dungeon_map_owned = owned;
    for (row = 2u; row < 6u; ++row)
        for (col = 2u; col < 10u; ++col)
            draw_hud_tile(col, row, HUD_TILE_SPACE, 0u);
    if (owned) apply_transfer_macro(&nes_ram[0x6BCDu]);
    for (col = 0u; col < 6u; ++col)
        draw_hud_tile((unsigned char)(2u + col), 1u, label[col], 0u);
    draw_hud_tile(8u, 1u, RAM(0x6BB1u), 0u);
}

/* NES Z_01.asm:UpdatePositionMarker places tile $3E at four-pixel OW and
 * eight-pixel UW horizontal intervals, four-pixel vertical intervals.
 * A BG tile cannot distinguish adjacent rooms in this 16x8 grid. */
static short original_marker_x(unsigned char room, unsigned char is_uw)
{
    short x = is_uw ? 0x12 : 0x11;
    x = (short)(x + (short)(signed char)RAM(0x6BACu));
    x = (short)(x + (short)((room & 0x0Fu) << (is_uw ? 3u : 2u)));
    return x;
}

static short original_marker_y(unsigned char room)
{
    /* NES sprite Y is one above its first displayed scanline; Genesis
     * viewport omits the NES top eight lines: NES Y - 7. */
    return (short)(((room & 0x70u) >> 2) + 0x17u - 7u);
}

void roomrom_hud_refresh_marker(unsigned char room_id,
                                unsigned char is_underworld,
                                unsigned char frame_counter)
{
    if (s_hud_id_cached == 0xFFu)
        return;
    /* UpdatePlayerPositionMarker returns while WhirlwindTeleportingState
     * != 0 (Z_01.asm:4087); the sprites hidden by the room change stay
     * hidden until the drop-off (T-171 t171_flute_whirlwind t346). */
    if (RAM(0x0522u) != 0u && RAM(0x0012u) != 0x05u) {
        roomrom_sprites_hide_hud_marker(0u);
        roomrom_sprites_hide_hud_marker(1u);
        return;
    }
    if (RAM(0x0522u) != 0u) return;
    if (s_hud_id_cached == ROOMROM_MAP_REDUX) {
        roomrom_sprites_hide_hud_marker(0u);
        roomrom_sprites_hide_hud_marker(1u);
        return;
    }
    roomrom_sprites_set_hud_marker(0u,
        original_marker_x(room_id, is_underworld),
        original_marker_y(room_id), 0u);

    if (is_underworld && room_has_compass()) {
        unsigned char level = (unsigned char)RAM(0x0010u);
        unsigned char target = (unsigned char)RAM(0x6BAEu);
        unsigned char inactive = 1u;
        /* NES attr $03 normally; an uncollected L1-L8 piece, or the L9
         * target (which has no piece bit), flashes to attr $02. */
        if (((level >= 1u && level <= 8u &&
              (((unsigned char)RAM(0x0671u) & (1u << (level - 1u))) == 0u)) ||
             level == 9u) &&
            (frame_counter & 0x1Fu) < 0x10u) inactive = 0u;
        roomrom_sprites_set_hud_marker(1u,
            original_marker_x(target, 1u),
            original_marker_y(target), inactive);
    } else {
        roomrom_sprites_hide_hud_marker(1u);
    }
}

/* Phase J.2 (2026-05-18): upload_common_hud_tile / _range / _chr +
 * upload_redux_automap_chr deleted. All HUD CHR content is now
 * force-included in bg_sparse_chr (per gen_bg_sparse.py HUD_FORCE_TILES
 * + redux automap range 0x30..0x4F + common HUD ranges 0x00..0x15 +
 * 0x20..0x24 + 0x61..0x6E + 0xF7..0xF9). Legacy expanded_bg_chr_x4
 * arrays no longer in tree. */

void roomrom_hud_upload_chr(void)
{
    /* Phase J (2026-05-18): HUD content (digits, glyphs, hearts, map
     * marker, redux automap) is now force-included in bg_sparse_chr at
     * sub-pals 0/1/2 (see gen_bg_sparse.py HUD_FORCE_TILES + redux
     * automap range). The sparse upload performed by
     * roomrom_ow_room_render_upload_chr / roomrom_uw_room_render_upload_chr
     * covers all HUD tiles via the universal sparse layout.
     *
     * Custom HUD CHR (TILE_REDUX_HEART_OUTLINE/_MAP_MARKER/_HEART_FILL =
     * NES IDs 0x50/0x51/0x52) is also in HUD_FORCE_TILES; sparse atlas
     * carries them.
     *
     * Pre-Phase-J this function uploaded 4x sub-pal copies of HUD CHR
     * via legacy bank stride. Post-Phase-J: no-op. Kept for API stability
     * (single caller at RoomRom/src/main.c:1009; renaming would require
     * touching that file too). */
    (void)0;
}

static unsigned char s_native_hud_snapshot[8];
static const unsigned short s_native_hud_cells[8] = {0x0658u, 0x066Du, 0x066Eu, 0x066Fu, 0x0670u, 0x0664u, 0x0668u, 0x066Au};
static unsigned char native_hud_changed(void)
{
    unsigned char i;
    for (i = 0u; i < 8u; ++i)
        if (s_native_hud_snapshot[i] != RAM(s_native_hud_cells[i])) return 1u;
    return 0u;
}

static void draw_hud_dynamic_parts(unsigned char hud_id);

static void draw_hud_dynamic(unsigned char hud_id)
{
    unsigned char i;
    for (i = 0u; i < 8u; ++i) s_native_hud_snapshot[i] = RAM(s_native_hud_cells[i]);
    draw_hud_dynamic_parts(hud_id);
    s_hud_force = 0u;
}

static void draw_hud_dynamic_parts(unsigned char hud_id)
{
    if (hud_id == ROOMROM_MAP_REDUX) {
        draw_status_counts_redux();
        /* Redux heart row anchored at col 4 of HUD row 5 (top of display). */
        draw_hearts_row(4u, 5u, hud_id);
    } else {
        if (s_last_is_uw_cached) draw_original_dungeon_map();
        else s_map_cue = 0u; /* No dungeon map in this HUD. T-172: a mode-3
                             * $44 cue otherwise stays set throughout OW
                             * play and defeats hud_refresh_impl's gate. */
        draw_status_counts_original();
        /* Original NES paints hearts at NT row 6 cols 22..24 = HUD row 5. */
        draw_hearts_row(22u, 5u, hud_id);
    }
}

void roomrom_hud_draw(unsigned char hud_id, unsigned char room_id,
                      unsigned char is_underworld)
{
    const unsigned char *macro = s_original_hud_macro;
    if (hud_id == ROOMROM_MAP_REDUX)
        macro = is_underworld ? s_redux_uw_hud_macro : s_redux_ow_hud_macro;

    /* T-118: room-to-room entries redrew the whole HUD (static macro +
     * window clear, a frame of 68000 time at every scroll) although its
     * static part depends only on hud_id and OW/UW; NES leaves the status
     * bar alone across rooms. Same layout still on screen: refresh only
     * the dynamic cells. */
    if (s_hud_built && hud_id == s_hud_id_cached &&
        is_underworld == s_last_is_uw_cached &&
        RAM(0x0010u) == s_hud_level_cached) {
        s_last_room_id_cached = room_id;
        draw_hud_dynamic(hud_id);
        (void)inventory_hud_consume_dirty();
        return;
    }

    clear_hud_pal();
    clear_hud_window();
    clear_hud_b();
    apply_transfer_macro(macro);
    s_dungeon_map_owned = 0xFFu;
    s_hud_force = 1u;               /* planes were rebuilt: redraw all */
    s_hud_id_cached = hud_id;
    s_last_room_id_cached = room_id;
    s_last_is_uw_cached = is_underworld;
    draw_hud_dynamic(hud_id);
    (void)inventory_hud_consume_dirty();
    s_hud_built = 1u;
    s_hud_level_cached = RAM(0x0010u);
}

/* ---- T-092: status-bar item selection (Z_07.asm
 * DrawStatusBarItemsAndEnsureItemSelected, Z_05.asm
 * FindAndSelectOccupiedItemSlot / Cycle9InDirection). Items[] = $657,
 * indexed by item slot (bow +3, potion +7, letter +$0F, boomerangs +$1D /
 * +$1E); SelectedItemSlot = $656. */
#define HUD_ITEMS(slot)       RAM((unsigned short)(0x0657u + (slot)))
#define HUD_SELECTED_SLOT     RAM(0x0656u)

static unsigned char cycle9(unsigned char y, unsigned char dir)
{
    if ((dir & 0x03u) == 0u) return y;
    y = (unsigned char)(y + 1u);
    if ((dir & 0x01u) == 0u) y = (unsigned char)(y - 2u);
    if (y == 0xFFu) y = 8u;
    if (y == 9u) y = 0u;
    return y;
}

/* FindAndSelectOccupiedItemSlot: dir 1 forward, 2 backward, from y. */
static void find_and_select_occupied_item_slot(unsigned char dir, unsigned char y)
{
    signed char x = 9;
    RAM(0x00EFu) = dir;
    /* T-125: nothing selectable (the NES loop then runs all 10 steps and
     * settles on slot 0); NES re-runs this every frame with no B item. */
    if ((HUD_ITEMS(0x1Du) | HUD_ITEMS(0x1Eu) | HUD_ITEMS(1u) | HUD_ITEMS(4u) |
         HUD_ITEMS(5u) | HUD_ITEMS(6u) | HUD_ITEMS(7u) | HUD_ITEMS(8u) |
         HUD_ITEMS(0x0Fu)) == 0u &&
        (HUD_ITEMS(2u) == 0u || HUD_ITEMS(3u) == 0u)) {
        HUD_SELECTED_SLOT = 0u;
        return;
    }
    for (;;) {
        y = cycle9(y, dir);
        if (y == 0u) {
            /* CheckBoomerangs: magic first, then wooden. */
            if (HUD_ITEMS(0x1Eu) != 0u || HUD_ITEMS(0x1Du) != 0u) goto found;
            y = 0u;
        } else if (y != 3u) {
            if (HUD_ITEMS(y) != 0u) goto found;
            if (y == 7u) {
                /* CheckLetter. */
                if (HUD_ITEMS(0x0Fu) != 0u) {
                    y = (HUD_ITEMS(7u) != 0u) ? 7u : 0x0Fu;
                    HUD_SELECTED_SLOT = y;
                    return;
                }
                y = 7u;
            }
        }
        if (--x < 0) { y = 0u; goto found; }
        continue;
found:
        if (y == 2u && HUD_ITEMS(3u) == 0u) continue;   /* arrows need the bow */
        HUD_SELECTED_SLOT = y;
        return;
    }
}

/* NES source: Z_05.asm:FindAndSelectOccupiedItemSlot / Cycle9InDirection.
 * Drained C: find_and_select_occupied_item_slot above.
 * Coverage: FULL occupied-slot search, including arrow/bow and letter.
 * Stance: EXTEND; expose the existing owner to the pause renderer. */
void hud_select_inventory_item(unsigned char direction)
{
    find_and_select_occupied_item_slot(direction, HUD_SELECTED_SLOT);
}

/* Returns 1 and the item slot to draw in the B box this frame, 0 when the
 * NES draws no B item this frame (empty slot re-selected, or nothing). */
unsigned char hud_status_bar_b_item(unsigned char *slot_out)
{
    unsigned char x = HUD_SELECTED_SLOT;
    if (x == 0u) {
        /* DrawStatusBarBoomerang. */
        if (HUD_ITEMS(0x1Eu) != 0u) { *slot_out = 0x1Eu; return 1u; }
        if (HUD_ITEMS(0x1Du) != 0u) { *slot_out = 0x1Du; return 1u; }
        find_and_select_occupied_item_slot(2u, 0u);
        return 0u;
    }
    if (HUD_ITEMS(x) == 0u) {
        /* CheckMissingItem. */
        if (x == 7u && HUD_ITEMS(0x0Fu) != 0u) {
            HUD_SELECTED_SLOT = 0x0Fu;
            return 0u;             /* FindItemOrDrawSword: letter found */
        }
        find_and_select_occupied_item_slot(2u, x);
        return 0u;
    }
    if (x == 0x0Fu && HUD_ITEMS(7u) != 0u) {
        HUD_SELECTED_SLOT = 7u;    /* DrawStatusBarPotion */
        x = 7u;
    }
    *slot_out = x;
    return 1u;
}

void roomrom_hud_set_counts_hidden(unsigned char hidden)
{
    hidden = hidden ? 1u : 0u;
    if (hidden == s_hud_counts_hidden) return;
    s_hud_counts_hidden = hidden;
    if (s_hud_id_cached == 0xFFu) return;
    s_hud_force = 1u;
    /* NES status-bar text is a transfer record: shown after the next NMI
     * (the counts and hearts at InitMode5Play, t192_death_continue). */
    render_plane_defer(1u);
    draw_hud_dynamic(s_hud_id_cached);
    render_plane_defer(0u);
}

/* T-172: hide the counts and hearts for the next HUD draw without drawing
 * now (a level load then draws the HUD once, already blank). */
void roomrom_hud_preset_counts_hidden(void)
{
    if (s_hud_counts_hidden) return;
    s_hud_counts_hidden = 1u;
    s_hud_force = 1u;
}

void roomrom_hud_invalidate(void)
{
    s_hud_built = 0u;
    s_hud_force = 1u;
}

/* V2.4k: HUD position toggle. */
void roomrom_hud_set_bottom_mode(unsigned char bottom)
{
    if ((s_hud_bottom != 0u) == (bottom != 0u)) return;  /* no-op */
    /* T-172: the HUD is the same in both places; a complete one moves with
     * one VRAM copy. The full rebuild (window clear + status-bar macro,
     * ~6k instructions) cost the pause-open tick a frame (t013_continue
     * t61). The rows it leaves behind are outside the moved window. */
    if (s_hud_built && s_hud_id_cached != 0xFFu) {
        const unsigned short from = (unsigned short)HUD_WIN_ROW_BASE;
        s_hud_bottom = bottom ? 1u : 0u;
        /* Copied in the next VBlank (render_plane_defer_flush): a VRAM copy
         * during the display stalls the 68000 for its whole length. */
        render_window_move_deferred(from, (unsigned short)HUD_WIN_ROW_BASE,
                                    ROOMROM_HUD_ROWS, s_hud_bottom,
                                    s_hud_bottom ? HUD_PAUSE_WINDOW_ROWS
                                                 : ROOMROM_HUD_ROWS);
        return;
    }
    /* Clear old position first (HUD tiles at HUD_WIN_ROW_BASE) */
    clear_hud_window();
    /* Flip mode */
    s_hud_bottom = bottom ? 1u : 0u;
    /* Switch Window plane position */
    if (s_hud_bottom)
        render_set_window_on_bottom(HUD_PAUSE_WINDOW_ROWS);
    else
        render_set_window_on_top(ROOMROM_HUD_ROWS);
    /* Redraw HUD at new HUD_WIN_ROW_BASE if previously drawn */
    if (s_hud_id_cached != 0xFFu) {
        roomrom_hud_draw(s_hud_id_cached, s_last_room_id_cached,
                         s_last_is_uw_cached);
    }
}

/* Phase 6 Task 6.10.6 (Step A): per-frame live overlay. Repaints just
 * the dynamic count/heart cells from g_inventory. Cheap (fewer than 20
 * VDP_setTileMapXY calls) and keeps the rupee tick / damage path
 * observable without re-running the full static macro.
 *
 * SAT DMA Lag Fix Plan D (debate 2026-05-09): dirty-gate via inventory
 * snapshot. ~99% of ticks have unchanged inventory; skipping the redraw
 * saves ~15 active-display VDP_setTileMapXY writes per skipped frame. */
static unsigned char s_status_due;
static unsigned char s_play_hud_pending;

void roomrom_hud_status_bar_formatted(void)
{
    s_status_due = 1u;
}

static void hud_refresh_impl(unsigned char nes_cadence)
{
    if (s_hud_id_cached == 0xFFu)
        return; /* HUD has not been drawn yet — nothing to refresh. */
    /* T2.7: advance scale-up animation each vblank, regardless of
     * inventory-dirty state. The animation forces a redraw below so
     * the new tile makes it to VRAM. */
    hud_heart_container_anim_tick();
    unsigned char anim_active = hud_heart_container_anim_active();
    /* T-172: in play the status bar (hearts and counts) follows NES
     * World_ChangeRupees: formatted on even frames when no transfer record
     * is pending (roomrom_hud_status_bar_formatted), shown after the next
     * NMI (t050_rock_push t317 damage: NES t319). Keep changes pending. */
    if (nes_cadence && !anim_active && !s_map_cue && !s_status_due)
        return;
    s_status_due = 0u;
    if (!anim_active && !s_map_cue && !native_hud_changed() &&
        !inventory_hud_consume_dirty()) {
        return; /* Inventory unchanged + no anim — skip the VDP traffic. */
    }
    if (anim_active) {
        /* Consume any pending dirty so the next post-anim tick still
         * has a clean dirty bit for normal inventory changes. */
        (void)inventory_hud_consume_dirty();
    }
    /* T-172: NES status-bar updates are transfer records shown after the
     * next NMI; the cells are written at the start of the next tick
     * (t050_rock_push t318: a heart one frame early). */
    if (nes_cadence) {
        /* Drawn at the start of the next tick, in its VBlank (the NES NMI
         * transfer): the work leaves the busy play tick (t171_patra_sword
         * t429 overran with the hearts redraw). */
        s_play_hud_pending = 1u;
        return;
    }
    render_plane_defer(1u);
    draw_hud_dynamic(s_hud_id_cached);
    render_plane_defer(0u);
}

void roomrom_hud_refresh_dynamic(void)
{
    hud_refresh_impl(0u);
}

/* Mode-5 play tick: UpdateHeartsAndRupees cadence (see above). */
void roomrom_hud_refresh_play(void)
{
    hud_refresh_impl(1u);
}

/* Tick start, after the VBlank process: the play tick's status-bar draw. */
void roomrom_hud_play_flush(void)
{
    if (!s_play_hud_pending) return;
    s_play_hud_pending = 0u;
    if (s_hud_id_cached == 0xFFu) return;
    draw_hud_dynamic(s_hud_id_cached);
}
