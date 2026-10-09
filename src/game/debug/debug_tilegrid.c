/* debug_tilegrid.c — title-MODE-button-entered tile-grid debug scene.
 *
 * Mirrors the layout of the custom NES test ROM (tools/gen_chr_viewer_rom.py
 * output build/probes/chr_viewer_rom.nes) so byte-diffs across CHR banks
 * can audit every Z1 atlas (tile_id, sub_pal) combo.
 *
 * Layout (matches NES test ROM):
 *   Plane A rows  0..15  cols 0..15  — BG 16x16 tile-id grid (tile_id =
 *                                       row*16 + col), rendered via the
 *                                       sparse-LUT-resolved Genesis slot
 *                                       for the active sub_pal.
 *   Plane A everywhere else          — tile 0 (blank).
 *   SAT slots 0..63                  — 8x8 sprite grid at y=$80+, x=$00+,
 *                                       tile_id = sprite_page*64 + slot.
 *                                       8x8 or 8x16 mode per s_8x16.
 *
 * Controls:
 *   B button        cycle BG sub_pal 0..3
 *   Start           cycle sprite-page 0..3 ($00-3F .. $C0-FF)
 *   Select          toggle 8x16 sprite mode
 *   A button        no-op (Genesis atlas is unified; no bank cycle)
 *
 * Build: linked into Debug.md via TITLE_C_SOURCES in build_debug.py.
 */
#include "debug_tilegrid.h"
#include "state_dump.h"
#include "../../abi/render_abi.h"
#include "../../../RoomRom/src/bg_sparse_chr.h"
#include "../../../RoomRom/src/atlas/items_chr_x4.h"
#include "../../../RoomRom/src/atlas/enemy_chr.h"
#include "../../../RoomRom/src/atlas/boss_chr.h"
/* T-118: the $E000 fill below overwrites the gameplay HUD window. */
extern void roomrom_hud_invalidate(void);

/* Gameplay-side atlas + palette upload routines (declared here to avoid
 * pulling subsystem headers). All defined under src/game/. */
extern void roomrom_ow_room_render_upload_chr(void);
extern void roomrom_sprites_upload_chr(void);
extern void roomrom_bg_palette_load_palram_full(const unsigned char *palram32);

/* Z1 OW orig palette (matches the custom NES test ROM's PALRAM). */
extern const unsigned char g_roomrom_ow_palram[2][32];

/* SCENE_OBJ slot VRAM location: tile_base = SPR_BASE(533) + 44 = 577.
 * VRAM byte addr = 577 * 32 = 18464 = $4820 (per enemy_chr.h comment). */
#define SCENE_OBJ_VRAM_OFFSET   (577u * 32u)

/* ----- VDP / hardware addresses (Debug.md PR-2 Option F layout) ----- */
#define PLANE_A_BASE  0xC000u
/* SAT relocated to $F400 per init_video; sprite render via render_set_sprite_full
 * (it uses the SGDK SAT cache, not direct VRAM writes). */

/* ----- Controller port 1 (Genesis 6-button protocol) ----- */
#define CTRL1_DATA (*(volatile unsigned char *)0x00A10003u)
#define CTRL1_CTRL (*(volatile unsigned char *)0x00A10009u)

/* Bit positions in our consolidated joypad byte. */
#define JB_UP    0x01u
#define JB_DOWN  0x02u
#define JB_LEFT  0x04u
#define JB_RIGHT 0x08u
#define JB_B     0x10u
#define JB_C     0x20u
#define JB_A     0x40u
#define JB_START 0x80u
/* Extended (6-button) bits — collected in a second byte. */
#define JX_Z     0x01u
#define JX_Y     0x02u
#define JX_X     0x04u
#define JX_MODE  0x08u

static unsigned char s_subpal     = 0u;
static unsigned char s_sprite_page = 0u;
static unsigned char s_8x16        = 0u;
static unsigned char s_bank        = 0u;   /* 0..7 — mirrors NES bank cycle */
static unsigned char s_joy_prev    = 0u;
static unsigned char s_joyx_prev   = 0u;

/* Lua-poke driven state mirror (auto-capture probe writes here, debug
 * scene picks up changes each iteration). Addresses $FF07E0..$FF07E3:
 *   $07E0 = desired bank (0..7)
 *   $07E1 = desired sub_pal (0..3)
 *   $07E2 = desired sprite_page (0..3)
 *   $07E3 = desired 8x16 (0 or 1)
 * Set $07E4 = $AA to signal "poke pending" — scene applies + clears flag. */
#define POKE_BANK   0x07E0u
#define POKE_SUBPAL 0x07E1u
#define POKE_PAGE   0x07E2u
#define POKE_8X16   0x07E3u
#define POKE_FLAG   0x07E4u

/* External atlas variant blobs (header bg_sparse_chr.h). Unsized extern
 * refs so they don't need updating when the sparse atlas regenerates
 * (size = BG_SPARSE_BLOB_BYTES, grew 19456->19648 when cave/NPC-text
 * punctuation glyphs were added). */
extern const unsigned char bg_sparse_chr_orig_uw [];
extern const unsigned char bg_sparse_chr_redux_ow[];
extern const unsigned char bg_sparse_chr_redux_uw[];

/* Bank cycle matching NES test ROM (chr_viewer_rom.nes) page layout.
 * Each bank = (BG variant, SCENE_OBJ content) pair.
 *
 * NES bank order (from gen_chr_viewer_rom.py build_chr):
 *   0  Common only        -> Genesis orig_ow BG + SCENE_OBJ blank
 *   1  OW                 -> orig_ow BG + enemy_owsp SCENE_OBJ
 *   2  UW1/2/7            -> orig_uw BG + enemy_uwsp127
 *   3  UW3/5/8            -> orig_uw BG + enemy_uwsp358
 *   4  UW4/6/9            -> orig_uw BG + enemy_uwsp469
 *   5  UW1257-boss        -> orig_uw BG + boss_uwspboss1257
 *   6  UW3468-boss        -> orig_uw BG + boss_uwspboss3468
 *   7  UW9-boss (Ganon)   -> orig_uw BG + boss_uwspboss9
 *
 * (NES page 8 = Demo/title — not extracted on Genesis side, omitted.) */
static const unsigned char *bank_bg_blob(unsigned char bank) {
    return (bank >= 2u) ? bg_sparse_chr_orig_uw : bg_sparse_chr_orig_ow;
}

static const unsigned char *bank_obj_blob(unsigned char bank, unsigned short *out_bytes) {
    switch (bank) {
        case 1u: *out_bytes = ROOMROM_ATLAS_ENEMY_OWSP_BANK_BYTES;
                 return roomrom_atlas_enemy_owsp;
        case 2u: *out_bytes = ROOMROM_ATLAS_ENEMY_PER_BANK_BYTES;
                 return roomrom_atlas_enemy_uwsp127;
        case 3u: *out_bytes = ROOMROM_ATLAS_ENEMY_PER_BANK_BYTES;
                 return roomrom_atlas_enemy_uwsp358;
        case 4u: *out_bytes = ROOMROM_ATLAS_ENEMY_PER_BANK_BYTES;
                 return roomrom_atlas_enemy_uwsp469;
        case 5u: *out_bytes = ROOMROM_ATLAS_BOSS_PER_BANK_BYTES;
                 return roomrom_atlas_boss_uwspboss1257;
        case 6u: *out_bytes = ROOMROM_ATLAS_BOSS_PER_BANK_BYTES;
                 return roomrom_atlas_boss_uwspboss3468;
        case 7u: *out_bytes = ROOMROM_ATLAS_BOSS_PER_BANK_BYTES;
                 return roomrom_atlas_boss_uwspboss9;
        default: *out_bytes = 0u;  /* bank 0 Common — leave SCENE_OBJ blank */
                 return 0;
    }
}

/* Upload current bank: BG variant blob + SCENE_OBJ content.
 *
 * Bank-0 special case (matches NES test ROM bank-0 = Common-only): the
 * NES side has BG pattern table $1700-$1F1F all-zero (no PatternBlock
 * loaded). To mirror exactly, zero out the scene-BG LUT slots on bank 0
 * after orig_ow upload — any slot whose tile_id is in range $70..$F1
 * (scene-specific BG range, per gen_chr_viewer_rom.py OFF_SCENE_BG). */
static void upload_bank(void) {
    /* BG variant -> VRAM BG_TILE_BASE (offset 32) */
    render_chr_upload(32u, bank_bg_blob(s_bank), BG_SPARSE_BLOB_BYTES);

    if (s_bank == 0u) {
        /* Bank 0 = NES Common-only. Zero scene-BG slots so the audit
         * sees the same all-zero pattern at tile_ids $70..$F1 that the
         * NES test ROM exposes. Misc range $F2..$FF stays loaded
         * (Common misc, present on NES bank 0). */
        unsigned short tid;
        unsigned char  sp;
        for (tid = 0x70u; tid < 0xF2u; tid++) {
            for (sp = 0u; sp < 4u; sp++) {
                unsigned short slot = bg_sparse_tile_lut[tid][sp];
                if (slot == 0xFFFFu) continue;
                unsigned short vram_addr =
                    (unsigned short)((1u + slot) * 32u);
                /* 32 zero bytes at VRAM tile (1 + slot). Direct CPU
                 * stream write inside vram_dma_upload — interrupt-safe. */
                static const unsigned char zero32[32] = {0};
                render_chr_upload(vram_addr, zero32, 32u);
            }
        }
    }

    /* SCENE_OBJ content -> VRAM offset 577*32 = $4820 */
    unsigned short obj_bytes;
    const unsigned char *obj_blob = bank_obj_blob(s_bank, &obj_bytes);
    if (obj_blob != 0 && obj_bytes != 0) {
        render_chr_upload((unsigned short)SCENE_OBJ_VRAM_OFFSET,
                          obj_blob, obj_bytes);
    }
}

/* Tiny delay to settle TH transitions on controller port. */
static inline void joy_settle(void) {
    volatile unsigned char i;
    for (i = 0; i < 8u; i++) { /* spin */ }
}

/* Genesis 6-button read. Returns base 3-button bits in *base
 * (active-high, bit positions per JB_* above) and extended bits in *ext.
 * Protocol: cycle TH high/low several times; on the 4th TH=0 read, bits
 * 3-0 are 0 for 6-button (or controller absent); on the next TH=1 read,
 * bits 3-0 = MODE, X, Y, Z in some order. We mask + invert per Sega doc. */
static void joy_read6(unsigned char *base, unsigned char *ext) {
    unsigned char b = 0u, x = 0u;
    unsigned char th0_first, th1_first, th0_second, th1_second;

    /* Cycle 1: TH=1 idle, TH=0, TH=1 — collects 3-button state. */
    CTRL1_DATA = 0x40u; joy_settle();
    CTRL1_DATA = 0x00u; joy_settle();
    th0_first = (unsigned char)(~CTRL1_DATA);    /* active-high mask */
    CTRL1_DATA = 0x40u; joy_settle();
    th1_first = (unsigned char)(~CTRL1_DATA);

    /* Cycle 2 (6-button extension): TH=0, TH=1 — 4th read gives extended bits. */
    CTRL1_DATA = 0x00u; joy_settle();
    th0_second = (unsigned char)(~CTRL1_DATA);   /* if 6-btn, bits 3-0 = 0000 */
    CTRL1_DATA = 0x40u; joy_settle();
    th1_second = (unsigned char)(~CTRL1_DATA);   /* if 6-btn, bits 3-0 = M/X/Y/Z */

    /* Idle */
    CTRL1_DATA = 0x40u; joy_settle();

    /* th0_first bits: bit5=Start, bit4=A. (3-btn TH=0 reading)
     * th1_first bits: bit5=C, bit4=B, bit3=R, bit2=L, bit1=D, bit0=U. */
    if (th0_first & 0x20u) b |= JB_START;
    if (th0_first & 0x10u) b |= JB_A;
    if (th1_first & 0x20u) b |= JB_C;
    if (th1_first & 0x10u) b |= JB_B;
    if (th1_first & 0x08u) b |= JB_RIGHT;
    if (th1_first & 0x04u) b |= JB_LEFT;
    if (th1_first & 0x02u) b |= JB_DOWN;
    if (th1_first & 0x01u) b |= JB_UP;

    /* 6-button detection: in 6-btn mode, th0_second bits 3-0 = 0000. If any
     * are 1, controller is 3-button (or absent) — return ext=0. Otherwise
     * th1_second bits 3-0 carry MODE/X/Y/Z (Sega docs use bit3=MODE bit2=X
     * bit1=Y bit0=Z but vendors differ; we treat bit0 of any set lower-
     * nibble as our "MODE-or-equivalent" trigger and let user discover). */
    if ((th0_second & 0x0Fu) == 0u) {
        /* 6-button: parse extended buttons. */
        if (th1_second & 0x08u) x |= JX_MODE;
        if (th1_second & 0x04u) x |= JX_X;
        if (th1_second & 0x02u) x |= JX_Y;
        if (th1_second & 0x01u) x |= JX_Z;
    }
    *base = b;
    *ext  = x;
}

/* Convenience: 3-button read (for cases where MODE not needed). */
static unsigned char joy_read3(void) {
    unsigned char base, ext;
    joy_read6(&base, &ext);
    return base;
}

/* Universal graphics grid: page-dispatch covering every Genesis atlas
 * surface in fixed-position 16x16 grids. Each (page, row, col) cell maps
 * to a deterministic NES origin tile so the NES test ROM (V2) can mirror
 * the layout for cell-by-cell PNG diff. Pages cycle via Start button.
 *
 * Page 0 (BG_VIEW)       — NES BG tile_ids $00..$FF, sub_pal-routed via LUT.
 * Page 1 (SPR_VIEW)      — NES SPR tile_ids $00..$FF, mapped to Genesis
 *                          SPR_TILE_BASE + tile_id.
 * Page 2 (ITEM_LINK_VIEW)— ITEM atlas (98 tiles, rows 0-5) + Link walk
 *                          poses (rows 6-7) + attack poses (row 8).
 * Page 3 (MISC_VIEW)     — Common Misc + SCENE_OBJ overlay tiles + bank
 *                          state indicator.
 */
#define BLANK_TILE 1000u

/* VRAM tile bases (must match sprite_render.c constants):
 *   COMMON (sprites_chr) = SPR_TILE_BASE        = 533
 *   LINK walk poses      = 533 + 238            = 771
 *   LINK attack poses    = 771 + 32             = 803
 *   ITEM atlas           = 803 + 16             = 819
 *   SCENE_OBJ overlay    = 577 (= SPR_TILE_BASE + 44, overlaps Common SPR) */
#define UG_SPR_TILE_BASE    533u
#define UG_LINK_VRAM_TILE   (UG_SPR_TILE_BASE + 238u)        /* 771 */
#define UG_ATTACK_VRAM_TILE (UG_LINK_VRAM_TILE + 32u)        /* 803 */
#define UG_ITEM_VRAM_TILE   (UG_ATTACK_VRAM_TILE + 16u)      /* 819 */
#define UG_SCENE_OBJ_BASE   577u

static void plane_clear(void) {
    /* Fill BOTH planes with blank-headroom tile. Plane A pixel 0 is
     * transparent and reveals Plane B underneath; if Plane B still holds
     * title art the screen looks tiled. Clear both. */
    unsigned short blank_attr = RENDER_TILE_ATTR_FULL(RENDER_PAL0, 0, 0, 0, BLANK_TILE);
    render_plane_fill(PLANE_A_BASE, blank_attr, 32u * 32u);
    render_plane_fill(0xE000u,      blank_attr, 32u * 32u);
    roomrom_hud_invalidate();

    /* Reset scroll registers. */
    *((volatile unsigned long *)0xC00004) = 0x40000010UL;
    *((volatile unsigned short *)0xC00000) = 0x0000;
    *((volatile unsigned long *)0xC00004) = 0x40020010UL;
    *((volatile unsigned short *)0xC00000) = 0x0000;
    *((volatile unsigned long *)0xC00004) = 0x7C000003UL;
    *((volatile unsigned long *)0xC00000) = 0x00000000UL;
}

/* Page 0: 16x16 BG sparse grid via LUT, rows 0..15 cols 0..15. */
static void redraw_page_bg(void) {
    unsigned short row, col;
    unsigned short cells[16];
    for (row = 0; row < 16u; row++) {
        for (col = 0; col < 16u; col++) {
            unsigned short tile_id = (unsigned short)(row * 16u + col);
            unsigned short slot = bg_sparse_tile_lut[tile_id][s_subpal];
            unsigned short attr;
            if (slot == 0xFFFFu) {
                attr = RENDER_TILE_ATTR_FULL(RENDER_PAL0, 0, 0, 0, BLANK_TILE);
            } else {
                unsigned short vram_tile = (unsigned short)(1u + slot);
                attr = RENDER_TILE_ATTR_FULL(RENDER_PAL0, 0, 0, 0, vram_tile);
            }
            cells[col] = attr;
        }
        render_plane_a_write_row(row, cells, 16u);
    }
}

/* Page 1: 16x16 SPR atlas grid (Genesis sprite slot SPR_TILE_BASE + N).
 * Cell (r, c) shows NES sprite tile_id (r*16 + c) which is loaded at
 * Genesis VRAM slot 533 + tile_id. SCENE_OBJ overlay overrides slots
 * 577..577+N when bank != 0; that's WHAT WE WANT — visual confirmation
 * the overlay landed. PAL1 = NES SPR sub-pal 0 (Link/sword/common).
 * sub_pal cycle selects PAL1 / PAL2 / PAL3 for SPR sub-pals 0/1/2. */
static void redraw_page_spr(void) {
    unsigned short row, col;
    unsigned short cells[16];
    /* Genesis SPR sub-pals route via OAM pal field; for plane A display,
     * we mimic by picking PAL1 + sub_pal (PAL2 = SPR sub-pal 1, PAL3 =
     * SPR sub-pal 2). sub_pal=3 not used (PR-3 collapsed to 3 sub-pals). */
    unsigned char pal = (unsigned char)(RENDER_PAL1 +
                                        ((s_subpal < 3u) ? s_subpal : 2u));
    for (row = 0; row < 16u; row++) {
        for (col = 0; col < 16u; col++) {
            unsigned short tile_id = (unsigned short)(row * 16u + col);
            unsigned short vram_tile = (unsigned short)(UG_SPR_TILE_BASE + tile_id);
            cells[col] = RENDER_TILE_ATTR_FULL(pal, 0, 0, 0, vram_tile);
        }
        render_plane_a_write_row(row, cells, 16u);
    }
}

/* Page 2: ITEM atlas (98 tiles, rows 0-6 partial 16-wide) +
 * Link walk poses (32 tiles, rows 7-8) + attack poses (16 tiles, row 9).
 * NES side will mirror this layout via packing ITEM/Link tiles into PRG
 * CHR-page region. */
static void redraw_page_item_link(void) {
    unsigned short row, col;
    unsigned short cells[16];

    /* Rows 0..5: ITEM atlas tiles 0..95 (96 of 98 tiles). */
    unsigned char pal_item = (unsigned char)(RENDER_PAL1 +
                                             ((s_subpal < 3u) ? s_subpal : 2u));
    for (row = 0; row < 6u; row++) {
        for (col = 0; col < 16u; col++) {
            unsigned short item_idx = (unsigned short)(row * 16u + col);
            unsigned short vram_tile = (unsigned short)(UG_ITEM_VRAM_TILE + item_idx);
            cells[col] = RENDER_TILE_ATTR_FULL(pal_item, 0, 0, 0, vram_tile);
        }
        render_plane_a_write_row(row, cells, 16u);
    }
    /* Row 6: last 2 ITEM tiles 96..97 + 14 blank. */
    for (col = 0; col < 16u; col++) {
        unsigned short item_idx = (unsigned short)(96u + col);
        if (item_idx < 98u) {
            unsigned short vram_tile = (unsigned short)(UG_ITEM_VRAM_TILE + item_idx);
            cells[col] = RENDER_TILE_ATTR_FULL(pal_item, 0, 0, 0, vram_tile);
        } else {
            cells[col] = RENDER_TILE_ATTR_FULL(RENDER_PAL0, 0, 0, 0, BLANK_TILE);
        }
    }
    render_plane_a_write_row(6u, cells, 16u);

    /* Rows 7..8: Link walk poses (32 tiles, 16 per row). */
    for (row = 7u; row < 9u; row++) {
        for (col = 0; col < 16u; col++) {
            unsigned short pose_idx = (unsigned short)((row - 7u) * 16u + col);
            unsigned short vram_tile = (unsigned short)(UG_LINK_VRAM_TILE + pose_idx);
            cells[col] = RENDER_TILE_ATTR_FULL(pal_item, 0, 0, 0, vram_tile);
        }
        render_plane_a_write_row(row, cells, 16u);
    }

    /* Row 9: Link attack poses (16 tiles). */
    for (col = 0; col < 16u; col++) {
        unsigned short vram_tile = (unsigned short)(UG_ATTACK_VRAM_TILE + col);
        cells[col] = RENDER_TILE_ATTR_FULL(pal_item, 0, 0, 0, vram_tile);
    }
    render_plane_a_write_row(9u, cells, 16u);
}

/* Page 3: SCENE_OBJ overlay tiles + Common Misc.
 * Rows 0..3: SCENE_OBJ slots 0..63 (64 tiles in 16x4) — Genesis VRAM tile
 *   UG_SCENE_OBJ_BASE + slot.
 * Row 4: Common Misc (14 tiles + 2 blank). NES Common Misc = common_chr
 *   bytes 7168.., loaded in Genesis VRAM slots 533+224..533+237 (= 757..770). */
static void redraw_page_misc(void) {
    unsigned short row, col;
    unsigned short cells[16];

    unsigned char pal_spr = (unsigned char)(RENDER_PAL1 +
                                            ((s_subpal < 3u) ? s_subpal : 2u));
    /* Rows 0..3: SCENE_OBJ tiles. */
    for (row = 0; row < 4u; row++) {
        for (col = 0; col < 16u; col++) {
            unsigned short slot = (unsigned short)(row * 16u + col);
            unsigned short vram_tile = (unsigned short)(UG_SCENE_OBJ_BASE + slot);
            cells[col] = RENDER_TILE_ATTR_FULL(pal_spr, 0, 0, 0, vram_tile);
        }
        render_plane_a_write_row(row, cells, 16u);
    }

    /* Row 4: Common Misc tiles (14 of 16 cells). */
    for (col = 0; col < 16u; col++) {
        if (col < 14u) {
            /* Genesis VRAM slot for Common Misc tile N = COMMON_VRAM_TILE_BASE
             * + 224 + N (224 = SPR section 112 + BG section 112). */
            unsigned short vram_tile = (unsigned short)(UG_SPR_TILE_BASE + 224u + col);
            cells[col] = RENDER_TILE_ATTR_FULL(RENDER_PAL0, 0, 0, 0, vram_tile);
        } else {
            cells[col] = RENDER_TILE_ATTR_FULL(RENDER_PAL0, 0, 0, 0, BLANK_TILE);
        }
    }
    render_plane_a_write_row(4u, cells, 16u);
}

/* Dispatcher: pick page renderer per s_sprite_page (renamed to s_view_page
 * in semantics — Start button cycles 0..3 already wired). */
static void redraw_bg(void) {
    plane_clear();
    switch (s_sprite_page) {
        case 0u: redraw_page_bg();        break;
        case 1u: redraw_page_spr();       break;
        case 2u: redraw_page_item_link(); break;
        case 3u: redraw_page_misc();      break;
        default: redraw_page_bg();        break;
    }
}

/* Populate SAT 0..63 with 8x8 sprite grid. Slot S shows sprite tile
 * (s_sprite_page * 64 + S) in current sub_pal, at y=$80 + (S>>3)*16,
 * x=(S&7)*16. Slots 64..79 = link=0 terminator. */
/* SAT VRAM base. Title context sets VDP reg 5 = $7C (debug_enter_title in
 * src/debug/a4_probe_main.c:99) -> SAT at $F800. Gameplay context relocates
 * to $F400. We enter debug from title so use $F800. */
#define SAT_VRAM_BASE 0xF800u

static void redraw_sat(void) {
    /* V1 (2026-05-19): clear all sprites. Universal-graphics scene draws
     * every atlas via Plane A grid; SAT sprites would overlap the Plane A
     * cells at row 16+ (y=128+ pixels). Clear ensures PNG diff vs NES side
     * is pure Plane A content with no SAT interference. */
    unsigned char slot;
    render_vram_open_write(SAT_VRAM_BASE);
    for (slot = 0u; slot < 80u; slot++) {
        *((volatile unsigned short *)0xC00000) = 0x0000;   /* y = 0 = off-screen */
        *((volatile unsigned short *)0xC00000) = 0x0000;   /* size + link = 0 terminator */
        *((volatile unsigned short *)0xC00000) = 0x0000;   /* attr */
        *((volatile unsigned short *)0xC00000) = 0x0000;   /* x */
    }
}

/* Edge-detect helper: returns non-zero if `mask` bit is set in curr but
 * not in prev. */
static unsigned char edge(unsigned char curr, unsigned char prev,
                          unsigned char mask) {
    return (unsigned char)((curr & mask) & ~(prev & mask));
}

void debug_tilegrid_main(void) {
    /* Sentinel write so a RAM Watch on $FF07F5 confirms entry. */
    *((volatile unsigned char *)0xFF07F5) = 0xDB;

    /* Upload gameplay atlas to VRAM — title context has only title CHR,
     * so bg_sparse_tile_lut slots point at title art unless we load the
     * gameplay atlas. Match what scene_load does during gameplay init. */
    roomrom_ow_room_render_upload_chr();   /* BG sparse atlas (532 tiles) */
    roomrom_sprites_upload_chr();          /* SPR common + ITEM atlas */
    roomrom_bg_palette_load_palram_full(g_roomrom_ow_palram[0]);  /* orig OW */

    /* Ensure CRAM[0] (global backdrop / PAL0 entry 0) is black. */
    *((volatile unsigned long *)0xC00004) = 0xC0000000UL;
    *((volatile unsigned short *)0xC00000) = 0x0000;

    /* Synchronously write 32 zero bytes to VRAM tile slot 1000 ($7D00).
     * Direct CPU write — bypasses DMA queue so result is visible before
     * we render plane A. This becomes our guaranteed-blank tile for fill. */
    {
        /* VDP CTRL formula for VRAM write at addr A:
         *   ctrl = (0x40000000 | ((A & 0x3FFF) << 16) | ((A & 0xC000) >> 14))
         * For A = $7D00:
         *   addr_lo = $7D00 & 0x3FFF = $3D00
         *   addr_hi = ($7D00 & 0xC000) >> 14 = 1
         *   ctrl    = 0x40000000 | (0x3D00 << 16) | 0x00000001 = 0x7D000001 ? wrong
         * Cleaner: use render_vram_open_write helper. */
        render_vram_open_write((unsigned short)(1000u * 32u));
        unsigned short i;
        for (i = 0; i < 16u; i++) {
            *((volatile unsigned short *)0xC00000) = 0x0000;
        }
    }

    redraw_bg();
    redraw_sat();

    /* Sentinel so probe can detect debug scene is live. */
    *((volatile unsigned char *)(0x00FF0000UL + POKE_FLAG)) = 0x00u;

    for (;;) {
        /* Lua-poke path: if probe wrote $FF07E4 = $AA, apply $FF07E0..3
         * state and clear flag. Mirrors button-press behavior but skips
         * edge-detect timing. Uses direct $FFxxxx addresses (no nes_ram
         * mirror struct in scope). */
        volatile unsigned char *flag = (volatile unsigned char *)(0x00FF0000UL + POKE_FLAG);
        if (*flag == 0xAAu) {
            volatile unsigned char *bank   = (volatile unsigned char *)(0x00FF0000UL + POKE_BANK);
            volatile unsigned char *subpal = (volatile unsigned char *)(0x00FF0000UL + POKE_SUBPAL);
            volatile unsigned char *page   = (volatile unsigned char *)(0x00FF0000UL + POKE_PAGE);
            volatile unsigned char *m8x16  = (volatile unsigned char *)(0x00FF0000UL + POKE_8X16);
            s_bank        = *bank   & 0x07u;
            s_subpal      = *subpal & 0x03u;
            s_sprite_page = *page   & 0x03u;
            s_8x16        = *m8x16  & 0x01u;
            upload_bank();
            redraw_bg();
            redraw_sat();
            *flag = 0x00u;   /* ack */
        }

        unsigned char base, ext;
        joy_read6(&base, &ext);
        state_dump_pad(STATE_DUMP_CTX_TILEGRID, base);   /* A+B+C+Start */

        /* A button -> cycle 8-bank state machine matching NES test ROM. */
        if (edge(base, s_joy_prev, JB_A)) {
            s_bank = (unsigned char)((s_bank + 1u) & 0x07u);
            upload_bank();
            redraw_bg();
            redraw_sat();
        }
        /* B button -> cycle sub_pal (mirrors NES B). */
        if (edge(base, s_joy_prev, JB_B)) {
            s_subpal = (unsigned char)((s_subpal + 1u) & 0x03u);
            redraw_bg();
            redraw_sat();
        }
        /* Start -> cycle sprite-page (mirrors NES Start). */
        if (edge(base, s_joy_prev, JB_START)) {
            s_sprite_page = (unsigned char)((s_sprite_page + 1u) & 0x03u);
            redraw_sat();
        }
        /* C button -> toggle 8x16 (Genesis has no Select; C is the
         * 3-button-safe alternative). Mirrors NES Select. */
        if (edge(base, s_joy_prev, JB_C)) {
            s_8x16 ^= 1u;
            redraw_sat();
        }

        s_joy_prev  = base;
        s_joyx_prev = ext;

        /* Spin a few frames between polls so SGDK DMA / SAT updates land. */
        volatile unsigned short i;
        for (i = 0u; i < 200u; i++) { /* idle */ }
    }
}
