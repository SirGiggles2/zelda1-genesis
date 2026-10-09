/* Task 5.4 Gate D in-ROM metadata probe.
 *
 * Built only when ROOMROM_PROBE_METADATA is defined. Verifies the
 * spec-mandated invariants of the OW metadata accessor + level/quest
 * manifest at boot time and publishes the (actual, expected) values
 * to ROOMROM_PROBE_METADATA_BASE for BizHawk Lua to inspect.
 *
 * Failures here = data drift in data/rooms/overworld.c or
 * RoomRom/data/uw_level1_quest1_rooms.json. The probe runs even if
 * the warp coordinator never fires, so a corrupted data manifest is
 * caught before any user input.
 */

#include "metadata_probe.h"
#include "../../../src/game/world/ow_meta.h"  /* Phase 12.2 promoted */
#include "../../../src/game/dungeon/cellar_meta.h"  /* Phase 12.2 promoted */
#include "../../data/levelinfo_start_rooms.h"
#include "../../data/uw_l1q1_cellar_pairs.h"
#include "../../../src/game/world/render/ow_render.h"  /* ROOMROM_HUD_ROWS (Phase 12.2 promoted) */

static void put_u16_be(volatile unsigned char *p, unsigned short v)
{
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)(v & 0xFFu);
}

static void put_pair(volatile unsigned char *block, unsigned char idx,
                     unsigned short actual, unsigned short expected)
{
    volatile unsigned char *slot = &block[4u + (unsigned short)idx * 4u];
    put_u16_be(slot,     actual);
    put_u16_be(slot + 2, expected);
}

void roomrom_probe_metadata_run(void)
{
    volatile unsigned char *block =
        (volatile unsigned char *)ROOMROM_PROBE_METADATA_BASE;
    unsigned char dest = 0u;
    unsigned char hit_l1q1;
    unsigned char miss_l2q1;

    block[0] = 0x47u;                          /* 'G' */
    block[1] = 0x44u;                          /* 'D' */
    block[2] = (unsigned char)ROOMROM_PROBE_METADATA_COUNT;
    block[3] = 0u;                             /* reserved */

    /* check[0]: roomrom_ow_meta_attr_b(0x37) == 0x07 */
    put_pair(block, 0,
             (unsigned short)roomrom_ow_meta_attr_b(0x37u),
             0x0007u);

    /* check[1]: roomrom_ow_meta_level_selector(0x37) == 0x04 */
    put_pair(block, 1,
             (unsigned short)roomrom_ow_meta_level_selector(0x37u),
             0x0004u);

    /* check[2]: roomrom_ow_meta_is_level_selector(0x04) == 1 */
    put_pair(block, 2,
             (unsigned short)roomrom_ow_meta_is_level_selector(0x04u),
             0x0001u);

    /* check[3]: roomrom_ow_meta_level_from_selector(0x04) == 1 */
    put_pair(block, 3,
             (unsigned short)roomrom_ow_meta_level_from_selector(0x04u),
             0x0001u);

    /* check[4]: levelinfo_start_room_for(1, 1, &dest) == 1 && dest == 0x73.
     * Encode return + dest in one u16 so a single (actual, expected) pair
     * captures both halves: hi byte = retval, lo byte = dest. */
    dest = 0u;
    hit_l1q1 = levelinfo_start_room_for(1u, 1u, &dest);
    put_pair(block, 4,
             (unsigned short)((hit_l1q1 << 8) | dest),
             0x0173u);

    /* check[5]: levelinfo_start_room_for(2, 1, &dest) == 0 (manifest miss).
     * dest must remain unchanged (the function only writes on hit). Encode
     * as hi byte = retval, lo byte = sentinel 0xAA we pre-load into dest;
     * if the function writes on miss, the sentinel disappears. */
    dest = 0xAAu;
    miss_l2q1 = levelinfo_start_room_for(2u, 1u, &dest);
    put_pair(block, 5,
             (unsigned short)((miss_l2q1 << 8) | dest),
             0x00AAu);

    /* check[6]: ROOMROM_PLAYFIELD_TOP_PX equivalent = HUD_ROWS * 8 = 56.
     * Header doesn't expose the macro; recompute from ROOMROM_HUD_ROWS
     * which is in ow_room_render_roomrom.h. */
    put_pair(block, 6,
             (unsigned short)(ROOMROM_HUD_ROWS * 8u),
             0x0038u);                          /* 56 */

    /* Task 5.6 cellar-pair assertions. Slice-1: L1Q1 source $22 ↔
     * cellar $7F (single pair). */
    /* check[7]: uw_l1q1_cellar_pairs_count == 1 */
    put_pair(block, 7,
             (unsigned short)uw_l1q1_cellar_pairs_count,
             0x0001u);

    /* check[8]: roomrom_uw_cellar_for_source(1,1,$22,&dest)==1 && dest==$7F. */
    dest = 0u;
    {
        unsigned char hit = roomrom_uw_cellar_for_source(1u, 1u, 0x22u, &dest);
        put_pair(block, 8,
                 (unsigned short)((hit << 8) | dest),
                 0x017Fu);
    }

    /* check[9]: roomrom_uw_cellar_source_for_cellar(1,1,$7F,&dest)==1 &&
     *           dest==$22. */
    dest = 0u;
    {
        unsigned char hit = roomrom_uw_cellar_source_for_cellar(1u, 1u,
                                                                0x7Fu, &dest);
        put_pair(block, 9,
                 (unsigned short)((hit << 8) | dest),
                 0x0122u);
    }

    /* check[10]: roomrom_uw_room_is_cellar(1,1,$7F)==1, !is_cellar($22). */
    {
        unsigned char yes = roomrom_uw_room_is_cellar(1u, 1u, 0x7Fu);
        unsigned char no  = roomrom_uw_room_is_cellar(1u, 1u, 0x22u);
        put_pair(block, 10,
                 (unsigned short)((yes << 8) | no),
                 0x0100u);
    }
}
