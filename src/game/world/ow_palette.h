/* Promoted overworld palette data and per-room background palette loader. */
#ifndef ROOMROM_OW_PALETTE_H
#define ROOMROM_OW_PALETTE_H

/* g_roomrom_ow_palram[map][i] holds NES Overworld PALRAM bytes
 * (i in [0..31]: bytes 0..15 = BG sub-pals, 16..31 = SPR sub-pals).
 * map index 0 = original, 1 = redux. */
extern const unsigned char g_roomrom_ow_palram[2][32];

/* Per-room NES BG palram patch. Captured live across 128 OW rooms.
 * Call after roomrom_bg_palette_load_palram_full to override the
 * static OW BG defaults with NES truth. */
void roomrom_ow_palette_patch_bg_per_room(unsigned char room_id);

#endif
