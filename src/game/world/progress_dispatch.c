/* progress_dispatch.c — native progress subsystem dispatch (Phase 4).
 *
 * Phase 4 first progress batch: room-flag persistence pair that
 * unblocks Phase 3 cave stubs (set/get UW-item-taken bit).
 *
 * Drain MATCH per finding 4_4n. Pure C, no shims (the
 * z07_get_room_flags transpile shim that drain calls is replaced
 * with an inline RAM-deref of the same SRAM pointer pattern).
 */

#include "progress_dispatch.h"
#include "platform_abi.h"      /* RAM, nes_ram[], NES_SRAM_*, OBJ */
#include "save_state.h"        /* SAVE_ROOM_FLAGS_PTR_LO/HI */
#include "progress_state.h"    /* SAVEFILE_PTR_LO/HI, SAVEFILE_MASK_LO/HI, ROOM_TILE_OBJ_*, CUR_INV_TILE, POWER_TRIFORCE_FANFARE_FLAG, CURTAIN_TIMER, LAST_BOSS_DEFEATED */
#include "world_state.h"       /* CUR_ROOM_ID, TRANSFER_BUF_BYTE/POS, OBJ_MOVE_TIMER */
#include "combat_state.h"      /* MON_TYPE, COMBAT_PART_INDEX, LINK_ACTION_TIMER */
#include "object_state.h"      /* OBJ_STATE */
#include "enemy_state.h"       /* LINK_X, LINK_Y, OBJ_X, OBJ_Y */
#include "item_state.h"        /* ITEM_SFX_SECONDARY, SAVE_SLOT_INDEX */
#include "room_state.h"        /* ROOM_TRANSFER_BUF_SELECT */
#include "room/room_dispatch.h" /* room_copy_column_to_tilebuf */

#define NES_SRAM_BASE 0x6000u

/* Inline equivalent of roomrt_get_room_flags / z07_get_room_flags
 * (src/oracle/room/room_runtime.c:14-22). Reads SRAM room-flags
 * pointer, derefs at CUR_ROOM_ID, returns the per-room flag byte.
 * Side effect: stashes the SRAM pointer into SAVEFILE_PTR_LO/HI
 * (NES caller convention — drain preserves this). */
static inline unsigned char progress_read_room_flags_inline(void)
{
    const unsigned char ptr_lo =
        nes_ram[NES_SRAM_BASE + NES_SRAM_ROOM_FLAGS_PTR_LO];
    const unsigned char ptr_hi =
        nes_ram[NES_SRAM_BASE + NES_SRAM_ROOM_FLAGS_PTR_HI];
    SAVEFILE_PTR_LO = ptr_lo;
    SAVEFILE_PTR_HI = ptr_hi;
    const unsigned short ptr =
        (unsigned short)(((unsigned short)ptr_hi << 8) | ptr_lo);
    return (unsigned char)nes_ram[ptr + CUR_ROOM_ID];
}

void progress_set_room_flag_uw_item_state(void)
{
    /* drain at progress_runtime.c:32-38. NES SetRoomFlagUWItemState:
     *   JSR GetRoomFlags    ; A = current room's flag byte; sets ptr scratch
     *   ORA #$10            ; mark UW-item-taken
     *   LDY CUR_ROOM_ID
     *   STA (ptr),Y         ; persist back into save slot table
     */
    unsigned char flags = progress_read_room_flags_inline();
    flags = (unsigned char)(flags | 0x10u);
    const unsigned short ptr =
        (unsigned short)(((unsigned short)SAVEFILE_PTR_HI << 8) | SAVEFILE_PTR_LO);
    nes_ram[ptr + CUR_ROOM_ID] = flags;
}

/* NES Z_01.asm PaletteRow7TransferRecord (8-byte transfer header,
 * extracted from src/data/palette_tables.inc by extract_misc.py). */
static const unsigned char k_palette_row7_transfer_record[8] = {
    0x3Fu, 0x1Cu, 0x04u, 0x0Fu, 0x07u, 0x17u, 0x27u, 0xFFu
};

/* NES Z_01.asm GanonColorTriples (9-byte color seed table,
 * src/data/palette_tables.inc). 3 triples of 3 bytes:
 *   [0..2] = brown ganon
 *   [3..5] = blue ganon
 *   [6..8] = ashes
 * drain reads `triples[color_index - 2 + j]` so brown=2, blue=5, ashes=8. */
static const unsigned char k_ganon_color_triples[9] = {
    0x07u, 0x17u, 0x30u, 0x16u, 0x2Cu, 0x3Cu, 0x27u, 0x06u, 0x16u
};

/* Common helper for the 3 ganon-palette-replace variants. */
static void progress_replace_palette_row_common(unsigned char color_index)
{
    /* Append PaletteRow7TransferRecord into transfer buf at TRANSFER_BUF_POS. */
    unsigned char len = TRANSFER_BUF_POS;
    for (unsigned char i = 0u; i < 8u; i++) {
        TRANSFER_BUF_BYTE(len) = k_palette_row7_transfer_record[i];
        len = (unsigned char)(len + 1u);
    }
    TRANSFER_BUF_POS = len;

    /* Stash 3 color triple bytes at RAM($0306..$0308). */
    for (int j = 0; j < 3; j++) {
        RAM(0x0306 + j) = k_ganon_color_triples[color_index - 2 + j];
    }
}

void progress_replace_ganon_brown_palette_row(void)
{
    progress_replace_palette_row_common(2u);
}

void progress_replace_ganon_blue_palette_row(void)
{
    progress_replace_palette_row_common(5u);
}

void progress_replace_ashes_palette_row(void)
{
    progress_replace_palette_row_common(8u);
}

unsigned char progress_reset_room_tile_obj_info(void)
{
    /* drain at progress_runtime.c:25-30. NES ResetRoomTileObjInfo
     * trivial: zero 3 RAM cells, return A=0. */
    ROOM_TILE_OBJ_0 = 0u;
    ROOM_TILE_OBJ_1 = 0u;
    ROOM_TILE_OBJ_2 = 0u;
    return 0u;
}

void progress_update_bomb_flash_effect(unsigned int slot)
{
    /* drain at progress_runtime.c:50-65. NES UpdateBombFlashEffect.
     * Animate the bomb-flash overlay tile mask by shifting on a few
     * specific timer values during state $13 (bomb explosion). */
    if (OBJ_STATE(slot) != 0x13u) {
        return;
    }
    unsigned char mask = CUR_INV_TILE;
    const unsigned char timer = (unsigned char)OBJ_MOVE_TIMER(slot);
    mask = (unsigned char)(mask >> 1);
    if (timer == 0x16u || timer == 0x11u) {
        mask = (unsigned char)((mask << 1) | 1u);
    } else if (timer == 0x12u || timer == 0x0Du) {
        mask = (unsigned char)(mask << 1);
    } else {
        return;
    }
    CUR_INV_TILE = mask;
}

void progress_check_tile_objects_blocking(void)
{
    /* drain at progress_runtime.c:134-152. NES CheckTileObjectsBlocking.
     * Slot 12..1 scan for blocking monster types ($68/$62/$65/$66) in
     * state 1; on hit within $10 px (X/Y) of Link, clear
     * COMBAT_PART_INDEX. */
    for (int slot = 12; slot >= 1; slot--) {
        const unsigned char mtype = (unsigned char)MON_TYPE(slot);
        if (mtype != 0x68u && mtype != 0x62u &&
            mtype != 0x65u && mtype != 0x66u) {
            continue;
        }
        if (OBJ_STATE(slot) != 1u) {
            continue;
        }
        {
            signed char dx =
                (signed char)((unsigned char)LINK_X - (unsigned char)OBJ_X(slot));
            if (dx < 0) {
                dx = (signed char)(-dx);
            }
            if ((unsigned char)dx >= 0x10u) {
                continue;
            }
        }
        {
            const unsigned char ly_adj = (unsigned char)((unsigned char)LINK_Y + 3u);
            signed char dy =
                (signed char)(ly_adj - (unsigned char)OBJ_Y(slot));
            if (dy < 0) {
                dy = (signed char)(-dy);
            }
            if ((unsigned char)dy >= 0x10u) {
                continue;
            }
        }
        COMBAT_PART_INDEX = 0u;
    }
}

/* NES Z_07.asm LevelMasks (line 747): bit-flag table for 8 levels. */
static const unsigned char k_level_masks[8] = {
    0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x20u, 0x40u, 0x80u
};

/* NES Z_01.asm SaveFileAAddressSets (line 3006) + Set1 (3010) + Set2
 * (3014). 3 save slots, 14-byte pointer set each = 42 bytes flat. */
static const unsigned char k_save_file_a_address_sets[42] = {
    /* Slot 0: $1A $60 $92 $60 $02 $60 $12 $65 $15 $65 $18 $65 $1B $65 */
    0x1Au, 0x60u, 0x92u, 0x60u, 0x02u, 0x60u, 0x12u, 0x65u,
    0x15u, 0x65u, 0x18u, 0x65u, 0x1Bu, 0x65u,
    /* Slot 1: $42 $60 $12 $62 $0A $60 $13 $65 $16 $65 $19 $65 $1C $65 */
    0x42u, 0x60u, 0x12u, 0x62u, 0x0Au, 0x60u, 0x13u, 0x65u,
    0x16u, 0x65u, 0x19u, 0x65u, 0x1Cu, 0x65u,
    /* Slot 2: $6A $60 $92 $63 $12 $60 $14 $65 $17 $65 $1A $65 $1D $65 */
    0x6Au, 0x60u, 0x92u, 0x63u, 0x12u, 0x60u, 0x14u, 0x65u,
    0x17u, 0x65u, 0x1Au, 0x65u, 0x1Du, 0x65u
};

void progress_fetch_file_a_address_set(void)
{
    /* drain at progress_runtime.c:123-132. NES FetchFileAAddressSet
     * (Z_01.asm:3030).
     *
     * NES: end_idx = $FF + $0E * (slot+1) = $0D + slot*$0E (= 13 for
     * slot 0, 27 for slot 1, 41 for slot 2 — index of last byte of
     * the slot's 14-byte block). Loop copies 14 bytes backwards into
     * ZP RAM($00..$0D). Then writes the items-pointer trailer at
     * RAM($0E)=$7F + RAM($0F)=$06. */
    const unsigned char file_slot = (unsigned char)SAVE_SLOT_INDEX;
    unsigned char end_idx = (unsigned char)(0x0Du + file_slot * 0x0Eu);
    for (int i = 13; i >= 0; i--) {
        RAM(i) = k_save_file_a_address_sets[end_idx];
        end_idx = (unsigned char)(end_idx - 1u);
    }
    RAM(0x000Eu) = 0x7Fu;
    RAM(0x000Fu) = 0x06u;
}

void progress_update_position_marker(unsigned char room_id, unsigned int idx)
{
    /* drain at progress_runtime.c:67-96. NES UpdatePositionMarker. */
    const unsigned char level = (unsigned char)CUR_LEVEL;
    const unsigned char row   = (unsigned char)((room_id & 0x70u) >> 2);
    const unsigned char col   = (unsigned char)(room_id & 0x0Fu);
    unsigned char tile_val;
    unsigned char col_shifted;

    MAP_MARKER_Y(idx) = (uint8_t)(row + 0x17u);
    if (level == 0u) {
        tile_val    = 17u;
        col_shifted = (unsigned char)(col << 2);
    } else {
        tile_val    = 18u;
        col_shifted = (unsigned char)(col << 3);
    }
    MAP_MARKER_TILE(idx) = 62u;
    MAP_MARKER_X(idx)    = (uint8_t)(col_shifted + tile_val +
                                     nes_ram[NES_SRAM_BASE + 0x0BACu]);

    if (idx == 0u) {
        MAP_MARKER_ATTR(0) = 0u;
        return;
    }
    {
        unsigned char attr = 3u;
        if (level != 9u && (RAM(0x0671) & k_level_masks[level - 1u])) {
            /* Item already found in this level — stay full-bright. */
        } else {
            const unsigned char flash = (unsigned char)(FRAME_COUNTER & 0x1Fu);
            if (flash < 0x10u) {
                attr = 2u;
            }
        }
        MAP_MARKER_ATTR(idx) = attr;
    }
}

void progress_update_player_position_marker(void)
{
    /* drain at progress_runtime.c:98-102. */
    if (MODE_VALUE == 9u) {
        return;
    }
    if (PLAYER_MARKER_DISABLE) {
        return;
    }
    progress_update_position_marker((unsigned char)CUR_ROOM_ID, 0u);
}

void progress_check_power_triforce_fanfare(void)
{
    /* NES source: Z_01.asm:CheckPowerTriforceFanfare.
     * Drained C: src/oracle/world/progress_runtime.c:154-168.
     * Coverage: PARTIAL (T-184 reversed normal/white selectors).
     * Stance: EXTEND the existing fanfare owner with source phase order. */
    if (!POWER_TRIFORCE_FANFARE_FLAG) {
        return;
    }
    if (!CURTAIN_TIMER) {
        progress_replace_ashes_palette_row();
        ITEM_SFX_SECONDARY = 32u;
        /* NES Z_01.asm:CheckPowerTriforceFanfare/@EndFanfare sets
         * LastBossDefeated, which unlocks secret-trigger-3 shutters. */
        LAST_BOSS_DEFEATED = 1u;
        LINK_ACTION_TIMER = 0u;
        POWER_TRIFORCE_FANFARE_FLAG = 0u;
        return;
    }
    {
        const unsigned char phase = (unsigned char)(CURTAIN_TIMER & 7u);
        ROOM_TRANSFER_BUF_SELECT = (phase < 4u) ? 0x18u : 0x78u;
    }
}

unsigned char progress_get_room_flag_uw_item_state(void)
{
    /* drain at progress_runtime.c:40-48. NES GetRoomFlagUWItemState
     * uses a different pointer-source path than the bare GetRoomFlags
     * — pulls SAVE_ROOM_FLAGS_PTR_* from the active save file (vs
     * the SRAM table that GetRoomFlags reads). drain stashes the
     * pointer into SAVEFILE_MASK_LO/HI as a side effect. */
    const unsigned char ptr_lo = SAVE_ROOM_FLAGS_PTR_LO;
    const unsigned char ptr_hi = SAVE_ROOM_FLAGS_PTR_HI;
    SAVEFILE_MASK_LO = ptr_lo;
    SAVEFILE_MASK_HI = ptr_hi;
    const unsigned short ptr =
        (unsigned short)(((unsigned short)ptr_hi << 8) | ptr_lo);
    return (unsigned char)(nes_ram[ptr + CUR_ROOM_ID] & 0x10u);
}

void progress_update_world_curtain_effect(void)
{
    /* drain at progress_runtime.c:104-117. NES UpdateWorldCurtainEffect.
     * Walks 2 columns from $007C+ITEM_VALUE_SCRATCH (i.e. RAM($007C)
     * + RAM($007D)), copies each into the transfer buffer, decrements
     * the left col + increments the right col, resets curtain timer. */
    if ((unsigned char)CURTAIN_TIMER) {
        return;
    }
    ITEM_VALUE_SCRATCH = 1u;
    do {
        const unsigned char col_idx = (unsigned char)ITEM_VALUE_SCRATCH;
        CUR_ROOM_FLAGS_PTR = RAM(0x007Cu + col_idx);
        room_copy_column_to_tilebuf();
        ITEM_VALUE_SCRATCH =
            (uint8_t)((unsigned char)ITEM_VALUE_SCRATCH - 1u);
    } while ((signed char)(unsigned char)ITEM_VALUE_SCRATCH >= 0);
    CUR_ROOM_FLAGS_PTR = 0xFFu;
    CURTAIN_TIMER = 5u;
    CURTAIN_LEFT_COL =
        (uint8_t)((unsigned char)CURTAIN_LEFT_COL - 1u);
    CURTAIN_RIGHT_COL =
        (uint8_t)((unsigned char)CURTAIN_RIGHT_COL + 1u);
}

void progress_update_world_curtain_effect_bank2(void)
{
    /* drain at progress_runtime.c:119-121. */
    progress_update_world_curtain_effect();
}
