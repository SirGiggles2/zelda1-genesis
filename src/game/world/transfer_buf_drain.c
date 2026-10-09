/* transfer_buf_drain.c — see transfer_buf_drain.h for full task header. */

#include "transfer_buf_drain.h"
#include "world_state.h"          /* TRANSFER_BUF_POS, TRANSFER_BUF_BYTE */
#include "bg_palette.h"           /* roomrom_bg_palette_nes_to_cram */
#include "render_abi.h"           /* render_cram_write_color, render_set_plane_a_word */
#include "platform_abi.h"         /* RAM macro */
#include "../../../engine/src/vram_layout.h" /* ROOMROM_BG_TILE_BASE */
#include "../../../engine/src/engine_state.h" /* roomrom_main_current_scene */
#include "render/ow_render.h"    /* roomrom_ow_room_render_set_tile */
#include "../dungeon/uw_render.h"  /* roomrom_uw_room_render_palette_at */
#include "../hud/hud_runtime.h"    /* roomrom_hud_status_bar_map_cue */
#include "ending_render.h"

/* Plane-bridge: NES PPU nametable ($2000-$2FFF) writes mapped to
 * Genesis Plane A cells. Uses bg_sparse_tile_lut[nes_tile_id][sub_pal]
 * to translate NES tile id to Genesis VRAM slot. Sub-palette is fixed
 * at 0 for text writes (cave NPC dialogue, UW person text all use
 * sub-pal 0). HUD offset = +7 plane rows for playfield region. */
extern const unsigned short bg_sparse_tile_lut[256][4];
#define PLANE_BRIDGE_BLANK_TILE 0u

/* NES source: Z_06 CellarAttrsTransferBuf, Z_05 CopyNextRow.
 * Drained C: this active transfer decoder and OW column renderer.
 * Coverage: FULL (Mode 9 play-area transfers). Stance: EXTEND.
 * Cellars use the OW pattern bank and palette 2, even in a dungeon.
 * Preserve the two protected patterns missing from the sparse atlas. */
static unsigned short playfield_tile_word(unsigned char raw, unsigned char pal)
{
    unsigned short slot;
    if (RAM(0x0012u) == 9u) return roomrom_cellar_tile_word(raw);
    slot = bg_sparse_tile_lut[raw][pal];
    return slot == 0xFFFFu ? PLANE_BRIDGE_BLANK_TILE : ROOMROM_BG_TILE_BASE + slot;
}

/* Maximum buffer span. NES NMI clears DynTileBuf and resets length
 * each frame; the working set in our writers stays well below $40
 * bytes (8-byte palette records, 24-byte attr records). Cap at $60
 * (96) to keep the walker bounded even if a future writer forgets
 * a terminator. */
#define TRANSFER_BUF_MAX_OFFSET 0x60u

/* NES TileBufSelector cell — Variables.inc:5 (`TileBufSelector := $14`).
 * Selectors are byte indices into TransferBufAddrs (Z_06.asm:489) so
 * valid values are even. Non-zero selector = pending static transfer. */
#define TILE_BUF_SELECTOR RAM(0x0014u)

/* ---- Static asm-side palette buffers ----
 *
 * Ported from reference/aldonunez/Z_06.asm. As more Mode N paths
 * activate on Genesis, add their TransferBufAddrs entries here and
 * extend resolve_static_buffer. Selector value = byte offset into
 * TransferBufAddrs (each entry is 2 bytes), so $2C = entry 22. */

/* Z_06.asm:813 Mode11DeadLinkPalette = selector $2C (entry 22). */
static const unsigned char k_mode11_dead_link_palette[] = {
    0x3Fu, 0x10u, 0x04u, 0x0Fu, 0x10u, 0x30u, 0x00u, 0xFFu
};

/* Z_06.asm:690 BlankTextBoxLines = selector $1E (entry 15): the person
 * text lines 1-2 (NT $21A4/$21C4, 24 blanks each). Cued when a cave item
 * is taken (t011 tick 501). */
static const unsigned char k_blank_text_box_lines[] = {
    0x21u, 0xA4u, 0x58u, 0x24u, 0x21u, 0xC4u, 0x58u, 0x24u, 0xFFu
};

/* Z_06.asm:694 BlankPersonWares = selector $2A (entry 21): text line 3
 * (NT $21E4, 24 blanks) and the price row (NT $22C8, 13 blanks). Cued by
 * UpdatePersonState_CueTransferBlankPersonWares (t011 tick 502). */
static const unsigned char k_blank_person_wares[] = {
    0x21u, 0xE4u, 0x58u, 0x24u, 0x22u, 0xC8u, 0x4Du, 0x24u, 0xFFu
};

/* Z_06.asm:722 WhitePaletteBottomHalfTransferBuf = selector $78: BG
 * palette rows 2-3 ($3F08) white. Mode $12 flashes it (T-013). */
static const unsigned char k_white_palette_bottom_half[] = {
    0x3Fu, 0x08u, 0x08u, 0x0Fu, 0x30u, 0x30u, 0x30u, 0x0Fu,
    0x30u, 0x30u, 0x30u, 0xFFu
};

/* Z_06.asm:718, selector $26: all cellar play-area attributes = 2. */
static const unsigned char k_cellar_attrs[] = {
    0x23u, 0xD0u, 0x60u, 0xAAu, 0x23u, 0xF0u, 0x50u, 0xAAu, 0xFFu
};

/* Z_06:685 EndingPaletteTransferBuf, selector $6A. */
static const unsigned char k_ending_palette[] = {
    0x3Fu,0x08u,0x08u,0x0Fu,0x22u,0x10u,0x00u,0x0Fu,0x2Au,0x10u,0x00u,
    0x3Fu,0x1Cu,0x04u,0x0Fu,0x27u,0x06u,0x16u,0xFFu
};

/* T-097 Mode 11 buffers (Z_06.asm:816-830, byte for byte). */
static const unsigned char k_mode11_bg_palette_bottom_half[] = {   /* $5E */
    0x3Fu, 0x08u, 0x08u, 0x0Fu, 0x17u, 0x16u, 0x26u, 0x0Fu,
    0x17u, 0x16u, 0x26u, 0xFFu
};
static const unsigned char k_mode11_attrs_top_half[] = {          /* $60 */
    0x23u, 0xD0u, 0x58u, 0xFFu, 0xFFu
};
static const unsigned char k_mode11_attrs_bottom_half[] = {       /* $62 */
    0x23u, 0xE8u, 0x58u, 0xFFu, 0xFFu
};
/* Z_06.asm BombCapacityPriceTextTransferBuf ($6C, "-100" under the
 * bomb-capacity seller) and LifeOrMoneyCostTextTransferBuf ($76,
 * "-1   -50" in the money-or-life room). Missing selectors drew nothing
 * (room_sweep Q1 L7 $48, Q2 L5 $0D). */
static const unsigned char k_bomb_capacity_price_text[] = {       /* $6C */
    0x22u, 0xCDu, 0x04u, 0x62u, 0x01u, 0x00u, 0x00u, 0xFFu
};
static const unsigned char k_life_or_money_cost_text[] = {        /* $76 */
    0x22u, 0xCBu, 0x0Au, 0x62u, 0x01u, 0x24u, 0x24u, 0x24u,
    0x24u, 0x24u, 0x62u, 0x05u, 0x00u, 0xFFu
};
static const unsigned char k_game_over[] = {                      /* $46 */
    0x23u, 0xE3u, 0x03u, 0x0Fu, 0x0Fu, 0xCFu, 0x22u, 0x4Cu,
    0x0Au, 0x10u, 0x0Au, 0x16u, 0x0Eu, 0x24u, 0x18u, 0x1Fu,
    0x0Eu, 0x1Bu, 0x24u, 0x22u, 0x6Cu, 0x4Au, 0x24u, 0xFFu
};

/* Sprite palette row 7 ($3F1C, NES sprite sub-palette 3 = PAL1 colors
 * 12..15) buffers, Z_06.asm:704-733 / 682 byte for byte. Cued by
 * InitMode5 @ChooseTileObjPalette (tile objects: gravestone, Armos, rock)
 * and the boss inits. T-171: unhandled selectors were dropped, so the
 * boss bank's sub-palette 3 copy (PAL1 13..15) kept the level's row 7
 * (t171_boss_l7 Aquamentus drawn $1A/$2A, NES $29/$30). */
static const unsigned char k_aquamentus_row7[]  = { 0x3Fu, 0x1Cu, 0x04u, 0x0Fu, 0x0Au, 0x29u, 0x30u, 0xFFu }; /* $08 */
static const unsigned char k_orange_boss_row7[] = { 0x3Fu, 0x1Cu, 0x04u, 0x0Fu, 0x17u, 0x27u, 0x30u, 0xFFu }; /* $0A */
static const unsigned char k_ghost_row7[]       = { 0x3Fu, 0x1Cu, 0x04u, 0x0Fu, 0x30u, 0x00u, 0x12u, 0xFFu }; /* $20 */
static const unsigned char k_green_bg_row7[]    = { 0x3Fu, 0x1Cu, 0x04u, 0x0Fu, 0x1Au, 0x37u, 0x12u, 0xFFu }; /* $22 */
static const unsigned char k_brown_bg_row7[]    = { 0x3Fu, 0x1Cu, 0x04u, 0x0Fu, 0x17u, 0x37u, 0x12u, 0xFFu }; /* $24 */
static const unsigned char k_ganon_row7[]       = { 0x3Fu, 0x1Cu, 0x04u, 0x0Fu, 0x16u, 0x2Cu, 0x3Cu, 0xFFu }; /* $36 */
static const unsigned char k_red_armos_row7[]   = { 0x3Fu, 0x1Cu, 0x04u, 0x0Fu, 0x0Fu, 0x1Cu, 0x16u, 0xFFu }; /* $7A */
static const unsigned char k_gleeok_row7[]      = { 0x3Fu, 0x1Cu, 0x04u, 0x0Fu, 0x2Au, 0x1Au, 0x0Cu, 0xFFu }; /* $7C */
/* $06 LevelPaletteRow7TransferBuf: InitMode5 @UseLevelPalette patches
 * its four colors from LevelInfo_PalettesTransferBuf+31 ($6B9D). */
static unsigned char s_level_row7[] = { 0x3Fu, 0x1Cu, 0x04u, 0x0Fu, 0x0Fu, 0x0Fu, 0x0Fu, 0xFFu };

/* Selector $18 = LevelInfo_PalettesTransferBuf (Variables.inc: $6B7E),
 * the level's palette record in the installed LevelInfo block:
 * $3F00 x $20 colors + terminator (36 bytes). */
#define LEVEL_PALETTES_TRANSFER_BUF 0x6B7Eu

/* Map selector -> static buffer pointer + length. Returns NULL if
 * unknown (caller clears selector to avoid pile-up). */
static const unsigned char *resolve_static_buffer(unsigned char selector,
                                                  unsigned char *out_len)
{
    switch (selector) {
    case 0x6Au: *out_len = (unsigned char)sizeof k_ending_palette; return k_ending_palette;
    case 0x6Cu:
        *out_len = (unsigned char)sizeof k_bomb_capacity_price_text;
        return k_bomb_capacity_price_text;
    case 0x76u:
        *out_len = (unsigned char)sizeof k_life_or_money_cost_text;
        return k_life_or_money_cost_text;
    case 0x1Eu:
        *out_len = (unsigned char)sizeof k_blank_text_box_lines;
        return k_blank_text_box_lines;
    case 0x2Au:
        *out_len = (unsigned char)sizeof k_blank_person_wares;
        return k_blank_person_wares;
    case 0x26u:
        *out_len = (unsigned char)sizeof k_cellar_attrs;
        return k_cellar_attrs;
    case 0x2Cu:
        *out_len = (unsigned char)sizeof k_mode11_dead_link_palette;
        return k_mode11_dead_link_palette;
    case 0x06u: {
        unsigned char i;
        for (i = 0u; i < 4u; ++i)
            s_level_row7[3u + i] = nes_ram[LEVEL_PALETTES_TRANSFER_BUF + 31u + i];
        *out_len = (unsigned char)sizeof s_level_row7;
        return s_level_row7;
    }
    case 0x08u: *out_len = (unsigned char)sizeof k_aquamentus_row7;  return k_aquamentus_row7;
    case 0x0Au: *out_len = (unsigned char)sizeof k_orange_boss_row7; return k_orange_boss_row7;
    case 0x20u: *out_len = (unsigned char)sizeof k_ghost_row7;       return k_ghost_row7;
    case 0x22u: *out_len = (unsigned char)sizeof k_green_bg_row7;    return k_green_bg_row7;
    case 0x24u: *out_len = (unsigned char)sizeof k_brown_bg_row7;    return k_brown_bg_row7;
    case 0x36u: *out_len = (unsigned char)sizeof k_ganon_row7;       return k_ganon_row7;
    case 0x7Au: *out_len = (unsigned char)sizeof k_red_armos_row7;   return k_red_armos_row7;
    case 0x7Cu: *out_len = (unsigned char)sizeof k_gleeok_row7;      return k_gleeok_row7;
    case 0x18u:
        *out_len = 36u;
        return (const unsigned char *)&nes_ram[LEVEL_PALETTES_TRANSFER_BUF];
    case 0x78u:
        *out_len = (unsigned char)sizeof k_white_palette_bottom_half;
        return k_white_palette_bottom_half;
    case 0x5Eu:
        *out_len = (unsigned char)sizeof k_mode11_bg_palette_bottom_half;
        return k_mode11_bg_palette_bottom_half;
    case 0x60u:
        *out_len = (unsigned char)sizeof k_mode11_attrs_top_half;
        return k_mode11_attrs_top_half;
    case 0x62u:
        *out_len = (unsigned char)sizeof k_mode11_attrs_bottom_half;
        return k_mode11_attrs_bottom_half;
    case 0x46u:
        *out_len = (unsigned char)sizeof k_game_over;
        return k_game_over;
    default:
        *out_len = 0u;
        return (const unsigned char *)0;
    }
}

/* Process a single $3F palette record at byte offset `payload_off`
 * within whichever source the caller is walking.
 *
 * Phase R (2026-05-18 perf polish): batch contiguous CRAM writes via
 * render_cram_subrange_upload when no slot wrap occurs. Previous impl
 * looped render_cram_write_color per color (one 2-byte CRAM word per
 * call); during dead-Link palette fade or item-pickup flash storms
 * this added up to 96 calls per frame. Batched path = 1 subrange
 * upload for the whole record. Slot wrap (rare; only when slot_base +
 * count > 32) falls back to per-color path. */
static void emit_palette_record(unsigned char lo,
                                unsigned char count,
                                const unsigned char *src,
                                unsigned char src_off,
                                unsigned char src_end)
{
    unsigned char slot_base = (unsigned char)(lo & 0x1Fu);
    /* Cap count to the bytes actually available in source. */
    unsigned char src_avail = (unsigned char)((src_off < src_end)
                                              ? (src_end - src_off) : 0u);
    if (count > src_avail) count = src_avail;
    if (count == 0u) return;

    /* Fast path: no slot wrap. Convert NES->CRAM into local buffer,
     * subrange-upload once. CRAM has 64 slots (4 PALs x 16); but
     * NES palram only addresses lower 32 slots ($00..$1F). */
    if ((unsigned short)slot_base + (unsigned short)count <= 32u) {
        unsigned short cram_buf[64];  /* upper bound = NES record max */
        unsigned char i;
        for (i = 0u; i < count; i++) {
            cram_buf[i] = roomrom_bg_palette_nes_to_cram(src[src_off + i]);
        }
        render_cram_subrange_upload((unsigned short)slot_base, cram_buf, count);
        /* NES source: Z_02 Sub0_Flash writes palette $3F10, which aliases
         * universal backdrop $3F00 on the PPU. Drained C: this record
         * decoder. Coverage: records spanning that alias. Stance: EXTEND.
         * Preserve the final write's order in a full $3F00/$20 record. */
        if (slot_base <= 0x10u && (unsigned short)slot_base + count > 0x10u)
            render_cram_write_color(0u, cram_buf[0x10u - slot_base]);
        return;
    }

    /* Fallback: slot wrap requires per-slot writes (subrange would
     * stride past CRAM end). Rare in NES Z1 records. */
    {
        unsigned char i;
        for (i = 0u; i < count; i++) {
            unsigned char nes_color = src[src_off + i];
            unsigned short cram = roomrom_bg_palette_nes_to_cram(nes_color);
            unsigned short slot = (unsigned short)((slot_base + i) & 0x1Fu);
            render_cram_write_color(slot, cram);
        }
    }
}

/* NES nametable record decoder. PPU address ($hi:$lo) range $2000-$2FFF
 * = nametable region; each NT is 32 cols × 30 rows. NES encoding sends
 * `count` consecutive tile ids starting at the decoded (col, row).
 *
 * Genesis: write Plane A cells via render_set_plane_a_word. HUD takes
 * top 7 plane rows (Window-overlaid); playfield cells = NES row + 7.
 * NES sub-palette for text is fixed at 0 (BG_attr=0). Tile id maps
 * via bg_sparse_tile_lut[nes_tile][0]; 0xFFFF sentinel = unmapped tile
 * → write blank tile (slot 0).
 *
 * Count byte (NES Z_07.asm ContinueTransferTileBuf): bit 7 set = VRAM
 * increment 32 (vertical, +1 row per tile); bit 6 set = one source byte
 * repeated `count` times. ChangeTileObjTiles records use $82 (vertical).
 *
 * T-050: OW playfield cells (NT rows 8..29) go through
 * roomrom_ow_room_render_set_tile, which knows the active room's plane
 * slot, attribute palette, raw-tile cache and walkability. */
/* T-097: attribute-table writes ($23C0-$23FF, any name table). Each byte
 * sets the sub-palette of four 2x2 cell quadrants; the play-area cells
 * (name-table rows 8..29) are redrawn from NES PlayAreaTiles ($6530,
 * column-major, 22 rows a column) with the sub-palette's atlas copy.
 * Status-bar attributes (rows 0..7) are the Genesis HUD's own. */
/* Attribute bytes written since the last room load: name-table text then
 * takes its sub-palette from them, as the NES does (Mode 11 GAME OVER on
 * palette 0 after the play area faded). 0 = none written. */
static unsigned char s_attr_shadow[64];
static unsigned char s_attr_shadow_active;
/* Name table cleared by TurnOffVideoAndClearArtifacts (Modes 8 and D):
 * its play-area cells are blank tile $24 or the mode's own text, which
 * that mode draws with the attribute's sub-palette (Mode 8 flashes its
 * selection by attribute record, Z_05.asm Mode8FlashTransferRecord). A
 * blank cell shows the backdrop under any sub-palette, so an attribute
 * write redraws nothing; PlayAreaTiles still holds the last room. */
static unsigned char s_nt_cleared;

void transfer_buf_attr_shadow_reset(void)
{
    s_attr_shadow_active = 0u;
    s_nt_cleared = 0u;
}

void transfer_buf_nt_cleared(void)
{
    s_nt_cleared = 1u;
}

static unsigned char attr_shadow_pal(unsigned char col, unsigned char row)
{
    const unsigned char v = s_attr_shadow[((row >> 2) << 3) | (col >> 2)];
    const unsigned char q = (unsigned char)((((row >> 1) & 1u) << 1) | ((col >> 1) & 1u));
    return (unsigned char)((v >> (q << 1)) & 3u);
}

extern unsigned short *roomrom_main_row_stage(void);   /* main.c (T-172) */

/* T-172: a record of whole attribute rows (Mode 11's death palette: 24
 * bytes = 12 name-table rows each) rewrote ~380 cells one VDP address at a
 * time: Death_Sub4/Sub5 took 3 frames each (NES 1). Build each name-table
 * row in the row stage and queue it for the next VBlank (the NMI timing
 * of the per-cell deferral). Returns 0 when the record does not qualify or
 * the stage / DMA queue is unavailable (the caller then goes per cell). */
static unsigned char emit_attribute_rows(unsigned char attr_off, unsigned char repeat,
                                         unsigned char count, const unsigned char *src,
                                         unsigned char src_off, unsigned char src_end)
{
    unsigned short *stage;
    unsigned char i, row, row0, row1;
    if ((attr_off & 7u) || (count & 7u) || count == 0u ||
        (unsigned short)attr_off + count > 0x40u)
        return 0u;
    if (repeat ? src_off >= src_end : (unsigned char)(src_off + count) > src_end)
        return 0u;
    stage = roomrom_main_row_stage();
    if (!stage) return 0u;
    for (i = 0u; i < count; i++) {
        s_attr_shadow[attr_off + i] = src[src_off + (repeat ? 0u : i)];
    }
    s_attr_shadow_active = 1u;
    row0 = (unsigned char)((attr_off >> 3) << 2);
    row1 = (unsigned char)(((attr_off + count) >> 3) << 2);
    if (row0 < 8u) row0 = 8u;
    if (row1 > 30u) row1 = 30u;
    for (row = row0; row < row1; ++row) {
        unsigned short *w = stage + (unsigned short)(row - 8u) * 32u;
        /* PlayAreaTiles is column-major (22 rows): step a column at a time. */
        const unsigned char *pa = (const unsigned char *)(unsigned long)
            &nes_ram[0x6530u + (unsigned char)(row - 8u)];
        const unsigned char *at = &s_attr_shadow[(row >> 2) << 3];
        const unsigned char vshift = (unsigned char)(((row >> 1) & 1u) << 2);
        unsigned short pc, pr;
        unsigned char col;
        for (col = 0u; col < 32u; col = (unsigned char)(col + 2u)) {
            /* attr_shadow_pal: one 2x2 quadrant per two columns. */
            const unsigned char pal = (unsigned char)((at[col >> 2] >>
                (vshift | (((col >> 1) & 1u) << 1))) & 3u);
            w[col] = playfield_tile_word(pa[0], pal);
            w[col + 1u] = playfield_tile_word(pa[0x16u], pal);
            pa += 2u * 0x16u;
        }
        /* Columns are consecutive plane cells (scroll offsets are whole
         * cells: ((col << 3) - sx) & 511 >> 3 = first + col, mod 64). */
        roomrom_main_nt_cell_to_plane(0u, row, &pc, &pr);
        if (pc + 32u <= 64u) {
            if (!render_plane_a_queue_row(pr, pc, w, 32u)) return 0u;
        } else {
            const unsigned short n = (unsigned short)(64u - pc);
            if (!render_plane_a_queue_row(pr, pc, w, n)) return 0u;
            if (!render_plane_a_queue_row(pr, 0u, w + n, (unsigned short)(32u - n))) return 0u;
        }
    }
    return 1u;
}

static void emit_attribute_record(unsigned char attr_off,
                                  unsigned char repeat,
                                  unsigned char count,
                                  const unsigned char *src,
                                  unsigned char src_off,
                                  unsigned char src_end)
{
    unsigned char i;
    /* A cellar (mode 9) draws every cell with sub-palette 2 whatever the
     * attributes say (roomrom_cellar_tile_word): CellarAttrsTransferBuf
     * (all 2) changes nothing on screen, and rewriting the 704 cells took
     * 1.5 frames (T-171: a lag frame in the L1 bow cellar load). The
     * shadow still follows the NES attribute table. A cellar return
     * (mode $0A in a level) laid the destination room out with its own
     * attributes (cellar_host_layout -> load_room), which InitModeA_Sub6/7
     * transfer again: the same words, and their rewrite overran a frame
     * (T-171 ls_l1_cellar f268). */
    if (s_nt_cleared || RAM(0x0012u) == 9u ||
        (RAM(0x0012u) == 0x0Au && RAM(0x0010u) != 0u)) {
        for (i = 0u; i < count && (unsigned char)(attr_off + i) < 0x40u; i++) {
            if ((unsigned char)(src_off + (repeat ? 0u : i)) >= src_end) break;
            s_attr_shadow[attr_off + i] = src[src_off + (repeat ? 0u : i)];
            s_attr_shadow_active = 1u;
        }
        return;
    }
    if (emit_attribute_rows(attr_off, repeat, count, src, src_off, src_end)) return;
    for (i = 0u; i < count; i++) {
        const unsigned char k = (unsigned char)(attr_off + i);
        unsigned char v, dr, dc;
        if (k >= 0x40u) break;
        if ((unsigned char)(src_off + (repeat ? 0u : i)) >= src_end) break;
        v = src[src_off + (repeat ? 0u : i)];
        s_attr_shadow[k] = v;
        s_attr_shadow_active = 1u;
        for (dr = 0u; dr < 4u; dr++) {
            const unsigned char row = (unsigned char)(((k >> 3) << 2) + dr);
            if (row < 8u || row >= 30u) continue;
            for (dc = 0u; dc < 4u; dc++) {
                const unsigned char col = (unsigned char)(((k & 7u) << 2) + dc);
                const unsigned char q = (unsigned char)(((dr >> 1) << 1) | (dc >> 1));
                const unsigned char pal = (unsigned char)((v >> (q << 1)) & 3u);
                const unsigned char raw =
                    nes_ram[0x6530u + (unsigned short)col * 0x16u + (unsigned char)(row - 8u)];
                unsigned short pc, pr;
                roomrom_main_nt_cell_to_plane(col, row, &pc, &pr);
                render_set_plane_a_word(pc, pr, playfield_tile_word(raw, pal));
            }
        }
    }
}

static void emit_nametable_record(unsigned char hi,
                                  unsigned char lo,
                                  unsigned char ctrl,
                                  unsigned char count,
                                  const unsigned char *src,
                                  unsigned char src_off,
                                  unsigned char src_end)
{
    /* Credits own two NTs and their attributes. Gameplay's one-room
     * bridge intentionally collapses NT addresses, so route before it. */
    if (RAM(0x0012u) == 0x13u && RAM(0x0011u) && RAM(0x0013u) >= 3u &&
        ending_render_record(hi, lo, ctrl, count, src, src_off, src_end)) return;
    const unsigned char vertical = (unsigned char)(ctrl & 0x80u);
    const unsigned char repeat   = (unsigned char)(ctrl & 0x40u);
    const unsigned char ow = (roomrom_main_current_scene() == ROOMROM_MAIN_SCENE_OW);
    /* PPU addr = ((hi & 0x0F) << 8) | lo; range $0000..$03FF = NT0 cells. */
    unsigned short ppu_off = (unsigned short)(((hi & 0x0Fu) << 8) | lo);
    if (ppu_off >= 0x3C0u) {
        emit_attribute_record((unsigned char)(ppu_off - 0x3C0u), repeat, count,
                              src, src_off, src_end);
        return;
    }
    unsigned char nes_row  = (unsigned char)(ppu_off >> 5);   /* /32 */
    unsigned char nes_col  = (unsigned char)(ppu_off & 0x1Fu);/* mod 32 */
    /* Name-table rows are screen rows (person text line 1 = $21A4 = row
     * 13). The plane cell comes from the room's scroll
     * (roomrom_main_nt_cell_to_plane: NT row r at screen line 8r - 8).
     * T-135 used r - 1 for caves (scroll 0); T-166: UW person text used
     * r + 7 without the scroll and landed below/left of the NES text. */

    unsigned char src_avail = (unsigned char)((src_off < src_end)
                                              ? (src_end - src_off) : 0u);
    if (repeat) {
        if (src_avail == 0u) return;
    } else if (count > src_avail) {
        count = src_avail;
    }

    unsigned char i;
    for (i = 0u; i < count; i++) {
        unsigned char nes_tile = src[src_off + (repeat ? 0u : i)];
        unsigned char cell_row = (unsigned char)(nes_row + (vertical ? i : 0u));
        unsigned char cell_col = (unsigned char)(nes_col + (vertical ? 0u : i));
        if (RAM(0x0012u) == 9u && cell_row >= 8u && cell_row < 30u && cell_col < 32u) {
            unsigned short pc, pr;
            roomrom_main_nt_cell_to_plane(cell_col, cell_row, &pc, &pr);
            render_set_plane_a_word(pc, pr, roomrom_cellar_tile_word(nes_tile));
            continue;
        }
        if (s_attr_shadow_active && cell_row < 30u && cell_col < 32u) {
            const unsigned short sl =
                bg_sparse_tile_lut[nes_tile][attr_shadow_pal(cell_col, cell_row)];
            unsigned short qc, qr;
            roomrom_main_nt_cell_to_plane(cell_col, cell_row, &qc, &qr);
            render_set_plane_a_word(qc, qr, (sl == 0xFFFFu)
                ? (unsigned short)PLANE_BRIDGE_BLANK_TILE
                : (unsigned short)(ROOMROM_BG_TILE_BASE + sl));
            continue;
        }
        if (ow && cell_row >= 8u && cell_row < 30u && cell_col < 32u &&
            roomrom_ow_room_render_set_tile(cell_col,
                                            (unsigned char)(cell_row - 8u),
                                            nes_tile)) {
            continue;
        }
        /* A name-table write keeps the cell's attribute (PPU): in the UW
         * play area that is the room's palette (T-171 t129 t5134: a 2x2
         * of $26 under inner palette 3 was drawn with palette 0). */
        const unsigned char pal =
            (roomrom_main_current_scene() == ROOMROM_MAIN_SCENE_UW &&
             cell_row >= 8u && cell_row < 30u && cell_col < 32u)
            ? roomrom_uw_room_render_palette_at(cell_col, cell_row) : 0u;
        unsigned short raw_slot = bg_sparse_tile_lut[nes_tile][pal & 3u];
        /* Mirror tile_word() (ow_render.c): VRAM tile = ROOMROM_BG_TILE_BASE
         * + sparse slot; unmapped (0xFFFF) -> blank tile 0. The original
         * code wrote the raw slot WITHOUT the +BG_BASE bias, so every text
         * glyph rendered one VRAM tile too low -> garbled cave NPC + UW
         * person text (chars + columns were correct; only the tile base was
         * missing). Byte-verified: Gen Plane A row held slots $48/$6D/$6A...
         * = I/T/S = "IT'S DANGEROUS" but displayed shifted by -1 tile. */
        unsigned short tile = (raw_slot == 0xFFFFu)
            ? (unsigned short)PLANE_BRIDGE_BLANK_TILE
            : (unsigned short)(ROOMROM_BG_TILE_BASE + raw_slot);
        unsigned short pc, pr;
        roomrom_main_nt_cell_to_plane((unsigned char)(cell_col & 0x1Fu), cell_row, &pc, &pr);
        render_set_plane_a_word(pc, pr, tile);
    }
}

/* Walk a record-formatted byte buffer up to `len`. */
static void drain_record_buffer(const unsigned char *buf, unsigned char len)
{
    unsigned char pos = 0u;
    while (pos < len) {
        unsigned char hi = buf[pos];
        if (hi >= 0x80u) {
            break;                         /* $FF (or any neg) = end */
        }
        if ((unsigned char)(pos + 2u) >= len) {
            break;                         /* truncated header */
        }
        unsigned char lo   = buf[pos + 1u];
        unsigned char ctrl = buf[pos + 2u];
        unsigned char count = (unsigned char)(ctrl & 0x3Fu);
        if (count == 0u) {
            count = 64u;                   /* NES encoding: 0 = 64 */
        }
        if (hi == 0x3Fu) {
            emit_palette_record(lo, count, buf,
                                (unsigned char)(pos + 3u), len);
        } else if (hi >= 0x20u && hi <= 0x2Fu) {
            emit_nametable_record(hi, lo, ctrl, count, buf,
                                  (unsigned char)(pos + 3u), len);
        }
        pos = (unsigned char)(pos + 3u + ((ctrl & 0x40u) ? 1u : count));
    }
}

/* Drain the dynamic TRANSFER_BUF at nes_ram[$0302..]. */
static void drain_dynamic_buffer(void)
{
    unsigned char end = (unsigned char)TRANSFER_BUF_POS;
    if (end == 0u) {
        /* TransferCurTileBuf (selector 0) walks DynTileBuf records up to
         * the $FF terminator whatever DynTileBufLen says; some writers
         * (CueTransferPondPaletteRow) copy a record without a length. */
        if ((unsigned char)TRANSFER_BUF_BYTE(0) >= 0x80u) return;
        end = TRANSFER_BUF_MAX_OFFSET;
    }
    if (end > TRANSFER_BUF_MAX_OFFSET) {
        end = TRANSFER_BUF_MAX_OFFSET;
    }
    unsigned char pos = 0u;
    while (pos < end) {
        unsigned char hi = (unsigned char)TRANSFER_BUF_BYTE(pos);
        if (hi >= 0x80u) {
            break;
        }
        if ((unsigned char)(pos + 2u) >= end) {
            break;
        }
        unsigned char lo   = (unsigned char)TRANSFER_BUF_BYTE(pos + 1u);
        unsigned char ctrl = (unsigned char)TRANSFER_BUF_BYTE(pos + 2u);
        unsigned char count = (unsigned char)(ctrl & 0x3Fu);
        if (count == 0u) {
            count = 64u;
        }
        if (hi == 0x3Fu || (hi >= 0x20u && hi <= 0x2Fu)) {
            /* Copy payload from TRANSFER_BUF macro into local buffer
             * so the per-record emitter can index it as a flat array
             * (avoids duplicating wrap/avail logic between dynamic
             * and static drain paths). */
            unsigned char payload[64];
            unsigned char i;
            unsigned char copy_len = (ctrl & 0x40u) ? 1u : count;
            unsigned char avail = (unsigned char)((pos + 3u < end)
                                                   ? (end - pos - 3u) : 0u);
            if (copy_len > avail) copy_len = avail;
            for (i = 0u; i < copy_len; i++) {
                payload[i] = (unsigned char)TRANSFER_BUF_BYTE(pos + 3u + i);
            }
            if (hi == 0x3Fu) {
                if ((ctrl & 0x40u) && copy_len == 1u) {
                    for (i = 1u; i < count; i++) payload[i] = payload[0];
                    copy_len = count;
                }
                emit_palette_record(lo, copy_len, payload, 0u, copy_len);
            } else {
                emit_nametable_record(hi, lo, ctrl, count, payload, 0u, copy_len);
            }
        }
        pos = (unsigned char)(pos + 3u + ((ctrl & 0x40u) ? 1u : count));
    }
    TRANSFER_BUF_POS     = 0u;
    TRANSFER_BUF_BYTE(0) = 0xFFu;
}

/* Drain the static TileBufSelector path. */
static void drain_static_selector(void)
{
    unsigned char selector = (unsigned char)TILE_BUF_SELECTOR;
    if (selector == 0u) {
        return;
    }
    unsigned char len = 0u;
    const unsigned char *buf;
    /* $44 LevelInfo_StatusBarMapTransferBuf: the Genesis HUD owns the
     * status bar map (hud_runtime.c), which draws it on this cue. */
    if (selector == 0x44u) roomrom_hud_status_bar_map_cue();
    buf = resolve_static_buffer(selector, &len);
    if (buf != (const unsigned char *)0) {
        drain_record_buffer(buf, len);
    }
    /* Always clear selector — unknown selectors are intentionally
     * ignored (no asm-side processing on Genesis) so leaving the
     * value set would re-trigger every frame. */
    TILE_BUF_SELECTOR = 0u;
}

/* T-172: a Genesis-native writer that replaces an NES DynTileBuf record
 * (door animation) notes it here, so NES code that tests the buffer
 * (World_ChangeRupees) sees it occupied until this tick's drain (NMI). */
static unsigned char s_native_record;

void transfer_buf_note_native_record(void)
{
    s_native_record = 1u;
}

unsigned char transfer_buf_dyn_busy(void)
{
    return (unsigned char)(s_native_record || !(nes_ram[0x0302u] & 0x80u));
}

void transfer_buf_drain(void)
{
    s_native_record = 0u;
    /* The NES transfers these records in the next frame's NMI: the cells
     * are written at the start of the next tick (render_plane_defer_flush
     * after the VBlank process), the RAM side effects stay here. */
    render_plane_defer(1u);
    drain_dynamic_buffer();
    drain_static_selector();
    render_plane_defer(0u);
}
