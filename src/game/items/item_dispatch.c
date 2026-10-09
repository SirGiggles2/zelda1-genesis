/* item_dispatch.c — native item subsystem dispatch (Phase 4).
 *
 * Drain MATCH (verified-by-use). Pure C; calls native core_*, room_*,
 * progress_*. Drain provenance: src/oracle/items/item_runtime.c.
 */

#include "item_dispatch.h"
#include <stdint.h>
#include "platform_abi.h"
#include "item_state.h"        /* ITEM_*, INVENTORY_VALUE, GAME_MODE,
                                * SAVE_SLOT_INDEX */
#include "combat_state.h"      /* LINK_RING_LEVEL, LINK_MAX_HEARTS */
#include "core/core_dispatch.h"      /* core_take_power_triforce, core_set_item_value,
                                      * core_take_5_rupees, core_take_one_rupee,
                                      * core_take_hearts, core_play_key_taken_tune,
                                      * core_take_hearts_no_sound */
#include "room/room_dispatch.h"      /* room_end_game_mode,
                                      * room_patch_and_cue_level_palettes_transfer */
#include "../../state/inventory.h"   /* T2.7: inventory_add_heart_container */

/* T5.2 Plan v5b — item pickup SFX. ID 7 = itempickup per
 * data/audio/MANIFEST.json (commit b25d549c). */
extern void audio_sfx_play(unsigned char sfx);

/* Asm-bound data tables. NOT shims (no z01_/z07_/c_ prefix).
 * Native code reads/writes same backing memory transpile-asm uses. */
extern const unsigned char ItemIdToSlot[];
extern const unsigned char ItemIdToDescriptor[];
extern const unsigned char LevelMasks[];
extern const unsigned char SaveSlotToPaletteRowOffset[];
extern unsigned char MenuPalettesTransferBuf[];

/* LinkColors_CommonCode — Z_01.asm:1869. Inline-baked: tiny enough
 * + read-only, native ownership cleaner than asm extern. */
static const unsigned char k_link_colors_common_code[3] = {
    0x29u, 0x32u, 0x16u
};

/* class 0 + complex item slots (triforce, letter, clock, etc).
 * drain at item_runtime.c:4-31. */
static void item_take_class0_complex(unsigned char item_slot)
{
    const unsigned char level_raw = (unsigned char)ITEM_LEVEL_RAW;
    if (level_raw == 0u) {
        return;
    }
    if (item_slot == 0x1Bu) {
        core_take_power_triforce();
        return;
    }
    if (item_slot == 0x11u) {
        ITEM_STATUS_BAR_MAP_TRIGGER = 1u;
    }
    {
        unsigned char level = (unsigned char)(level_raw - 1u);
        if (level >= 8u) {
            item_slot = (unsigned char)(item_slot + 2u);
            level = (unsigned char)(level & 7u);
        } else {
            level = (unsigned char)(level & 7u);
        }
        INVENTORY_VALUE(item_slot) = (uint8_t)(
            (unsigned char)INVENTORY_VALUE(item_slot) | LevelMasks[level]);
    }
    if (item_slot != 0x1Au) {
        return;
    }
    (void)room_end_game_mode();
    GAME_MODE = 18u;
}

/* class 2 — graded items (rings, swords). drain at item_runtime.c:33-50. */
static void item_handle_class2(unsigned char item_slot)
{
    const unsigned char val = (unsigned char)ITEM_VALUE_SCRATCH;
    if (val < (unsigned char)INVENTORY_VALUE(item_slot)) {
        return;
    }
    INVENTORY_VALUE(item_slot) = val;
    if (item_slot != 0x0Bu) {
        return;
    }
    {
        const unsigned char ring_val = (unsigned char)LINK_RING_LEVEL;
        const unsigned char color = k_link_colors_common_code[ring_val & 3u];
        const unsigned char save_slot = (unsigned char)SAVE_SLOT_INDEX;
        const unsigned char palette_off =
            SaveSlotToPaletteRowOffset[save_slot & 3u];
        MenuPalettesTransferBuf[20u + palette_off] = color;
    }
    room_patch_and_cue_level_palettes_transfer();
}

/* class 1 — incremental items (+1, +5 rupees, hearts, key, etc).
 * drain at item_runtime.c:52-108. */
static void item_check_class1(unsigned char item_slot, unsigned char item_class)
{
    if (item_class != 0x10u) {
        if (item_class == 0x20u) {
            item_handle_class2(item_slot);
            return;
        }
        {
            unsigned char result = 0xFFu;
            if (item_slot == 7u && result >= 3u) {
                result = 2u;
            }
            if (item_slot == 1u && result >= (unsigned char)LINK_MAX_HEARTS) {
                result = (unsigned char)LINK_MAX_HEARTS;
            }
            core_set_item_value((unsigned int)result, (unsigned int)item_slot);
        }
        return;
    }
    if (item_slot == 0x18u) {
        const unsigned char cur = (unsigned char)INVENTORY_VALUE(item_slot);
        if (cur >= 0xF0u) {
            return;
        }
        core_set_item_value((unsigned int)(unsigned char)(cur + 0x11u),
                            (unsigned int)item_slot);
        /* T2.7: native scale-up anim on the newly visible heart slot.
         * Mirrors the NES RAM write above into g_inventory and kicks
         * the 3-frame fade-in (EMPTY -> HALF -> FULL) over ~500ms. */
        (void)inventory_add_heart_container();
        return;
    }
    if (item_slot == 0x1Cu) {
        core_take_5_rupees();
        return;
    }
    if (item_slot == 0x16u) {
        core_take_one_rupee();
        return;
    }
    if (item_slot == 0x19u) {
        core_take_hearts();
        return;
    }
    if (item_slot == 0x17u) {
        core_play_key_taken_tune();
    }
    if (item_slot == 0x14u) {
        core_take_hearts_no_sound();
        return;
    }
    {
        const unsigned int sum = (unsigned int)(unsigned char)ITEM_VALUE_SCRATCH +
                                 (unsigned int)(unsigned char)INVENTORY_VALUE(item_slot);
        unsigned char result = (sum > 0xFFu) ? 0xFFu : (unsigned char)sum;
        if (item_slot == 7u && result >= 3u) {
            result = 2u;
        }
        if (item_slot == 1u && result >= (unsigned char)LINK_MAX_HEARTS) {
            result = (unsigned char)LINK_MAX_HEARTS;
        }
        core_set_item_value((unsigned int)result, (unsigned int)item_slot);
    }
}

void item_take_item(unsigned char item_id)
{
    /* drain at item_runtime.c:110-137. NES TakeItem. */
    ITEM_SFX_PRIMARY = 8u;
    if (item_id == 0x0Eu) {
        ITEM_SFX_PRIMARY = 2u;
    }
    if ((unsigned char)GAME_MODE != 5u) {
        ITEM_FREEZE_FLAG = 0x80u;
        ITEM_SFX_SECONDARY = 8u;
        ITEM_PICKUP_ID = item_id;
    }
    const unsigned char idx = (unsigned char)(item_id & 0x3Fu);
    /* ItemIdToSlot/ItemIdToDescriptor have 36 entries; mask to safe range. */
    const unsigned char item_slot = (idx < 36u) ? ItemIdToSlot[idx] : 0u;
    const unsigned char descriptor =
        (idx < 36u) ? ItemIdToDescriptor[idx] : 0x01u;
    ITEM_VALUE_SCRATCH = (uint8_t)(descriptor & 0x0Fu);
    {
        const unsigned char item_class =
            (unsigned char)(descriptor & 0xF0u);
        if (item_class != 0u) {
            item_check_class1(item_slot, item_class);
            return;
        }
    }
    if (item_slot == 0x11u || item_slot == 0x10u ||
        item_slot == 0x1Au || item_slot == 0x1Bu) {
        item_take_class0_complex(item_slot);
        return;
    }
    core_set_item_value((unsigned int)(unsigned char)ITEM_VALUE_SCRATCH,
                        (unsigned int)item_slot);
}
