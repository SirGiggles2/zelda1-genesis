#include "bg_palette.h"
#include "render_abi.h"
#include "platform_abi.h"

/* Reuse the existing misc_palettes NES-color-index -> Gen-CRAM-word LUT.
 * Same table consumed by ow_room_render_roomrom.c, uw_room_render_roomrom.c,
 * and roomrom_sprites.c -- single source of truth for color conversion.
 */
extern const unsigned char misc_palettes[1208];

unsigned short roomrom_bg_palette_nes_to_cram(unsigned char nes_color)
{
    unsigned short off = (unsigned short)(nes_color & 0x3Fu) * 2u;
    return (unsigned short)misc_palettes[off]
         | ((unsigned short)misc_palettes[off + 1] << 8);
}

/* Cache of converted NES sprite palram in CRAM-word form, populated by
 * load_palram_full. Indexed [subpal_idx*4 + color_idx], 0 <= subpal_idx
 * <= 3, 0 <= color_idx <= 3. Used for sword-beam color flash. */
static unsigned short s_sprite_palram_cram[16];
static unsigned char  s_sprite_palram_loaded = 0u;

/* LevelInfo palettes ($6B7E transfer record, $6B81 = $3F00): $3F11. */
#define LEVEL_PALETTE_LINK_COLOR (0x6000u + 0x0B92u)

static void load_slot16(unsigned char gen_slot, const unsigned char *nes16)
{
    unsigned short pal16[16];
    unsigned char i;
    for (i = 0; i < 16; i++) {
        pal16[i] = roomrom_bg_palette_nes_to_cram(nes16[i]);
    }
    render_load_palette((unsigned short)gen_slot, pal16);
}

void roomrom_bg_palette_load_palram_full(const unsigned char *palram32)
{
    /* CRAM load — Phase B target layout (see src/game/world/bg_palette.h
     * for the full architectural rationale + sword-beam migration).
     *
     * PAL0[0..15] <- NES BG palram ($3F00..$3F0F), sub-pals 0..3 packed
     *                via pixel-bias rule (BG_4x stays — Phase C deferred).
     * PAL1[0..15] <- NES SPR palram ($3F10..$3F1F). [0..3] = sub-pal 0
     *                (Link, sword, common); [4..15] retained for any
     *                code still doing pixel-bias.
     * PAL2[0..3]  <- NES SPR sub-pal 1 colors ($3F14..$3F17). Bomb,
     *                explosion, FX sprites route here via OAM pal=PAL2.
     *                Color 0 = universal transparent.
     * PAL3[0..3]  <- NES SPR sub-pal 2 colors ($3F18..$3F1B). Candle,
     *                magic shot, OWSP red enemies route here via
     *                OAM pal=PAL3. Color 0 = universal transparent.
     *
     * Sword-beam color flash (Z_07.asm:3459 ATTR = base | (FrameCounter
     * & 3)) is reproduced by cycling the beam sprite's OAM pal field
     * across PAL1/PAL2/PAL3 each frame in combat_runtime update_beam.
     * NO ephemeral CRAM rewrite happens here; PAL2/PAL3 stay valid for
     * concurrent bomb / candle rendering. */
    unsigned char i;
    unsigned char spr[16];
    /* T-171: NES $3F11 (Link's tunic) is the level palette byte $6B92,
     * which PatchAndCueLevelPalettesTransfer sets to LinkColors[InvRing]
     * on every level load and ring pickup. The captured palettes passed
     * here all hold the green $29, so a ring left Link green (t121_ring1:
     * NES $32, Genesis $29). The level palette byte is the source. */
    for (i = 0; i < 16; i++) spr[i] = palram32[16 + i];
    if (nes_ram[LEVEL_PALETTE_LINK_COLOR] != 0u)
        spr[1] = nes_ram[LEVEL_PALETTE_LINK_COLOR];
    load_slot16(0, palram32 + 0);
    load_slot16(1, spr);
    /* Cache the 16 sprite-palram CRAM words. Retained for the beam
     * pal-cycle path (which reads sub-pal CRAM words to validate PAL2/3
     * are populated) and for any future per-sub-pal load. */
    for (i = 0; i < 16; i++) {
        s_sprite_palram_cram[i] = roomrom_bg_palette_nes_to_cram(spr[i]);
    }
    s_sprite_palram_loaded = 1u;

    /* PAL2[0..3] = NES SPR sub-pal 1 ($3F15..$3F17) — BLUE RAMP slot.
     * Per Z_04.asm anim_attr=$01 family (Blue Lynel, Blue Octorok,
     * Blue Tektite, Blue Zora, Blue Leever, Blue Darknut) renders
     * sprites with sub-pal 1 = $0F/$02/$22/$30 (black/blue/blue-lt/white).
     *
     * Sub-pal 3 (anim_attr=$03 — Blue Moblin, Blue Goriya, Wizzrobe)
     * also routes here via subpal_routing clamp; gets blue ramp instead
     * of NES Lost Hills cyan — acceptable visual match since target
     * intent IS blue. Census shows sub-pal 1 enemies vastly outnumber
     * sub-pal 3 (which only exists in select UW/OW Moblin rooms).
     *
     * Probe-confirmed bug 2026-05-23: prior layout had PAL2 holding
     * sub-pal 3 (Lost Hills $0F/$0F/$1C/$16 brown/red) causing all
     * blue enemies to render with dark Lost Hills colors. User report:
     * "Blue lynel and blue octorck have the wrong colors". */
    {
        unsigned short pal2[16] = {0};
        pal2[1] = roomrom_bg_palette_nes_to_cram(palram32[16 + 5]);   /* $3F15 */
        pal2[2] = roomrom_bg_palette_nes_to_cram(palram32[16 + 6]);   /* $3F16 */
        pal2[3] = roomrom_bg_palette_nes_to_cram(palram32[16 + 7]);   /* $3F17 */
        render_load_palette(2u, pal2);
    }

    /* PAL3[0..3] = NES SPR sub-pal 2 ($3F18..$3F1B = red ramp).
     * Red enemies (Octorok, Tektite, anim_attr=$02 family) render here. */
    {
        unsigned short pal3[16] = {0};
        pal3[1] = roomrom_bg_palette_nes_to_cram(palram32[16 + 9]);   /* $3F19 */
        pal3[2] = roomrom_bg_palette_nes_to_cram(palram32[16 + 10]);  /* $3F1A */
        pal3[3] = roomrom_bg_palette_nes_to_cram(palram32[16 + 11]);  /* $3F1B */
        render_load_palette(3u, pal3);
    }
}

void roomrom_bg_palette_refresh_link_color(void)
{
    const unsigned char c = nes_ram[LEVEL_PALETTE_LINK_COLOR];
    if (c == 0u) return;
    s_sprite_palram_cram[1] = roomrom_bg_palette_nes_to_cram(c);
    render_cram_write_color(17u, s_sprite_palram_cram[1]);   /* $3F11 */
}

const unsigned short *roomrom_bg_palette_get_sprite_subpal_cram(
    unsigned char subpal_idx)
{
    if (!s_sprite_palram_loaded) return (const unsigned short *)0;
    return &s_sprite_palram_cram[(unsigned short)(subpal_idx & 0x3u) * 4u];
}

void roomrom_bg_palette_load_bg_only(const unsigned char *palram16)
{
    load_slot16(0, palram16);
}

/* PPUMASK grayscale consumer (T-110).
 *
 * NES source: Z_01.asm UpdateBombFlashEffect sets/clears CurPpuMask_2001
 * ($FE) bit 0; the PPU then outputs every palette entry as (index & $30).
 * Genesis has no grayscale bit: the render adapter keeps a CRAM shadow and,
 * while a gray table is set, writes every color through it (a display
 * post-process, as on the NES).
 *
 * CRAM holds converted words, not NES indices, so the index is recovered
 * by a reverse lookup of the NES->CRAM table. Two words are ambiguous in
 * that table: $0000 (NES $0D-$0F,$1D-$1F,$2E,$2F,$3E,$3F) resolves to
 * $0F, the only black Zelda's palettes use; $0AAA ($10 and $3D) resolves
 * to $10. Every other collision group shares one gray (& $30), so the
 * lookup is exact for them. */
#define NES_CUR_PPU_MASK 0x00FEu
static unsigned char  s_gray_active = 0u;

/* Genesis color word (9 significant bits: BBB0GGG0RRR0 >> 1) -> gray.
 * Built once; first NES index wins, which resolves $0000 to $0D (same
 * gray as $0F) and $0AAA to $10. Words not produced by the NES table
 * (none in Zelda's palettes) fall back to the $0F gray. */
static unsigned short s_gray_by_word[512];
static unsigned char  s_gray_table_ready = 0u;

static unsigned short word_key(unsigned short w)
{
    return (unsigned short)(((w >> 1) & 0x7u) | ((w >> 2) & 0x38u) |
                            ((w >> 3) & 0x1C0u));
}

static void build_gray_table(void)
{
    unsigned short k;
    signed char i;
    for (k = 0u; k < 512u; ++k)
        s_gray_by_word[k] = roomrom_bg_palette_nes_to_cram(0x0Fu & 0x30u);
    /* Descending, so the lowest NES index sharing a word is written last. */
    for (i = 63; i >= 0; --i) {
        s_gray_by_word[word_key(roomrom_bg_palette_nes_to_cram((unsigned char)i))] =
            roomrom_bg_palette_nes_to_cram((unsigned char)((unsigned char)i & 0x30u));
    }
    s_gray_table_ready = 1u;
}

void roomrom_ppu_mask_grayscale_init(void)
{
    /* Built outside gameplay frames: the first flash must not pay for it. */
    if (!s_gray_table_ready) build_gray_table();
}

void roomrom_ppu_mask_grayscale_sync(void)
{
    unsigned char want = (unsigned char)(nes_ram[NES_CUR_PPU_MASK] & 0x01u);
    if (want == s_gray_active) return;
    if (!s_gray_table_ready) build_gray_table();
    render_cram_set_grayscale(want ? s_gray_by_word : (const unsigned short *)0);
    s_gray_active = want;
}
