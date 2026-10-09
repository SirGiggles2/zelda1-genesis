#include "uw_dark.h"
#include "platform_abi.h"
#include "render_abi.h"                   /* render_cram_subrange_upload */
#include "../world/bg_palette.h"          /* roomrom_bg_palette_nes_to_cram */
#include "../world/world_dispatch.h"      /* world_animate_world_fading */

/* T-111: UW dark rooms, NES palette model.
 *
 * NES source: Z_05.asm IsDarkRoom_Bank5, InitMode7_Sub5/Sub6 (fade to
 * black before scrolling into a dark room), InitMode4 Sub2/Sub3 (fade to
 * light entering a light room from an unlit dark room; CandleState reset
 * on every entry); Z_04.asm UpdateCandle; Z_07.asm UpdateFire (a standing
 * fire in the UW calls UpdateCandle), UpdateMode5Play (BrighteningRoom
 * replaces the play update); Z_01.asm AnimateWorldFading.
 * Drained C: world_animate_world_fading (world_dispatch.c).
 * Stance: REPLACE the Genesis plane blanking (fill_plane_a_dark) and its
 * persistent per-room lit table: NES darkens BG palette rows 2-3 only,
 * and a candle-lit room goes dark again on the next entry. */
#define NES_CUR_LEVEL        0x0010u
#define NES_LBA_E            0x6A7Eu   /* LevelBlockAttrsE */
#define NES_PALETTE_CYCLES   0x6BFAu   /* LevelInfo_PaletteCycles */
#define NES_FADE_CYCLE       0x051Cu
#define NES_BRIGHTENING      0x051Eu
#define NES_CANDLE_STATE     0x051Fu

unsigned char uw_dark_is_dark_room(unsigned char room_id)
{
    if (nes_ram[NES_CUR_LEVEL] == 0u) return 0u;
    return (nes_ram[NES_LBA_E + room_id] & 0x80u) ? 1u : 0u;
}

void uw_dark_apply_cycle_row(unsigned char cycle)
{
    unsigned short cram[8];
    unsigned char val = cycle, idx, i;
    if (val & 0x80u) val = (unsigned char)(val ^ 0x83u);
    idx = (unsigned char)(((unsigned char)(val << 3) + val) & 0xFCu);
    for (i = 0u; i < 8u; ++i)
        cram[i] = roomrom_bg_palette_nes_to_cram(
            nes_ram[NES_PALETTE_CYCLES + (unsigned char)(idx + i)]);
    render_cram_subrange_upload(8u, cram, 8u);
}

void uw_dark_update_candle(void)
{
    switch (nes_ram[NES_CANDLE_STATE]) {
    case 0u:                                  /* UpdateCandle_Begin */
        if (!uw_dark_is_dark_room(nes_ram[0x00EBu])) return;
        nes_ram[NES_FADE_CYCLE] = 0xC0u;      /* fade to light */
        nes_ram[NES_BRIGHTENING] = (unsigned char)(nes_ram[NES_BRIGHTENING] + 1u);
        nes_ram[NES_CANDLE_STATE] = 1u;
        return;
    case 1u:                                  /* UpdateCandle_Brightening */
        if (world_animate_world_fading() == 0u) {
            nes_ram[NES_BRIGHTENING] = 0u;
            nes_ram[NES_CANDLE_STATE] = 2u;
        }
        return;
    default:                                  /* UpdateCandle_Done */
        return;
    }
}

unsigned char uw_dark_brightening(void)
{
    return nes_ram[NES_BRIGHTENING] != 0u ? 1u : 0u;
}
