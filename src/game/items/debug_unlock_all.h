/* debug_unlock_all.h — Debug.md helper to populate g_inventory with every
 * item maxed out. Lets the inventory subscreen render a fully-populated
 * view without playing through Z1 to collect items.
 *
 * Wired into the gameplay-entry path (src/debug/a4_probe_main.c) so it
 * fires once when the user transitions from title to gameplay via the
 * A+B+C (Quest 1) or X+Y+Z (Quest 2) chord. Both run the same current
 * gameplay runtime as File Select, which never calls this debug profile.
 */
#ifndef DEBUG_UNLOCK_ALL_H
#define DEBUG_UNLOCK_ALL_H

/* Fills g_inventory with every item at max ownership / count. Also writes
 * the NES RAM mirror cells ($657-$67E) so any drained gameplay code that
 * reads via OBJ(NES_*) macros sees the unlocked state immediately. */
void debug_unlock_all_items(void);
extern unsigned char g_debug_session;   /* T-090 */

#endif /* DEBUG_UNLOCK_ALL_H */
