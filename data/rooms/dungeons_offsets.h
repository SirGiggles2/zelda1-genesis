/* Auto-generated layout constants for data/rooms/dungeons.c.
 *
 * The blob `rooms_dungeons[]` concatenates UW LevelBlock attribute
 * tables, UW LevelInfo blocks, room layouts, cellar layouts, column
 * heaps, and Q2 patch tables. See tools/extract_rooms.py
 * `build_dungeons_blob()` for the canonical writer.
 *
 * Per-block sizes:
 *   LevelBlockUW{1,2}Q{1,2}: 768 bytes (6 attr sub-tables × 128 rooms)
 *   LevelInfoUW{1..9}:       256 bytes
 *
 * Block order (Task 5.6 needs only first 13):
 *   0  LevelBlockUW1Q1   offset $0000 size $0300
 *   1  LevelBlockUW2Q1   offset $0300 size $0300
 *   2  LevelBlockUW1Q2   offset $0600 size $0300
 *   3  LevelBlockUW2Q2   offset $0900 size $0300
 *   4  LevelInfoUW1      offset $0C00 size $0100
 *   5  LevelInfoUW2      offset $0D00 size $0100
 *   ...
 *   12 LevelInfoUW9      offset $1400 size $0100
 *
 * AttrsA / AttrsB / AttrsD / etc. sub-tables within each LevelBlock
 * are 128 bytes each, indexed by room id (0..127).
 *
 * FoeCounts is the anchor inside each LevelInfo block (NES-Z1
 * convention) — see ROOMROM_UW_LEVELINFO_FOE_COUNTS_OFFSET[level-1]
 * below for the per-level offset within each LI block.
 *
 * Verified 2026-05-07 via Python (build/probes/ph5/task_5_6/nes_ground_truth.md):
 *   L1 cellar array = [$7F]; AttrsA[$7F] = AttrsB[$7F] = $22
 */

#ifndef ROOMROM_DUNGEONS_OFFSETS_H
#define ROOMROM_DUNGEONS_OFFSETS_H

/* LevelBlock size + sub-table layout (NES Z_05.asm:FindDoorAttrByDoorBit
 * + family). 6 sub-tables of 128 bytes each. */
#define ROOMROM_UW_LEVELBLOCK_BYTES         0x300u   /* 768 */
#define ROOMROM_UW_LEVELBLOCK_SUBTABLE_BYTES 0x80u   /* 128 */
#define ROOMROM_UW_LEVELBLOCK_ATTRS_A_REL   0x000u
#define ROOMROM_UW_LEVELBLOCK_ATTRS_B_REL   0x080u
#define ROOMROM_UW_LEVELBLOCK_ATTRS_C_REL   0x100u
#define ROOMROM_UW_LEVELBLOCK_ATTRS_D_REL   0x180u
#define ROOMROM_UW_LEVELBLOCK_ATTRS_E_REL   0x200u
#define ROOMROM_UW_LEVELBLOCK_ATTRS_F_REL   0x280u

/* Block-base offsets within rooms_dungeons[]. */
#define ROOMROM_UW_LEVELBLOCK_UW1Q1_BASE    0x0000u
#define ROOMROM_UW_LEVELBLOCK_UW2Q1_BASE    0x0300u
#define ROOMROM_UW_LEVELBLOCK_UW1Q2_BASE    0x0600u
#define ROOMROM_UW_LEVELBLOCK_UW2Q2_BASE    0x0900u

#define ROOMROM_UW_LEVELINFO_BASE           0x0C00u

/* Q2 LevelInfo patch tables (Z_06.asm UpdateMode2Load_Full). Replacement
 * arrays 1..9 are contiguous (as in ROM) starting at REPL_BASE; the 18-byte
 * LevelInfoUWQ2ReplacementAddrs table follows array 9 so the NES's
 * Sizes[L]+1-byte copy never leaves the blob. Offsets match
 * data/rooms/MANIFEST.json (checked by tools/audit/test_q2_levelinfo.py). */
#define ROOMROM_UW_Q2_LI_REPL_SIZES_OFF     0x1930u  /* 6448, 9 bytes */
#define ROOMROM_UW_Q2_LI_REPL_BASE_OFF      0x1939u  /* 6457 */
#define ROOMROM_UW_Q2_LI_PATCH_DEST         0x6BA7u  /* LevelInfo_ShortcutOrItemPosArray */
/* Q2 overworld room patch tables (Z_06.asm @PatchQ2Rooms), ROM-extracted
 * into rooms_dungeons; offsets match data/rooms/MANIFEST.json. */
#define ROOMROM_OW_Q2_ATTRB_REPL_OFFSETS_OFF 0x1920u /* 6432, 8 bytes */
#define ROOMROM_OW_Q2_ATTRB_REPL_VALUES_OFF  0x1928u /* 6440, 8 bytes */
#define ROOMROM_UW_LEVELINFO_BLOCK_BYTES    0x100u   /* 256 */

/* LevelInfo internal layout — fields are at fixed offsets from a
 * FoeCounts anchor (NES convention). FoeCounts is at fixed offset $24 in every correctly extracted
 * LevelInfo block.
 *
 * Offsets RELATIVE TO FoeCounts: */
#define ROOMROM_UW_LI_START_ROOM_REL        0x0Bu
#define ROOMROM_UW_LI_TRIFORCE_ROOM_REL     0x0Cu
#define ROOMROM_UW_LI_LEVEL_NUMBER_REL      0x0Fu
#define ROOMROM_UW_LI_CELLAR_ARRAY_REL      0x10u
#define ROOMROM_UW_LI_CELLAR_ARRAY_LEN      10u
#define ROOMROM_UW_LI_BOSS_ROOM_REL         0x1Au

/* Per-level FoeCounts offset table: all entries $24 per NES SRAM layout.
 * Indexed by (level - 1), so level 1..9 maps to indices 0..8. */
extern const unsigned char ROOMROM_UW_LEVELINFO_FOE_COUNTS_OFFSET[9];

#endif /* ROOMROM_DUNGEONS_OFFSETS_H */
