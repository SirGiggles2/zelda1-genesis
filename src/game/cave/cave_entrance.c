/* cave_entrance.c — NES-aligned OW→cave dispatch via LevelBlockAttrsB.
 *
 * NES source: reference/aldonunez/Z_05.asm:7313 HandleWarpOW.
 * Stance:     REPLACE (Tier 0 hardcoded $6A retired 2026-05-24).
 *
 * NES dispatch (Z_05.asm:7338-7368):
 *
 *   CMP #$24  BEQ entrance         ; armos pad / special warp
 *   CMP #$88  BEQ entrance         ; rock pile / bombable
 *   CMP #$70  BCC return_no_entry
 *   CMP #$74  BCS return_no_entry
 *   STA ObjCollidedTile = $70      ; collapse $70..$73 → $70
 * entrance:
 *   LDY RoomId
 *   LDA LevelBlockAttrsB, Y
 *   AND #$FC                       ; selector
 *   CMP #$40
 *   BCC @LoadLevel                 ; < $40 → dungeon (handled elsewhere)
 *   LDY #$0B                       ; default Mode $0B (regular cave)
 *   CMP #$50
 *   BNE :+
 *   INY                            ; selector == $50 → Mode $0C (shortcut)
 * :
 *   TYA  / JMP SetTargetMode
 *
 * Cave-id derivation (per OverworldPersonTextSelectors layout, Z_01.asm:53-56):
 *   cave_idx = (selector - $40) >> 2   ; 0..19 = 20 distinct caves
 *   cave_id  = $6A + cave_idx          ; $6A..$7D
 *
 * Dungeon-vs-cave split:
 *   Dungeon path (selector < $40) is handled by detect_warp_ow in
 *   src/game/world/transition.c via roomrom_ow_meta_is_level_selector.
 *   This function returns 0 for dungeon selectors so the caller falls
 *   through to the dungeon coordinator instead of firing cave entry.
 */

#include "cave_entrance.h"
#include "../world/ow_meta.h"
#include "platform_abi.h"

cave_id_t cave_entrance_check(unsigned char tile, unsigned char room_id)
{
    unsigned char selector;

    /* Z_05.asm:7320-7327 — tile range filter. */
    if (tile != 0x24u && tile != 0x88u &&
        !(tile >= 0x70u && tile <= 0x73u)) {
        return (cave_id_t)0;
    }

    /* Z_05.asm:7338-7344 — per-room selector. */
    selector = roomrom_ow_meta_level_selector(room_id);

    /* Z_05.asm:7345 BCC @LoadLevel — dungeon dispatch handled by
     * detect_warp_ow in transition.c. Return 0 so caller skips cave
     * path and falls through to dungeon coordinator. */
    if (roomrom_ow_meta_is_level_selector(selector)) {
        return (cave_id_t)0;
    }

    /* Selector $00 falls through the level-selector check above
     * (`selector < $40u` is true for $00). Defensive: also reject
     * any non-cave selector explicitly. */
    if (selector < 0x40u) {
        return (cave_id_t)0;
    }

    /* Z_05.asm:7346-7353 — cave dispatch (Mode B unless $50 → Mode C).
     * Cave-id derived per the OverworldPersonTextSelectors index. */
    return (cave_id_t)roomrom_ow_meta_cave_id_from_selector(selector);
}

unsigned char cave_shortcut_destination(unsigned char source_room,
                                        unsigned char link_x,
                                        unsigned char link_y,
                                        unsigned char grid_offset)
{
    unsigned char offset, i;
    if (grid_offset != 0u || link_y != 0x9Du) return 0xFFu;
    offset = link_x == 0x50u ? 1u :
             link_x == 0x80u ? 2u :
             link_x == 0xB0u ? 3u : 0u;
    if (offset == 0u) return 0xFFu;
    for (i = 0u; i < 4u; ++i) {
        if ((unsigned char)RAM(0x6BB2u + i) == source_room)
            return (unsigned char)RAM(0x6BB2u + ((i + offset) & 3u));
    }
    return 0xFFu;
}
