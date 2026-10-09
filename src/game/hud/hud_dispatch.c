/* hud_dispatch.c — native HUD subsystem dispatch (Phase 4).
 *
 * Drain MATCH (verified-by-use). Pure C; calls native cave price
 * formatter (cave_format_decimal_byte) and native core helpers.
 */

#include "hud_dispatch.h"
#include <stdint.h>
#include "platform_abi.h"
#include "world_state.h"       /* TRANSFER_BUF_BYTE, FRAME_COUNTER */
#include "combat_state.h"      /* LINK_HEARTS, LINK_PARTIAL_HEART */
#include "item_state.h"        /* LINK_BOMB_COUNT, INVENTORY_VALUE */
#include "cave_state.h"        /* LINK_RUPEES, CAVE_DOOR_REPAIR_RUPEE_DELTA */
#include "room_state.h"        /* ROOM_TRANSFER_BUF_SELECT */
#include "cave/cave_dispatch.h"   /* cave_format_decimal_byte */
#include "core/core_dispatch.h"   /* core_format_char_doublet */

/* NES Z_01.asm StatusBarTransferBufTemplate (line 2804) — 41 bytes.
 * src/data/ui_layout.inc:18. */
static const unsigned char k_status_bar_template[41] = {
    0x20u, 0xB6u, 0x08u, 0x24u, 0x24u, 0x24u, 0x24u, 0x24u,
    0x24u, 0x24u, 0x24u, 0x20u, 0xD6u, 0x08u, 0x24u, 0x24u,
    0x24u, 0x24u, 0x24u, 0x24u, 0x24u, 0x24u, 0x20u, 0x6Cu,
    0x03u, 0x21u, 0x00u, 0x24u, 0x20u, 0xACu, 0x03u, 0x21u,
    0x00u, 0x24u, 0x20u, 0xCCu, 0x03u, 0x21u, 0x00u, 0x24u,
    0xFFu
};

/* NES source: Z_01.asm:FormatHeartsInTextBuf.
 * Drained C: hud_format_hearts_in_text_buf below.
 * Coverage: FULL heart tile choice; Stance: EXTEND (shared pure helper).
 * HeartValues encodes capacity minus one and whole hearts; the fractional
 * heart displays full at $80, half below it, empty at zero. */
unsigned char hud_heart_tile(unsigned char hearts, unsigned char partial,
                             unsigned char index)
{
    unsigned char whole = (unsigned char)(hearts & 15u);
    if (!hearts || index > (hearts >> 4) || index >= 16u) return 0x24u;
    if (index < whole) return 0xF2u;
    if (index > whole || !partial) return 0x66u;
    return partial >= 0x80u ? 0xF2u : 0x65u;
}

void hud_format_hearts_in_text_buf(unsigned char start_off)
{
    /* drain at hud_runtime.c:6-50. NES FormatHeartsInTextBuf. */
    RAM(0x000Du) = start_off;
    const unsigned char hearts = (unsigned char)RAM(0x000Eu);
    RAM(0x000Bu) = (uint8_t)(start_off + 7u);
    unsigned char row_pos = 7u;
    for (unsigned char slot = 0u; slot < 16u; ++slot) {
        unsigned char tile;
        if (row_pos == 0xFFu) {
            RAM(0x000Bu) = (uint8_t)(RAM(0x000Du) + 0x12u);
            row_pos = 18u;
        }
        tile = hud_heart_tile(hearts, (unsigned char)RAM(0x000Fu),
                              (unsigned char)(15u - slot));
        /* NES @CheckPartial: a half heart clears $0529 (the sword-shot
         * allowance MakeSwordShot reads). T-171 asm_equiv: the drain
         * cleared CurRoomHistoryIndex $0620 instead. */
        if (tile == 0x65u) RAM(0x0529u) = 0u;
        RAM(0x000Cu) = row_pos;
        TRANSFER_BUF_BYTE(RAM(0x000Bu)) = tile;
        RAM(0x000Bu) = (uint8_t)(RAM(0x000Bu) - 1u);
        row_pos = (uint8_t)(RAM(0x000Cu) - 1u);
    }
}

void hud_copy_triplet_to_text_buf(void)
{
    /* drain at hud_runtime.c:52-57. */
    const unsigned char base_off = (unsigned char)RAM(0x0000u);
    TRANSFER_BUF_BYTE(base_off) = (uint8_t)RAM(0x0003u);
    TRANSFER_BUF_BYTE((unsigned char)(base_off - 1u)) = (uint8_t)RAM(0x0002u);
    TRANSFER_BUF_BYTE((unsigned char)(base_off - 2u)) = (uint8_t)RAM(0x0001u);
}

void hud_format_decimal_count_byte(unsigned char val)
{
    /* drain at hud_runtime.c:59-68. NES FormatDecimalCountByte. */
    cave_format_decimal_byte(val);
    unsigned char hundreds = (unsigned char)RAM(0x0001u);
    if (hundreds == 0x24u) {
        hundreds = 33u;
    }
    RAM(0x0001u) = hundreds;
    if (RAM(0x0002u) == 0x24u) {
        core_format_char_doublet(RAM(0x0003u));
    }
}

void hud_format_decimal_count_byte_in_text_buf(unsigned char val,
                                                unsigned char buf_offset)
{
    /* drain at hud_runtime.c:70-74. */
    RAM(0x0000u) = buf_offset;
    hud_format_decimal_count_byte(val);
    hud_copy_triplet_to_text_buf();
}

void hud_format_status_bar_text(void)
{
    /* drain at hud_runtime.c:76-93. NES FormatStatusBarText. */
    for (unsigned char i = 0u; i <= 40u; ++i) {
        TRANSFER_BUF_BYTE(i) = k_status_bar_template[i];
    }
    RAM(0x000Eu) = (uint8_t)LINK_HEARTS;
    RAM(0x000Fu) = (uint8_t)LINK_PARTIAL_HEART;
    hud_format_hearts_in_text_buf(3u);
    hud_format_decimal_count_byte_in_text_buf(LINK_RUPEES, 27u);
    if (RAM(0x0664u) != 0u) {
        RAM(0x0000u) = 33u;
        RAM(0x0001u) = 33u;
        core_format_char_doublet(10u);
        hud_copy_triplet_to_text_buf();
    } else {
        hud_format_decimal_count_byte_in_text_buf((unsigned char)RAM(0x066Eu), 33u);
    }
    hud_format_decimal_count_byte_in_text_buf(LINK_BOMB_COUNT, 39u);
}

void hud_world_change_rupees(void)
{
    /* drain at hud_runtime.c:95-122. NES WorldChangeRupees. */
    if (ROOM_TRANSFER_BUF_SELECT != 0u) {
        return;
    }
    if (!(TRANSFER_BUF_BYTE(0) & 0x80u)) {
        return;
    }
    hud_tick_native_rupees(FRAME_COUNTER);
    if (!(FRAME_COUNTER & 1u)) hud_format_status_bar_text();
}

/* NES source: Z_01.asm:World_ChangeRupees.
 * Drained C: hud_world_change_rupees. Coverage: FULL currency mutation.
 * Stance: EXTEND; share state logic without NES PPU transfer ownership.
 * The native renderer owns its own display transfers. */
void hud_tick_native_rupees(unsigned char frame_counter)
{
    const unsigned char rupees = (unsigned char)LINK_RUPEES;
    if (rupees == 0u) {
        INVENTORY_VALUE(39) = 0u;
    } else if (rupees == 0xFFu) {
        INVENTORY_VALUE(38) = 0u;
    }
    if (frame_counter & 1u) {
        return;
    }
    if (RAM(0x067Du) != 0u) {
        RAM(0x067Du) = (uint8_t)(RAM(0x067Du) - 1u);
        LINK_RUPEES = (uint8_t)(LINK_RUPEES + 1u);
        RAM(0x0604u) = 16u;  /* ROOM_SFX_MAIN */
    }
    if (CAVE_DOOR_REPAIR_RUPEE_DELTA == 0) {
        return;
    }
    CAVE_DOOR_REPAIR_RUPEE_DELTA = (int8_t)(CAVE_DOOR_REPAIR_RUPEE_DELTA - 1);
    LINK_RUPEES = (uint8_t)(LINK_RUPEES - 1u);
    RAM(0x0604u) = 16u;  /* ROOM_SFX_MAIN */
}

/* NES World_FillHearts (Z_05.asm), run every play frame from
 * UpdateHeartsAndRupees before World_ChangeRupees. Potions, fairies and
 * the pond fairy set World_IsFillingHearts ($63). */
void hud_world_fill_hearts(void)
{
    unsigned char hv;
    if (RAM(0x0063u) == 0u) return;                /* World_IsFillingHearts */
    RAM(0x0604u) = 0x10u;                          /* Tune0Request: heart */
    if ((unsigned char)RAM(0x0670u) < 0xF8u) {     /* HeartPartial */
        RAM(0x0670u) = (uint8_t)((unsigned char)RAM(0x0670u) + 6u);
        return;
    }
    RAM(0x0670u) = 0u;
    /* CompareHeartsToContainers: [00] = hearts, compare containers. */
    hv = (unsigned char)RAM(0x066Fu);              /* HeartValues */
    RAM(0x0000u) = (uint8_t)(hv & 0x0Fu);
    if ((unsigned char)(hv >> 4) != (unsigned char)(hv & 0x0Fu)) {
        RAM(0x066Fu) = (uint8_t)(hv + 1u);         /* INC HeartValues */
        return;
    }
    RAM(0x0670u) = 0xFFu;                          /* DEC from 0 */
    RAM(0x052Eu) = 0u;                             /* SwordBlocked */
    RAM(0x0063u) = 0u;
    RAM(0x00E0u) = 0u;                             /* Paused */
}
