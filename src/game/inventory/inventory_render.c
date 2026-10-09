/* inventory_render.c — see header.
 * NES: Z_05.asm UpdateMenuCommon1, UpdateMenuScrollDown/Up; live T-242
 *      captures show one moving menu/HUD/room strip, never two menu images.
 * Drained: save_menu_runtime.c savert_update_menu_scroll_common; retained
 *      menu ownership/selection and native adapter's VBlank transfer path.
 * Coverage: PARTIAL; scroll composition and boundaries, not full menu timing.
 * Stance: EXTEND the renderer with a frozen strip; no RoomRom edits.
 *
 * V1 layout: simple text + slot grid. Renders entire Plane A as one
 * static frame on subscreen-enter. NES reference is row-by-row scroll
 * (Z_05.asm:6763-6837 Submenu_CueTransferRowUW/OW) but for V1 we paint
 * the full layout in one frame; scroll-in animation (P6.3) wraps this.
 *
 * Tile_ids referenced:
 *   $00-$09 = digits '0'-'9' (Common BG)
 *   $0A-$23 = uppercase 'A'-'V' (Common BG; alphabet starts at $0A per Z1)
 *   $24+    = punctuation
 *   $24..$2F = various symbols (',' '!' "'" '"' '?' '-' '.' etc)
 *
 * Genesis: tile_id via bg_sparse_tile_lut[tile][sub_pal] -> slot in
 * VRAM = ROOMROM_BG_TILE_BASE + slot.
 */
#include "inventory_render.h"
#include "inventory_palette.h"
#include "inventory_tilemap.h"
#include "inventory_uw_tilemap.h"
#include "inventory_sprite_chr.h"
#include "../../../RoomRom/src/roomrom_main_state.h"  /* roomrom_main_current_scene */
#include "../world/draw_dispatch.h"
#include "../world/render/subpal_routing.h"
#include "../room/room_dispatch.h" /* native per-level map/compass ownership */
#include "../hud/hud_runtime.h" /* single occupied-slot selection owner */
#include "../dungeon/uw_render.h"  /* roomrom_uw_room_render_get_level */
#include "../dungeon/uw_map_builder.h"
static unsigned char s_dungeon_map[8][16];

/* Scene captured on subscreen enter: 0 = OW (triforce), 1 = UW (dungeon
 * map). Selects the tilemap/sub-pal source in write_inventory_row. */
static unsigned char s_subscreen_uw = 0u;
#include "../../abi/platform_abi.h"
#include "../../abi/render_abi.h"
#include "../../../RoomRom/src/bg_sparse_chr.h"
#include "../../../RoomRom/src/roomrom_vram_map.h"
#include "../../../RoomRom/src/atlas/items_chr_x4.h"
#include "../../state/inventory.h"

/* Genesis VRAM tile for item N: ITEM_VRAM_TILE_BASE + ROOMROM_ITEM_TILE_<X>.
 * V2.4c (2026-05-25): derived from ROOMROM_ITEM_TILE_BASE macro instead of
 * hardcoded 819 (= old SPR_TILE_BASE 533 + LINK 238 + walk 32 + attack 16).
 * Bumping SPR_TILE_BASE for inventory atlas force-includes silently broke
 * hardcoded version (Gemini H2 review finding). Source from
 * roomrom_vram_map.h for auto-update on any SPR/ITEM_BASE shift. */
#define ITEM_VRAM_TILE_BASE (unsigned short)(ROOMROM_ITEM_TILE_BASE - 1u)
/* SAT VRAM base, gameplay context per PR-2 Option F. */
#define SAT_VRAM_BASE_GAMEPLAY 0xF400u

/* Helper: pack a SAT entry word block and write to a SAT slot via direct
 * VRAM. Y/x are 9-bit per Genesis VDP spec; size_link packs SIZE<<8 | next. */
static void sat_write(unsigned char slot, unsigned short y,
                      unsigned short size, unsigned char link,
                      unsigned short attr, unsigned short x)
{
    unsigned short addr = (unsigned short)(SAT_VRAM_BASE_GAMEPLAY + slot * 8u);
    render_vram_open_write(addr);
    *((volatile unsigned short *)0xC00000) = y;
    *((volatile unsigned short *)0xC00000) =
        (unsigned short)((size << 8) | link);
    *((volatile unsigned short *)0xC00000) = attr;
    *((volatile unsigned short *)0xC00000) = x;
}

/* V2.8 (Phase 7 v2 systematic-debugging 2026-05-20):
 *
 * Cross-search NES CHR vs Genesis VRAM revealed Common SPR atlas only
 * contains NES tiles $20-$2A at SPR_BASE+nes_tile direct mapping. Other
 * inventory tile_ids ($34/$36/$42/$46/etc) extracted to items_chr_x4
 * atlas at non-linear offsets per `RoomRom/src/atlas/items_chr_x4.h`.
 *
 * Hardcoded LUT per NES inventory slot -> Genesis VRAM tile, derived
 * from /c/tmp/{nes,gen}_subscreen captures + cross-search.
 *
 * NES slot indexing follows Items[] array layout (Variables.inc:237):
 *   0  boomerang  (special @FoundBoomerang path, NES tile $36)
 *   1  bombs      (InvBombs $658, NES tile $34)
 *   2  arrow      (InvArrow $659, NES tile $28)
 *   3  bow        (Bow $65A, NES tile $2A)
 *   4  candle     (InvCandle $65B, NES tile $26)
 *   5  recorder   ($65C, NES tile $24)
 *   6  food       (InvFood $65D, NES tile $22)
 *   7  potion     (Potion $65E, NES tile $40)
 *   8  wand       ($65F, NES tile $4A)
 *   9  raft       (InvRaft $660, NES tile $6C — NOT in atlas, placeholder)
 *   A  book       (InvBook $661, NES tile $42)
 *   B  ring       (InvRing $662, NES pause tile $46; atlas idx 64)
 *   C  ladder     (InvLadder $663, NES tile $2C)
 *   D  magic_key  (InvMagicKey $664, NES tile $4E)
 *   E  bracelet   (InvBracelet $665, NES tile $4C)
 *   F  letter     (InvLetter $666, NES tile $6A — NOT in atlas)
 *  10  compass    (InvCompass $667, NES tile $2E)
 *  11  map        (InvMap $668, NES tile $32)
 */

#define INV_SLOT_COUNT 18u
#define INV_TILE_MISSING 0xFFFFu

/* Genesis VRAM tile slot per inventory slot. V2.4c (2026-05-25):
 * derived from ROOMROM_SPR_TILE_BASE + ROOMROM_ITEM_TILE_BASE macros
 * instead of hardcoded slot numbers. Original table baked for old
 * SPR_TILE_BASE=533 / ITEM_TILE_BASE=820. Atlas force-includes for V2.1
 * subscreen text/triforce bumped SPR_TILE_BASE to 609 / ITEM_TILE_BASE
 * to 896, silently invalidating the hardcoded slots (Gemini H2). */
#define INV_SPR(off)   (unsigned short)(ROOMROM_SPR_TILE_BASE + (off))
#define INV_ITEM(off)  (unsigned short)(ROOMROM_ITEM_TILE_BASE + (off))
/* Dedicated subscreen item-icon CHR uploaded to free VRAM $A000 (tile
 * 1280) on enter — holds the live subscreen tiles absent from / wrong in
 * the items atlas: idx 0/1 recorder $24, 2/3 candle $26, 4/5 raft $6C,
 * 6/7 ladder $76 (inventory_sprite_chr.c). */
#define DSPR_BASE ROOMROM_SUBSCREEN_SPRITE_TILE_BASE
#define DSPR(idx) (unsigned short)(DSPR_BASE + (idx))
/* V3.0 (2026-05-30): byte-exact remap. The SUBSCREEN tile for each item
 * slot is Anim_ItemFrameTiles[Anim_ItemFrameOffsets[slot]] (Z_01.asm:5194-
 * 5207), NOT the item PICKUP tile the old table used. Atlas indices below
 * are keyed by NES tile id (the atlas idx whose extracted bytes ARE that
 * NES tile), so the misleading ROOMROM_ITEM_TILE_* *names* are irrelevant —
 * what matters is the byte content. Derived from pause_byte_diff active
 * capture (tiles + sub-pals) + the NES Anim tables. Three subscreen icons
 * ($24 recorder, $26 candle, $6C raft) are NOT in the items atlas yet and
 * are extracted in inventory_subscreen_chr (see below). The later atlas
 * offsets were rechecked against live NES pause CHR and Genesis VRAM: the
 * previous lookup pointed two tiles early for several icons. */
static const unsigned short k_inv_slot_to_vram_tile[INV_SLOT_COUNT] = {
    INV_ITEM(6),    /* 0  boomerang  NES $36 -> atlas idx 6  (BOOMERANG) */
    INV_ITEM(20),   /* 1  bombs      NES $34 -> atlas idx 20 (bytes $34) */
    INV_ITEM(14),   /* 2  arrow      NES $28 -> atlas idx 14 (bytes $28) */
    INV_ITEM(76),   /* 3  bow        NES $2A -> atlas idx 76 (bytes $2A) */
    DSPR(2),        /* 4  candle    NES $26 -> dedicated CHR (live extract) */
    DSPR(0),        /* 5  recorder  NES $24 -> dedicated CHR (live extract) */
    INV_ITEM(78),   /* 6  food       NES $22 -> atlas idx 78 (bytes $22) */
    INV_ITEM(80),   /* 7  potion     NES $40 -> atlas idx 80 (bytes $40) */
    INV_ITEM(82),   /* 8  wand       NES $4A -> atlas idx 82 (bytes $4A) */
    DSPR(4),        /* 9  raft      NES $6C -> dedicated CHR (live extract) */
    INV_ITEM(66),   /* A  book       NES $42 -> atlas idx 66 (bytes $42) */
    INV_ITEM(64),   /* B  ring       NES $46 -> atlas idx 64 (bytes $46) */
    DSPR(6),        /* C  ladder     NES $76 -> dedicated CHR (live; atlas $76 wrong) */
    INV_ITEM(68),   /* D  magic_key  NES $2C -> atlas idx 68 (bytes $2C) */
    INV_ITEM(72),   /* E  bracelet   NES $4E -> atlas idx 72 (bytes $4E) */
    INV_ITEM(74),   /* F  letter     NES $4C -> atlas idx 74 (bytes $4C) */
    INV_ITEM(54),   /* 10 compass    NES $2E -> atlas idx 54 (COMPASS) */
    INV_ITEM(56),   /* 11 map        NES $32 -> atlas idx 56 (MAP) */
};

/* Per-slot NES SPR sub-pal -> Genesis PAL (per Phase B/F routing):
 *   NES SPR sub-pal 0 -> Genesis PAL1[0..3]
 *   NES SPR sub-pal 1 -> Genesis PAL2[1..3] (compact)
 *   NES SPR sub-pal 2 -> Genesis PAL3[1..3] (compact)
 *
 * Cross-search NES OAM attr bits (2026-05-20):
 *   slot 0 boomerang ($36): sub_pal 1   slot 1 bombs ($34): sub_pal 1
 *   slot 2 arrow ($28):     sub_pal 1   slot 3 bow ($2A):   sub_pal 0
 *   slot 4 candle ($26):    sub_pal 2   slot 5 recorder($24):sub_pal 2
 *   slot 6 food ($22):      sub_pal 2   slot 7 potion ($40): sub_pal 2
 *   slot 8 wand ($4A):      sub_pal 1   slot $A book ($42):  sub_pal 2
 *   slot $C ladder ($2C):   sub_pal 2   slot $D magic_key ($4E): sub_pal 2
 *   slot $E bracelet ($4C): sub_pal 1   slot $10 compass ($2E): sub_pal 0
 *   slot $11 map ($32):     sub_pal 0
 */
/* V3.0: sub-pals read from the pause_byte_diff active OAM capture (NES SPR
 * sub-pal p -> Genesis PAL(p+1): p0->PAL1, p1->PAL2, p2->PAL3). */
static const unsigned char k_inv_slot_to_pal[INV_SLOT_COUNT] = {
    RENDER_PAL2,  /* 0  boomerang $36 p1 */
    RENDER_PAL2,  /* 1  bombs     $34 p1 */
    RENDER_PAL2,  /* 2  arrow     $28 p1 */
    RENDER_PAL1,  /* 3  bow       $2A p0 */
    RENDER_PAL3,  /* 4  candle    $26; grade palette resolved in inv_slot_pal */
    RENDER_PAL3,  /* 5  recorder  $24 p2 */
    RENDER_PAL3,  /* 6  food      $22 p2 */
    RENDER_PAL3,  /* 7  potion    $40; grade palette resolved in inv_slot_pal */
    RENDER_PAL2,  /* 8  wand      $4A p1 */
    RENDER_PAL1,  /* 9  raft      $6C p0 */
    RENDER_PAL3,  /* A  book      $42 p2 */
    RENDER_PAL3,  /* B  ring      $46; grade palette resolved in inv_slot_pal */
    RENDER_PAL1,  /* C  ladder    $76 p0 */
    RENDER_PAL3,  /* D  magic_key $2C p2 */
    RENDER_PAL3,  /* E  bracelet  $4E p2 */
    RENDER_PAL2,  /* F  letter    $4C p1 */
    RENDER_PAL1,  /* 10 compass   $2E p0 */
    RENDER_PAL1,  /* 11 map       $32 p0 */
};

/* NES tile bottom-half = tile+1 (8x16 mode). Genesis VRAM has both tiles
 * stored sequentially when extracted from common.c / items_chr_x4.c.
 * For TOP+BOTTOM rendering, write 2 SAT entries: top at (X, Y) using
 * vram_tile, bottom at (X, Y+8) using vram_tile+1.
 *
 * Genesis SAT SIZE(1, 2) is 8 wide x 16 tall = one sprite covers both
 * tiles automatically (PT0=top half, PT1=bottom half). So a single SAT
 * entry with SIZE(1,2) and tile_id = vram_top renders both halves.
 *
 * For the LEFT+RIGHT (16-px wide) inventory item, we emit:
 *   left  sprite: SIZE(1,2) at (X, Y) tile=vram_top
 *   right sprite: SIZE(1,2) at (X+8, Y) tile=vram_top hflip=1
 */
static unsigned char tile_is_narrow(unsigned char nes_tile)
{
    if (nes_tile == 0xF3u) return 1u;
    return (nes_tile >= 0x20u && nes_tile < 0x62u) ? 1u : 0u;
}

#define PLANE_A_BASE      0xC000u
/* Shared VRAM contract: tile zero is blank; tile1000 is item art. */
#define BLANK_TILE        ROOMROM_BLANK_TILE
#define SUBSCREEN_SUBPAL  0u  /* PAL0 for BG */

static unsigned char s_active = 0u;

/* T-167: the menu is drawn in the plane slot the room does not use (cols
 * 0-31 or 32-63; roomrom_main_menu_col_base) and H scroll points there,
 * like the NES drawing it in its other name table. The room's cells
 * (cave interior, person text, opened secrets) stay in the plane and come
 * back on close. Before, the menu filled the whole plane and the close
 * rebuilt the room with load_room: in a cave that drew the overworld. */
extern unsigned char roomrom_main_menu_col_base(void);
extern void roomrom_main_set_hscroll(short h);
static unsigned char s_col_base = 0u;

/* P6.5 cursor state — hoisted so inventory_subscreen_enter can reference. */
#define B_ITEM_SLOT_COUNT  8u
static unsigned char  s_cursor_slot  = 0u;
static unsigned char  s_prev_joy     = 0u;
static unsigned char  s_cursor_sat   = 0u;

/* P6.3 scroll state machine. NES Z_05.asm:152+ UpdateMenuCommon scrolls
 * over ~22 frames. We replicate via row-by-row replacement: each tick
 * writes one row of inventory tilemap to plane A. */
typedef enum {
    SCROLL_IDLE   = 0,
    SCROLL_IN     = 1,   /* writing inventory rows 0..N */
    SCROLL_ACTIVE = 2,   /* subscreen fully visible */
    SCROLL_OUT    = 3    /* clearing inventory rows N..0 */
} scroll_state_t;
static scroll_state_t s_scroll_state = SCROLL_IDLE;
static unsigned char  s_scroll_row   = 0u;
#define SCROLL_TOTAL_ROWS 28u

/* Phase B (2026-05-30, hardened after multi-LLM review): REAL vertical-
 * scroll animation replacing the row-by-row pop-in. NES slides the menu
 * DOWN from the top: CurVScroll $EF->$41 = 174 px at 3 px/frame over 58
 * frames (Z_05.asm:265-294), back up on close. The gameplay Plane A is
 * ALREADY V64 (main.c:639 VDP_setPlaneSize(64,64)) — NO plane-size toggle
 * needed. The 176 px menu parks off-screen above the V64 viewport and VSRAM
 * ramps 174 -> 0 to slide it down. HScroll ($F000) and SAT ($F400) sit
 * OUTSIDE the V64 window ($C000-$DFFF) so the plane fill never touches them.
 * BG_A and BG_B share $C000 (main.c:634-635), so BOTH plane vscroll words
 * are ramped together — else BG_B shows a static menu ghost behind BG_A. */
#define SCROLL_VSCROLL_TOP  174  /* menu parked off-screen above (= NES delta) */
#define SCROLL_VSCROLL_STEP 3
static short s_vscroll = 0;

/* Set BG_A AND BG_B vertical scroll (VSRAM slots 0/1) to v — both planes
 * share $C000, so they must scroll in lockstep. */
/* Plane rows on screen at the slide's start (VSRAM 174): 174/8 .. +28. */
#define MENU_ROWS_FIRST_VISIBLE (SCROLL_VSCROLL_TOP / 8)
#define MENU_ROWS_SCREEN_END    ((SCROLL_VSCROLL_TOP + 224) / 8 + 1)
static unsigned short s_menu_rows_pending;   /* rows 0..n-1 not yet written */

static void write_inventory_row(unsigned short row);   /* forward decl */

/* Two rows a slide tick, bottom up, always ahead of the reveal; all of
 * them once the menu settles. */
static void menu_rows_catch_up(unsigned char all)
{
    unsigned char n = all ? 0xFFu : 2u;
    while (s_menu_rows_pending > 0u && n--) {
        --s_menu_rows_pending;
        write_inventory_row(s_menu_rows_pending);
    }
}

static unsigned char s_icons_pending;

static void menu_icons_upload(void)
{
    unsigned char t, k;
    if (!s_icons_pending) return;
    s_icons_pending = 0u;
    render_vram_open_write(0xA000u);
    for (t = 0u; t < 16u; ++t)
        for (k = 0u; k < 32u; k += 2u)
            *((volatile unsigned short *)0xC00000) =
                (unsigned short)(((unsigned short)k_inventory_sprite_chr[t][k] << 8) |
                                 k_inventory_sprite_chr[t][k + 1u]);
}

static void set_subscreen_vscroll(short v)
{
    render_vsram_open_write(0u);
    render_vsram_write_word((unsigned short)v);   /* slot 0 = BG_A */
    render_vsram_write_word((unsigned short)v);   /* slot 1 = BG_B */
}

static void draw_cursor(void);   /* forward decl */
static void draw_item_sprites(void);   /* forward decl */
static void write_inventory_row(unsigned short row);   /* forward decl */

unsigned char inventory_subscreen_is_active(void)
{
    return s_active;
}

unsigned short inventory_get_vram_tile_for_slot(unsigned char cursor_slot)
{
    if (cursor_slot >= INV_SLOT_COUNT) return INV_TILE_MISSING;
    return k_inv_slot_to_vram_tile[cursor_slot];
}

/* NES source: Z_05.asm:UpdateSubmenuSelection / CheckBoomerangs;
 * Z_07.asm:DrawItemInInventory, DrawItemBySlot.
 * Drained C: draw_dispatch.c:draw_item_icon (side-effect-free).
 * Coverage: every graded item. DrawItemBySlot adds the item value to
 * ItemSlotToPaletteOffsetsOrValues for slots 0, 2, 4, 7 and $B (sword,
 * arrow, candle, potion, ring): blue candle/potion sub-pal 1, red 2.
 * The boomerang cell (inventory slot 0) is item slot $1D/$1E.
 * Stance: EXTEND; k_inv_slot_to_pal holds the fixed-colour slots. */
static unsigned char inv_slot_pal(unsigned char slot)
{
    unsigned char nes_attr;
    unsigned char item_slot = slot;
    if (slot == 0u)
        item_slot = g_inventory.boomerang_magic ? 0x1Eu : 0x1Du;
    else if (slot != 0x02u && slot != 0x04u && slot != 0x07u && slot != 0x0Bu)
        return k_inv_slot_to_pal[slot];
    (void)draw_item_icon(item_slot, nes_ram[0x0657u + item_slot], &nes_attr);
    return roomrom_spr_subpal_to_pal(nes_attr);
}

unsigned char inventory_get_pal_for_slot(unsigned char cursor_slot)
{
    if (cursor_slot >= INV_SLOT_COUNT) return RENDER_PAL1;
    return inv_slot_pal(cursor_slot);
}

/* Resolve a NES tile_id to its Genesis VRAM tile via sparse LUT.
 * Returns BLANK_TILE if not in atlas. */
static unsigned short tile_for(unsigned char nes_tile, unsigned char sub_pal)
{
    unsigned short slot = bg_sparse_tile_lut[nes_tile][sub_pal];
    if (slot == 0xFFFFu) return BLANK_TILE;
    return (unsigned short)(ROOMROM_BG_TILE_BASE + slot);
}

/* Convert ASCII char to NES Z1 tile_id. Z1 BG glyph layout per common.c:
 *   '0'..'9' -> tile $00..$09
 *   'A'..'Z' -> tile $0A..$23
 *   ' '      -> tile $24 (blank)
 *   '!'      -> tile $25
 *   "'"      -> tile $26
 *   ','      -> tile $27
 *   '-'      -> tile $28 (per Z1 ROM dump)
 *   '.'      -> tile $29
 *   '?'      -> tile $2A
 *   '"'      -> tile $2B
 * Other chars fall through to BLANK_TILE. */
static unsigned char ascii_to_tile(char c)
{
    if (c >= '0' && c <= '9') return (unsigned char)(c - '0');
    if (c >= 'A' && c <= 'Z') return (unsigned char)(0x0A + (c - 'A'));
    if (c == ' ') return 0x24u;
    if (c == '!') return 0x25u;
    if (c == '\'') return 0x26u;
    if (c == ',') return 0x27u;
    if (c == '-') return 0x28u;
    if (c == '.') return 0x29u;
    if (c == '?') return 0x2Au;
    if (c == '"') return 0x2Bu;
    return 0x24u;  /* default to blank */
}

/* Write a string to Plane A row starting at col using NES BG sub-pal index.
 *
 * Per Phase F pixel-bias rule: tile pixels are biased so values N..N+3
 * route through PAL0 to NES sub-pal N colors. So attr pal_field = 0
 * always; the SUB-PAL selection happens at TILE LOOKUP time via
 * bg_sparse_tile_lut[tile_id][sub_pal_idx] which returns the biased
 * VRAM slot for that (tile, sub-pal) combo.
 *
 * sub_pal=0 -> NES BG sub-pal 0 ($30 white / $00 gray / $12 blue defaults)
 * sub_pal=1 -> NES BG sub-pal 1 ($16 red / $27 orange / $36 peach)
 * sub_pal=2 -> NES BG sub-pal 2 ($1A green / $37 yellow / $12 blue)
 * sub_pal=3 -> NES BG sub-pal 3 ($17 brown / $37 yellow / $12 blue) */
static void write_text_pal(unsigned short row, unsigned short col,
                           const char *str, unsigned char sub_pal)
{
    unsigned short cells[32];
    unsigned short n = 0u;
    while (str[n] != '\0' && (col + n) < 32u && n < 32u) {
        unsigned char tid = ascii_to_tile(str[n]);
        cells[n] = RENDER_TILE_ATTR_FULL(0u, 0, 0, 0,
                                         tile_for(tid, sub_pal));
        ++n;
    }
    unsigned short full[32];
    unsigned short i;
    unsigned short blank_attr = RENDER_TILE_ATTR_FULL(0u, 0, 0, 0, BLANK_TILE);
    for (i = 0; i < 32u; ++i) full[i] = blank_attr;
    for (i = 0; i < n; ++i) {
        if ((col + i) < 32u) full[col + i] = cells[i];
    }
    render_plane_a_write_row(row, full, 32u);
}

/* Backwards-compat shim — default PAL0. */
static void write_text(unsigned short row, unsigned short col,
                       const char *str)
{
    write_text_pal(row, col, str, SUBSCREEN_SUBPAL);
}

/* Draw an owned item sprite. Writes ONE SAT entry at the given pixel
 * position with the item tile_id. SIZE(1,2) = 8x16 to match NES OAM 8x16
 * mode for item icons. */
static unsigned char s_next_sat_slot;

static void draw_item_icon_8x16(unsigned char tile_offset, unsigned short y, unsigned short x)
{
    unsigned short vram_tile = (unsigned short)(ITEM_VRAM_TILE_BASE + tile_offset);
    unsigned short attr = RENDER_TILE_ATTR_FULL(RENDER_PAL1, 0, 0, 0, vram_tile);
    unsigned char link = (unsigned char)(s_next_sat_slot + 1u);
    sat_write(s_next_sat_slot, y, RENDER_SPRITE_SIZE(1, 2), link, attr, x);
    ++s_next_sat_slot;
}

/* Draw a 2x2 (16x16) item icon — for items rendered with sprite_size=(2,2)
 * like sword_horz, explosion, etc. */
static void draw_item_icon_2x2(unsigned char tile_offset, unsigned short y, unsigned short x)
{
    unsigned short vram_tile = (unsigned short)(ITEM_VRAM_TILE_BASE + tile_offset);
    unsigned short attr = RENDER_TILE_ATTR_FULL(RENDER_PAL1, 0, 0, 0, vram_tile);
    unsigned char link = (unsigned char)(s_next_sat_slot + 1u);
    sat_write(s_next_sat_slot, y, RENDER_SPRITE_SIZE(2, 2), link, attr, x);
    ++s_next_sat_slot;
}

/* V2.1 (2026-05-20): write a row using the captured NES subscreen
 * nametable (CIRAM page 1). NES VScroll=$41 = 65 px = ~8 NES NT rows,
 * so Gen plane row 0 = NES NT row 8. Map: nes_row = gen_row + 8.
 *
 * Per-tile sub_pal routing (based on NES PALRAM during active subscreen):
 *   letters/digits ($00-$23) on NES NT rows 12, 18, 19 = sub_pal 1 (red)
 *     row 29 "TRIFORCE" label = sub_pal 3 (brown/yellow)
 *   box frame ($69-$6E) = sub_pal 0 (default white/blue)
 *   triforce triangle ($E7-$F1, $F5) = sub_pal 3 (brown/yellow)
 *   all blank ($24) = sub_pal 0 */
static unsigned char tile_subpal(unsigned short nes_row, unsigned char tid)
{
    /* V2.4c (2026-05-25): per-tile NES BG sub-pal hint, fed to
     * bg_sparse_tile_lut[tid][sub_pal] which returns the pixel-bias
     * variant for that sub-pal. Plane attr pal stays 0 (PAL0 holds all
     * 4 NES BG sub-pals packed via pixel bias).
     *
     * Atlas force-includes (gen_bg_sparse.py V2.1 lines 246-276) provide:
     *   - letters $0A-$23 + digits $00-$09 at sub-pals 1+3 (red+brown)
     *   - triforce $E7-$F1+$F5 at sub-pal 3 (brown/yellow ramp)
     *   - box frame $69-$6E at sub-pal 0 (default white/blue)
     *
     * SPR_TILE_BASE bumped 533→609 to accommodate growth without
     * corrupting Common SPR slots (V2.6 revert root cause). */
    /* Triforce triangle → sub-pal 3 (NES brown/yellow) */
    if ((tid >= 0xE7u && tid <= 0xF1u) || tid == 0xF5u) return 3u;
    /* Text on red rows */
    if (tid < 0x24u) {
        if (nes_row == 12u || nes_row == 18u || nes_row == 19u) return 1u;
        if (nes_row == 29u) return 3u;  /* TRIFORCE label brown (k_inventory_tilemap row 29 = TRIFORCE letters per line 44) */
    }
    return 0u;
}

/* BizHawk's visible NES frame omits the first eight scanlines. The active
 * subscreen starts at NT row 9 in the 224-line Genesis viewport. */
#define NES_VSCROLL_NES_ROW_OFFSET  9u

/* T-172: the menu's static cells (captured tilemap + sub-palettes) for the
 * current variant, built once; opening the menu only re-resolves the
 * dungeon-map sheet. Building all ~700 cells per open made the pause take
 * 4 frames (NES 1). Index = NES name-table row 9..29 (gen rows 0..20). */
#define INV_CACHE_ROWS (30u - NES_VSCROLL_NES_ROW_OFFSET)
static unsigned short s_menu_cells[2][INV_CACHE_ROWS][32];   /* [0] OW, [1] UW */
static unsigned char s_menu_cells_built;
static unsigned short s_triforce_cells[4][10];
extern const unsigned char misc_ui_layout[];

/* Both variants, once (gameplay entry calls it during the load). */
void inventory_menu_cells_build(void)
{
    unsigned short v, r, i;
    if (s_menu_cells_built) return;
    for (v = 0u; v < 2u; ++v)
        for (r = 0u; r < INV_CACHE_ROWS; ++r) {
            const unsigned short nes_row = (unsigned short)(r + NES_VSCROLL_NES_ROW_OFFSET);
            for (i = 0u; i < 32u; ++i) {
                unsigned char tid = v ? k_inventory_uw_tilemap[nes_row][i]
                                      : k_inventory_tilemap[nes_row][i];
                unsigned char sp  = v ? k_inventory_uw_subpal[nes_row][i]
                                      : k_inventory_subpal[nes_row][i];
                s_menu_cells[v][r][i] = RENDER_TILE_ATTR_FULL(0u, 0, 0, 0, tile_for(tid, sp));
            }
        }
    s_menu_cells_built = 1u;
}

/* NES source: Z_05.asm UpdateMenuStartOW; Z_06.asm TriforceRow0..3TransferBuf.
 * Drained C: save_menu_runtime.c dispatches an unimplemented
 * c_update_menu_start_ow; inventory_menu_cells_build owns active rendering.
 * Coverage: FULL OW piece-tile construction; PARTIAL full pause presentation.
 * Stance: EXTEND the existing cached renderer (T-189).
 * The ROM-derived ui_layout manifest places offsets/replacements/empty
 * tables at87/111/135. Offsets include each NES transfer record's header
 * and terminator: record lengths8,10,12,14; mutable data begins at+4.
 * Preserve cached borders, then rebuild just the earned-piece region
 * on every open. Never mutate the common OW/UW cache or inventory. */
static void build_ow_triforce_cells(void)
{
    const unsigned char *offsets = misc_ui_layout + 87u;
    const unsigned char *replacements = misc_ui_layout + 111u;
    const unsigned char *empty = misc_ui_layout + 135u;
    unsigned char tiles[38];
    unsigned char i, r, c;
    const unsigned char mask = nes_ram[0x0671u];
    inventory_menu_cells_build();
    for (r = 0u; r < 4u; ++r)
        for (c = 0u; c < 10u; ++c)
            s_triforce_cells[r][c] = s_menu_cells[0][23u + r - NES_VSCROLL_NES_ROW_OFFSET][11u + c];
    for (i = 24u; i != 0u;) {
        --i;
        tiles[offsets[i]] = empty[i];
    }
    for (i = 0u; i < 24u; ++i) {
        unsigned char off = offsets[i];
        if (mask & (1u << (i / 3u))) {
            unsigned char old = tiles[off];
            tiles[off] = (old == 0xE5u || old == 0xE6u) ? 0xF5u : replacements[i];
        }
    }
    for (i = 0u; i < 24u; ++i) {
        unsigned char off = offsets[i];
        if (off < 8u) { r = 0u; c = (unsigned char)(15u + off); }
        else if (off < 18u) { r = 1u; c = (unsigned char)(14u + off - 8u); }
        else if (off < 30u) { r = 2u; c = (unsigned char)(13u + off - 18u); }
        else { r = 3u; c = (unsigned char)(12u + off - 30u); }
        s_triforce_cells[r][c - 11u] = RENDER_TILE_ATTR_FULL(0u, 0, 0, 0,
            tile_for(tiles[off], k_inventory_subpal[23u + r][c]));
    }
}

static void write_inventory_row(unsigned short gen_row)
{
    unsigned short i;
    const unsigned short addr = (unsigned short)(PLANE_A_BASE + gen_row * 128u + s_col_base * 2u);
    /* Gen row 0 = NES NT row 9 after the visible-frame crop. */
    unsigned short nes_row = (unsigned short)(gen_row + NES_VSCROLL_NES_ROW_OFFSET);
    const unsigned short *cells;
    if (nes_row >= 30u) {
        unsigned short blank_attr = RENDER_TILE_ATTR_FULL(0u, 0, 0, 0, BLANK_TILE);
        render_plane_fill_row(0u, s_col_base, gen_row, 32u, blank_attr);
        return;
    }
    inventory_menu_cells_build();
    cells = s_menu_cells[s_subscreen_uw ? 1u : 0u][gen_row];
    render_vram_open_write(addr);
    if (!s_subscreen_uw && nes_row >= 23u && nes_row < 27u) {
        for (i = 0u; i < 11u; ++i) *((volatile unsigned short *)0xC00000) = cells[i];
        for (i = 11u; i < 21u; ++i)
            *((volatile unsigned short *)0xC00000) = s_triforce_cells[nes_row - 23u][i - 11u];
        for (i = 21u; i < 32u; ++i) *((volatile unsigned short *)0xC00000) = cells[i];
        return;
    }
    if (s_subscreen_uw && nes_row >= 21u && nes_row < 29u) {
        /* NES sheet transfers target NT2 rows21..28, columns12..27: live
         * visited-room glyphs replace the captured map sheet. */
        const unsigned char *map = s_dungeon_map[nes_row - 21u];
        for (i = 0; i < 12u; ++i) *((volatile unsigned short *)0xC00000) = cells[i];
        for (i = 12u; i < 28u; ++i)
            *((volatile unsigned short *)0xC00000) =
                RENDER_TILE_ATTR_FULL(0u, 0, 0, 0,
                    tile_for(map[i - 12u], k_inventory_uw_subpal[nes_row][i]));
        for (i = 28u; i < 32u; ++i) *((volatile unsigned short *)0xC00000) = cells[i];
        return;
    }
    for (i = 0; i < 32u; ++i) *((volatile unsigned short *)0xC00000) = cells[i];
}

/* L2 (Phase 7 v2): NES SubmenuItemXs table from Z_05.asm:7803.
 * Indexed by item slot $00..$0F. Slot logic per DrawSubmenuItems:
 *   slot 0..4 -> Y=$36 (selectable B-item row 1)
 *   slot 5..8, $0F -> Y=$46 (selectable B-item row 2)
 *   slot 9..$0F (excl $10/$11) -> Y=$1E (unselectable passive row)
 *   slot $10 (compass) -> X=$2C Y=$9E
 *   slot $11 (map)     -> X=$2C Y=$76
 *
 * Genesis SAT offset: NES OAM Y -> SAT Y = OAM_Y + 0x79 (Y+1 NES quirk,
 * Genesis +128, visible NES frame cropped by eight lines). X = OAM_X + 0x80.
 */
#define SUBSCREEN_SAT_Y_OFFSET 0x79u
static const unsigned char k_submenu_item_xs[16] = {
    0x80u, 0x98u, 0xACu, 0xB4u, 0xC8u,  /* slots 0..4 */
    0x80u, 0x98u, 0xB0u, 0xC8u,         /* slots 5..8 */
    0x80u, 0x94u, 0xA0u, 0xB0u, 0xC0u, 0xCCu, 0xB0u   /* slots 9..$0F */
};

/* Slot Y per range. Returns NES OAM Y; caller applies the viewport offset. */
static unsigned char slot_to_nes_y(unsigned char slot)
{
    if (slot < 5u)  return 0x36u;
    if (slot == 0x0Fu) return 0x46u;
    if (slot < 9u)  return 0x46u;
    if (slot < 0x10u) return 0x1Eu;
    return 0x1Eu;  /* fallback */
}

/* Map inventory slot index -> ROOMROM_ITEM_TILE_* + ownership check.
 * Returns 1 if owned, 0 if not (skip draw). Tile_id written to *tile_out. */
static unsigned char slot_to_item(unsigned char slot, unsigned char *tile_out)
{
    switch (slot) {
        case 0x00:  /* boomerang (wood + magic share same tile in current atlas) */
            if (!g_inventory.boomerang_wood && !g_inventory.boomerang_magic) return 0u;
            *tile_out = ROOMROM_ITEM_TILE_BOOMERANG; return 1u;
        case 0x01:  /* bombs */
            if (g_inventory.bombs == 0u) return 0u;
            *tile_out = ROOMROM_ITEM_TILE_BOMB; return 1u;
        case 0x02:  /* bow */
            if (!g_inventory.bow) return 0u;
            *tile_out = ROOMROM_ITEM_TILE_BOW; return 1u;
        case 0x03:  /* candle */
            if (g_inventory.candle == 0u) return 0u;
            *tile_out = ROOMROM_ITEM_TILE_CANDLE_FIRE_F0; return 1u;
        case 0x04:  /* recorder (whistle) */
            if (!(g_inventory.items & ITEMS_BIT_FLUTE)) return 0u;
            *tile_out = ROOMROM_ITEM_TILE_RECORDER; return 1u;
        case 0x05:  /* food */
            if (!g_inventory.food) return 0u;
            *tile_out = ROOMROM_ITEM_TILE_FOOD; return 1u;
        case 0x06:  /* potion */
            if (g_inventory.potion == 0u) return 0u;
            *tile_out = ROOMROM_ITEM_TILE_POTION; return 1u;
        case 0x07:  /* magic rod / wand — no dedicated tile in atlas, use vert sword as placeholder */
            if (!(g_inventory.items & ITEMS_BIT_WAND)) return 0u;
            *tile_out = ROOMROM_ITEM_TILE_SWORD_VERT; return 1u;
        case 0x08:  /* raft */
            if (!g_inventory.raft) return 0u;
            *tile_out = ROOMROM_ITEM_TILE_RAFT; return 1u;
        case 0x09:  /* book of magic */
            if (!g_inventory.book) return 0u;
            *tile_out = ROOMROM_ITEM_TILE_BOOK_OF_MAGIC; return 1u;
        case 0x0A:  /* ring */
            if (g_inventory.ring == 0u) return 0u;
            *tile_out = ROOMROM_ITEM_TILE_RING; return 1u;
        case 0x0B:  /* ladder */
            if (!g_inventory.ladder) return 0u;
            *tile_out = ROOMROM_ITEM_TILE_LADDER; return 1u;
        case 0x0C:  /* magic key */
            if (!g_inventory.magic_key) return 0u;
            *tile_out = ROOMROM_ITEM_TILE_MAGIC_KEY; return 1u;
        case 0x0D:  /* bracelet */
            if (!g_inventory.bracelet) return 0u;
            *tile_out = ROOMROM_ITEM_TILE_BRACELET; return 1u;
        case 0x0E:  /* letter — no dedicated tile, placeholder with book glyph */
            if (!g_inventory.letter) return 0u;
            *tile_out = ROOMROM_ITEM_TILE_BOOK_OF_MAGIC; return 1u;
        case 0x0F:  /* potion (letter overlap — NES special-cases this) */
            if (g_inventory.potion == 0u) return 0u;
            *tile_out = ROOMROM_ITEM_TILE_POTION; return 1u;
        default:
            return 0u;
    }
}

/* Check if NES inventory slot N is owned by player. Reads g_inventory
 * fields aligned with NES Items[] array layout (Variables.inc:236+). */
/* NES source: Z_05.asm:HasCompass/HasMap.
 * Drained C: room_dispatch.c:room_has_compass/room_has_map.
 * Coverage: PARTIAL pause ownership and target visibility. Stance: EXTEND. */
static unsigned char slot_owned(unsigned char slot)
{
    switch (slot) {
        case 0x00: return (g_inventory.boomerang_wood || g_inventory.boomerang_magic) ? 1u : 0u;
        case 0x01: return (g_inventory.bombs > 0u) ? 1u : 0u;
        case 0x02: return (g_inventory.arrow > 0u) ? 1u : 0u;
        case 0x03: return g_inventory.bow;
        case 0x04: return (g_inventory.candle > 0u) ? 1u : 0u;
        case 0x05: return (g_inventory.items & ITEMS_BIT_FLUTE) ? 1u : 0u;  /* recorder */
        case 0x06: return g_inventory.food;
        case 0x07: return (g_inventory.potion > 0u) ? 1u : 0u;
        case 0x08: return (g_inventory.items & ITEMS_BIT_WAND) ? 1u : 0u;
        case 0x09: return g_inventory.raft;
        case 0x0A: return g_inventory.book;
        case 0x0B: return (g_inventory.ring > 0u) ? 1u : 0u;
        case 0x0C: return g_inventory.ladder;
        case 0x0D: return g_inventory.magic_key;
        case 0x0E: return g_inventory.bracelet;
        case 0x0F: return g_inventory.letter;
        case 0x10: return room_has_compass() ? 1u : 0u;
        case 0x11: return room_has_map() ? 1u : 0u;
        default:   return 0u;
    }
}

/* Per-slot NES SUBSCREEN tile (Anim_ItemFrameTiles[Anim_ItemFrameOffsets
 * [slot]], Z_01.asm:5194-5207). Drives the draw-mode dispatch. */
static const unsigned char k_inv_slot_to_nes_tile[INV_SLOT_COUNT] = {
    0x36u,0x34u,0x28u,0x2Au,0x26u,0x24u,0x22u,0x40u,0x4Au,
    0x6Cu,0x42u,0x46u,0x76u,0x2Cu,0x4Eu,0x4Cu,0x2Eu,0x32u
};

/* Draw one inventory slot, replicating Anim_WriteSpecificItemSprites'
 * three draw modes (Z_01.asm:5279-5303), selected by the NES tile id:
 *   $F3 or [$20,$62) -> @Narrow   : single 8x16 sprite, +4 X centering.
 *   [$62,$6C)        -> @Slim/Wide : left tile + (tile+2) right, 8px apart.
 *   >= $6C           -> @Mirrored  : left tile + same tile h-flipped right.
 * Genesis SAT: X+128, Y+SUBSCREEN_SAT_Y_OFFSET. */
static void draw_inv_slot(unsigned char slot, unsigned short nes_x, unsigned char nes_y)
{
    if (slot >= INV_SLOT_COUNT) return;
    unsigned short vram_tile = k_inv_slot_to_vram_tile[slot];
    if (vram_tile == INV_TILE_MISSING) return;

    unsigned char  nes_tile = k_inv_slot_to_nes_tile[slot];
    unsigned char  pal      = inv_slot_pal(slot);
    unsigned short sat_y    = (unsigned short)(nes_y + SUBSCREEN_SAT_Y_OFFSET);
    unsigned char  wide     = (nes_tile != 0xF3u && nes_tile >= 0x62u);

    if (!wide) {
        /* @Narrow — single 8x16 at X+4. */
        unsigned short sat_x = (unsigned short)(nes_x + 4u + 0x80u);
        unsigned char  link  = (unsigned char)(s_next_sat_slot + 1u);
        unsigned short attr  = RENDER_TILE_ATTR_FULL(pal, 0, 0, 0, vram_tile);
        sat_write(s_next_sat_slot, sat_y, RENDER_SPRITE_SIZE(1, 2), link, attr, sat_x);
        ++s_next_sat_slot;
        return;
    }

    /* Wide: two 8x16 sprites 8px apart, no centering. Mirrored (>=$6C) uses
     * the same tile h-flipped on the right; Slim ([$62,$6C)) uses tile+2. */
    {
        unsigned char  mirrored = (nes_tile >= 0x6Cu);
        unsigned short sat_x    = (unsigned short)(nes_x + 0x80u);
        unsigned char  link     = (unsigned char)(s_next_sat_slot + 1u);
        unsigned short la       = RENDER_TILE_ATTR_FULL(pal, 0, 0, 0, vram_tile);
        sat_write(s_next_sat_slot, sat_y, RENDER_SPRITE_SIZE(1, 2), link, la, sat_x);
        ++s_next_sat_slot;
        unsigned short rtile = mirrored ? vram_tile : (unsigned short)(vram_tile + 2u);
        unsigned char  rhf   = mirrored ? 1u : 0u;
        unsigned char  link2 = (unsigned char)(s_next_sat_slot + 1u);
        unsigned short ra    = RENDER_TILE_ATTR_FULL(pal, 0, 0, rhf, rtile);
        sat_write(s_next_sat_slot, sat_y, RENDER_SPRITE_SIZE(1, 2), link2,
                  ra, (unsigned short)(sat_x + 8u));
        ++s_next_sat_slot;
    }
}

/* Draw all owned inventory item icons per NES subscreen layout. */
/* UW position marker: $3E dot (8x16) at NES (nes_x, nes_y), tile = DSPR(8)
 * (sub-pal 0/1/2 via PAL1) or DSPR(14) (PAL0-biased for SPR sub-pal 3). */
static void draw_marker_sprite(unsigned short nes_x, unsigned short nes_y,
                               unsigned char pal, unsigned short tile)
{
    unsigned short attr = RENDER_TILE_ATTR_FULL(pal, 0, 0, 0, tile);
    unsigned char  link = (unsigned char)(s_next_sat_slot + 1u);
    sat_write(s_next_sat_slot, (unsigned short)(nes_y + SUBSCREEN_SAT_Y_OFFSET),
              RENDER_SPRITE_SIZE(1, 2), link, attr, (unsigned short)(nes_x + 0x80u));
    ++s_next_sat_slot;
}

/* UW compass/map icon. wide!=0 -> mirrored pair (left + h-flip 8px right);
 * else single narrow 8x16 with +4 X centering. */
static void draw_uw_item(unsigned short vram, unsigned short nes_x,
                         unsigned short nes_y, unsigned char pal, unsigned char wide)
{
    unsigned short sat_y = (unsigned short)(nes_y + SUBSCREEN_SAT_Y_OFFSET);
    if (!wide) {
        unsigned char lk = (unsigned char)(s_next_sat_slot + 1u);
        sat_write(s_next_sat_slot, sat_y, RENDER_SPRITE_SIZE(1, 2), lk,
                  RENDER_TILE_ATTR_FULL(pal, 0, 0, 0, vram),
                  (unsigned short)(nes_x + 4u + 0x80u));
        ++s_next_sat_slot;
        return;
    }
    unsigned short sx = (unsigned short)(nes_x + 0x80u);
    unsigned char  lk = (unsigned char)(s_next_sat_slot + 1u);
    sat_write(s_next_sat_slot, sat_y, RENDER_SPRITE_SIZE(1, 2), lk,
              RENDER_TILE_ATTR_FULL(pal, 0, 0, 0, vram), sx);
    ++s_next_sat_slot;
    unsigned char lk2 = (unsigned char)(s_next_sat_slot + 1u);
    /* NES mirrors the compass right half 7 px from the left (measured), not
     * the usual 8 (Anim_WriteMirroredSpritePair separation for this item). */
    sat_write(s_next_sat_slot, sat_y, RENDER_SPRITE_SIZE(1, 2), lk2,
              RENDER_TILE_ATTR_FULL(pal, 0, 0, 1, vram), (unsigned short)(sx + 7u));
    ++s_next_sat_slot;
}

static void draw_item_sprites(void)
{
    s_next_sat_slot = 0u;

    /* Slot 0 boomerang at fixed (X=$80, Y=$36) -- NES @FoundBoomerang path. */
    if (slot_owned(0u))
        draw_inv_slot(0u, 0x80u, 0x36u);

    /* Slots 1..$0F via SubmenuItemXs[slot] + Y per range. */
    unsigned char slot;
    for (slot = 1u; slot <= 0x0Fu; ++slot) {
        if (!slot_owned(slot)) continue;
        /* NES skips the letter slot ($0F) when a potion is owned — the
         * potion sprite was already emitted at slot 7 (Z_05.asm:7855-7858).
         * Without this Gen draws potion twice at the same cell. */
        if (slot == 0x0Fu && g_inventory.potion > 0u) continue;
        unsigned short nes_x = k_submenu_item_xs[slot];
        unsigned char  nes_y = slot_to_nes_y(slot);
        draw_inv_slot(slot, nes_x, nes_y);
    }

    /* Slots $10/$11 compass+map: NES draws these via HasCompass/HasMap,
     * which test the CURRENT level's bit — present in UW (dungeon), never
     * in OW. Gate on CurLevel ($10) so OW matches NES (no compass/map). */
    {
        unsigned char room = nes_ram[0x00EBu];
        /* Z_05.asm:UpdateMenuCommon1 reuses Z_01.asm:UpdatePositionMarker,
         * which adds the installed LevelInfo offset to both status markers. */
        short status_x_offset = (short)(signed char)nes_ram[0x6BACu];
        if (!s_subscreen_uw) {
            /* OW status-bar map marker: X=(room&$0F)*4+$11, Y=(room&$70)>>2
             * +$17, scrolled 175 px with the menu (Z_01.asm:4083). */
            draw_marker_sprite((unsigned short)((short)(((room & 0x0Fu) << 2) + 0x11u) + status_x_offset),
                               (unsigned short)(((room & 0x70u) >> 2) + 0x17u + 175u),
                               RENDER_PAL1, DSPR(8));
        } else {
            /* UW dungeon-map markers (Z_05.asm:295-355) + compass/map. */
            /* CurLevel ($0010) is set reliably on the MODE toggle (s_uw_level
             * is not), so index the per-level map config by it. */
            unsigned char  level = nes_ram[0x0010u];
            unsigned char rot = uw_map_rotation(level);
            unsigned short rx  = (rot < 8u)
                ? (unsigned short)(rot << 3)
                : (unsigned short)(0u - (unsigned short)((16u - rot) << 3));
            /* Player marker on the map sheet (final scroll-end position). */
            draw_marker_sprite((unsigned short)(((room & 0x0Fu) << 3) + rx + 0x62u),
                               (unsigned short)(((room & 0xF0u) >> 1) + 0x69u),
                               RENDER_PAL1, DSPR(8));
            /* Status-bar map marker (UW cols *8 + $12; Y scrolled). */
            draw_marker_sprite((unsigned short)((short)(((room & 0x0Fu) << 3) + 0x12u) + status_x_offset),
                               (unsigned short)(((room & 0x70u) >> 2) + 0x17u + 175u),
                               RENDER_PAL1, DSPR(8));
            /* Triforce/compass map marker: status-bar formula on
             * TriforceRoomId (NES sub-pal 3 -> PAL3). */
            if (room_has_compass()) {
                unsigned char tr = uw_map_triforce_room(level);
                draw_marker_sprite((unsigned short)((short)(((tr & 0x0Fu) << 3) + 0x12u) + status_x_offset),
                                   (unsigned short)(((tr & 0x70u) >> 2) + 0x17u + 175u),
                                   RENDER_PAL1, DSPR(14));
            }
            /* Compass ($6A mirrored) at ($2C,$9E); map ($4C narrow) at
             * ($2C,$76) — UW-extracted tiles, NES SPR sub-pal 2 -> PAL3. */
            if (slot_owned(0x10u)) draw_uw_item(DSPR(10), 0x2Cu, 0x9Eu, RENDER_PAL3, 1u);
            if (slot_owned(0x11u)) draw_uw_item(DSPR(12), 0x2Cu, 0x76u, RENDER_PAL3, 0u);
        }
    }

    /* B-item box: NES @DrawBreakoutItem (Z_05.asm:7949-7954) redraws the
     * currently-SELECTED item inside the box at fixed ($40,$36) — the
     * "USE B BUTTON" equipped indicator. */
    /* Z_05 UpdateSubmenuSelection only draws an owned breakout item.
     * Empty inventory still has cursor slot0, but no boomerang preview. */
    if (slot_owned(s_cursor_slot))
        draw_inv_slot(s_cursor_slot, 0x40u, 0x36u);

    s_cursor_sat = s_next_sat_slot;
    draw_cursor();
}

void inventory_subscreen_enter(void)
{
    /* Native pickup/count ownership must be visible on the entry frame. */
    inventory_sync_from_native();
    /* Capture scene: UW (dungeon) renders the map subscreen, OW the
     * triforce subscreen. Latched here so the whole open uses one source. */
    s_subscreen_uw = (roomrom_main_current_scene() == ROOMROM_MAIN_SCENE_UW) ? 1u : 0u;

    if (s_subscreen_uw) uw_map_build(nes_ram[0x0010u], s_dungeon_map);
    else build_ow_triforce_cells();

    /* L4 (Phase 7 v2): swap CRAM to NES subscreen palette before any
     * BG/sprite write so first rendered frame is correctly colored. */
    inventory_palette_load_subscreen(s_subscreen_uw, nes_ram[0x0010u],
                                     roomrom_main_current_quest());

    /* G4: load the per-level triforce-marker tint into the otherwise-free
     * PAL1 index 9 (CRAM 25). NES SPR sub-pal3[1] = installed LevelInfo
     * offset $20 ($6B9E). ROM-verified 2026-09-24: offset $20 of the nine
     * bank-6 LevelInfoUW records reproduces the former live-NES capture
     * table byte-for-byte (as do $2D rot, $2E sbx, $2F start, $30 tri). */
    if (s_subscreen_uw) {
        extern unsigned short roomrom_bg_palette_nes_to_cram(unsigned char);
        unsigned char lvl = nes_ram[0x0010u];
        unsigned short c = (lvl >= 1u && lvl <= 9u)
            ? roomrom_bg_palette_nes_to_cram(nes_ram[0x6B9Eu]) : 0u;
        render_cram_write_color(25u, c);
    }

    /* Upload the 8 live-extracted subscreen item-icon tiles to free VRAM
     * $A000 (tile DSPR_BASE). These cover icons absent from / wrong in the
     * items atlas: recorder $24, candle $26, raft $6C, ladder $76. */
    /* T-172: uploaded on the first slide tick (menu_icons_upload): the
     * item sprites that use them appear only once the menu settles. */
    s_icons_pending = 1u;

    /* H scroll onto the menu's plane slot (both planes). The old reset
     * wrote control $7C000003 = VRAM $FC00, not the H scroll table $F000,
     * so it never took effect (harmless while the menu used column 0). */
    s_col_base = roomrom_main_menu_col_base();
    {
        unsigned short room_col, room_row;
        roomrom_main_nt_cell_to_plane(0u, 8u, &room_col, &room_row);
        render_pause_scene_deferred(s_col_base, room_col, room_row);
    }
    /* The adapter publishes scroll/window with the copied strip in VBlank.
     * Switching now exposes the previous pause's cells for one frame. */

    /* Hide all sprites during scroll-in (so frozen gameplay sprites
     * don't render over partially-built subscreen). */
    {
        unsigned char i;
        unsigned short sat_addr = SAT_VRAM_BASE_GAMEPLAY;
        /* T-172: the VDP draws the link chain from entry 0; entry 0 =
         * Y 0 (above the screen), link 0 ends it, so no sprite shows.
         * Zeroing all 80 entries (320 VRAM words during the display)
         * stretched the pause-open tick past a frame (t013_continue t61). */
        (void)i;
        render_vram_open_write(sat_addr);
        *((volatile unsigned short *)0xC00000) = 0x0000;
        *((volatile unsigned short *)0xC00000) = 0x0000;
        *((volatile unsigned short *)0xC00000) = 0x0000;
        *((volatile unsigned short *)0xC00000) = 0x0000;
    }

    unsigned short blank_attr = RENDER_TILE_ATTR_FULL(SUBSCREEN_SUBPAL, 0, 0, 0,
                                                      BLANK_TILE);
    /* $E000 is the WINDOW plane (main.c:636), NOT Plane B — clearing it
     * erases the bottom HUD strip the subscreen wants. BG_B shares $C000
     * with BG_A (main.c:634-635), so the Plane A fill below clears both. */

    /* Phase B: the gameplay plane is ALREADY V64 (main.c:639) — no plane-
     * size toggle. Clear the menu's 32-column plane slot (all 64 rows; the
     * room's slot is left alone, T-167) and write the FULL menu to rows
     * 0..27 in one shot; the animation is a real VSRAM ramp, NOT
     * progressive tile writes.
     * Park the viewport 174 px above the menu; tick ramps it down. */
    {
        /* T-172: VRAM writes during active display bound the open (it ran
         * 3 frames past its tick, t013_save t61; NES opens in one). At
         * VSRAM 174 the screen shows plane rows 21..49; the slide (3 px a
         * frame) reveals one menu row upward every ~2.7 frames and never
         * shows rows 50..63. Write what is on screen now; the slide ticks
         * write rows 20..0 ahead of the reveal (menu_rows_catch_up). */
        unsigned short r;
        for (r = SCROLL_TOTAL_ROWS; r < MENU_ROWS_SCREEN_END; ++r)
            render_plane_fill_row(0u, s_col_base, r, 32u, blank_attr);
        for (r = MENU_ROWS_FIRST_VISIBLE; r < SCROLL_TOTAL_ROWS; ++r)
            write_inventory_row(r);
        s_menu_rows_pending = MENU_ROWS_FIRST_VISIBLE;
    }
    s_vscroll = (short)SCROLL_VSCROLL_TOP;     /* menu off-screen above */
    /* Scroll is published with render_pause_scene_deferred, above. */

    /* Start scroll-in: tick ramps VSRAM 174 -> 0 (menu slides down). */
    s_active       = 1u;
    s_scroll_state = SCROLL_IN;
    s_scroll_row   = 0u;
    s_prev_joy     = 0u;
    s_cursor_slot  = nes_ram[0x0656u];
}

/* L3 (Phase 7 v2): NES SubmenuCursorXs from Z_05.asm:7909.
 * Indexed by selectable B-item slot 0..8 (cursor only moves over slots
 * with B-mappable items). */
static const unsigned char k_submenu_cursor_xs[9] = {
    0x80u, 0x98u, 0xB0u, 0xB0u, 0xC8u,
    0x80u, 0x98u, 0xB0u, 0xC8u
};

/* Genesis VRAM slot for NES sprite tile $1E (small white square).
 * NES SPR tile_id $1E lives in Common SPR pattern table; Genesis port
 * maps via ROOMROM_SPR_TILE_BASE (533) + nes_tile. */
#define CURSOR_NES_TILE_ID  0x1Eu
#define CURSOR_VRAM_TILE    (ROOMROM_SPR_TILE_BASE + CURSOR_NES_TILE_ID)

/* Frame counter for flash animation. Phase 7 v2 L3: cursor PAL alternates
 * every 8 frames per Z_05.asm:7942 (AND #$08, LSR x3, ADC #$01). */
static void draw_cursor(void)
{
    /* Cursor Y: B-item row 1 = $36 (slot<5), row 2 = $46. */
    unsigned char  nes_y = (s_cursor_slot < 5u) ? 0x36u : 0x46u;
    unsigned short sat_y = (unsigned short)(nes_y + SUBSCREEN_SAT_Y_OFFSET);
    /* Letter pseudo-selection$0F occupies potion cursor position7. */
    unsigned char  cursor_slot = (s_cursor_slot == 0x0Fu) ? 7u : s_cursor_slot;
    unsigned char  nes_x = k_submenu_cursor_xs[cursor_slot % 9u];
    unsigned short lx    = (unsigned short)(nes_x + 0x80u);

    /* Flash palette: NES alternates sprite palette rows 5/6 per FrameCounter
     * bit 3 (Z_05.asm:7990-7999) -> Genesis SPR PAL2/PAL3. */
    unsigned char  pal = (unsigned char)(((nes_ram[0x0015u] >> 3) & 1u) ?
                                          RENDER_PAL3 : RENDER_PAL2);

    /* NES draws the cursor as TWO $1E sprites forming the selection box:
     * left at SubmenuCursorXs[slot], right at +8 h-flipped (Z_05.asm:7966-
     * 8003). Each is 8x16 ($1E top + $1F bottom) in NES 8x16 mode. The
     * highlighted item itself is already in the grid (draw_item_sprites),
     * so the box just overlays it — do NOT redraw the item here. */
    unsigned char  s0 = s_cursor_sat;
    unsigned char  s1 = (unsigned char)(s0 + 1u);
    unsigned short la = RENDER_TILE_ATTR_FULL(pal, 0, 0, 0, CURSOR_VRAM_TILE);
    unsigned short ra = RENDER_TILE_ATTR_FULL(pal, 0, 0, 1, CURSOR_VRAM_TILE);
    sat_write(s0, sat_y, RENDER_SPRITE_SIZE(1, 2), s1, la, lx);
    sat_write(s1, sat_y, RENDER_SPRITE_SIZE(1, 2), 0u, ra, (unsigned short)(lx + 8u));
    s_next_sat_slot = (unsigned char)(s1 + 1u);
}

void inventory_subscreen_exit(void)
{
    /* Trigger scroll-out — tick clears rows N..0 over ~28 frames. */
    s_scroll_state = SCROLL_OUT;
    s_scroll_row   = SCROLL_TOTAL_ROWS;  /* clear from bottom up */
    /* NES hides inventory sprites before scrolling back to the room. */
    sat_write(0u, 0u, 0u, 0u, 0u, 0u);
    /* s_active stays 1 until scroll completes; tick deactivates + signals
     * main.c to call load_room. */

    /* The status bar stays in the scrolling strip until the room returns. */
}

/* NES UpdateMenuActive (MenuState 7 UW / 8 OW): the menu is down and
 * takes input. */
unsigned char inventory_subscreen_menu_active(void)
{
    return (unsigned char)(s_active && s_scroll_state == SCROLL_ACTIVE);
}

/* T-013: UpdateMenuActive pad 2 Up+A -> GameMode 8 (MenuState 0): the
 * menu goes at once, no scroll-out; mode 8 redraws the whole screen. */
void inventory_subscreen_abort(void)
{
    extern void roomrom_hud_set_bottom_mode(unsigned char bottom);
    s_active = 0u;
    s_scroll_state = SCROLL_IDLE;
    roomrom_hud_set_bottom_mode(0u);
    render_set_window_on_top(7u);
}

/* Query: is scroll-out done (so main.c knows to call load_room)? */
unsigned char inventory_subscreen_scrolled_out(void)
{
    return (s_scroll_state == SCROLL_IDLE && s_active == 0u);
}

void inventory_subscreen_tick(unsigned char joy_state)
{
    if (!s_active) return;

    /* Phase B scroll state machine — real VSRAM ramp, 3 px/frame (NES rate). */
    if (s_scroll_state == SCROLL_IN) {
        /* NES active VScroll is $41: the eight-line viewport crop is in
         * the tile-row origin above; Genesis VSRAM +1 aligns its pixel phase. */
        s_vscroll -= SCROLL_VSCROLL_STEP;
        menu_icons_upload();
        menu_rows_catch_up(s_vscroll <= 0);
        if (s_vscroll <= 0) {
            s_vscroll = 1;
            set_subscreen_vscroll(1);
            s_scroll_state = SCROLL_ACTIVE;
            draw_item_sprites();  /* sprites appear once menu is settled */
        } else {
            set_subscreen_vscroll(s_vscroll);
        }
        return;
    }

    if (s_scroll_state == SCROLL_OUT) {
        /* Menu slides back UP off the top: VSRAM 1 -> 174. */
        s_vscroll += SCROLL_VSCROLL_STEP;
        if (s_vscroll >= SCROLL_VSCROLL_TOP) {
            /* Reset scroll to 0 before main.c's load_room repaints the room.
             * The plane is already V64 (gameplay default) — do NOT toggle to
             * V32 here (that left gameplay in the wrong plane mode). */
            set_subscreen_vscroll(0);
            render_set_window_on_top(7u);
            s_scroll_state = SCROLL_IDLE;
            s_active = 0u;
            /* Clear ALL 80 SAT slots so no stale item/cursor sprite ghosts
             * onto the first gameplay frame. */
            {
                unsigned char i;
                render_vram_open_write(SAT_VRAM_BASE_GAMEPLAY);
                for (i = 0; i < 80u; ++i) {
                    *((volatile unsigned short *)0xC00000) = 0x0000;
                    *((volatile unsigned short *)0xC00000) = 0x0000;
                    *((volatile unsigned short *)0xC00000) = 0x0000;
                    *((volatile unsigned short *)0xC00000) = 0x0000;
                }
            }
        } else {
            set_subscreen_vscroll(s_vscroll);
        }
        return;
    }

    /* SCROLL_ACTIVE — accept input + re-publish sprites every frame.
     *
     * V2.5 fix: SGDK VBlank SAT DMA flush wipes our direct VRAM writes
     * each frame. Re-render items + cursor per tick so they persist
     *
     * V2.10 (2026-05-20): NES-faithful cursor navigation across slots 0..8.
     * D-pad L/R cycles within row (wrap). U/D switches row keeping column.
     * A writes SelectedItemSlot to nes_ram[$656]. */
    draw_item_sprites();

    /* Joy bits per SGDK joy.h: UP=$01 DOWN=$02 LEFT=$04 RIGHT=$08
     * B=$10 C=$20 A=$40 START=$80. */
    unsigned char pressed = (unsigned char)(joy_state & ~s_prev_joy);
    s_prev_joy = joy_state;

    /* NES source: Z_05.asm:UpdateSubmenuSelection.
     * Drained C: hud_runtime.c:find_and_select_occupied_item_slot.
     * Coverage: FULL selection ownership; preview guarded above.
     * Stance: REPLACE the unguarded grid navigation with the shared owner.
     * Right/left search across occupied slots. Up/down validate selection;
     * the bow itself is not selectable and arrows require its ownership. */
    if (pressed & 0x0Fu) {
        unsigned char old_slot = s_cursor_slot;
        unsigned char direction = (pressed & 0x08u) ? 1u :
                                  (pressed & 0x04u) ? 2u : 0u;
        hud_select_inventory_item(direction);
        s_cursor_slot = nes_ram[0x0656u];
        g_inventory.selected_b_item = s_cursor_slot;
        extern void roomrom_set_b_item_from_inv_cursor(unsigned char);
        roomrom_set_b_item_from_inv_cursor(s_cursor_slot);
        if (s_cursor_slot != old_slot)
            nes_ram[0x0602u] = 1u;       /* Tune1Request: selection changed */
    }

    draw_cursor();
}
