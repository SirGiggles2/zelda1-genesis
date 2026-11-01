/* T-261 boss overlay drawing (Remix only).
 *
 * Every encounter: the name sweeps into the middle of the play area in big
 * letters, holds, then glides up into HUD row 6, where the name and one
 * giant health bar stay for the fight (user 2026-10-29: "That's how I want
 * every boss"; multi-part bosses share "one giant health bar"). The whole
 * game holds until the name reaches the HUD (user 2026-10-29: "no one
 * should be able to move until it gets into the hud").
 * Banner sprites are appended after the enemy chain, so game sprites keep
 * their slots; a frame without free slots shows the name in the HUD instead.
 *
 * Evidence (tools/audit/t261_vram_probe.py, 79 boss rooms x 4 samples):
 *   VRAM $F800-$FBFF: no SAT tile reference; only 4 static bytes (title SAT
 *     remnant). Pause uses the gameplay SAT at $F400.
 *   CRAM stable in every sample (word 0BBB0GGG0RRR): PAL0[1] 0EEE white
 *     (letters); PAL3[4] 0000 black, PAL3[10] 0006 dark red, PAL3[12] 068E red.
 *   HUD window row 6 blank in every column (screen y 48..55).
 * Letters: NES BG $0A..$23 = A..Z (CreditsTextLines.dat decode), atlas via
 * bg_sparse_tile_lut sub-palette 0 (all 26 present).
 *
 * VRAM tiles from BO_TILE_BASE:
 *   +0..+8   bar tiles, k = filled pixels of one 8-px cell
 *   +9..+48  name letters, 16x16 (pixels doubled both ways), 4 tiles each in
 *            name order, column-major so two letters make one 32x16 sprite.
 * $FC00-$FE1F (tiles 2016..2032): VDP tables end at the SAT ($F400, 64
 * entries in H32); t261_vram_probe region_hi checks it stays free. */
#include "boss_overlay.h"
#include "enemy_state.h"
#include "game_profile.h"
#include "render_abi.h"
#include "hud_runtime.h"
#include "../world/render/sprite_slots.h"
#include "../enemies/enemy_render.h"                /* g_enemy_render_last_sat_slot */
#include "vram_layout.h"                       /* ROOMROM_BG_TILE_BASE */

#define BO_TILE_BASE      1984u                     /* VRAM $F800 */
#define BO_BAR_TILES      9u
#define BO_LETTER_BASE    (BO_TILE_BASE + BO_BAR_TILES)
#define BO_MAX_LETTERS    10u
#define BO_LETTER_TILES   4u                        /* 16x16 letter */
#define BO_PAL0           0x0000u                   /* letters: PAL0 white */
#define BO_PAL3           0x6000u                   /* bar: PAL3 reds */
#define BO_BANNER_Y       124                       /* middle of 56..224, 16 tall */
#define BO_STRIP_COL      2u                        /* name, HUD row 6 */
#define BO_STRIP_BAR_COL  14u                       /* giant bar, cols 14..29 */
#define BO_STRIP_BAR_CELLS (BOSS_HUD_BAR_PX / 8u)
#define BO_HUD_NAME_X     (BO_STRIP_COL * 8)        /* banner landing spot */
#define BO_HUD_NAME_Y     44                        /* 16-tall letters centred on row 6 (y 48..55) */
#define BO_IN_FRAMES      16u
#define BO_UP_FRAMES      32u

extern const unsigned short bg_sparse_tile_lut[256][4];

static unsigned char s_loaded_name = 0xFFu;
static unsigned char s_name_len;                   /* of s_loaded_name */
unsigned char g_boss_strip_live;                  /* HUD strip on screen (guard for callers) */
static unsigned char s_strip_gen;                  /* g_boss_overlay_gen last drawn */
static unsigned char s_strip_clear;                /* HUD clear generation at our draw */
static unsigned char s_strip_name;                 /* name cells currently shown */
static unsigned char s_drawn_fill;                 /* hud_fill the bar cells show */

/* 68000 divu.w: avoids a __udivsi3 call (quotient <= BOSS_HUD_BAR_PX). */
static unsigned short divu16(unsigned long n, unsigned short d)
{
    __asm__("divu.w %1,%0" : "+d"(n) : "d"(d));
    return (unsigned short)n;
}

/* Bar tiles k = 0..8 filled pixels: rows 2/5 edge (PAL3[4] black), rows 3-4
 * fill PAL3[12] / empty PAL3[10], rest transparent. Precomputed so the
 * encounter start costs no CPU: DMA'd straight from ROM at VBlank
 * (t261_render_check compares the VRAM bytes with the same rule). */
static const unsigned short k_bar_tiles[16u * BO_BAR_TILES] = {
    0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x4444u, 0x4444u, 0xAAAAu, 0xAAAAu, 0xAAAAu, 0xAAAAu, 0x4444u, 0x4444u, 0x0000u, 0x0000u, 0x0000u, 0x0000u,  /* 0 px */
    0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x4444u, 0x4444u, 0xCAAAu, 0xAAAAu, 0xCAAAu, 0xAAAAu, 0x4444u, 0x4444u, 0x0000u, 0x0000u, 0x0000u, 0x0000u,  /* 1 px */
    0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x4444u, 0x4444u, 0xCCAAu, 0xAAAAu, 0xCCAAu, 0xAAAAu, 0x4444u, 0x4444u, 0x0000u, 0x0000u, 0x0000u, 0x0000u,  /* 2 px */
    0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x4444u, 0x4444u, 0xCCCAu, 0xAAAAu, 0xCCCAu, 0xAAAAu, 0x4444u, 0x4444u, 0x0000u, 0x0000u, 0x0000u, 0x0000u,  /* 3 px */
    0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x4444u, 0x4444u, 0xCCCCu, 0xAAAAu, 0xCCCCu, 0xAAAAu, 0x4444u, 0x4444u, 0x0000u, 0x0000u, 0x0000u, 0x0000u,  /* 4 px */
    0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x4444u, 0x4444u, 0xCCCCu, 0xCAAAu, 0xCCCCu, 0xCAAAu, 0x4444u, 0x4444u, 0x0000u, 0x0000u, 0x0000u, 0x0000u,  /* 5 px */
    0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x4444u, 0x4444u, 0xCCCCu, 0xCCAAu, 0xCCCCu, 0xCCAAu, 0x4444u, 0x4444u, 0x0000u, 0x0000u, 0x0000u, 0x0000u,  /* 6 px */
    0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x4444u, 0x4444u, 0xCCCCu, 0xCCCAu, 0xCCCCu, 0xCCCAu, 0x4444u, 0x4444u, 0x0000u, 0x0000u, 0x0000u, 0x0000u,  /* 7 px */
    0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x4444u, 0x4444u, 0xCCCCu, 0xCCCCu, 0xCCCCu, 0xCCCCu, 0x4444u, 0x4444u, 0x0000u, 0x0000u, 0x0000u, 0x0000u,  /* 8 px */
};

static unsigned char queue_bar_tiles(void)
{
    return render_vram_queue_words((unsigned short)(BO_TILE_BASE * 32u), k_bar_tiles,
                                   (unsigned short)(16u * BO_BAR_TILES));
}

static unsigned char name_len(const char *n)
{
    unsigned char len = 0u;
    while (n[len] && len < BO_MAX_LETTERS) ++len;
    return len;
}

static unsigned short letter_tile(char c)
{
    return (unsigned short)(ROOMROM_BG_TILE_BASE +
                            bg_sparse_tile_lut[(unsigned char)(0x0Au + (c - 'A'))][0]);
}

/* Letter i of the name at 16x16 (every pixel doubled both ways), from the
 * ROM atlas (the letter tiles are identical in all four bg_sparse_chr
 * variants; the render check compares them with the live VRAM atlas).
 * Tiles TL, BL, TR, BR: tile t is column t >> 1, half t & 1. A 4bpp byte
 * holds two pixels (high nibble left), so source byte hi:lo doubles to
 * hi:hi, lo:lo. */
extern const unsigned char bg_sparse_chr_orig_uw[];
static unsigned char queue_letter(const char *n, unsigned char i)
{
    const unsigned char *src = bg_sparse_chr_orig_uw +
        (unsigned long)bg_sparse_tile_lut[(unsigned char)(0x0Au + (n[i] - 'A'))][0] * 32u;
    unsigned char out[32u * BO_LETTER_TILES], t, r;
    for (t = 0u; t < BO_LETTER_TILES; ++t) {
        for (r = 0u; r < 8u; ++r) {
            const unsigned char *s = &src[((t & 1u) * 4u + (r >> 1)) * 4u + (t >> 1) * 2u];
            unsigned char *d = &out[t * 32u + r * 4u];
            d[0] = (unsigned char)((s[0] & 0xF0u) | (s[0] >> 4));
            d[1] = (unsigned char)((s[0] << 4) | (s[0] & 0x0Fu));
            d[2] = (unsigned char)((s[1] & 0xF0u) | (s[1] >> 4));
            d[3] = (unsigned char)((s[1] << 4) | (s[1] & 0x0Fu));
        }
    }
    return render_chr_queue_copy((unsigned short)((BO_LETTER_BASE + BO_LETTER_TILES * i) * 32u),
                                 out, (unsigned short)(32u * BO_LETTER_TILES));
}

/* Upload a few tiles per frame through the VBlank DMA queue: direct VRAM
 * access from the sweep waits in frame_pipeline_wait for the pending frame
 * (T-261 lag gate: one lost tick at every encounter start). Step 0 = bar
 * tiles, steps 1..len = letters; 0xFF = done. */
#define BO_LETTERS_PER_FRAME 2u
static unsigned char s_upload_step = 0xFFu;

static unsigned char upload_continue(void)
{
    const char *n = k_boss_names[g_boss_overlay.name_id];
    unsigned char k;
    if (s_upload_step == 0u) {
        if (!queue_bar_tiles()) return 0u;
        s_upload_step = 1u;
        return 0u;
    }
    for (k = 0u; k < BO_LETTERS_PER_FRAME && s_upload_step <= s_name_len; ++k) {
        if (!queue_letter(n, (unsigned char)(s_upload_step - 1u))) return 0u;
        ++s_upload_step;
    }
    if (s_upload_step <= s_name_len) return 0u;
    s_upload_step = 0xFFu;
    return 1u;
}

/* Banner position for this frame: in from the right, hold in the middle,
 * then glide to the HUD name spot. 16x16 multiplies (muls.w). */
static void banner_pos(unsigned char len, short *px, short *py)
{
    const short w = (short)(len << 4);
    const short home = (short)((256 - w) >> 1);
    const short t = (short)(BOSS_BANNER_FRAMES - g_boss_overlay.banner_timer);
    const short up0 = (short)(BOSS_BANNER_FRAMES - BO_UP_FRAMES);
    *px = home; *py = BO_BANNER_Y;
    if (t < (short)BO_IN_FRAMES) {
        *px = (short)(256 - (((short)(256 - home) * t) >> 4));
    } else if (t >= up0) {
        const short k = (short)(t - up0 + 1);           /* 1..BO_UP_FRAMES */
        *px = (short)(home + (((short)(BO_HUD_NAME_X - home) * k) >> 5));
        *py = (short)(BO_BANNER_Y + (((short)(BO_HUD_NAME_Y - BO_BANNER_Y) * k) >> 5));
    }
}

static void strip_clear(void)
{
    unsigned char c;
    render_plane_defer(1u);
    for (c = 0u; c < 32u; ++c) roomrom_hud_boss_strip_word(c, 0u);
    render_plane_defer(0u);
    g_boss_strip_live = 0u;
}

/* Giant bar: sum over live parts of fill * spawn HP (= current HP to 1/16)
 * over the spawn HP of every part seen; dead parts count as empty and the
 * bar never rises. No part seen yet (big Digdogger, invincible) = full.
 *
 * Cost bound (T-261 lag gate): at most BO_CELLS_PER_FRAME bar words per
 * frame. On encounter start (or after the HUD was wiped) the bar fills in
 * from the left; afterwards only the cells between the old and new fill are
 * written. The name (at most BO_MAX_LETTERS words) is written whole, so it
 * replaces the banner in one frame. */
#define BO_CELLS_PER_FRAME 2u
static unsigned char s_bar_ready;                  /* bar cells drawn since reset */
static unsigned char s_name_ready;                 /* name cells drawn since reset */
static unsigned char s_strip_done;                 /* every name and bar cell drawn */

static unsigned short bar_word(unsigned char i)
{
    const short f = (short)g_boss_overlay.hud_fill - (short)(8u * i);
    return (unsigned short)(0x8000u | BO_PAL3 | (BO_TILE_BASE +
                            (f <= 0 ? 0u : f >= 8 ? 8u : (unsigned short)f)));
}

static void strip_draw(unsigned char show_name)
{
    const char *n = k_boss_names[g_boss_overlay.name_id];
    const unsigned char bar = (unsigned char)(g_boss_overlay.name_id != BOSS_NAME_GANON);
    const unsigned char bar_cells = (unsigned char)(bar ? BO_STRIP_BAR_CELLS : 0u);
    unsigned char budget = BO_CELLS_PER_FRAME, i;
    if (g_boss_strip_live && s_strip_gen == g_boss_overlay_gen && s_strip_name == show_name &&
        s_strip_clear == g_hud_clear_gen && s_strip_done) return;
    s_strip_done = 0u;
    if (!g_boss_strip_live || s_strip_clear != g_hud_clear_gen) {
        s_bar_ready = 0u; s_name_ready = 0u;
        s_strip_clear = g_hud_clear_gen;
        g_boss_strip_live = 1u;
        s_drawn_fill = 0xFFu;
    }
    if (s_strip_gen != g_boss_overlay_gen || s_drawn_fill == 0xFFu) {
        unsigned short sum = 0u, den, px;
        s_strip_gen = g_boss_overlay_gen;
        for (i = 0u; i < g_boss_overlay.bar_count; ++i)
            sum = (unsigned short)(sum + (unsigned short)g_boss_overlay.fill_px[i] * g_boss_overlay.spawn_hp[i]);
        den = (unsigned short)(g_boss_overlay.hp_full_sum * BOSS_BAR_WIDTH);
        px = den ? divu16((unsigned long)sum * BOSS_HUD_BAR_PX + den - 1u, den) : BOSS_HUD_BAR_PX;
        if (s_drawn_fill == 0xFFu && g_boss_overlay.hud_fill == 0u) g_boss_overlay.hud_fill = (unsigned char)px;
        else if (px < g_boss_overlay.hud_fill) g_boss_overlay.hud_fill = (unsigned char)px;
    }
    render_plane_defer(1u);   /* written at the next VBlank, no pipeline wait */
    if (s_strip_name != show_name) {                     /* name hidden/shown: redo */
        s_name_ready = 0u;
        s_strip_name = show_name;
    }
    while (s_name_ready < s_name_len) {
        roomrom_hud_boss_strip_word((unsigned char)(BO_STRIP_COL + s_name_ready),
            show_name ? (unsigned short)(0x8000u | BO_PAL0 | letter_tile(n[s_name_ready])) : 0u);
        ++s_name_ready;
    }
    while (budget && s_bar_ready < bar_cells) {
        roomrom_hud_boss_strip_word((unsigned char)(BO_STRIP_BAR_COL + s_bar_ready), bar_word(s_bar_ready));
        ++s_bar_ready; --budget;
    }
    if (bar && s_bar_ready >= bar_cells && s_drawn_fill != 0xFFu &&
        s_drawn_fill != g_boss_overlay.hud_fill) {       /* cells between old and new fill */
        const unsigned char a = s_drawn_fill, b = g_boss_overlay.hud_fill;
        unsigned char lo = (unsigned char)((a < b ? a : b) >> 3);
        unsigned char hi = (unsigned char)(((a > b ? a : b) + 7u) >> 3);
        if (hi > BO_STRIP_BAR_CELLS) hi = BO_STRIP_BAR_CELLS;
        for (i = lo; i < hi; ++i)
            roomrom_hud_boss_strip_word((unsigned char)(BO_STRIP_BAR_COL + i), bar_word(i));
    }
    render_plane_defer(0u);
    if (s_bar_ready >= bar_cells) s_drawn_fill = g_boss_overlay.hud_fill;
    s_strip_done = (unsigned char)(s_bar_ready >= bar_cells);
}

static unsigned short s_emit_sat, s_emit_last;     /* where the last sweep appended */

__attribute__((noinline)) unsigned short boss_overlay_emit(unsigned short sat, unsigned short last)
{
    unsigned char banner = 0u;
    /* Steady fight: no banner, model and HUD unchanged and fully drawn.
     * (Without s_strip_done a damage-free frame stopped a half-drawn strip:
     * the name showed two more letters per hit.) */
    if (g_boss_overlay.active && !g_boss_overlay.banner_timer && g_boss_strip_live && s_strip_name &&
        s_strip_done && s_strip_gen == g_boss_overlay_gen && s_strip_clear == g_hud_clear_gen) return sat;
    if (!game_profile_is_remix()) return sat;
    s_emit_sat = sat;
    s_emit_last = last;
    if (!g_boss_overlay.active) {
        if (g_boss_strip_live) strip_clear();
        s_loaded_name = 0xFFu;
        s_upload_step = 0xFFu;
        return sat;
    }
    if (s_loaded_name != g_boss_overlay.name_id) {
        s_loaded_name = g_boss_overlay.name_id;
        s_name_len = name_len(k_boss_names[g_boss_overlay.name_id]);
        s_upload_step = 0u;
    }
    /* Tiles land at VBlank: draw from the frame after the last queue. */
    if (s_upload_step != 0xFFu) {
        (void)upload_continue();
        return sat;
    }
    if (g_boss_overlay.banner_timer) {
        /* Two letters (32x16) per sprite. Without free slots the banner is
         * skipped but still times out, so the hold always ends. */
        if ((unsigned short)(sat + (s_name_len + 1u) / 2u) <= (unsigned short)(last + 1u)) {
            short x0, y0;
            unsigned char i;
            banner_pos(s_name_len, &x0, &y0);
            for (i = 0u; i < s_name_len; i = (unsigned char)(i + 2u)) {
                const unsigned char w = (unsigned char)(s_name_len - i < 2u ? 2u : 4u);
                render_set_sprite_inline(sat, (signed short)(x0 + 16 * i), y0, RENDER_SPRITE_SIZE(w, 2),
                                         (unsigned short)(0x8000u | BO_PAL0 |
                                                          (BO_LETTER_BASE + BO_LETTER_TILES * i)),
                                         (unsigned char)(sat < last ? sat + 1u : 0u));
                ++sat;
            }
        }
        banner = 1u;
        --g_boss_overlay.banner_timer;
    }
    strip_draw((unsigned char)!banner);
    return sat;
}

/* One held frame (boss_overlay_intro_hold): redraw the banner where the
 * last sweep appended it, after the unchanged game sprites, and end the
 * sprite chain behind it. Kept out of line: t261_render_check breaks
 * on it to count held frames (once per held frame, no hot path). */
__attribute__((noinline, used)) void boss_overlay_intro_frame(void)
{
    unsigned short sat = boss_overlay_emit(s_emit_sat, s_emit_last);
    if (sat <= s_emit_last) {
        render_set_sprite_inline(sat, (signed short)-32, (signed short)-32,
                                 RENDER_SPRITE_SIZE(1, 1), 0u, 0u);
        ++sat;
    }
    g_enemy_render_last_sat_slot = (unsigned char)sat;
}
