/* Candle fire in NES object slots $10/$11 (T-110). See candle_fire.c.
 *
 * NES authority: Z_01.asm:WieldCandle (3948), Z_07.asm:UpdateFire (4622).
 * Q-speed $20 (0.5 px/frame) for distance $10, then stand $3F frames;
 * h-flip toggles every 4 frames. Fires share slots with bombs; the update
 * runs from roomrom_bomb_update (UpdateBombOrFire).
 */

#ifndef ROOMROM_CANDLE_FIRE_H
#define ROOMROM_CANDLE_FIRE_H

#include "../../src/game/world/render/sprite_render.h"

void roomrom_candle_fire_init(void);
void roomrom_candle_fire_spawn(link_face_t face, short link_x, short link_y);
void roomrom_candle_fire_update(void);
unsigned char roomrom_candle_fire_active(void);

/* NES UsedCandle ($0513): blocks a second blue-candle fire. Cleared on
 * room entry (InitMode_EnterRoom clears $0300-$051F). */
unsigned char roomrom_candle_fire_used_this_room(void);
void          roomrom_candle_fire_mark_used(void);
void          roomrom_candle_fire_room_reset(void);

/* HandleShotBlocked book fire: WieldCandle's slot search + fire setup
 * without Link's state change; returns the slot left in X. */
unsigned char candle_fire_wield_from_shot(void);

/* UpdateFire for slot x (state $21 moving / $22 standing). */
void bomb_fire_update_fire(unsigned char x);

#endif
