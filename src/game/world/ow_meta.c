/* OW LevelBlock attribute accessor implementation.
 *
 * NES source: reference/aldonunez/Z_05.asm:HandleWarpOW (line 7313),
 *             specifically the selector dispatch at lines 7338-7344.
 *
 * Stance: GREENFIELD (drain audit 2026-05-06: zero drained rows for
 * CheckWarps / HandleWarpOW). NES disasm is the final authority.
 */

#include "ow_meta.h"
#include "platform_abi.h"
#include "../../../data/rooms/overworld_offsets.h"

extern const unsigned char rooms_overworld[];

unsigned char roomrom_ow_meta_attr_a(unsigned char room_id)
{
    if (room_id >= ROOMROM_OW_ROOM_COUNT) {
        return 0u;
    }
    return rooms_overworld[ROOMROM_OW_LEVELBLOCK_ATTRS_A_OFFSET + room_id];
}

unsigned char roomrom_ow_meta_attr_b(unsigned char room_id)
{
    if (room_id >= ROOMROM_OW_ROOM_COUNT) {
        return 0u;
    }
    /* HandleWarpOW reads installed LevelBlockAttrsB. Quest 2 patches
     * selected bytes after the ROM blob is copied to NES SRAM; using the
     * unpatched blob routes Q2 cave doors to Q1 destinations. */
    return (unsigned char)RAM(0x68FEu + room_id);
}

unsigned char roomrom_ow_meta_level_selector(unsigned char room_id)
{
    return (unsigned char)(roomrom_ow_meta_attr_b(room_id) & 0xFCu);
}

unsigned char roomrom_ow_meta_is_level_selector(unsigned char selector)
{
    return (unsigned char)(selector < 0x40u);
}

unsigned char roomrom_ow_meta_level_from_selector(unsigned char selector)
{
    return (unsigned char)(selector >> 2);
}

unsigned char roomrom_ow_meta_is_shortcut_cave_selector(unsigned char selector)
{
    return (unsigned char)(selector == 0x50u);
}

unsigned char roomrom_ow_meta_cave_id_from_selector(unsigned char selector)
{
    return (unsigned char)(0x6Au + ((selector - 0x40u) >> 2));
}
