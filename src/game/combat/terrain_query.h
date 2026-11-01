/* NES source: Z_07.asm:GetCollidableTile/GetCollidingTileMoving.
 * Drained C: collision_dispatch.c terrain query.
 * Coverage: native coordinate/terrain decision; world storage still bridged.
 * Stance: ADOPT existing byte-wrap, adjacent-column and OW special rules. */
#ifndef TERRAIN_QUERY_H
#define TERRAIN_QUERY_H
/* The drained byte-wrap query can spill past the visible704 cells through
 * offset735. Supply the existing736-byte window until native room ownership
 * migrates; do not clamp/wrap the row to the visible22-row geometry. */
typedef struct {
    const volatile unsigned char *columns;
    unsigned char level, room;
} TerrainQueryView;
typedef struct { unsigned char tile, column; } TerrainTileResult;
TerrainTileResult terrain_query_tile(const TerrainQueryView *world,
    unsigned char x, unsigned char y, unsigned char direction_bits,
    unsigned char hotspot_offset, unsigned char is_player);
#endif
