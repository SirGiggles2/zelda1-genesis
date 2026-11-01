#ifndef SWORD_RUNTIME_H
#define SWORD_RUNTIME_H
#include "../../state/weapon_state.h"
/* Pure per-owner rules. Caller owns action gates, Link animation, publication,
 * render and beam events. No player singleton or NES image is accessed. */
void sword_runtime_start(PlayerSwordState *sw, unsigned char owner,
    unsigned char x, unsigned char y, unsigned char dir);
/* Returns Link's new animation countdown on a state transition, else zero.
 * State 6 is exposed to the adapter before its ordered reset/publication. */
unsigned char sword_runtime_tick(PlayerSwordState *sw);
void sword_runtime_pose(PlayerSwordState *sw, unsigned char x,
    unsigned char y, unsigned char dir, unsigned char remix);
#endif
