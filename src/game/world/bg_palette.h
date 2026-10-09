#ifndef ROOMROM_BG_PALETTE_H
#define ROOMROM_BG_PALETTE_H

/* Centralized NES->Genesis palette conversion + bulk loaders.
 *
 * load_palram_full writes Gen PAL0 (16 colors from palram32[0..15]) and
 * Gen PAL1 (16 colors from palram32[16..31]). Today also primes PAL3[1..3]
 * with NES SPR sub-pal 2 colors so OWSP red enemies can route via PAL3
 * instead of pixel-bias hacks. Does NOT touch PAL2 (caller-managed).
 *
 * load_bg_only writes Gen PAL0 only. Use when caller has BG-half bytes.
 *
 * CRAM LAYOUT — current (pre VRAM cleanup pass)
 * ---------------------------------------------
 * PAL0[0..15] = NES BG palram, sub-pals 0..3 packed (pixel-bias encoded).
 *               BG/HUD tiles bias pixel value by (sub_pal*4 + in) so a
 *               single PAL holds all 4 NES BG sub-palettes.
 * PAL1[0..15] = NES SPR palram, sub-pals 0..3 packed (same pixel-bias rule).
 *               Sprites (Link, common, items) reference PAL1 always with
 *               tile data biased per sub-pal -- this is the 3x ITEM bank
 *               replication (items_chr_x4).
 * PAL2[0..3]  = ephemeral sword-beam flash. roomrom_combat update_beam
 *               rewrites these 4 colors per frame via
 *               render_cram_subrange_upload.
 * PAL3[0]     = transparent.
 * PAL3[1..3]  = NES SPR sub-pal 2 colors (OWSP red enemy routing per
 *               2026-05-15 enemy visibility fix).
 * PAL3[4..15] = unused.
 *
 * CRAM LAYOUT — target (post Phase B, ITEM bank 3x -> 1x)
 * --------------------------------------------------------
 * PAL0[0..15] = unchanged (BG_4x replication stays -- see ARCH NOTE below).
 * PAL1[0..3]  = NES SPR sub-pal 0 colors. Sprites with sub_pal==0 use
 *               pal=PAL1 in OAM attr. (PAL1[4..15] = NES SPR sub-pals 1..3
 *               packed, retained for any code still doing pixel-bias.)
 * PAL2[0..3]  = NES SPR sub-pal 1 colors. Sprites with sub_pal==1 use
 *               pal=PAL2 in OAM attr (bomb, explosion, fire FX).
 *               PAL2[12..15] = sword-beam flash (relocated from [0..3]).
 *               Beam tile re-encoded with pixel values 12..15.
 * PAL3[0..3]  = NES SPR sub-pal 2 colors (promoted from [1..3] -- color 0
 *               = transparent). Sprites with sub_pal==2 use pal=PAL3
 *               (candle, magic shot, OWSP red enemies).
 * PAL3[4..15] = unused / future expansion.
 *
 * HARD RULE: color 0 of every PAL = transparent. Sprite sub-pal 3 (rare in
 * Z1; census shows static-only refs) is unsupported in the new layout --
 * sub-pal 3 sprites clamp to sub-pal 2 (same as today, where the 3x ITEM
 * atlas already dropped sub-pal 3 per 2026-05-08 fix).
 *
 * ARCH NOTE -- why BG stays 4x (Phase C deferred)
 * -----------------------------------------------
 * Naive plan was "give PAL0..PAL3 to the 4 NES BG sub-pals; nametable cell
 * pal-bits 13:14 select sub-pal natively, drop the 4x CHR replication."
 * Adversarial review caught the flaw: doing that leaves zero CRAM slots
 * for sprite sub-pals 0..2 (Link, items, FX), which would force sprites to
 * share BG palettes -- visually catastrophic (Link in BG-color set, bomb
 * in tree-green, etc). Genesis only has 4 PALs total; we can't dedicate
 * all 4 to BG and still preserve sprite color fidelity.
 *
 * Options surveyed:
 *   (a) BG=4 PALs, sprites share BG colors      -> color-wrong sprites, no
 *   (b) BG=1 PAL pixel-biased, SPR gets 3 PALs  -> current scheme; Phase B
 *   (c) HBlank mid-frame CRAM swap              -> complex, latency hazard
 *   (d) Per-scene palette context (cave vs OW)  -> deferred follow-up
 *
 * Phase B picks (b). BG bank stays 1024 tiles (4x). The 24 KB recovery
 * the original plan promised is not architecturally feasible without (c)
 * or (d), both out of scope for this pass.
 */

unsigned short roomrom_bg_palette_nes_to_cram(unsigned char nes_color);
void roomrom_bg_palette_load_palram_full(const unsigned char *palram32);
void roomrom_bg_palette_load_bg_only(const unsigned char *palram16);
/* Rewrite PAL1 color 1 (NES $3F11, Link's tunic) from the level palette
 * byte $6B92. For load paths that set the save's ring after the room
 * palette went up (File Select handoff, debug item unlock). */
void roomrom_bg_palette_refresh_link_color(void);

/* Returns pointer to 4 cached CRAM words for NES sprite sub-palette
 * `subpal_idx` (0..3) from the most recent load_palram_full. Layout
 * mirrors NES palram $3F10+$04*subpal: word 0 = universal-bg mirror
 * (transparent), words 1-3 = visible NES sub-palette colors.
 *
 * Used by the sword-beam color flash (Z_07.asm:3459 ATTR = base |
 * (FrameCounter & 3) cycles palette index per frame). Returns NULL
 * if no palram has been loaded yet. */
const unsigned short *roomrom_bg_palette_get_sprite_subpal_cram(
    unsigned char subpal_idx);

/* Apply NES PPUMASK grayscale (CurPpuMask $FE bit 0) to CRAM on each
 * edge. Call once per frame after anything that writes $FE. */
void roomrom_ppu_mask_grayscale_sync(void);
/* Build the word->gray table (call once outside gameplay frames). */
void roomrom_ppu_mask_grayscale_init(void);

#endif
