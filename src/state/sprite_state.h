#ifndef SPRITE_STATE_H
#define SPRITE_STATE_H

#include "platform_abi.h"

/* Sprite / OAM shadow cells. Owned by sprite_runtime.
 * OAM layout: 64 sprites * 4 bytes, starting at NES_OAM_BASE.
 *   [0] y, [1] tile, [2] attr, [3] x
 */
#define OAM_BYTE(i)        F_ram_0200_R5((unsigned short)(i))
#define OAM_SPRITE_Y(n)    F_ram_0200_R6(((unsigned short)(n) * 4))
#define OAM_SPRITE_TILE(n) F_ram_0201_R3(((unsigned short)(n) * 4))
#define OAM_SPRITE_ATTR(n) F_ram_0202_R4(((unsigned short)(n) * 4))
#define OAM_SPRITE_X(n)    F_ram_0203_R4(((unsigned short)(n) * 4))

#define OAM_SPRITE_COUNT   64

#endif /* SPRITE_STATE_H */
