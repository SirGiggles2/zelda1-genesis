/* src/intro_title.c
 *
 * Title runtime: load assets, per-vblank step (glow only for now;
 * waterfall added in Task 12), fade-out driver.
 *
 * S1.F3: all local VDP helpers removed; calls go through render_abi.h.
 */
#include "intro_title.h"
#include "render_abi.h"
/* T-118: the $E000 fill below overwrites the gameplay HUD window. */
extern void roomrom_hud_invalidate(void);

/* S1.F4: display-enable register writes moved to render_display_enable(). */

extern const unsigned char  intro_title_bg_chr[];
extern const unsigned long  intro_title_bg_chr_size;
extern const unsigned char  intro_title_sprite_chr[];
extern const unsigned long  intro_title_sprite_chr_size;
extern const unsigned short intro_title_tilemap_rows;
extern const unsigned short intro_title_tilemap[];
extern const unsigned short intro_title_palette[64];
extern const unsigned short intro_title_fade_cycles[14][64];
extern const unsigned short intro_title_fade_delays[14];
extern const unsigned short intro_title_glow_colors[8];
extern const unsigned char  intro_title_glow_delays[8];

#define PLANE_A_BASE 0xC000u

static unsigned char s_glow_cycle = 0;
static unsigned char s_glow_timer = 0;
static unsigned char s_fade_cycle = 0;
static unsigned short s_fade_delay = 0;
static unsigned char s_waterfall_frame = 0;
static unsigned char s_waterfall_phase = 0;
/* Per-wave-row rolling Y counter (NES TitleWaveYs[0..2]). Each frame
 * Y += 2; wraps $E3 -> $B2. Initial values $B6/$C8/$D8 set in setup
 * (linker has no .data section so non-zero statics must init at runtime). */
static unsigned char s_wave_ys[3];

/* NES InitialTitleSprites table (Z_02.asm:342-356) -- 28 sprites, 4 bytes
 * each: [Y, tile, attr, X]. Last 6 entries (Y=$67, tile=$A0) are the
 * waterfall row; their tile field is rotated each 8 frames to animate
 * water using WaterfallCrestTiles. */
static const unsigned char nes_title_sprites[112] = {
    0x77, 0xCA, 0xC2, 0xD0,  0x77, 0xCC, 0xC2, 0xC8,
    0x77, 0xCA, 0x82, 0x28,  0x77, 0xCC, 0x82, 0x30,
    0x27, 0xCA, 0x42, 0xD0,  0x27, 0xCC, 0x42, 0xC8,
    0x27, 0xCA, 0x02, 0x28,  0x27, 0xCC, 0x02, 0x30,
    0x57, 0xCE, 0x02, 0x74,  0x57, 0xD0, 0x02, 0x7C,
    0x31, 0xD2, 0x02, 0x57,  0x4F, 0xD2, 0x02, 0xCC,
    0x67, 0xD2, 0x02, 0x7B,  0x83, 0xD2, 0x02, 0x50,
    0x31, 0xD4, 0x02, 0x5F,  0x3F, 0xD4, 0x02, 0x24,
    0x41, 0xD4, 0x02, 0x64,  0x7B, 0xD4, 0x02, 0x90,
    0x27, 0xD6, 0x02, 0x50,  0x2B, 0xD6, 0x02, 0xA0,
    0x4F, 0xD6, 0x02, 0x2C,  0x7B, 0xD6, 0x02, 0xBC,
    0x67, 0xA0, 0x03, 0x60,  0x67, 0xA0, 0x03, 0x68,
    0x67, 0xA0, 0x03, 0x70,  0x67, 0xA0, 0x03, 0x78,
    0x67, 0xA0, 0x03, 0x80,  0x67, 0xA0, 0x03, 0x88,
};

#define SPRITE_TABLE_VRAM 0xF800u
#define TITLE_SPRITE_COUNT  28u   /* sprites 0..27 from InitialTitleSprites */
#define WATERFALL_BASE      28u   /* waterfall sprites start here */
#define WATERFALL_ROWS      4u    /* 1 crest + 3 wave rows */
#define WATERFALL_COLS      4u    /* 4 sprites per row */
#define WATERFALL_COUNT     16u   /* 4 rows x 4 cols */
#define SPRITE_COUNT        44u   /* total title-phase sprites */

/* Waterfall layout per Z_02.asm:1005-1109 + WaterfallSpriteXs/Tiles tables.
 * Row 0 = crest (above the waves), rows 1-3 = waves (top to bottom).
 * NES X positions $50/$58/$60/$68 (4 sprites in a row, 8 px apart).
 * NES Y positions: crest above $B6, waves at $B6/$C8/$D8.
 * Tiles: WaterfallCrestTiles=$A2/$A4/$A6/$A8 (toggled with +8 per
 * FrameCounter bit 3); WaterfallWaveTiles=$B2/$B4/$B6/$B8 (same toggle). */
static const unsigned char waterfall_xs[4] = {0x50, 0x58, 0x60, 0x68};
static const unsigned char waterfall_ys[4] = {0xA8, 0xB6, 0xC8, 0xD8};
static const unsigned char waterfall_crest_tiles[4] = {0xA2, 0xA4, 0xA6, 0xA8};
static const unsigned char waterfall_wave_tiles[4]  = {0xB2, 0xB4, 0xB6, 0xB8};

/* Build one Genesis sprite-table entry from NES sprite [Y, tile, attr, X].
 * NES PPUCTRL bit 5 = 1 at title -> 8x16 sprites. Top tile = ntile (always
 * even for this scene); bottom tile = ntile + 1. Genesis encodes 8x16 as
 * Genesis SPRITE_SIZE(w,h) = ((w-1)<<2) | (h-1). For 8x16 (1 wide, 2 tall):
 * ((1-1)<<2) | (2-1) = 1.
 * Sprite CHR uploaded at VRAM $2000 -> Gen top tile = 256 + nes_tile.
 *
 * NES attr bits: 0-1 = palette (sprite pal 0-3), 5 = behind-BG priority,
 * 6 = HFlip, 7 = VFlip.
 * Genesis word 2 layout: pri(15) | pal(13-14) | vflip(12) | hflip(11) | tile(0-10).
 *
 * NES sprite pal X -> Gen pal X directly (CRAM combined layout puts
 * NES sprite pal P at slots 4-7 of Gen pal P, but the cell palette
 * field is the index of the Gen palette to use; sprite tiles are
 * encoded with color_shift=4 so pixel values 1-3 reference slots 5-7
 * of cell pal). Title sprite CHR was generated with color_shift=4.
 */
static void title_sprite_upload(void) {
    render_vram_open_write(SPRITE_TABLE_VRAM);

    /* First 28 sprites: InitialTitleSprites (logo decoration, sword pieces,
     * bird, V-chevron under ZELDA). */
    for (unsigned char i = 0; i < TITLE_SPRITE_COUNT; i++) {
        unsigned char ny    = nes_title_sprites[i*4 + 0];
        unsigned char ntile = nes_title_sprites[i*4 + 1];
        unsigned char nattr = nes_title_sprites[i*4 + 2];
        unsigned char nx    = nes_title_sprites[i*4 + 3];

        unsigned short y = (unsigned short)(128u + (unsigned short)(ny + 1u));
        unsigned short x = (unsigned short)(128u + (unsigned short)nx);
        unsigned short tile = (unsigned short)(256u + (unsigned short)ntile);
        unsigned short pal   = (unsigned short)(nattr & 3u);
        unsigned short prio  = (unsigned short)((nattr >> 5) & 1u);
        unsigned short hflip = (unsigned short)((nattr >> 6) & 1u);
        unsigned short vflip = (unsigned short)((nattr >> 7) & 1u);

        unsigned short link = (unsigned short)(i + 1u);
        unsigned short word1 = (unsigned short)((1u << 8) | link);  /* size=8x16 */
        unsigned short word2 = (unsigned short)((prio << 15) | (pal << 13)
                                              | (vflip << 12) | (hflip << 11)
                                              | (tile & 0x7FFu));

        render_vram_write_word(y);
        render_vram_write_word(word1);
        render_vram_write_word(word2);
        render_vram_write_word(x);
    }

    /* Waterfall sprites: 1 crest row + 3 wave rows, 4 cols each.
     * Tile field gets rewritten by title_waterfall_step() per frame.
     * Initial tile = base tile from waterfall_(crest|wave)_tiles[col]. */
    for (unsigned char r = 0; r < WATERFALL_ROWS; r++) {
        unsigned char ny = waterfall_ys[r];
        const unsigned char *tiles = (r == 0u) ? waterfall_crest_tiles
                                               : waterfall_wave_tiles;
        for (unsigned char c = 0; c < WATERFALL_COLS; c++) {
            unsigned char idx = (unsigned char)(WATERFALL_BASE + r*WATERFALL_COLS + c);
            unsigned short y = (unsigned short)(128u + (unsigned short)(ny + 1u));
            unsigned short x = (unsigned short)(128u + (unsigned short)waterfall_xs[c]);
            unsigned short tile = (unsigned short)(256u + (unsigned short)tiles[c]);
            /* NES sprite pal 0 (real-NES OAM dump shows attr=$00 for waterfall). */
            unsigned short pal = 0u;
            unsigned short link = (unsigned short)(idx + 1u);
            if (idx == SPRITE_COUNT - 1u) link = 0u;
            unsigned short word1 = (unsigned short)((1u << 8) | link);  /* size=8x16 */
            unsigned short word2 = (unsigned short)((pal << 13) | (tile & 0x7FFu));

            render_vram_write_word(y);
            render_vram_write_word(word1);
            render_vram_write_word(word2);
            render_vram_write_word(x);
        }
    }
}

/* Per Z_02.asm:1034 UpdateSpritesForWaterfallWave + :1086 Crest:
 *
 *   Wave row R (R=0..2):
 *     - TitleWaveYs[R] += 2 each frame; if >= $E3 wrap to $B2.
 *     - Tile offset depends on the new Y:
 *         Y < $B9  -> offset 0
 *         Y < $C2  -> offset 8
 *         Y >= $C2 -> offset $10
 *     - Each of 4 sprites: tile = WaterfallWaveTiles[col] + offset, Y = rolling.
 *
 *   Crest row (fixed):
 *     - Y = $A8.
 *     - Every 8 frames flip tile = WaterfallCrestTiles[col] + (0 or 8).
 *
 * Rewrites Y (word 0) and tile/attr (word 2) for each waterfall sprite. */
static unsigned char wave_tile_offset(unsigned char y) {
    if (y < 0xB9u) return 0u;
    if (y < 0xC2u) return 8u;
    return 0x10u;
}

static void title_waterfall_step(void) {
    s_waterfall_frame++;
    /* Crest tile flip every 8 frames. */
    if ((s_waterfall_frame & 0x07u) == 0u) {
        s_waterfall_phase ^= 1u;
    }
    unsigned char crest_offset = s_waterfall_phase ? 8u : 0u;

    /* 3 wave rows: Y += 2; wrap. Update sprites. */
    for (unsigned char r = 0; r < 3u; r++) {
        unsigned char y = (unsigned char)(s_wave_ys[r] + 2u);
        if (y >= 0xE3u) y = 0xB2u;
        s_wave_ys[r] = y;
        unsigned char wave_off = wave_tile_offset(y);
        for (unsigned char c = 0; c < WATERFALL_COLS; c++) {
            /* Wave rows occupy waterfall sprite indices BASE+4 .. BASE+15
             * (rows 1..3 of the 4-row block; row 0 is the crest). */
            unsigned char idx = (unsigned char)(WATERFALL_BASE + (r + 1u)*WATERFALL_COLS + c);
            unsigned short gen_y = (unsigned short)(128u + (unsigned short)(y + 1u));
            unsigned char nes_tile = (unsigned char)(waterfall_wave_tiles[c] + wave_off);
            unsigned short tile = (unsigned short)(256u + (unsigned short)nes_tile);
            unsigned short pal = 0u;
            unsigned short word2 = (unsigned short)((pal << 13) | (tile & 0x7FFu));
            unsigned short base_addr = (unsigned short)(SPRITE_TABLE_VRAM + idx*8u);
            /* Update Y (word 0). */
            render_vram_open_write(base_addr);
            render_vram_write_word(gen_y);
            /* Update tile/attr (word 2). */
            render_vram_open_write((unsigned short)(base_addr + 4u));
            render_vram_write_word(word2);
        }
    }

    /* Crest row: fixed Y=$A8, tile = WaterfallCrestTiles[col] + crest_offset. */
    for (unsigned char c = 0; c < WATERFALL_COLS; c++) {
        unsigned char idx = (unsigned char)(WATERFALL_BASE + 0u*WATERFALL_COLS + c);
        unsigned char nes_tile = (unsigned char)(waterfall_crest_tiles[c] + crest_offset);
        unsigned short tile = (unsigned short)(256u + (unsigned short)nes_tile);
        unsigned short pal = 0u;
        unsigned short word2 = (unsigned short)((pal << 13) | (tile & 0x7FFu));
        unsigned short addr = (unsigned short)(SPRITE_TABLE_VRAM + idx*8u + 4u);
        render_vram_open_write(addr);
        render_vram_write_word(word2);
    }
}

void intro_title_setup(void) {
    /* Display off during upload (display will be all-black on enable
     * because CRAM is rewritten before the display flips on). */
    render_display_enable(0);

    /* CHR uploads. */
    render_chr_upload(0x0000, intro_title_bg_chr,
                      (unsigned short)intro_title_bg_chr_size);
    render_chr_upload(0x2000, intro_title_sprite_chr,
                      (unsigned short)intro_title_sprite_chr_size);

    /* Plane A: title rows. Plane B blank. */
    render_plane_fill(PLANE_A_BASE, 0x0024u, 32u * 32u);
    render_plane_fill(0xE000u,      0x0024u, 32u * 32u);
    roomrom_hud_invalidate();
    for (unsigned short r = 0; r < intro_title_tilemap_rows; r++) {
        render_plane_a_write_row(r, &intro_title_tilemap[r * 32], 32u);
    }

    /* CRAM: title palette. */
    render_cram_upload(intro_title_palette, 64u);

    render_vsram_open_write(0u);
    render_vsram_write_word(0u);

    /* Reset title state. */
    s_glow_cycle = 0;
    s_glow_timer = intro_title_glow_delays[0];
    s_fade_cycle = 0;
    s_fade_delay = 0;
    s_waterfall_frame = 0;
    s_waterfall_phase = 0;
    s_wave_ys[0] = 0xB6u;
    s_wave_ys[1] = 0xC8u;
    s_wave_ys[2] = 0xD8u;

    /* Sprite list: 28 title sprites including 6 waterfall. */
    title_sprite_upload();

    /* Display on. */
    render_display_enable(1);
}

void intro_title_step(void) {
    /* Triforce glow: every 6 frames (16 at end), advance cycle and
     * patch Gen pal 1 slot 2 (= NES BG pal 1 color 2 = PALRAM $3F06). */
    if (s_glow_timer == 0) {
        s_glow_cycle++;
        if (s_glow_cycle >= 8u) s_glow_cycle = 0;
        s_glow_timer = intro_title_glow_delays[s_glow_cycle];
        render_cram_write_color((unsigned short)(1*16 + 2),
                                intro_title_glow_colors[s_glow_cycle]);
    } else {
        s_glow_timer--;
    }
    title_waterfall_step();
}

void intro_title_fade_apply(unsigned char idx) {
    if (idx >= 14u) return;
    render_cram_upload(intro_title_fade_cycles[idx], 64u);
    s_fade_delay = intro_title_fade_delays[idx];
    s_fade_cycle = idx;
}

void intro_title_fade_step(void) {
    if (s_fade_delay > 0) {
        s_fade_delay--;
        return;
    }
    if (s_fade_cycle + 1u >= 14u) {
        /* Last cycle's delay just expired -- fade is done. */
        s_fade_cycle = 14u;
        return;
    }
    intro_title_fade_apply((unsigned char)(s_fade_cycle + 1u));
}

unsigned char intro_title_fade_done(void) {
    return (s_fade_cycle >= 14u) ? 1u : 0u;
}

void intro_title_fade_reset(void) {
    s_fade_cycle = 0;
    s_fade_delay = 0;
}

void intro_title_blackout(void) {
    /* CRAM all black, holding the plane content but invisible.
     * render_cram_open_write opens the CRAM data port; subsequent
     * render_vram_write_word calls stream to that port (same VDP_DATA_WORD
     * address regardless of VRAM vs CRAM target). */
    render_cram_open_write(0u);
    for (unsigned short i = 0; i < 64u; i++) render_vram_write_word(0x0000u);
}
