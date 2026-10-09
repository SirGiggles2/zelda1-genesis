/* Task 5.4 Gate D: in-ROM metadata sanity probe.
 *
 * Built only when ROOMROM_PROBE_METADATA is defined. Asserts that the
 * OW LevelBlock accessors, level/quest manifest, and playfield-top
 * constant produce the values the spec requires for L1 entrance
 * resolution. Results published to a fixed RAM block so BizHawk Lua
 * can read them.
 *
 * Layout @ ROOMROM_PROBE_METADATA_BASE = 0xFF7300:
 *
 *   off  size  field
 *   ---  ----  ----------------------------------------------------
 *   0    1     magic 'G' (0x47)
 *   1    1     magic 'D' (0x44)
 *   2    1     assertion count (u8)
 *   3    1     reserved
 *   4    2     check[0] actual   (u16 BE)
 *   6    2     check[0] expected (u16 BE)
 *   8    2     check[1] actual
 *   10   2     check[1] expected
 *   ... etc, 4 bytes per check.
 *
 * Lua compares each pair and reports pass/fail. Total block size
 * = 4 + 4 * count bytes. Slice 1 = 7 checks = 32 bytes.
 */

#ifndef ROOMROM_PROBE_METADATA_H
#define ROOMROM_PROBE_METADATA_H

#define ROOMROM_PROBE_METADATA_BASE       0x00FF7300UL
#define ROOMROM_PROBE_METADATA_BLOCK_SIZE 48u   /* 4 + 11*4 */
#define ROOMROM_PROBE_METADATA_COUNT      11u

void roomrom_probe_metadata_run(void);

#endif /* ROOMROM_PROBE_METADATA_H */
