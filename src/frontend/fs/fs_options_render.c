/* Phase 9 Task 9.3 — File Select OPTIONS submenu rendering.
 *
 * Reuses the FS scene's existing CHR upload (NES font tiles 0x00..0x09
 * = digits, 0x0A..0x23 = uppercase A..Z, 0x24 = space) so this module
 * uploads NO new VRAM data. All rows are written into Plane A via
 * render_plane_write_row.
 *
 * Cursor: heart sprite (already at VRAM tile 0x104) reused via SAT
 * entry 4; entries 0..3 (FS_NAV cursor + 3 slot Links) are hidden by
 * setting their Y offscreen.
 */

#include "fs_options_render.h"
#include "fs_options.h"
#include "fs_render.h"
#include "../../game/options/options_runtime.h"
#include "../../game/options/options_state.h"
#include "render_abi.h"

#define PLANE_A_BASE   0xC000
#define SAT_VRAM       0xF800
#define HEART_CHR_BASE 0x104

#define TILE_SPACE       0x24u
#define TILE_BORDER_VERT 0x6Cu
#define TILE_BORDER_BL   0x6Eu
#define TILE_BORDER_HORIZ 0x6Au
#define TILE_BORDER_BR   0x6Du
#define TILE_BORDER_TL   0x69u
#define TILE_BORDER_TR   0x6Bu

#define BORDER_LEFT_COL  3u
#define BORDER_RIGHT_COL 28u

#define HEADER_ROW   3u
#define FIRST_ROW    5u    /* first option row */
#define LABEL_COL    5u
#define VALUE_COL   18u
#define ROW_STRIDE   1u    /* rows are contiguous (15 rows fit in 24 row band) */

#define SAT_ENTRY_OPTIONS_CURSOR  4u

/* Glyph helpers (NES-letter encoding: 'A'=0x0A .. 'Z'=0x23). */
static unsigned char letter_tile(char c)
{
    if (c >= '0' && c <= '9') return (unsigned char)(c - '0');
    if (c >= 'A' && c <= 'Z') return (unsigned char)(0x0A + (c - 'A'));
    if (c >= 'a' && c <= 'z') return (unsigned char)(0x0A + (c - 'a'));
    return TILE_SPACE;
}

static void scratch_clear_row(unsigned short *cells)
{
    unsigned short c;
    for (c = 0u; c < 32u; ++c) cells[c] = TILE_SPACE;
    cells[BORDER_LEFT_COL]  = TILE_BORDER_VERT;
    cells[BORDER_RIGHT_COL] = TILE_BORDER_VERT;
}

static void scratch_write_text(unsigned short *cells, unsigned short col,
                               const char *s)
{
    while (*s != 0 && col < 32u) {
        cells[col] = letter_tile(*s);
        ++col;
        ++s;
    }
}

static void scratch_write_value(unsigned short *cells, const char *s)
{
    /* Right-trim by writing into VALUE_COL..VALUE_COL+8. */
    unsigned short col;
    for (col = VALUE_COL; col < VALUE_COL + 9u && col < BORDER_RIGHT_COL; ++col) {
        cells[col] = TILE_SPACE;
    }
    scratch_write_text(cells, VALUE_COL, s);
}

/* Per-id label (max 12 chars to leave room for value). Order matches
 * OPTION_ID_* enum so we can index by row. */
static const char *const ROW_LABELS[FS_OPTIONS_SAVE_ROW + 1u] = {
    "LOW HP WARN",     /* 0  LOW_HEALTH_WARNING bool   */
    "AUTOMAP",         /* 1  AUTOMAP             bool   */
    "DUNG COLOR",      /* 2  DUNGEON_COLORS      bool   */
    "SHOW SECRET",     /* 3  VISIBLE_SECRETS     bool   */
    "DIAG SWORD",      /* 4  DIAGONAL_SWORD      bool   */
    "NO FLASHING",     /* 5  NO_REDUCED_FLASHING bool   */
    "AB SWAP",         /* 6  AB_SWAP             bool   */
    "AUTO PICKUP",     /* 7  AUTO_COLLECT_DROPS  bool   */
    "SWORD",           /* 8  SWORD_STYLE         enum   */
    "LIKELIKE",        /* 9  LIKE_LIKE_BEHAVIOR  enum   */
    "BOMB UPGR",       /* 10 BOMB_UPGRADE        enum   */
    "START HP",        /* 11 START_HEARTS        numeric*/
    "LOSTWOODS",       /* 12 LOST_WOODS          enum   */
    "DARK ROOM",       /* 13 DARK_ROOM_LIGHT     enum   */
    "ROOM SCROLL",     /* 14 ROOM_SCROLL         enum   */
    "SAVE"             /* 15 SAVE row                    */
};

static const char *bool_value_str(unsigned char v) { return (v != 0u) ? "ON" : "OFF"; }

static const char *sword_value_str(unsigned char v)
{
    switch (v) {
    case OPTIONS_SWORD_VANILLA:     return "VANILLA";
    case OPTIONS_SWORD_STAB_ONLY:   return "STAB";
    case OPTIONS_SWORD_BEAM_ALWAYS: return "BEAM";
    default: return "VANILLA";
    }
}

static const char *likelike_value_str(unsigned char v)
{
    return (v == OPTIONS_LIKELIKE_NO_EAT) ? "NO EAT" : "VANILLA";
}

static const char *bombupg_value_str(unsigned char v)
{
    switch (v) {
    case OPTIONS_BOMBUPG_VANILLA: return "VANILLA";
    case OPTIONS_BOMBUPG_PLUS4:   return "PLUS 4";
    case OPTIONS_BOMBUPG_PLUS8:   return "PLUS 8";
    default: return "VANILLA";
    }
}

static const char *lwoods_value_str(unsigned char v)
{
    return (v == OPTIONS_LWOODS_RELAXED) ? "RELAX" : "VANILLA";
}

static const char *dark_value_str(unsigned char v)
{
    switch (v) {
    case OPTIONS_DARK_VANILLA: return "VANILLA";
    case OPTIONS_DARK_PARTIAL: return "PARTIAL";
    case OPTIONS_DARK_BRIGHT:  return "BRIGHT";
    default: return "VANILLA";
    }
}

static const char *scroll_value_str(unsigned char v)
{
    return (v == OPTIONS_SCROLL_CLASSIC) ? "CLASSIC" : "SMOOTH";
}

/* Format START HEARTS numeric as decimal "NN". */
static void format_decimal_2(unsigned char value, char out[3])
{
    unsigned char tens = (unsigned char)(value / 10u);
    unsigned char ones = (unsigned char)(value % 10u);
    if (tens != 0u) {
        out[0] = (char)('0' + tens);
        out[1] = (char)('0' + ones);
        out[2] = 0;
    } else {
        out[0] = (char)('0' + ones);
        out[1] = 0;
        out[2] = 0;
    }
}

static const char *value_string_for_row(uint8_t row, char numbuf[3])
{
    unsigned char v;

    if (row >= FS_OPTIONS_SAVE_ROW) return "";
    v = options_get((unsigned int)row);

    if (row <= OPTION_ID_AUTO_COLLECT_DROPS) return bool_value_str(v);
    switch (row) {
    case OPTION_ID_SWORD_STYLE:       return sword_value_str(v);
    case OPTION_ID_LIKE_LIKE_BEHAVIOR:return likelike_value_str(v);
    case OPTION_ID_BOMB_UPGRADE:      return bombupg_value_str(v);
    case OPTION_ID_START_HEARTS:
        format_decimal_2(v, numbuf);
        return numbuf;
    case OPTION_ID_LOST_WOODS:        return lwoods_value_str(v);
    case OPTION_ID_DARK_ROOM_LIGHT:   return dark_value_str(v);
    case OPTION_ID_ROOM_SCROLL:       return scroll_value_str(v);
    default: return "";
    }
}

static void write_row(unsigned short row_idx, uint8_t option_row)
{
    unsigned short cells[32];
    char numbuf[3] = {0,0,0};

    scratch_clear_row(cells);
    scratch_write_text(cells, LABEL_COL, ROW_LABELS[option_row]);
    if (option_row < FS_OPTIONS_SAVE_ROW) {
        scratch_write_value(cells, value_string_for_row(option_row, numbuf));
    }
    render_plane_write_row(PLANE_A_BASE, row_idx, cells, 32u);
}

static void write_header_row(unsigned short row_idx)
{
    unsigned short cells[32];
    /* Top border line: TL .. horizontal .. TR */
    unsigned short c;
    for (c = 0; c < 32u; ++c) cells[c] = TILE_SPACE;
    cells[BORDER_LEFT_COL]  = TILE_BORDER_TL;
    for (c = (unsigned short)(BORDER_LEFT_COL + 1u); c < BORDER_RIGHT_COL; ++c) {
        cells[c] = TILE_BORDER_HORIZ;
    }
    cells[BORDER_RIGHT_COL] = TILE_BORDER_TR;
    render_plane_write_row(PLANE_A_BASE, row_idx, cells, 32u);

    /* Title row: "OPTIONS" centered (col 12). */
    scratch_clear_row(cells);
    scratch_write_text(cells, 12u, "OPTIONS");
    render_plane_write_row(PLANE_A_BASE, (unsigned short)(row_idx + 1u), cells, 32u);
}

static void write_footer_row(unsigned short row_idx)
{
    unsigned short cells[32];
    unsigned short c;
    for (c = 0; c < 32u; ++c) cells[c] = TILE_SPACE;
    cells[BORDER_LEFT_COL]  = TILE_BORDER_BL;
    for (c = (unsigned short)(BORDER_LEFT_COL + 1u); c < BORDER_RIGHT_COL; ++c) {
        cells[c] = TILE_BORDER_HORIZ;
    }
    cells[BORDER_RIGHT_COL] = TILE_BORDER_BR;
    render_plane_write_row(PLANE_A_BASE, row_idx, cells, 32u);
}

static void hide_fs_nav_sprites(void)
{
    /* SAT entries 0..3 are FS_NAV cursor + 3 slot Links. Hide them by
     * setting size_link to 0 (link=0 terminates list immediately, but
     * leaving Y at offscreen is the canonical hide). Keep entry 4 as
     * the OPTIONS cursor, which we draw next via fs_options_render_cursor. */
    render_sat_write(SAT_VRAM, 0u, 0u, 0u, 0u, 0u);
    render_sat_write(SAT_VRAM, 1u, 0u, 0u, 0u, 0u);
    render_sat_write(SAT_VRAM, 2u, 0u, 0u, 0u, 0u);
    render_sat_write(SAT_VRAM, 3u, 0u, 0u, 0u, 0u);
}

void fs_options_render_full(uint8_t cursor_row)
{
    unsigned short i;
    unsigned short body_row;
    unsigned short cells[32];

    /* Wipe Plane A rows 0..29 to a clean state — old FS_NAV tiles bleed
     * through otherwise. Plane B was zeroed at fs_render_clear_screen. */
    for (i = 0; i < 32u; ++i) cells[i] = TILE_SPACE;
    for (i = 0; i < 30u; ++i) {
        render_plane_write_row(PLANE_A_BASE, i, cells, 32u);
    }

    write_header_row(HEADER_ROW);
    body_row = (unsigned short)(FIRST_ROW + 1u);  /* +1 because header is 2 lines */
    for (i = 0u; i < FS_OPTIONS_ROW_COUNT; ++i) {
        write_row((unsigned short)(body_row + i), (uint8_t)i);
    }
    /* Closing border one row below the SAVE row. */
    write_footer_row((unsigned short)(body_row + FS_OPTIONS_ROW_COUNT));

    hide_fs_nav_sprites();
    fs_options_render_cursor(cursor_row);
}

void fs_options_render_row_value(uint8_t row)
{
    unsigned short body_row = (unsigned short)(FIRST_ROW + 1u);
    write_row((unsigned short)(body_row + row), row);
}

void fs_options_render_cursor(uint8_t cursor_row)
{
    /* Cursor sprite at SAT entry 4. Y = (FIRST_ROW + 1 + cursor_row)*8
     * pixels; +128 SAT bias. X = label col - 2 cells = (LABEL_COL-2)*8
     * + 128. tile = HEART_CHR_BASE, palette 1 (cursor pal). */
    unsigned short body_row = (unsigned short)(FIRST_ROW + 1u);
    unsigned short y_pix = (unsigned short)((body_row + cursor_row) * 8u);
    unsigned short sat_y = (unsigned short)(y_pix + 128u);
    unsigned short sat_x = (unsigned short)(((LABEL_COL - 2u) * 8u) + 128u);
    unsigned short tile_attr = (unsigned short)((1u << 13) | (unsigned short)HEART_CHR_BASE);
    unsigned short size_link = 0x0000u; /* 1×1 cell, link=0 (end of list) */

    render_sat_write(SAT_VRAM, (uint8_t)SAT_ENTRY_OPTIONS_CURSOR,
                     sat_y, size_link, tile_attr, sat_x);
}

void fs_options_render_leave(uint8_t fs_nav_cursor)
{
    /* Hide OPTIONS cursor + redraw FS_NAV scene. */
    render_sat_write(SAT_VRAM, (uint8_t)SAT_ENTRY_OPTIONS_CURSOR, 0u, 0u, 0u, 0u);

    fs_render_clear_screen();
    fs_render_static_layout();
    fs_render_extra_rows();
    fs_render_all_slots();
    fs_render_cursor(fs_nav_cursor);
}
