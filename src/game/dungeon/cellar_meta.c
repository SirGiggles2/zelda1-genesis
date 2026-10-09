/* UW cellar pair lookup implementation.
 *
 * NES source authority: reference/aldonunez/Z_05.asm:7253-7301.
 * Slice-1 (Task 5.6) reads from generated uw_l1q1_cellar_pairs table.
 */

#include "cellar_meta.h"
#include "../../../RoomRom/data/uw_l1q1_cellar_pairs.h"

unsigned char roomrom_uw_cellar_for_source(unsigned char level,
                                           unsigned char quest,
                                           unsigned char room_id,
                                           unsigned char *out_cellar)
{
    unsigned char cellar;
    if (out_cellar == 0) return 0u;
    if (level >= 10u || quest >= 3u || room_id >= 128u) return 0u;
    cellar = uw_cellar_for_source_lookup[level][quest][room_id];
    if (cellar == 0xFFu) return 0u;
    *out_cellar = cellar;
    return 1u;
}

unsigned char roomrom_uw_cellar_source_for_cellar(unsigned char level,
                                                  unsigned char quest,
                                                  unsigned char cellar_id,
                                                  unsigned char *out_source)
{
    unsigned char source;
    if (out_source == 0) return 0u;
    if (level >= 10u || quest >= 3u || cellar_id >= 128u) return 0u;
    source = uw_cellar_source_for_cellar_lookup[level][quest][cellar_id];
    if (source == 0xFFu) return 0u;
    *out_source = source;
    return 1u;
}

unsigned char roomrom_uw_room_is_cellar(unsigned char level,
                                        unsigned char quest,
                                        unsigned char room_id)
{
    if (level >= 10u || quest >= 3u || room_id >= 128u) return 0u;
    return uw_room_is_cellar_lookup[level][quest][room_id] ? 1u : 0u;
}
