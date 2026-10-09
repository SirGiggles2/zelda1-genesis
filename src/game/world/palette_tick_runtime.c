#include "palette_tick_runtime.h"
#include "bg_palette.h"  /* Phase 12.2 promoted */
#include "../../state/palette_state.h"

static palette_tick_state_t s_tick;
static PaletteState         s_pal;

void roomrom_palette_tick_init(const unsigned char *palram32)
{
    unsigned char i;
    palette_tick_init(&s_tick);
    for (i = 0u; i < PALETTE_PALRAM_BYTES; i++)
        s_pal.nes_palram[i] = palram32 ? palram32[i] : 0u;
    s_pal.generation        = 0u;
    s_pal.cram_cache_valid  = 0u;
}

void roomrom_palette_tick_frame(unsigned short frame_counter)
{
    uint16_t gen_before;
    if (s_tick.toggle_count == 0u) return;
    gen_before = s_pal.generation;
    palette_tick(&s_tick, &s_pal, frame_counter);
    if (s_pal.generation != gen_before) {
        /* PALRAM changed — re-upload BG+SPR palette. */
        roomrom_bg_palette_load_palram_full(s_pal.nes_palram);
    }
}
