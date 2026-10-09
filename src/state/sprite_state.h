#ifndef SPRITE_STATE_H
#define SPRITE_STATE_H

#include "platform_abi.h"

/* Sprite / OAM shadow cells. Owned by sprite_runtime.
 * OAM layout: 64 sprites * 4 bytes, starting at NES_OAM_BASE.
 *   [0] y, [1] tile, [2] attr, [3] x
 */
#define OAM_BYTE(i)        RAM(NES_OAM_BASE + (unsigned short)(i))
#define OAM_SPRITE_Y(n)    RAM(NES_OAM_BASE + ((unsigned short)(n) * 4) + 0)
#define OAM_SPRITE_TILE(n) RAM(NES_OAM_BASE + ((unsigned short)(n) * 4) + 1)
#define OAM_SPRITE_ATTR(n) RAM(NES_OAM_BASE + ((unsigned short)(n) * 4) + 2)
#define OAM_SPRITE_X(n)    RAM(NES_OAM_BASE + ((unsigned short)(n) * 4) + 3)

#define OAM_SPRITE_COUNT   64

#endif /* SPRITE_STATE_H */
