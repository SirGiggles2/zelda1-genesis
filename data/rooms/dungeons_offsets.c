/* NES LevelInfo copied from its ROM pointer: FoeCounts is always
 * $6BA2-$6B7E = $24. Decreasing offsets compensated for mis-extraction. */
#include "dungeons_offsets.h"
const unsigned char ROOMROM_UW_LEVELINFO_FOE_COUNTS_OFFSET[9] = {
    0x24u, 0x24u, 0x24u, 0x24u, 0x24u, 0x24u, 0x24u, 0x24u, 0x24u
};
