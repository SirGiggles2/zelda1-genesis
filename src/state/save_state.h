#ifndef SAVE_STATE_H
#define SAVE_STATE_H

#include "platform_abi.h"

/* SRAM-backed save state. NES SRAM maps at NES_SRAM_BASE. */

#define SAVE_BYTE(off)  F_ram_6000_X2((unsigned short)(off))

#define SAVE_ROOM_UNIQUE_ID(room_id) \
    SAVE_BYTE(NES_SRAM_ROOM_UNIQUE_ID_BASE + (room_id))

#define SAVE_ROOM_FLAGS_PTR_LO  SAVE_BYTE(NES_SRAM_ROOM_FLAGS_PTR_LO)
#define SAVE_ROOM_FLAGS_PTR_HI  SAVE_BYTE(NES_SRAM_ROOM_FLAGS_PTR_HI)

#define CONTINUE_COUNT(slot)    F_ram_0630_R5((slot))

#endif /* SAVE_STATE_H */
