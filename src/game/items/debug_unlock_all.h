/* debug_unlock_all.h — Zelda.md helper to populate g_inventory with every
 * item maxed out. Lets the inventory subscreen render a fully-populated
 * view without playing through Z1 to collect items.
 *
 * Retained for developer tooling. Title input never calls this profile;
 * File Select starts a normal saved/new-game session.
 */
#ifndef DEBUG_UNLOCK_ALL_H
#define DEBUG_UNLOCK_ALL_H

/* Fills g_inventory with every item at max ownership / count. Also writes
 * the NES RAM mirror cells ($657-$67E) so any drained gameplay code that
 * reads via OBJ(NES_*) macros sees the unlocked state immediately. */
void debug_unlock_all_items(void);
extern unsigned char g_debug_session;   /* T-090 */

#endif /* DEBUG_UNLOCK_ALL_H */
