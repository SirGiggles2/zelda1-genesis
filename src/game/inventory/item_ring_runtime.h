#ifndef ITEM_RING_RUNTIME_H
#define ITEM_RING_RUNTIME_H
#include "../../state/loadout.h"
extern LoadoutRing g_item_rings[4];
void item_ring_reset_all(void);
void item_ring_tick(unsigned char enabled);
unsigned char item_ring_single_frozen(void);
unsigned char item_ring_player_browsing(unsigned char player);
/* Return whether P1 needs a post-cycle HUD refresh. */
unsigned char item_ring_cycle_inputs(void);
void item_ring_refresh(void);
#endif
