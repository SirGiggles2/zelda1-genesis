/* NES source: Z_02 DrawCredits / Z_07 IsrNmi (240-pixel NT pages).
 * Drained C: mode_wingame/transfer_buf_drain and native render ABI.
 * Coverage: credits row/attribute presentation, not ending progression.
 * Stance: EXTEND native presentation; no PPU emulation or new assets.
 *
 * NES pages contain 30 rows. Put the initial screen at virtual rows0..29,
 * then incoming credit rows at30 onward, modulo the native64-row plane.
 * Continuous pixel scroll avoids exposing the two unused rows in each
 * NES256-pixel address page. The row DMA sources stay resident until NMI.
 */
#include "ending_render.h"
#include "render_abi.h"
#include "../../../engine/src/engine_state.h"
#include "../../../engine/src/vram_layout.h"

extern const unsigned short bg_sparse_tile_lut[256][4];
extern const unsigned char common_chr[7616];
/* Live Q1+Q2 credits need15 pairs absent from the room-derived atlas.
 * Reuse16 scene-art slots while the room is blanked. All ending glyphs
 * are common CHR; glyph/HUD/misc atlas slots remain resident. Nothing is
 * added to the distributed asset set or to the permanent VRAM budget. */
#define ENDING_GLYPH_CACHE_COUNT 16u
static unsigned short s_glyph_words[ENDING_GLYPH_CACHE_COUNT][16];
static unsigned short s_original_glyph_words[ENDING_GLYPH_CACHE_COUNT * 16u];
static unsigned char s_glyph_tile[ENDING_GLYPH_CACHE_COUNT];
static unsigned char s_glyph_pal[ENDING_GLYPH_CACHE_COUNT];
static unsigned char s_glyph_count;
static unsigned short s_glyph_base;
unsigned char g_ending_render_fault;
static unsigned short s_words[64][32];
static unsigned char s_tiles[64][32];
static unsigned char s_attrs[2][64];
static unsigned short s_write_base;
static unsigned char s_active;

static void queue_row(unsigned short row)
{
    /* A full queue still produces the row; never silently drop credits. */
    if (!render_plane_a_queue_row(row, 0u, s_words[row], 32u))
        render_plane_a_write_row(row, s_words[row], 32u);
}

void ending_render_reset(void)
{
    if (s_active && !(g_ending_render_fault & 1u)) {
        const unsigned short addr = (unsigned short)((ROOMROM_BG_TILE_BASE + s_glyph_base) * 32u);
        if (!render_vram_queue_words(addr, s_original_glyph_words,
                                    ENDING_GLYPH_CACHE_COUNT * 16u))
            render_chr_upload(addr, (const unsigned char *)s_original_glyph_words,
                              ENDING_GLYPH_CACHE_COUNT * 32u);
    }
    s_active = 0u;
}

void ending_render_begin(void)
{
    unsigned short row, col;
    unsigned short misc_start = 0xFFFFu;
    s_glyph_count = g_ending_render_fault = 0u;
    s_glyph_base = 0xFFFFu;
    for (row = 0x70u; row < 0xF2u; ++row)
        for (col = 0u; col < 4u; ++col)
            if (bg_sparse_tile_lut[row][col] < s_glyph_base)
                s_glyph_base = bg_sparse_tile_lut[row][col];
    for (row = 0xF2u; row < 256u; ++row)
        for (col = 0u; col < 4u; ++col)
            if (bg_sparse_tile_lut[row][col] < misc_start)
                misc_start = bg_sparse_tile_lut[row][col];
    if ((unsigned short)(s_glyph_base + ENDING_GLYPH_CACHE_COUNT) > misc_start)
        g_ending_render_fault = 1u;
    if (!g_ending_render_fault)
        render_vram_read_run((unsigned short)((ROOMROM_BG_TILE_BASE + s_glyph_base) * 32u),
                             s_original_glyph_words, ENDING_GLYPH_CACHE_COUNT * 16u);
    for (row = 0u; row < 64u; ++row)
        for (col = 0u; col < 32u; ++col) {
            s_words[row][col] = 0u;
            s_tiles[row][col] = 0x24u;
        }
    for (row = 0u; row < 2u; ++row)
        for (col = 0u; col < 64u; ++col) s_attrs[row][col] = 0u;
    /* NES row0 is outside the cropped Genesis frame and scrolls upward.
     * HUD Window rows0..6 represent NES rows1..7. Preserve current words,
     * including the peace text (not present in PlayAreaTiles anymore). */
    for (row = 1u; row < 30u; ++row) {
        if (row < 8u) {
            render_vram_read_run((unsigned short)(0xE000u + (row - 1u) * 64u),
                                 s_words[row], 32u);
        } else {
            unsigned short pc, pr, n;
            roomrom_main_nt_cell_to_plane(0u, (unsigned char)row, &pc, &pr);
            n = (unsigned short)(64u - pc);
            if (n > 32u) n = 32u;
            render_vram_read_run((unsigned short)(0xC000u + pr * 128u + pc * 2u),
                                 s_words[row], n);
            if (n < 32u)
                render_vram_read_run((unsigned short)(0xC000u + pr * 128u),
                                     s_words[row] + n, (unsigned short)(32u - n));
        }
    }
    /* Incoming rows are written at least one tile before they become
     * visible. Only the existing screen needs installation now. */
    for (row = 0u; row < 30u; ++row) queue_row(row);
    s_active = 1u;
}

void ending_render_scroll(unsigned short pixels)
{
    if (!s_active) return;
    /* Called after queued row DMA, before mode logic: previous tick's
     * scroll is what the NES NMI presents. Both native planes alias C000. */
    render_set_window_on_top(0u);
    render_scene_scroll_set(0, (short)((pixels + 8u) & 511u));
}

void ending_render_write_row(unsigned short virtual_row, unsigned char nt_row)
{
    s_write_base = (unsigned short)(virtual_row - nt_row);
}

static unsigned short tile_word(unsigned char tile, unsigned char pal)
{
    const unsigned short slot = bg_sparse_tile_lut[tile][pal];
    unsigned char i;
    unsigned short source, word;
    if (slot != 0xFFFFu) return (unsigned short)(ROOMROM_BG_TILE_BASE + slot);
    for (i = 0u; i < s_glyph_count; ++i)
        if (s_glyph_tile[i] == tile && s_glyph_pal[i] == pal)
            return (unsigned short)(ROOMROM_BG_TILE_BASE + s_glyph_base + i);
    if (s_glyph_count >= ENDING_GLYPH_CACHE_COUNT ||
        (tile >= 0x70u && tile < 0xF2u) || g_ending_render_fault) {
        g_ending_render_fault |= 2u;
        return 0u;
    }
    i = s_glyph_count++;
    s_glyph_tile[i] = tile;
    s_glyph_pal[i] = pal;
    /* Existing extract_chr.py/common.c layout:112 SPR,112 BG,14 misc
     * tiles, all already packed4bpp. Bias nonzero nibbles into PAL0's
     * NES subpalette, exactly as the existing sparse generator does. */
    source = tile < 0x70u ? (unsigned short)(3584u + tile * 32u)
                         : (unsigned short)(7168u + (tile - 0xF2u) * 32u);
    for (word = 0u; word < 16u; ++word) {
        unsigned char a = common_chr[source + word * 2u];
        unsigned char b = common_chr[source + word * 2u + 1u];
        const unsigned char bias = (unsigned char)(pal << 2);
        a = (unsigned char)((a & 0xF0u ? (a & 0xF0u) + (bias << 4) : 0u) |
                            (a & 15u ? (a & 15u) + bias : 0u));
        b = (unsigned char)((b & 0xF0u ? (b & 0xF0u) + (bias << 4) : 0u) |
                            (b & 15u ? (b & 15u) + bias : 0u));
        s_glyph_words[i][word] = (unsigned short)(((unsigned short)a << 8) | b);
    }
    word = (unsigned short)(ROOMROM_BG_TILE_BASE + s_glyph_base + i);
    if (!render_vram_queue_words((unsigned short)(word * 32u), s_glyph_words[i], 16u)) {
        /* Existing synchronous ABI consumes this persistent RAM source. */
        render_chr_upload((unsigned short)(word * 32u),
                          (const unsigned char *)s_glyph_words[i], 32u);
    }
    return word;
}

static unsigned char palette_at(unsigned char nt, unsigned char row,
                                 unsigned char col)
{
    const unsigned char at = s_attrs[nt][((row >> 2) << 3) | (col >> 2)];
    const unsigned char shift = (unsigned char)(((row & 2u) << 1) | (col & 2u));
    return (unsigned char)((at >> shift) & 3u);
}

unsigned char ending_render_record(unsigned char hi, unsigned char lo,
    unsigned char ctrl, unsigned char count, const unsigned char *src,
    unsigned char src_off, unsigned char src_end)
{
    unsigned short off = (unsigned short)(((hi & 3u) << 8) | lo);
    const unsigned char nt = (unsigned char)((hi >> 3) & 1u);
    const unsigned char repeat = (unsigned char)(ctrl & 0x40u);
    unsigned char i;
    if (!s_active) return 0u;
    for (i = 0u; i < count; ++i) {
        unsigned short source = (unsigned short)(src_off + (repeat ? 0u : i));
        unsigned char raw;
        if (source >= src_end || off >= 1024u) break;
        raw = src[source];
        if (off >= 0x3C0u) {
            const unsigned char at = (unsigned char)(off - 0x3C0u);
            const unsigned char first_row = (unsigned char)((at >> 3) << 2);
            unsigned char dr, dc;
            s_attrs[nt][at] = raw;
            for (dr = 0u; dr < 4u && first_row + dr < 30u; ++dr) {
                const unsigned char nr = (unsigned char)(first_row + dr);
                const unsigned short pr = (unsigned short)((s_write_base + nr) & 63u);
                for (dc = 0u; dc < 4u; ++dc) {
                    const unsigned char col = (unsigned char)(((at & 7u) << 2) + dc);
                    s_words[pr][col] = tile_word(s_tiles[pr][col], palette_at(nt, nr, col));
                }
            }
        } else {
            const unsigned char nr = (unsigned char)(off >> 5);
            const unsigned char col = (unsigned char)(off & 31u);
            const unsigned short pr = (unsigned short)((s_write_base + nr) & 63u);
            s_tiles[pr][col] = raw;
            s_words[pr][col] = tile_word(raw, palette_at(nt, nr, col));
        }
        off = (unsigned short)(off + ((ctrl & 0x80u) ? 32u : 1u));
    }
    if ((((hi & 3u) << 8) | lo) >= 0x3C0u) {
        unsigned char row = (unsigned char)((((lo - 0xC0u) >> 3) << 2));
        /* DrawCredits emits exactly one eight-byte attribute block row. */
        unsigned char n;
        for (n = 0u; n < 4u && row + n < 30u; ++n)
            queue_row((unsigned short)((s_write_base + row + n) & 63u));
    } else {
        queue_row((unsigned short)((s_write_base + ((((hi & 3u) << 8) | lo) >> 5)) & 63u));
    }
    return 1u;
}
