#ifndef ITEM_STATE_H
#define ITEM_STATE_H

#include "platform_abi.h"
#include "scratch_state.h"

/* Shared item/inventory state for promoted z_01 gameplay code. */
#define ITEM_VALUE_SCRATCH              ZP_TMPA
#define ITEM_LEVEL_RAW                 F_ram_0010_R2
#define GAME_MODE                      F_ram_0012_R3

#define ITEM_STATUS_BAR_MAP_TRIGGER    F_ITEM_STATUS_BAR_MAP_TRIGGER_R  /* NES StatusBarMapTrigger: map taken */
#define ITEM_FREEZE_FLAG               F_ram_0506_R2
#define ITEM_PICKUP_ID                 F_ram_0505_R2

#define ITEM_SFX_PRIMARY               F_ram_0602_R2
#define ITEM_SFX_SECONDARY             F_ram_0600_R2

#define LINK_RING_LEVEL                F_ram_0662_R
#define SAVE_SLOT_INDEX                F_ram_0016_R2
#define LINK_BOMB_COUNT                F_ram_0658_R
#define LINK_ARROW_OR_ROD_LEVEL        F_ram_0659_R2
#define LINK_CANDLE_LEVEL              F_ram_065B_R
#define LINK_MAX_HEARTS                F_ram_067C_R

#define INVENTORY_VALUE(slot)          F_ram_0657_R5((slot))

#endif
