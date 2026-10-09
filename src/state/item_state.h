#ifndef ITEM_STATE_H
#define ITEM_STATE_H

#include "platform_abi.h"
#include "scratch_state.h"

/* Shared item/inventory state for promoted z_01 gameplay code. */
#define ITEM_VALUE_SCRATCH              ZP_TMPA
#define ITEM_LEVEL_RAW                 RAM(0x0010)
#define GAME_MODE                      RAM(0x0012)

#define ITEM_STATUS_BAR_MAP_TRIGGER    RAM(0x04E5)  /* NES StatusBarMapTrigger: map taken */
#define ITEM_FREEZE_FLAG               RAM(0x0506)
#define ITEM_PICKUP_ID                 RAM(0x0505)

#define ITEM_SFX_PRIMARY               RAM(0x0602)
#define ITEM_SFX_SECONDARY             RAM(0x0600)

#define LINK_RING_LEVEL                RAM(0x0662)
#define SAVE_SLOT_INDEX                RAM(0x0016)
#define LINK_BOMB_COUNT                RAM(0x0658)
#define LINK_ARROW_OR_ROD_LEVEL        RAM(0x0659)
#define LINK_CANDLE_LEVEL              RAM(0x065B)
#define LINK_MAX_HEARTS                RAM(0x067C)

#define INVENTORY_VALUE(slot)          RAM(0x0657 + (slot))

#endif
