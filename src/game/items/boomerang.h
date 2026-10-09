#ifndef ROOMROM_BOOMERANG_H
#define ROOMROM_BOOMERANG_H

/* engine S7 v6 boomerang.
 *
 * NES Z1 reference (reference/aldonunez/Z_05.asm WieldBoomerang +
 * Z_07.asm UpdateBoomerangOrFood + AnimateBoomerangAndCheckCollision
 * at Z_07:4209):
 *
 *   - Wielded with B button. Sets ObjState[$0F] = $10 = "flying out".
 *   - Q-speed = $C0 (3 px/frame). Anim counter = 3 frames per phase.
 *   - 8-phase rotation cycle (BoomerangFrameCycle / BoomerangBaseSpriteAttrCycle
 *     at Z_07:3779). 2 frames per phase => 16 frames per full rotation.
 *   - Travels in input-or-facing direction until distance limit, then
 *     pauses ($20 spread state), slows ($30), returns to Link ($40),
 *     deactivates when caught ($50).
 *   - 16x16 wide-object sprite. 3 distinct frame shapes (tile $36+2*N
 *     for N in 0..2): vertical, diagonal, horizontal silhouettes that
 *     cycle to imply spin. Plus per-phase attr (vflip+hflip) for 8
 *     visual orientations.
 *
 * Simplified engine v6 model:
 *   - Triggered by Z button (no inventory cycle yet).
 *   - 32 frames out, 32 frames return, despawn.
 *   - 8-phase rotation cycle ticks every 2 frames.
 *   - SAT slot 3 (after sword=1, beam=2).
 */

#include "../../src/game/world/render/sprite_render.h"

void roomrom_boomerang_init(void);
void roomrom_boomerang_throw(link_face_t face, short link_x, short link_y);
void roomrom_boomerang_update(short link_x, short link_y);
unsigned char roomrom_boomerang_active(void);
void roomrom_food_wield(void);                    /* T-057 NES WieldFood */
void roomrom_food_apply_chase_target(void);

#endif
