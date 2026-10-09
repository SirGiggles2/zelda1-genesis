/* inventory_palette.h — L4 (Phase 7 v2) subscreen CRAM swap.
 *
 * Captured NES PALRAM during active inventory subscreen
 * (build/probes/nes_subscreen_capture.lua, $3F00..$3F1F).
 * On subscreen enter -> CRAM swap to these colors; on exit -> load_room
 * restores gameplay palette.
 */
#ifndef INVENTORY_PALETTE_H
#define INVENTORY_PALETTE_H

/* 32 NES PALRAM bytes: 16 BG ($3F00..$3F0F) + 16 SPR ($3F10..$3F1F).
 * Defined in inventory_palette.c. */
extern const unsigned char k_inventory_subscreen_palram[32];

extern const unsigned char k_inventory_uw_palram[32];
extern const unsigned char k_inventory_uw_palram_lv[10][32];

/* Load subscreen palette into Genesis CRAM PAL0-PAL3. uw!=0 selects the UW
 * (dungeon) palette for `level` (1..9, per-level dungeon tint); else OW. */
void inventory_palette_load_subscreen(unsigned char uw, unsigned char level,
                                      unsigned char quest);

#endif
