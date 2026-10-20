#ifndef LOADOUT_H
#define LOADOUT_H

#define LOADOUT_SLOT_NONE 0xFFu

/* Remix B slot is NES SelectedItemSlot ($0656); C has no NES cell. */
unsigned char loadout_c_slot(void);
void loadout_set_c_slot(unsigned char slot);

#endif /* LOADOUT_H */
