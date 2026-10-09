#ifndef ROOMROM_UW_ROOM_BLOB_H
#define ROOMROM_UW_ROOM_BLOB_H

#define ROOMROM_UW_BLOB_ROWS 22u
#define ROOMROM_UW_BLOB_COLS 32u

extern const unsigned short g_uw_room_count;
extern const unsigned short g_uw_room_lookup[2][3][10][128];
extern const unsigned char  g_uw_room_index[][4];   /* {map_id, quest, level, room_id} */
extern const unsigned char  g_uw_room_nt[][22 * 32];
extern const unsigned char  g_uw_room_attr[][64];
extern const unsigned char  g_uw_room_palette[][32];

#endif
