#ifndef ROOMROM_BOMB_H
#define ROOMROM_BOMB_H

/* NES bomb / fire object slots $10 and $11 (T-110). See bomb.c.
 *
 * NES Z1 reference: Z_01.asm WieldBomb, Z_07.asm UpdateBombOrFire /
 * UpdateBomb. A bomb starts in state $11; its minor state steps through
 * $12 (fuse, $30 frames), $13 (detonating, $18), $14 ($0C, UW wall
 * check), $15 ($06) and then clears. Tile $34 (item slot 1 frame 0),
 * clouds $70/$72 (frames 1-2), palette row 1.
 */

#include "../../src/game/world/render/sprite_render.h"

void roomrom_bomb_init(void);
void roomrom_bomb_place(link_face_t face, short link_x, short link_y);
/* UpdateBombOrFire for slot $10 then $11 (bombs and fires). */
void roomrom_bomb_update(void);
unsigned char roomrom_bomb_active(void);
/* T-260: a bomb/fire slot is empty (state 0), so a combo blast can use it. */
unsigned char roomrom_bomb_slot_free(void);
unsigned char roomrom_bomb_slot_reserved(unsigned char slot);
unsigned char roomrom_bomb_reserve(unsigned char player);
/* T-260: detonate at (x, y) on the next update through the normal bomb
 * states ($12 timer 0 -> $13 blast -> $14 wall check). 0 = no free slot. */
unsigned char roomrom_bomb_blast_at(unsigned char player, unsigned char x, unsigned char y);
/* T-260 arrow-burst bomb: placed like WieldBomb from the loaded player
 * context (recipe already charged); fires 8 arrows when it detonates. */
void roomrom_bomb_place_emitting(void);
unsigned char roomrom_bomb_emitting(void);

/* Shared with candle_fire.c. */
unsigned char bomb_fire_nes_dir_for_face(link_face_t face);
void bomb_fire_place_weapon(unsigned char slot, unsigned char link_dir,
                            unsigned char link_x, unsigned char link_y);
unsigned char bomb_fire_flash_attrs(unsigned char slot, unsigned char attrs);

#endif
