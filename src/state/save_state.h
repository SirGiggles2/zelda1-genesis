#ifndef SAVE_STATE_H
#define SAVE_STATE_H

#include "platform_abi.h"

/* SRAM-backed save state. NES SRAM maps at NES_SRAM_BASE. */

#define SAVE_BYTE(off)  nes_ram[NES_SRAM_BASE + (unsigned short)(off)]

#define SAVE_ROOM_UNIQUE_ID(room_id) \
    SAVE_BYTE(NES_SRAM_ROOM_UNIQUE_ID_BASE + (room_id))

#define SAVE_ROOM_FLAGS_PTR_LO  SAVE_BYTE(NES_SRAM_ROOM_FLAGS_PTR_LO)
#define SAVE_ROOM_FLAGS_PTR_HI  SAVE_BYTE(NES_SRAM_ROOM_FLAGS_PTR_HI)

#define CONTINUE_COUNT(slot)    RAM(NES_CONTINUE_COUNT_BASE + (slot))

#endif /* SAVE_STATE_H */
