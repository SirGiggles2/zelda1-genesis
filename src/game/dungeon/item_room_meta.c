/* Underworld room-item metadata and pickup bridge.
 * NES source: Z_01.asm:TakeItem; Z_05.asm:CreateRoomObjects.
 * Drained C: src/game/items/item_dispatch.c:item_take_item.
 * Coverage: PARTIAL (native awards and room flags; cart save wiring pending).
 * Stance: EXTEND.
 */

#include "item_room_meta.h"
#include "../../state/inventory.h"
#include "../items/item_dispatch.h"
#include "../cave/cave_dispatch.h"
#include "../../../engine/data/uw_item_rooms.h"

/* Canonical inventory storage in the NES RAM mirror. */
#include "../../state/item_state.h"
#include "../../state/save_state.h"

static unsigned char s_triforce_pickup_active;

unsigned char roomrom_uw_item_for_room(unsigned char level,
                                       unsigned char quest,
                                       unsigned char room_id,
                                       struct uw_item_room_meta *out)
{
    unsigned short idx_plus_one;
    if (out == 0) return 0u;
    if (level >= 10u || quest >= 3u || room_id >= 128u) return 0u;
    idx_plus_one = uw_item_room_lookup[level][quest][room_id];
    if (idx_plus_one == 0u) return 0u;
    *out = uw_item_rooms[(unsigned short)(idx_plus_one - 1u)];
    return 1u;
}

/* NES Z_01.asm:GetRoomFlagUWItemState uses the installed LevelInfo
 * pointer. The two UW tables belong to native progress, not a room-only
 * cache. Ignore an uninstalled/overworld pointer during frontend setup. */
static unsigned short item_flags_base(void)
{
    unsigned short ptr = (unsigned short)(SAVE_ROOM_FLAGS_PTR_LO |
        ((unsigned short)SAVE_ROOM_FLAGS_PTR_HI << 8));
    return (ptr == 0x06FFu || ptr == 0x077Fu) ? ptr : 0u;
}

unsigned char roomrom_uw_item_taken(unsigned char room_id)
{
    unsigned short ptr = item_flags_base();
    return (ptr && room_id < 128u && (RAM(ptr + room_id) & 0x10u)) ? 1u : 0u;
}

void roomrom_uw_item_set_taken(unsigned char room_id)
{
    unsigned short ptr = item_flags_base();
    if (ptr && room_id < 128u) RAM(ptr + room_id) |= 0x10u;
}

void roomrom_uw_item_clear_taken(void)
{
    unsigned short i;
    for (i = 0x06FFu; i < 0x07FFu; ++i) RAM(i) &= 0xEFu;
    s_triforce_pickup_active = 0u;
}

/* NES source: Z_01.asm:TryTakeRoomItem/TryTakeItem.
 * Drained C: cave_dispatch.c:cave_try_take_room_item.
 * Coverage: PARTIAL (native room-item position, collision and award).
 * Stance: EXTEND the existing pickup path; slot 19 owns live reward state. */
unsigned char roomrom_uw_item_try_pickup(unsigned char level,
                                         unsigned char room_id,
                                         unsigned char link_x,
                                         unsigned char link_y)
{
    unsigned char item_id = RAM(0x00ABu);
    if (level < 1u || level > 9u || room_id >= 128u || !item_flags_base()) return 0u;
    if (roomrom_uw_item_taken(room_id)) return 0u;
    /* Publish the settled native player position at the pickup boundary.
     * The drained routine checks halt/lifetime, uses slot 19's live X/Y,
     * deactivates it, and marks the room before awarding the item. */
    RAM(0x0070u) = link_x;
    RAM(0x0084u) = link_y;
    cave_try_take_room_item();
    if (!roomrom_uw_item_taken(room_id)) return 0u;
    if (item_id == UW_ITEM_ID_TRIFORCE) {
        s_triforce_pickup_active = 1u;
        g_inventory.triforce = RAM(0x0671u);
    }
    inventory_hud_mark_dirty();
    return 1u;
}

unsigned char roomrom_uw_triforce_pickup_active(void)
{
    return s_triforce_pickup_active;
}

unsigned char roomrom_uw_item_inv_compass(void)
{
    return (unsigned char)INVENTORY_VALUE(UW_INV_SLOT_COMPASS + (ITEM_LEVEL_RAW == 9u ? 2u : 0u));
}

unsigned char roomrom_uw_item_inv_map(void)
{
    return (unsigned char)INVENTORY_VALUE(UW_INV_SLOT_MAP + (ITEM_LEVEL_RAW == 9u ? 2u : 0u));
}

unsigned char roomrom_uw_item_inv_triforce(void)
{
    return (unsigned char)INVENTORY_VALUE(UW_INV_SLOT_TRIFORCE);
}

void roomrom_uw_item_publish_persist(void)
{
    volatile unsigned char *dst =
        (volatile unsigned char *)ROOMROM_DEBUG_ITEM_TAKEN_BASE;
    unsigned short i;
    for (i = 0u; i < 256u; i++) dst[i] = roomrom_uw_item_taken((unsigned char)i);
}
