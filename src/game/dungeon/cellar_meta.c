/* NES source: Z_05 CheckWarps / CheckSubroom; Variables.inc cellar array.
 * Drained C: dungeon/cellar_mode.c:cellar_try_enter/cellar_check_exit.
 * Coverage: PARTIAL (classification/source queries were limited to L1Q1).
 * Stance: EXTEND using the existing ROM record owner, for both quests. */
#include "cellar_meta.h"
#include "../world/level_info_install.h"

unsigned char roomrom_uw_cellar_for_source(unsigned char level,
                                           unsigned char quest,
                                           unsigned char room_id,
                                           unsigned char *out_cellar)
{
    const unsigned char *attrs = level_info_uw_attributes(level, quest);
    const unsigned char *cellars = level_info_uw_cellars(level, quest);
    unsigned char i, cellar;
    if (out_cellar == 0 || attrs == 0 || room_id >= 128u) return 0u;
    for (i = 0u; i < LEVEL_INFO_UW_CELLAR_COUNT; ++i) {
        cellar = cellars[i];
        if (cellar < 128u && (attrs[cellar] == room_id || attrs[0x80u + cellar] == room_id)) {
            *out_cellar = cellar; return 1u;
        }
    }
    return 0u;
}

unsigned char roomrom_uw_cellar_source_for_cellar(unsigned char level,
                                                  unsigned char quest,
                                                  unsigned char cellar_id,
                                                  unsigned char *out_source)
{
    const unsigned char *attrs = level_info_uw_attributes(level, quest);
    if (out_source == 0 || attrs == 0 || !roomrom_uw_room_is_cellar(level, quest, cellar_id)) return 0u;
    /* Diagnostic canonical source A. Real CheckSubroom exit selects A/B by
     * Link X; never collapse a connecting passage into this single result. */
    *out_source = attrs[cellar_id];
    return 1u;
}

unsigned char roomrom_uw_room_is_cellar(unsigned char level,
                                        unsigned char quest,
                                        unsigned char room_id)
{
    const unsigned char *cellars = level_info_uw_cellars(level, quest);
    unsigned char i;
    if (cellars == 0 || room_id >= 128u) return 0u;
    for (i = 0u; i < LEVEL_INFO_UW_CELLAR_COUNT; ++i)
        if (cellars[i] == room_id) return 1u;
    return 0u;
}
