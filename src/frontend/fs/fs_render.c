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

/* Palette 0: BG text/border. Palette 1: LIFE entries 0..3 and heart
 * cursor entries 7..9. Each slot uses its own palette 1..3, entries 4..6,
 * so occupied green and faded colours remain independent on menu 1. */
#define PAL_BG_CURSOR 1u

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

/* The reference's 224-line viewport crops eight lines from the NES top.
 * Keep source-space coordinates and translate them once at each writer. */
#define FS_ROW_SHIFT  1u /* NES reference screenshots crop the top eight pixels. */
static uint8_t s_misc_scene;
static uint8_t s_misc_copy;
static uint8_t s_register_slot = 0xFFu;
static uint8_t s_anim_clock;
static uint8_t s_anim_slot = 0xFFu;
static uint8_t s_board_index, s_name_col;
static uint16_t s_name_row;
static void write_text(uint16_t row, uint8_t col, const char *text);
static void box(uint8_t left,uint8_t right,uint16_t top,uint16_t bottom);
static void put_cell(uint16_t row,uint16_t col,uint16_t tile,uint8_t pal);

void fs_render_static_layout(void) {
    render_cram_open_write_byte(34u);
    render_vram_write_word(fs_palettes[1][1]);
    s_misc_scene = 0u;
    s_register_slot = 0xFFu;
    s_anim_clock = 0u;
    s_anim_slot = 0xFFu;
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
        render_plane_write_row(PLANE_A_BASE, row, cells, 32u);
    }
    /* Extend the original border to enclose both new actions. */
    for (uint8_t c=4u;c<28u;c++) put_cell(25u,c,0x24u,0u);
    box(3u,28u,8u,28u);
    write_text(8u,10u,"NAME");
    write_text(25u,9u,"RENAME SAVE");
    write_text(27u,9u,"SOUND TEST");
    write_text(8u,20u,"MODE");
}

/* Redux main Link positions: ($30,$58 + slot*$18) in NES OAM space.
 * Misc menus use ($50,$30 + slot*$18); visible Y is OAM Y+1, minus crop. */
void fs_render_slot(uint8_t slot_idx) {
    uint16_t nes_y = (uint16_t)((s_misc_scene ? 0x30u : 0x58u) + (uint16_t)slot_idx * 0x18u);
    uint16_t sat_y = (uint16_t)(nes_y + 129u - FS_ROW_SHIFT*8u);   /* +128 SAT bias */
    uint16_t sat_x = (uint16_t)((s_misc_scene ? 0x50u : 0x30u) + 128u);   /* NES X=$30, +128 bias → 0xB0 */

    uint8_t sat_entry = (uint8_t)(1u + slot_idx);
    uint16_t palette = (uint16_t)(slot_idx + 1u);
    /* Link pixels use entries 4..6, leaving BG LIFE and cursor colours
     * independent in the same Genesis palette. Exact Redux NES colours. */
    static const unsigned short bright[3][3] = {
        {0x00E6u, 0x008Eu, 0x0048u},
        {0x0E88u, 0x008Eu, 0x0048u},
        {0x002Cu, 0x008Eu, 0x0048u}
    };
    static const unsigned short faded[3] = {0x0082u, 0x0048u, 0x0024u};
    render_cram_open_write_byte((unsigned short)(palette * 32u + 8u));
    render_vram_write_words((fs_sram_slot_occupied(slot_idx) || s_register_slot == slot_idx)
                            ? bright[s_misc_scene ? slot_idx : 0u] : faded, 3u);

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
    static const uint8_t cursor_ys[7] = {0x5C,0x74,0x8C,0xA8,0xB8,0xC8,0xD8};
    if (row >= 7u) return;

    uint16_t sat_y = (uint16_t)(cursor_ys[row] + 129u - FS_ROW_SHIFT*8u);
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
 * the cropped viewport. Palettes follow the static layout: text pal 0, LIFE
 * (hearts) pal 1.
 * ---------------------------------------------------------------------------
 */
#define SLOT_NAME_COL   9u
#define SLOT_DASH_COL  17u
#define SLOT_HEART_COL 18u
#define TILE_DASH      0x2Fu   /* the FS layout's own dash glyph ($2F in this CHR) */
#define PAL_TEXT        0u
#define PAL_LIFE        1u

static uint16_t slot_row(uint8_t slot) { return (uint16_t)((s_misc_scene ? 6u : 11u) + 3u * slot); }

static void put_cell(uint16_t row, uint16_t col, uint16_t tile, uint8_t pal)
{
    render_vram_open_write((unsigned short)(PLANE_A_BASE + (row-FS_ROW_SHIFT) * 64u + col * 2u));
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
    uint16_t row = slot_row(slot);
    uint8_t i;
    if (s_misc_scene) {
        for (i = 0u; i < 8u; ++i)
            put_cell(row, (uint16_t)(13u + i), active ? glyph(name[i]) : 0x24u, PAL_TEXT);
        return;
    }

    for (i = 0u; i < 8u; ++i)
        put_cell(row, (uint16_t)(SLOT_NAME_COL + i), active ? glyph(name[i]) : 0x24u, PAL_TEXT);
    put_cell(row, SLOT_DASH_COL, 0x24u, PAL_TEXT);
    for (i = 0u; i < 8u; ++i) {
        put_cell(row, (uint16_t)(SLOT_HEART_COL + i),
                 0x24u, PAL_LIFE);
        put_cell((uint16_t)(row + 1u), (uint16_t)(SLOT_HEART_COL + i),
                 0x24u, PAL_LIFE);
    }
    /* FormatDecimalByte: 3 digits, leading spaces; an inactive slot shows
     * blanks, an active one at least "0". */
    {
        for (i=0u;i<3u;i++) put_cell(row+1u,SLOT_NAME_COL+i,0x24u,0u);
    }
    if (active) write_text(row,SLOT_HEART_COL,
        save_game_slot_mode(slot)==SAVE_MODE_MD_REMIX ? "MD REMIX" : "ORIGINAL");
}

/* Name-entry cursor: SAT entry 4, heart tile under the current name
 * character (NES ModeEandFCursorSprites uses the same $F3 heart). */
void fs_render_name_cursor(uint8_t show, uint8_t col, uint16_t row)
{
    s_name_col = col; s_name_row = row;
    if (!show) { sat_write(4u,0u,0u,0u,0u); sat_write(5u,0u,0u,0u,0u); return; }
    /* Redux register uses blinking block cursors, not another heart. */
    sat_write(4u, (s_anim_clock & 8u) ? (uint16_t)(row*8u+128u-FS_ROW_SHIFT*8u) : 0u,
              5u, (PAL_BG_CURSOR<<13)|0x10Du, (uint16_t)(col*8u+128u));
}

/* ---------------------------------------------------------------------------
 * T-099 name registration board (NES Mode $E, ModeE_CharMap Z_02.asm:1351):
 * 44 characters, 11 per row, 4 rows. Redux adds its own alphabet box.
 * ---------------------------------------------------------------------------
 */
static const uint8_t k_char_map[44] = {
    0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
    0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
    0x20, 0x21, 0x22, 0x23, 0x2F, 0x2C, 0x28, 0x29, 0x2A, 0x2B, 0x2E,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x24
};
#define BOARD_TITLE_ROW 3u
#define BOARD_ROW0      17u
#define BOARD_COL0       6u

uint8_t fs_board_char(uint8_t idx) { return k_char_map[idx % 44u]; }

void fs_board_cell(uint8_t idx, uint8_t *col, uint16_t *row)
{
    *col = (uint8_t)(BOARD_COL0 + 2u * (idx % 11u));
    *row = (uint16_t)(BOARD_ROW0 + 2u * (idx / 11u));
}

static uint8_t ascii_tile(char c) {
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c-'A'+0x0Au);
    if (c >= '0' && c <= '9') return (uint8_t)(c-'0');
    if (c == '?') return 0x2Eu;
    if (c == '\'') return 0x2Au; /* Existing font apostrophe. */
    return 0x24u;
}
static void write_text(uint16_t row, uint8_t col, const char *text) {
    while (*text && col < 32u) put_cell(row,col++,glyph(ascii_tile(*text++)),0u);
}
static void box(uint8_t left, uint8_t right, uint16_t top, uint16_t bottom) {
    for (uint16_t r=top;r<=bottom;r++) {
        put_cell(r,left, r==top ? 0x69u : r==bottom ? 0x6Eu : 0x6Cu,0u);
        put_cell(r,right,r==top ? 0x6Bu : r==bottom ? 0x6Du : 0x6Cu,0u);
        if (r==top || r==bottom)
            for (uint8_t c=(uint8_t)(left+1u);c<right;c++) put_cell(r,c,0x6Au,0u);
    }
}
static void misc_title(const char *title) {
    for (uint8_t c=4u;c<29u;c++) put_cell(3u,c,0x6Au,0u);
    uint8_t len=0; while(title[len]) len++;
    uint8_t col=len==18u ? 8u : (uint8_t)((33u-len)/2u);
    put_cell(3u,(uint16_t)(col-1u),0x24u,0u);
    write_text(3u,col,title);
    put_cell(3u,(uint16_t)(col+len),0x24u,0u);
}
void fs_render_register_board(uint8_t slot)
{
    fs_render_clear_screen();
    s_misc_scene=1u; s_misc_copy=1u; s_register_slot=slot; s_anim_clock=0u;
    misc_title("REGISTER YOUR NAME");
    box(4u,28u,15u,25u);
    for (uint8_t i=0;i<44u;i++) {
        uint8_t col; uint16_t row; fs_board_cell(i,&col,&row);
        put_cell(row,col,glyph(k_char_map[i]),0u);
    }
    fs_render_all_slots();
    fs_render_misc_cursor(slot);
    write_text(27u,3u,"A WRITE B BACK START DONE");
}
void fs_render_misc_menu(uint8_t copy)
{
    fs_render_clear_screen();
    s_misc_scene=1u; s_misc_copy=copy; s_register_slot=0xFFu;
    /* Blue erase cursor, pink copy cursor: Redux file_select.asm $9EB0. */
    static const unsigned short blue_cursor[3]={0x060Cu,0x0C02u,0x0EEEu};
    render_cram_open_write_byte(14u);
    render_vram_write_words(blue_cursor,3u);
    misc_title(copy ? "COPY SAVE" : "ERASE SAVE");
    fs_render_all_slots();
    write_text(15u,10u,"QUIT");
    box(6u,26u,18u,26u);
    fs_render_prompt(copy ? "COPY WHICH SAVE?" : "ERASE WHICH SAVE?");
}
void fs_render_rename_menu(void) {
    misc_title("RENAME SAVE");
    fs_render_prompt("RENAME WHICH SAVE?");
}
void fs_render_rename_board(uint8_t slot) {
    (void)slot;
    misc_title("RENAME YOUR FILE");
    write_text(27u,3u,"A WRITE B BACK START DONE");
}
void fs_render_page(const char *title) {
    fs_render_clear_screen();
    for (uint8_t i=0u;i<80u;i++) sat_clear_entry(i);
    s_misc_scene=1u; s_register_slot=0xFFu; s_anim_clock=0u;
    misc_title(title); box(3u,29u,5u,28u);
}
void fs_render_text(uint16_t row,uint8_t col,const char *text) { write_text(row,col,text); }
void fs_render_choice_text(uint16_t row,uint8_t col,const char *text,uint8_t enabled) {
    while (*text && col<32u)
        put_cell(row,col++,glyph(ascii_tile(*text++)),enabled ? PAL_TEXT : FS_DIM_TEXT_PALETTE);
}
void fs_render_file_identity(uint8_t slot) {
    const volatile unsigned char *name=save_game_slot_name(slot);
    for (uint8_t i=0u;i<8u;i++) put_cell(6u,18u+i,glyph(name[i]),0u);
}
void fs_render_page_cursor(uint16_t row) {
    sat_write(0u,(uint16_t)((row-FS_ROW_SHIFT)*8u+128u),0u,
              (uint16_t)((PAL_BG_CURSOR<<13)|HEART_CHR_BASE),160u);
}
void fs_render_quest_stats(uint8_t slot,uint8_t quest,uint16_t row) {
    uint8_t h=save_game_quest_stat(slot,quest,0u),p=save_game_quest_stat(slot,quest,1u);
    uint8_t d=save_game_quest_stat(slot,quest,2u);
    char deaths[]="DEATHS 000";
    deaths[7]=(char)('0'+d/100u); deaths[8]=(char)('0'+d/10u%10u); deaths[9]=(char)('0'+d%10u);
    write_text(row,7u,deaths);
    for (uint8_t i=0u;i<16u;i++) put_cell(row+1u,7u+i,heart_tile(h,p,i),PAL_LIFE);
}
void fs_render_prompt(const char *prompt) {
    for (uint8_t c=7u;c<26u;c++) put_cell(20u,c,0x24u,0u);
    write_text(20u,8u,prompt);
}
void fs_render_misc_cursor(uint8_t row) {
    if (row > 3u) return;
    sat_write(0u,(uint16_t)(0x33u+row*24u+129u-FS_ROW_SHIFT*8u),1u,
              (uint16_t)(((s_misc_copy ? PAL_BG_CURSOR : 0u)<<13)|HEART_CHR_BASE),0x45u+128u);
}
static void slot_name_colour(uint8_t slot, uint8_t pal) {
    const volatile unsigned char *name=save_game_slot_name(slot);
    for (uint8_t i=0u;i<8u;i++)
        put_cell((uint16_t)(6u+slot*3u),(uint16_t)(13u+i),
                 save_game_slot_active(slot) ? glyph(name[i]) : 0x24u,pal);
}
static void misc_marker(uint8_t entry,uint8_t slot,uint8_t pal,uint8_t next) {
    sat_write(entry,(uint16_t)(0x33u+slot*24u+129u-FS_ROW_SHIFT*8u),next,
              (uint16_t)((pal<<13)|HEART_CHR_BASE),0x45u+128u);
}
void fs_render_copy_destination(uint8_t source) {
    render_cram_open_write_byte(66u); render_vram_write_word(0x002Cu);
    slot_name_colour(source,2u);
    /* File-coloured fixed marker remains at the selected copy source. */
    static const unsigned short bright[3][3]={
        {0x00E6u,0x008Eu,0x0048u},{0x0E88u,0x008Eu,0x0048u},{0x002Cu,0x008Eu,0x0048u}};
    render_cram_open_write_byte(14u);
    render_vram_write_words(bright[source],3u);
    misc_marker(4u,source,0u,0u);
}
void fs_render_confirmation(const char *prompt, uint8_t choice, uint8_t copy,
                            uint8_t source, uint8_t destination) {
    fs_render_prompt(prompt);
    for (uint8_t c=7u;c<26u;c++) {
        put_cell(22u,c,0x24u,0u); put_cell(24u,c,0x24u,0u);
    }
    write_text(22u,11u,"OKAY"); write_text(24u,11u,"QUIT");
    /* Grey target sprite/name and coloured file digits from Redux confirmation. */
    static const unsigned short gray[3]={0x0AAAu,0x0EEEu,0x0666u};
    render_cram_open_write_byte((unsigned short)((destination+1u)*32u+8u));
    render_vram_write_words(gray,3u);
    render_cram_open_write_byte(98u); render_vram_write_word(0x0666u);
    slot_name_colour(destination,3u);
    render_cram_open_write_byte(66u); render_vram_write_word(0x002Cu);
    if (copy) {
        fs_render_copy_destination(source);
        render_cram_open_write_byte(34u); render_vram_write_word(0x00A0u); /* Redux BG $1A. */
        for (uint8_t c=12u;c<20u;c++) put_cell(20u,c,glyph(ascii_tile(prompt[c-8u])),2u);
        for (uint8_t c=20u;c<24u;c++) put_cell(20u,c,glyph(ascii_tile(prompt[c-8u])),1u);
        /* Keep source marker and a grey marker at the destination. */
        render_cram_open_write_byte(78u);
        render_vram_write_words(gray,3u);
        misc_marker(4u,source,0u,5u);
        misc_marker(5u,destination,2u,0u);
    } else {
        for (uint8_t c=14u;c<22u;c++) put_cell(20u,c,glyph(ascii_tile(prompt[c-8u])),2u);
        misc_marker(4u,destination,0u,0u);
    }
    sat_write(0u,(uint16_t)(0xAFu+choice*16u+129u-FS_ROW_SHIFT*8u),1u,
              (uint16_t)(((copy ? PAL_BG_CURSOR : 0u)<<13)|HEART_CHR_BASE),0x4Cu+128u);
}

/* Name field while typing: the chosen slot's name row, from a buffer. */
void fs_render_name_field(uint8_t slot, const uint8_t *name)
{
    uint8_t i;
    for (i = 0u; i < 8u; ++i)
        put_cell(slot_row(slot), (uint16_t)(13u + i), glyph(name[i]), PAL_TEXT);
}

uint16_t fs_slot_name_row(uint8_t slot) { return slot_row(slot); }

/* Blinking block over the selected board character; independent heart marks file. */
void fs_render_board_cursor(uint8_t idx)
{
    uint8_t c; uint16_t row; s_board_index=idx;
    fs_board_cell(idx,&c,&row);
    sat_write(5u,(s_anim_clock&8u) ? (uint16_t)(row*8u+128u-FS_ROW_SHIFT*8u) : 0u,
              0u,(PAL_BG_CURSOR<<13)|0x10Du,(uint16_t)(c*8u+128u));
}
void fs_render_tick(uint8_t selected, uint8_t animate)
{
    if (s_register_slot != 0xFFu) {
        s_anim_clock++;
        fs_render_board_cursor(s_board_index);
        fs_render_name_cursor(1u,s_name_col,s_name_row);
        return;
    }
    uint8_t slot=(animate && selected<3u && fs_sram_slot_occupied(selected)) ? selected : 0xFFu;
    if (slot != s_anim_slot) {
        if (s_anim_slot<3u) fs_render_slot(s_anim_slot);
        s_anim_slot=slot; s_anim_clock=0u;
    }
    if (slot<3u && (++s_anim_clock & 7u)==0u) {
        uint16_t tile=(uint16_t)(LINK_CHR_BASE | ((s_anim_clock&8u) ? 0x0800u : 0u));
        sat_write((uint8_t)(slot+1u),(uint16_t)(0x58u+slot*24u+129u-FS_ROW_SHIFT*8u),
                  (uint16_t)(0x0500u|((slot==2u)?4u:slot+2u)),
                  (uint16_t)(((slot+1u)<<13)|tile),0x30u+128u);
    }
}
