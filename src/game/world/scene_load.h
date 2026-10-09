/* Coordinator that loads a scene's CHR / palette / sprite-atlas state.
 *
 * Per atlas spec section 7: each scene type has a list of categories
 * that should be resident in VRAM during that scene. This function
 * walks that list and calls the existing per-category upload helpers
 * (roomrom_sprites_upload_chr, roomrom_bg_palette_load_palram_full,
 * etc) in the correct order.
 *
 * Wraps the existing init sequence so future scene transitions
 * (title -> game, OW -> dungeon entry) have a single ABI to call.
 *
 * Variant: 0 = orig, 1 = redux. Forwards to all per-category
 * variant-aware loaders.
 */
#ifndef ROOMROM_SCENE_LOAD_H
#define ROOMROM_SCENE_LOAD_H

#include "atlas/roomrom_scene_vram_contracts.h"

void roomrom_scene_load(roomrom_scene_id_t scene_id, unsigned char variant);
/* T-129: after a dungeon enemy-bank swap, load PatternBlockUWSP. */
void roomrom_scene_uw_sprite_base_tick(void);

#endif /* ROOMROM_SCENE_LOAD_H */
