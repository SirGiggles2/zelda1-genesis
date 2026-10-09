/* Underworld item metadata and pickup bridge.
 * Rewards use native item_take_item and installed native room flags.
 * Cartridge save/load wiring remains a separate lifecycle requirement.
 */

#ifndef ROOMROM_UW_ITEM_ROOM_META_H
#define ROOMROM_UW_ITEM_ROOM_META_H

#include "../data/uw_item_rooms.h"

unsigned char roomrom_uw_item_for_room(unsigned char level,
                                       unsigned char quest,
                                       unsigned char room_id,
                                       struct uw_item_room_meta *out);

/* Taken bit in the installed native UW world-flags table. */
unsigned char roomrom_uw_item_taken(unsigned char room_id);
void          roomrom_uw_item_set_taken(unsigned char room_id);
void          roomrom_uw_item_clear_taken(void);

/* Try the native slot-19 reward at the settled player position.
 * Returns 1 if the existing NES pickup routine awarded it. */
unsigned char roomrom_uw_item_try_pickup(unsigned char level,
                                         unsigned char room_id,
                                         unsigned char link_x,
                                         unsigned char link_y);

unsigned char roomrom_uw_triforce_pickup_active(void);

unsigned char roomrom_uw_item_inv_compass(void);  /* INVENTORY_VALUE(16) */
unsigned char roomrom_uw_item_inv_map(void);      /* INVENTORY_VALUE(17) */
unsigned char roomrom_uw_item_inv_triforce(void); /* INVENTORY_VALUE(26) */

void roomrom_uw_item_publish_persist(void);

#define ROOMROM_DEBUG_ITEM_TAKEN_BASE  0x00FF7D00UL
#define ROOMROM_DEBUG_ITEM_TAKEN_BYTES 256u

#endif /* ROOMROM_UW_ITEM_ROOM_META_H */
