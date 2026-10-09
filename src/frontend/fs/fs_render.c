/* src/fs_render.c — VDP primitives for native File Select. */
#include "fs_render.h"
#include "render_abi.h"
#include "save_game.h"   /* T-099 slot names / hearts / deaths */
#include <stdint.h>

/* Generated assets. */
extern const uint16_t fs_palettes[4][4];
extern const uint8_t  fs_static_tilemap[960];
extern const uint8_t  fs_static_attr[64];
extern const uint8_t  fs_link_sprite_chr[];
extern const uint8_t  fs_heart_cursor_chr[];
extern const uint8_t  fs_font_chr[];
extern const uint8_t  fs_border_chr[];

/* CRAM palette indices (matches src/gen/fs_palette.c layout).
 *
 * Genesis only has 4 CRAM palettes vs NES's 4 BG + 4 sprite. Layout:
 *   pal 0  BG attr 0 cells
 *   pal 1  BG attr 1 cells (LIFE/hearts) — also used by heart cursor sprite
 *   pal 2  bright Link  (OCCUPIED save slot)
 *   pal 3  faded  Link  (EMPTY    save slot — Redux dark-olive tint)
 * Heart cursor moved off pal 3 to free pal 3 for the empty-slot Link tint
 * (Redux menu_tweaks.asm:397-404). Cursor palette is now BG pal 1 — close
 * enough red/yellow shading to read as the NES light-red/white heart. */
#define PAL_BG_LINK_BRIGHT  2u
#define PAL_BG_LINK_FADED   3u
#define PAL_BG_CURSOR       1u

#define PLANE_A_BASE  0xC000
#define PLANE_B_BASE  0xE000  /* boot.asm Reg4=$8407 → plane B @ $E000 */
#define SAT_VRAM      0xF800  /* Sprite Attribute Table — boot.asm Reg5=$857C */

/* CHR tile indices in Genesis VRAM (set by fs_init):
 *   BG CHR block at tiles 0x00..0xFF (256 tiles covering all NES BG tiles).
 *   Link sprite CHR at tile 0x100..0x103 (4 tiles: left-top, left-bot, right-top, right-bot).
 *   Heart cursor CHR at tile 0x104 (1 tile).
 *
 * We place sprite tiles ABOVE the full BG block to avoid collision: the nametable
 * references NES BG tiles 0x80-0x84 for hearts/name display, so VRAM 0x80-0x84
 * must hold those BG glyphs, not Link sprite data.
 */
#define LINK_CHR_BASE   0x100
#define HEART_CHR_BASE  0x104

/* ---------------------------------------------------------------------------
 * SAT write helper.
 *
 * Genesis SAT layout (each entry = 8 bytes = 4 words):
 *   word 0: Y position  (bits 9:0, +128 bias; top 6 bits = 0)
 *   word 1: size + link (bits 15:11 = size, bits 6:0 = next-sprite link)
 *   word 2: tile_attr   (pri<<15 | pal<<13 | flipV<<12 | flipH<<11 | tile[10:0])
 *   word 3: X position  (bits 9:0, +128 bias; top 6 bits = 0)
 *
 * Size field encoding (bits 15:11 of word 1):
 *   H size (bits 12:11): 00=1cell, 01=2cells, 10=3cells, 11=4cells
 *   V size (bits 14:13): 00=1cell, 01=2cells, 10=3cells, 11=4cells
 *   e.g. 2H×2V (16×16px) = H=01,V=01 → bits 14:11 = 0101 → word1 = 0x0500
 *
 * entry: sprite slot index (0..79 in H32 mode)
 * y, x: screen pixel positions INCLUDING the +128 SAT offset.
 * size: encoded size word (e.g. 0x0500 for 2×2 cells = 16×16 px).
 * tile_attr: encoded tile+palette+priority word.
 * link: next sprite index (0 = end-of-list).
 * ---------------------------------------------------------------------------
 */
static void sat_write(uint8_t entry, uint16_t y, uint16_t size_link,
                      uint16_t tile_attr, uint16_t x) {
    render_sat_write(SAT_VRAM, entry, y, size_link, tile_attr, x);
}

/* ---------------------------------------------------------------------------
 * sat_clear: write a hidden (offscreen) entry.
 * Genesis hides a sprite when Y >= 480 in H32; simplest is Y=0 (above screen).
 * Using Y=0 (which maps to screen row -128, invisible) and link=0 to terminate.
 * ---------------------------------------------------------------------------
 */
static void sat_clear_entry(uint8_t entry) {
    sat_write(entry, 0, 0, 0, 0);
}

/* ---------------------------------------------------------------------------
 * fs_sram_slot_occupied: weak default — every slot empty until SRAM is wired.
 * Fresh ROM has no save data, so all 3 Link sprites tint with the faded
 * (empty-slot) palette. Real SRAM read lands in v3.1.
 * ---------------------------------------------------------------------------
 */
extern uint8_t fs_sram_slot_occupied(uint8_t slot);
__attribute__((weak)) uint8_t fs_sram_slot_occupied(uint8_t slot) {
    (void)slot;
    return 0;
}

void fs_render_clear_screen(void) {
    unsigned short zero_row[32];
    for (unsigned short i = 0; i < 32; i++) zero_row[i] = 0;
    /* V32 plane = 32 rows; clear all 32 rows of both planes.
     * Plane B must be cleared too — uninitialised cells point to VRAM tile 0,
     * which holds NES font tile 0 ('0' digit) → background fills with '0's. */
    for (unsigned short row = 0; row < 32; row++) {
        render_plane_write_row(PLANE_A_BASE, row, zero_row, 32u);
        render_plane_write_row(PLANE_B_BASE, row, zero_row, 32u);
    }
}

/* attr_palette_for_cell: decode NES attribute table to per-cell palette.
 *
 * NES attr byte at row=ar (0..7), col=ac (0..7) covers a 4×4 BG-cell region.
 * Each 2-bit field selects palette for one 2×2 sub-quadrant:
 *   bits 1:0 = top-left (cells [ar*4..ar*4+1, ac*4..ac*4+1])
 *   bits 3:2 = top-right (cells [ar*4..ar*4+1, ac*4+2..ac*4+3])
 *   bits 5:4 = bottom-left (cells [ar*4+2..ar*4+3, ac*4..ac*4+1])
 *   bits 7:6 = bottom-right (cells [ar*4+2..ar*4+3, ac*4+2..ac*4+3])
 */
static uint8_t attr_palette_for_cell(uint16_t row, uint16_t col) {
    uint16_t ar = row >> 2;          /* attr-row index 0..7 (last row 28-29 reuses ar=7) */
    uint16_t ac = col >> 2;
    if (ar >= 8) ar = 7;             /* clamp: 30 rows / 4 = 7.5 → ar=7 covers rows 28-29 */
    uint8_t b = fs_static_attr[ar * 8 + ac];
    uint16_t sub_row = (row >> 1) & 1u;   /* 0 = top half of 4×4, 1 = bottom half */
    uint16_t sub_col = (col >> 1) & 1u;
    uint8_t shift = (uint8_t)((sub_row << 2) | (sub_col << 1));  /* 0,2,4,6 */
    return (uint8_t)((b >> shift) & 0x03u);
}

/* v3: shift entire NES menu UP 3 rows so PLAYERS + OPTIONS fit inside the
 * border. Source row r' = r + FS_ROW_SHIFT. Dest rows past the shifted border
 * (>= 30 - SHIFT) get blank fill; PLAYERS/OPTIONS render on top via
 * fs_render_extra_rows. Link sprite + cursor Y tables also subtract the shift. */
#define FS_ROW_SHIFT  3u

void fs_render_static_layout(void) {
    unsigned short cells[32];
    for (unsigned short row = 0; row < 30; row++) {
        unsigned short src = (unsigned short)(row + FS_ROW_SHIFT);
        for (unsigned short col = 0; col < 32; col++) {
            uint8_t tile, pal;
            if (src < 30) {
                tile = fs_static_tilemap[src * 32 + col];
                pal  = attr_palette_for_cell(src, col);
            } else {
                tile = 0x24u;  /* space */
                pal  = 0u;
            }
            cells[col] = (uint16_t)(((uint16_t)pal & 0x3u) << 13) | (uint16_t)tile;
        }
        /* Shift COPY (dest row 18) + ERASE (dest row 20) text 1 cell right
         * to match PLAYERS/OPTIONS LABEL_COL=7 alignment. Side rails at
         * cols 3 + 28 stay untouched; the rightmost shifted cell drops
         * onto col 28's space slot, so we restore the right rail after. */
        if (row == 18u || row == 20u) {
            unsigned short right_rail = cells[28];
            for (int c = 28; c >= 5; c--) cells[c] = cells[c - 1];
            cells[4]  = 0x24u;   /* fill the new gap with a space */
            cells[28] = right_rail;
        }
        render_plane_write_row(PLANE_A_BASE, row, cells, 32u);
    }
}

/* ---------------------------------------------------------------------------
 * fs_render_slot: write Link sprite for one save slot row.
 *
 * NES geometry (Mode1_WriteLinkSprites, Z_02.asm:2634-2638):
 *   base_y = $58 (NES screen Y), X = $30; +$18 per slot.
 *   Slot 0: NES_Y=$58, Slot 1: $70, Slot 2: $88.
 *
 * Genesis SAT coordinates = NES + 128:
 *   Slot 0: sat_y=$D8 (216), Slot 1: $F0 (240), Slot 2: $108 (264)
 *   X: sat_x = 0x30 + 128 = 0xB0 (176)
 *
 * Link occupies 2×2 cells (16×16 px) on Genesis using a single SAT entry
 * with size=0x0500 (H=2cells,V=2cells). The 4 Genesis tiles at LINK_CHR_BASE
 * cover: [0x80]=left-top, [0x81]=left-bot, [0x82]=right-top, [0x83]=right-bot.
 * Genesis stores them as col-major: tile_index=0x80 → left col, 0x82 → right col.
 *
 * Palette assignment:
 *   Occupied slot: palette index matches slot number (0,1,2) → colors green/blue/red.
 *   Empty slot: palette index 3 → dim/grey palette.
 * ---------------------------------------------------------------------------
 */
void fs_render_slot(uint8_t slot_idx) {
    /* NES Y base for Link sprites = $58; increment $18 per slot.
     * v3 shifts UI up FS_ROW_SHIFT*8 px to make room for PLAYERS/OPTIONS. */
    uint16_t nes_y = (uint16_t)(0x58u - (FS_ROW_SHIFT * 8u) + (uint16_t)slot_idx * 0x18u);
    uint16_t sat_y = (uint16_t)(nes_y + 128u);   /* +128 SAT bias */
    uint16_t sat_x = (uint16_t)(0x30u + 128u);   /* NES X=$30, +128 bias → 0xB0 */

    /* Per-slot tint matching NES Redux:
     *   occupied slot → pal 2 (bright green Link)
     *   empty    slot → pal 3 (faded dark-olive Link) — Redux menu_tweaks.asm
     *                   :397-404 writes $19/$17/$07 to sprite pal buf for any
     *                   slot where $0633,y == 0.
     * Per-slot color (blue slot 1 / red slot 2) still deferred — all 3 occupied
     * slots share pal 2 because Gen has only 4 CRAM palettes. */
    uint8_t sat_entry = (uint8_t)(1u + slot_idx);
    uint16_t palette = fs_sram_slot_occupied(slot_idx)
                     ? (uint16_t)PAL_BG_LINK_BRIGHT
                     : (uint16_t)PAL_BG_LINK_FADED;

    /* tile_attr: priority=0, palette=palette, no flip, tile=LINK_CHR_BASE.
     * Genesis tile_attr word: pri(15) | pal(14:13) | flipV(12) | flipH(11) | tile(10:0) */
    uint16_t tile_attr = (uint16_t)((palette & 0x3u) << 13) | (uint16_t)LINK_CHR_BASE;

    /* size_link: H=2cells,V=2cells → 0x0500; link → next sprite in SAT chain.
     * Chain: cursor (entry 0) → slot 0 (1) → slot 1 (2) → slot 2 (3) → end.
     * Slot 2 (last) terminates with link=0; others link to entry+1. */
    /* Slot 2 links to entry 4, the name-entry cursor (hidden unless the
     * register board is open; T-099). */
    uint8_t next_link = (slot_idx == 2u) ? 4u : (uint8_t)(sat_entry + 1u);
    uint16_t size_link = (uint16_t)(0x0500u | next_link);

    /* Sprite entries: use slots 1, 2, 3 (slot 0 reserved for cursor). */
    sat_write(sat_entry, sat_y, size_link, tile_attr, sat_x);
}

/* ---------------------------------------------------------------------------
 * fs_render_cursor: write heart sprite at the currently selected row.
 *
 * NES OAM: Mode1CursorSpriteTriplet = {tile=$F3, attr=$03, X=$28}.
 * Mode1CursorSpriteYs = {$5C,$74,$8C,$A8,$B8} for slots 0..4.
 *
 * Genesis: heart cursor is a single 1×1 cell sprite (8×8 px).
 * Palette: attr $03 = palette 3 (4th sprite palette on NES). Use palette 3.
 * X: NES $28 = 40; sat_x = 40+128 = 168 = $A8.
 * Y: NES slot Y table values +128.
 * ---------------------------------------------------------------------------
 */
void fs_render_cursor(uint8_t row) {
    /* NES slot Y for cursor (Mode1CursorSpriteYs, Z_02.asm:2591-2592) +
     * v3 Redux extension: PLAYERS row 25 → Y $C8, OPTIONS row 26 → Y $D0.
     * (NT_row * 8 matches NES convention used for COPY/ERASE rows.) */
    /* All Ys reduced by FS_ROW_SHIFT*8 (=24) vs NES original to match v3 menu shift.
     * PLAYERS row = 23 (post-shift), OPTIONS row = 24. */
    static const uint8_t cursor_ys[7] = {
        0x44, 0x5C, 0x74, 0x90, 0xA0,    /* slot0..ERASE shifted up 24 */
        0xB8,  /* PLAYERS — row 23 (Y = 23*8) */
        0xC8   /* OPTIONS — row 25 (Y = 25*8); row 24 is visual gap */
    };
    if (row >= 7) return;

    uint16_t sat_y = (uint16_t)(cursor_ys[row] + 128u);
    uint16_t sat_x = (uint16_t)(0x28u + 128u);   /* NES X=$28 */

    /* tile_attr: palette=PAL_BG_CURSOR (Gen pal 1, shared with BG attr 1), no flip, tile=HEART_CHR_BASE. */
    uint16_t tile_attr = (uint16_t)((uint16_t)PAL_BG_CURSOR << 13) | (uint16_t)HEART_CHR_BASE;

    /* size: 1×1 cell (8×8 px) = 0x0000 size field.
     * link=1 chains scan to SAT entry 1 (slot 0 Link sprite). */
    uint16_t size_link = 0x0001u;

    /* SAT entry 0: cursor is always first (matches NES Sprites[0]). */
    sat_write(0, sat_y, size_link, tile_attr, sat_x);
}

void fs_render_all_slots(void) {
    for (uint8_t i = 0; i < 3; i++) fs_render_slot(i);
    fs_render_name_cursor(0u, 0u, 0u);
    for (uint8_t i = 0; i < 3; i++) fs_render_slot_text(i);
}

/* ---------------------------------------------------------------------------
 * T-099: slot line text, from the NES slot info (Names $638, SaveSlotHearts
 * $650, DeathCounts $630) filled at title Start by save_game_boot().
 *
 * NES Mode1SlotLineTransferBuf (Z_02.asm:2108) + InitMode1_FillAndTransfer-
 * SlotTiles: slot k line starts at PPU $2109 + $60*(k+1) (the offset loop
 * runs CurSaveSlot+1 times): name 8 chars at row 11+3k col 9, $62 at col
 * 17, hearts top row cols 18..25 (hearts 8..15) and $2132+$60*(k+1) =
 * row 12+3k cols 18..25 (hearts 0..7). Death count (Mode1DeathCounts-
 * TransferBuf) at row 12+3k col 9, 3 chars. Genesis rows = NES - 3
 * (FS_ROW_SHIFT). Palettes follow the static layout: text pal 0, LIFE
 * (hearts) pal 1.
 * ---------------------------------------------------------------------------
 */
#define SLOT_NAME_COL   9u
#define SLOT_DASH_COL  17u
#define SLOT_HEART_COL 18u
#define TILE_DASH      0x2Fu   /* the FS layout's own dash glyph ($2F in this CHR) */
#define PAL_TEXT        0u
#define PAL_LIFE        1u

static void render_side_only_row(unsigned short row);
static uint16_t slot_row(uint8_t slot) { return (uint16_t)(8u + 3u * slot); }

static void put_cell(uint16_t row, uint16_t col, uint16_t tile, uint8_t pal)
{
    render_vram_open_write((unsigned short)(PLANE_A_BASE + row * 64u + col * 2u));
    render_vram_write_word((uint16_t)(((uint16_t)pal & 3u) << 13) | tile);
}

/* NES font code -> FS VRAM tile. The FS BG block is a Redux capture whose
 * glyphs match the NES except these (fs_main.c uploads the NES ones). */
static uint16_t glyph(uint8_t nes_code)
{
    switch (nes_code) {
    case 0x00: return 0x105u;
    case 0x62: return 0x106u;
    case 0x63: return 0x107u;
    case 0x2B: return 0x108u;
    default:   return nes_code;
    }
}

/* NES FormatHeartsInTextBuf tile rule (twin of hud_heart_tile in
 * src/game/hud; duplicated here to keep src/frontend free of src/game). */
static uint8_t heart_tile(uint8_t hearts, uint8_t partial, uint8_t index)
{
    uint8_t whole = (uint8_t)(hearts & 15u);
    if (!hearts || index > (hearts >> 4) || index >= 16u) return 0x24u;
    if (index < whole) return 0xF2u;
    if (index > whole || !partial) return 0x66u;
    return partial >= 0x80u ? 0xF2u : 0x65u;
}

void fs_render_slot_text(uint8_t slot)
{
    const volatile unsigned char *name = save_game_slot_name(slot);
    uint8_t active = save_game_slot_active(slot);
    uint8_t hv = save_game_slot_hearts(slot);
    uint8_t hp = save_game_slot_heart_partial(slot);
    uint16_t row = slot_row(slot);
    uint8_t i;

    for (i = 0u; i < 8u; ++i)
        put_cell(row, (uint16_t)(SLOT_NAME_COL + i), active ? glyph(name[i]) : 0x24u, PAL_TEXT);
    put_cell(row, SLOT_DASH_COL, TILE_DASH, PAL_TEXT);
    for (i = 0u; i < 8u; ++i) {
        put_cell(row, (uint16_t)(SLOT_HEART_COL + i),
                 active ? heart_tile(hv, hp, (uint8_t)(8u + i)) : 0x24u, PAL_LIFE);
        put_cell((uint16_t)(row + 1u), (uint16_t)(SLOT_HEART_COL + i),
                 active ? heart_tile(hv, hp, i) : 0x24u, PAL_LIFE);
    }
    /* FormatDecimalByte: 3 digits, leading spaces; an inactive slot shows
     * blanks, an active one at least "0". */
    {
        uint8_t d = save_game_slot_deaths(slot);
        uint8_t h = (uint8_t)(d / 100u), t = (uint8_t)((d / 10u) % 10u), o = (uint8_t)(d % 10u);
        put_cell((uint16_t)(row + 1u), SLOT_NAME_COL,       (active && h) ? glyph(h) : 0x24u, PAL_TEXT);
        put_cell((uint16_t)(row + 1u), SLOT_NAME_COL + 1u,  (active && (h || t)) ? glyph(t) : 0x24u, PAL_TEXT);
        put_cell((uint16_t)(row + 1u), SLOT_NAME_COL + 2u,  active ? glyph(o) : 0x24u, PAL_TEXT);
    }
}

/* Name-entry cursor: SAT entry 4, heart tile under the current name
 * character (NES ModeEandFCursorSprites uses the same $F3 heart). */
void fs_render_name_cursor(uint8_t show, uint8_t col, uint16_t row)
{
    uint16_t tile_attr = (uint16_t)((uint16_t)PAL_BG_CURSOR << 13) | (uint16_t)HEART_CHR_BASE;
    if (!show) { sat_write(4u, 0u, 0u, tile_attr, 0u); return; }
    sat_write(4u, (uint16_t)(row * 8u + 128u), 0x0000u, tile_attr, (uint16_t)(col * 8u + 128u));
}

/* ---------------------------------------------------------------------------
 * T-099 name registration board (NES Mode $E, ModeE_CharMap Z_02.asm:1351):
 * 44 characters, 11 per row, 4 rows. Drawn inside the File Select border in
 * place of the COPY/ERASE/PLAYERS/OPTIONS rows while registering.
 * ---------------------------------------------------------------------------
 */
static const uint8_t k_char_map[44] = {
    0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
    0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
    0x20, 0x21, 0x22, 0x23, 0x62, 0x63, 0x28, 0x29, 0x2A, 0x2B, 0x2C,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x24
};
#define BOARD_TITLE_ROW 17u
#define BOARD_ROW0      19u
#define BOARD_COL0       6u
static const uint8_t k_register_title[18] = {   /* REGISTER YOUR NAME */
    0x1B, 0x0E, 0x10, 0x12, 0x1C, 0x1D, 0x0E, 0x1B, 0x24,
    0x22, 0x18, 0x1E, 0x1B, 0x24, 0x17, 0x0A, 0x16, 0x0E
};

uint8_t fs_board_char(uint8_t idx) { return k_char_map[idx % 44u]; }

void fs_board_cell(uint8_t idx, uint8_t *col, uint16_t *row)
{
    *col = (uint8_t)(BOARD_COL0 + 2u * (idx % 11u));
    *row = (uint16_t)(BOARD_ROW0 + 2u * (idx / 11u));
}

void fs_render_register_board(void)
{
    uint16_t r;
    uint8_t i;
    for (r = 16u; r <= 25u; ++r) render_side_only_row(r);
    for (i = 0u; i < 18u; ++i) put_cell(BOARD_TITLE_ROW, (uint16_t)(7u + i), k_register_title[i], PAL_TEXT);
    for (i = 0u; i < 44u; ++i) {
        uint8_t c; uint16_t row;
        fs_board_cell(i, &c, &row);
        put_cell(row, c, glyph(k_char_map[i]), PAL_TEXT);
    }
}

/* Name field while typing: the chosen slot's name row, from a buffer. */
void fs_render_name_field(uint8_t slot, const uint8_t *name)
{
    uint8_t i;
    for (i = 0u; i < 8u; ++i)
        put_cell(slot_row(slot), (uint16_t)(SLOT_NAME_COL + i), glyph(name[i]), PAL_TEXT);
}

uint16_t fs_slot_name_row(uint8_t slot) { return slot_row(slot); }

/* Board cursor: the heart sprite (SAT entry 0) left of the character. */
void fs_render_board_cursor(uint8_t idx)
{
    uint8_t c; uint16_t row;
    uint16_t tile_attr = (uint16_t)((uint16_t)PAL_BG_CURSOR << 13) | (uint16_t)HEART_CHR_BASE;
    fs_board_cell(idx, &c, &row);
    sat_write(0u, (uint16_t)(row * 8u + 128u), 0x0001u, tile_attr, (uint16_t)((c - 1u) * 8u + 128u));
}

/* ---------------------------------------------------------------------------
 * v3 Redux extension: PLAYERS + OPTIONS rows.
 *
 * NES capture has 5 menu rows (3 slots + COPY + ERASE). Redux extends FS with
 * PLAYERS (cycle 1..4) and OPTIONS (submenu, v5). These rows aren't in the
 * captured nametable, so we render them in C using existing font tiles
 * (A-Z at NES BG tiles 0x0A-0x23, digits 0-9 at 0x00-0x09).
 *
 * Layout (NT cells, palette 0):
 *   row 25 col 4..10 = "PLAYERS", col 13 = digit (1..4)
 *   row 26 col 4..10 = "OPTIONS"
 *
 * These match NES letter encoding so the same fs_bg_chr_full block renders
 * them correctly without extra CHR upload.
 * ---------------------------------------------------------------------------
 */
/* v3 row layout (post FS_ROW_SHIFT=3):
 *   row 22 = side-rails only (replaces shifted-down NES bottom border)
 *   row 23 = PLAYERS  N
 *   row 24 = side-rails only (visual gap between PLAYERS and OPTIONS)
 *   row 25 = OPTIONS
 *   row 26 = bottom border line
 * All inside the extended border. */
#define PLAYERS_ROW   23u
#define OPTIONS_ROW   25u
#define LABEL_COL      7u   /* +1 col right vs original NES NT for COPY/ERASE row alignment */
#define DIGIT_COL     16u   /* "PLAYERS" at col 7..13, 2 spaces, digit at col 16 */
#define EXTRA_PAL      0u   /* palette 0 — same as COPY/ERASE labels */

static const uint8_t TILE_PLAYERS[7] = { 0x19, 0x15, 0x0A, 0x22, 0x0E, 0x1B, 0x1C };  /* P L A Y E R S */
static const uint8_t TILE_OPTIONS[7] = { 0x18, 0x19, 0x1D, 0x12, 0x18, 0x17, 0x1C };  /* O P T I O N S */
#define TILE_SPACE       0x24u
#define TILE_BORDER_VERT 0x6Cu  /* NES side-border tile (col 3 + col 28 in rows 4..24) */
#define TILE_BORDER_BL   0x6Eu  /* NES bottom-left corner tile (row 25 col 3) */
#define TILE_BORDER_HORIZ 0x6Au /* NES bottom horizontal tile (row 25 cols 4..27) */
#define TILE_BORDER_BR   0x6Du  /* NES bottom-right corner tile (row 25 col 28) */
#define BORDER_LEFT_COL   3u
#define BORDER_RIGHT_COL 28u
#define BOTTOM_BORDER_ROW 26u   /* v3: extended bottom border row (post-shift target) */

static unsigned short s_extra_row_buf[32];

/* Build a row with side borders + label text. Empty cells = space; cols 3 + 28
 * are the vertical border tiles so the box extends to enclose the text. */
static void render_label_row(unsigned short row, const uint8_t *text7) {
    for (unsigned short c = 0; c < 32; c++) s_extra_row_buf[c] = (uint16_t)TILE_SPACE;
    s_extra_row_buf[BORDER_LEFT_COL]  = (uint16_t)TILE_BORDER_VERT;
    s_extra_row_buf[BORDER_RIGHT_COL] = (uint16_t)TILE_BORDER_VERT;
    for (unsigned short i = 0; i < 7u; i++) {
        s_extra_row_buf[LABEL_COL + i] = (uint16_t)((EXTRA_PAL & 0x3u) << 13) | (uint16_t)text7[i];
    }
    render_plane_write_row(PLANE_A_BASE, row, s_extra_row_buf, 32u);
}

/* Replace the bottom-border line that the shifted nametable wrote into row
 * (25 - SHIFT) = 22 with plain side borders, so PLAYERS/OPTIONS rows below it
 * are inside the enclosing box. */
static void render_side_only_row(unsigned short row) {
    for (unsigned short c = 0; c < 32; c++) s_extra_row_buf[c] = (uint16_t)TILE_SPACE;
    s_extra_row_buf[BORDER_LEFT_COL]  = (uint16_t)TILE_BORDER_VERT;
    s_extra_row_buf[BORDER_RIGHT_COL] = (uint16_t)TILE_BORDER_VERT;
    render_plane_write_row(PLANE_A_BASE, row, s_extra_row_buf, 32u);
}

/* Write the closing bottom-border line: BL corner, horizontal, BR corner. */
static void render_bottom_border_row(unsigned short row) {
    for (unsigned short c = 0; c < 32; c++) s_extra_row_buf[c] = (uint16_t)TILE_SPACE;
    s_extra_row_buf[BORDER_LEFT_COL]  = (uint16_t)TILE_BORDER_BL;
    for (unsigned short c = BORDER_LEFT_COL + 1u; c < BORDER_RIGHT_COL; c++) {
        s_extra_row_buf[c] = (uint16_t)TILE_BORDER_HORIZ;
    }
    s_extra_row_buf[BORDER_RIGHT_COL] = (uint16_t)TILE_BORDER_BR;
    render_plane_write_row(PLANE_A_BASE, row, s_extra_row_buf, 32u);
}

void fs_render_extra_rows(void) {
    /* Erase the shifted bottom border that landed at row (NES 25 - SHIFT) = 22,
     * insert a visual gap row 24 between PLAYERS and OPTIONS, extend the side
     * rails through to row 25, and close the box with a bottom border row. */
    render_side_only_row(22u);
    render_label_row(PLAYERS_ROW, TILE_PLAYERS);
    render_side_only_row(24u);                     /* gap row between PLAYERS and OPTIONS */
    render_label_row(OPTIONS_ROW, TILE_OPTIONS);
    render_bottom_border_row(BOTTOM_BORDER_ROW);
}

void fs_render_players_row(uint8_t value) {
    /* Patch the digit cell at row PLAYERS_ROW col DIGIT_COL in-place.
     * Range clamp 1..4 (caller already wraps, but be defensive).
     * NES digit '1'..'4' = BG tile 0x01..0x04. */
    if (value < 1u) value = 1u;
    if (value > 4u) value = 4u;

    unsigned short addr = (unsigned short)(PLANE_A_BASE + (PLAYERS_ROW * 64u) + (DIGIT_COL * 2u));
    render_vram_open_write(addr);
    render_vram_write_word((uint16_t)((EXTRA_PAL & 0x3u) << 13) | (uint16_t)value);
}
