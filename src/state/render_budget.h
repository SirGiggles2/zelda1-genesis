#ifndef RENDER_BUDGET_H
#define RENDER_BUDGET_H

/* Active gameplay is H32 (VDP register12 bit0 clear), 256px wide.
 * SGDK permits an80-entry backing cache but hardware visits at most64.
 * Multiplayer composition admits entries against the exact line limits;
 * bodies/masks keep priority and crowded effects rotate admission.
 * The former80/20 envelope did not describe the actual gameplay mode. */
#define RENDER_BUDGET_VDP_TOTAL_SPRITES 64u
#define RENDER_BUDGET_VDP_PER_LINE 16u
#define RENDER_BUDGET_VDP_PIXELS_PER_LINE 256u

/* Fixed masks, P1/HUD, all player bodies and extra swords occupy0..28.
 * The35 remaining entries compose extra item records and actors. These
 * are capacities, not promises that every requested sprite fits a line. */
#define RENDER_BUDGET_FIXED_SPRITES 29u
#define RENDER_BUDGET_DYNAMIC_SPRITES (RENDER_BUDGET_VDP_TOTAL_SPRITES - RENDER_BUDGET_FIXED_SPRITES)
#endif
