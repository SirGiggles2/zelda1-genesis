#ifndef ROOMROM_ARROW_H
#define ROOMROM_ARROW_H

/* RoomRom S7 v7 arrow.
 *
 * NES Z1 reference: Z_05.asm WieldArrow + Z_07.asm OffsetAndDrawArrow
 * (item slot Y=2). Anim_ItemFrameOffsets[2] = $07. Tiles:
 *   frame 0 (vertical):   $28 + $29
 *   frame 1 (horizontal): $86, $87, $88, $89  (wide, hflip for left)
 *
 * Behavior: q-speed $C0 (3 px/frame) in facing direction. No
 * animation cycle (single frame). Despawns when off-screen.
 */

#include "../../src/game/world/render/sprite_render.h"

void roomrom_arrow_init(void);
void roomrom_arrow_fire(link_face_t face, short link_x, short link_y);
void roomrom_arrow_update(void);
unsigned char roomrom_arrow_active(void);

#endif
