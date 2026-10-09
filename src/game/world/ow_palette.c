/* Promoted overworld palette data and per-room background palette loader. */
#include "ow_palette.h"

#include "render_abi.h"    /* render_load_palette */
#include "bg_palette.h"    /* nes_to_cram */
#include "platform_abi.h"  /* nes_ram */
#include "ow_bg_palram_table.h"   /* k_ow_bg_palram_per_room — NES truth */

/* NES OW PALRAM runtime-patched values. Captured live from NES at
 * frame 240 post-warp via probe_nes_full_room_dump.lua across all
 * 128 OW rooms. All rooms share same BG sub-pal 2/3 = $17 $16 $26
 * (Lost Hills brown/red/pink) — overrides LevelInfoOW.dat defaults
 * which had $1A $37 $12 (green) and $17 $37 $12 (brown-green).
 * Original sprite sub-pal 3 is selected at room load by ow_render.c
 * from the installed room tile object and block attributes. */
const unsigned char g_roomrom_ow_palram[2][32] = {
    /* orig */ { 0x0F, 0x30, 0x00, 0x12, 0x0F, 0x16, 0x27, 0x36, 0x0F, 0x17, 0x16, 0x26, 0x0F, 0x17, 0x16, 0x26, 0x0F, 0x29, 0x27, 0x17, 0x0F, 0x02, 0x22, 0x30, 0x0F, 0x16, 0x27, 0x30, 0x0F, 0x0C, 0x1C, 0x2C },
    /* redux */ { 0x0F, 0x30, 0x00, 0x12, 0x0F, 0x16, 0x27, 0x30, 0x0F, 0x17, 0x16, 0x26, 0x0F, 0x17, 0x16, 0x26, 0x0F, 0x29, 0x27, 0x17, 0x0F, 0x02, 0x22, 0x30, 0x0F, 0x16, 0x27, 0x30, 0x0F, 0x0C, 0x1C, 0x2C },
};

/* Patch CRAM PAL0[0..15] with per-room NES BG palram bytes.
 * Captured live from NES across 128 OW rooms — values vary per-room
 * (Lost Hills brown for some, default green-OW for others).
 * Called by ow_render after roomrom_bg_palette_load_palram_full to
 * override the static defaults. */
void roomrom_ow_palette_patch_bg_per_room(unsigned char room_id)
{
    const unsigned char *bg = k_ow_bg_palram_per_room[room_id & 0x7Fu];
    unsigned short pal0[16] = {0};
    unsigned char i;
    for (i = 0u; i < 16u; ++i) {
        pal0[i] = roomrom_bg_palette_nes_to_cram(bg[i]);
    }
    render_load_palette(0u, pal0);
}
