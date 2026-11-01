/* Historical 128-room NES OW sprite sub-pal 3 scan. This snapshot is not
 * linked into the game: the forced-warp settle used by the original probe
 * disagrees with live room-entry palette selection (see T-138/T-159).
 * Retained only so the old capture can be inspected and reproduced. */
#ifndef ROOMROM_OW_SUBPAL3_TABLE_H
#define ROOMROM_OW_SUBPAL3_TABLE_H

extern const unsigned char k_ow_subpal3_per_room[128][4];

#endif /* ROOMROM_OW_SUBPAL3_TABLE_H */
