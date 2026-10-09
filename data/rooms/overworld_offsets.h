/* Auto-generated layout constants for data/rooms/overworld.c.
 *
 * The blob `rooms_overworld[]` concatenates the OW LevelBlock attribute
 * sub-tables, OW LevelInfo, common bank-6 data, OW room layouts, cave
 * layouts, and the OW column-heap blob. See tools/extract_rooms.py
 * `build_overworld_blob()` for the canonical writer.
 *
 * The constants below pin the offsets readers depend on. They are the
 * single source of truth: tools (Python) and runtime (C) both consume
 * this header so a future blob layout change updates one place.
 *
 * Verified 2026-05-06 via Python:
 *   blob[ROOMROM_OW_LEVELBLOCK_ATTRS_B_OFFSET + 0x37] == 0x07
 *   (Level 1 entrance: attr_b $07 -> selector $04 -> level 1)
 */

#ifndef ROOMROM_OW_OVERWORLD_OFFSETS_H
#define ROOMROM_OW_OVERWORLD_OFFSETS_H

/* OW LevelBlock starts at the beginning of the blob and is 768 bytes
 * (6 sub-tables x 128 rooms). Each sub-table is 128 bytes. */
#define ROOMROM_OW_LEVELBLOCK_BASE              0x000u
#define ROOMROM_OW_LEVELBLOCK_SUBTABLE_BYTES    0x080u
#define ROOMROM_OW_ROOM_COUNT                   0x080u   /* 128 rooms */

#define ROOMROM_OW_LEVELBLOCK_ATTRS_A_OFFSET    (ROOMROM_OW_LEVELBLOCK_BASE + 0x000u)
#define ROOMROM_OW_LEVELBLOCK_ATTRS_B_OFFSET    (ROOMROM_OW_LEVELBLOCK_BASE + 0x080u)
#define ROOMROM_OW_LEVELBLOCK_ATTRS_C_OFFSET    (ROOMROM_OW_LEVELBLOCK_BASE + 0x100u)
#define ROOMROM_OW_LEVELBLOCK_ATTRS_D_OFFSET    (ROOMROM_OW_LEVELBLOCK_BASE + 0x180u)
#define ROOMROM_OW_LEVELBLOCK_ATTRS_E_OFFSET    (ROOMROM_OW_LEVELBLOCK_BASE + 0x200u)
#define ROOMROM_OW_LEVELBLOCK_ATTRS_F_OFFSET    (ROOMROM_OW_LEVELBLOCK_BASE + 0x280u)

#define ROOMROM_OW_LEVELBLOCK_TOTAL_BYTES       (6u * ROOMROM_OW_LEVELBLOCK_SUBTABLE_BYTES)

#endif /* ROOMROM_OW_OVERWORLD_OFFSETS_H */
