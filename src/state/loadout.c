#include "loadout.h"

/* Session loadout. B remains NES $0656; only the second item slot is new.
 * Save-file persistence is T-255 follow-up work. */
static unsigned char s_c_slot = LOADOUT_SLOT_NONE;

unsigned char loadout_c_slot(void)
{
    return s_c_slot;
}

void loadout_set_c_slot(unsigned char slot)
{
    s_c_slot = (slot < 9u) ? slot : LOADOUT_SLOT_NONE;
}
