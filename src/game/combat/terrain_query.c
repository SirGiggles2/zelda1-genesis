/* NES source: Z_07.asm:GetCollidableTile/GetCollidingTileMoving.
 * Drained C: collision_dispatch.c collidable_tile (pre-migration76bb523bc).
 * Coverage: native coordinate/terrain query, preserving byte wrap and spill.
 * Stance: ADOPT verified terrain body; no NES RAM or global-player dependency. */
#include "terrain_query.h"
static const unsigned char k_walkable_tile[256] = {
    [0x8Du] = 1u, [0x91u] = 1u, [0x9Cu] = 1u, [0xACu] = 1u, [0xADu] = 1u,
    [0xCCu] = 1u, [0xD2u] = 1u, [0xD5u] = 1u, [0xDFu] = 1u
};
TerrainTileResult terrain_query_tile(const TerrainQueryView *world,
    unsigned char x, unsigned char y, unsigned char dir,
    unsigned char hotspot, unsigned char is_player)
{
    TerrainTileResult result;
    unsigned char adjusted_y = (unsigned char)(y + 0x0Bu);
    unsigned char tile_y = adjusted_y, tile_x = x;
    unsigned char row, tile;
    unsigned short offset;
    if (dir & 0x0Cu) {
        if ((dir & 4u) == 0u || adjusted_y < 0xDDu)
            tile_y = (unsigned char)(adjusted_y + hotspot);
    } else if (dir & 1u) {
        if (tile_x < 0xF0u) tile_x = (unsigned char)(tile_x + hotspot);
    } else if (tile_x >= 0x10u) {
        tile_x = (unsigned char)(tile_x + hotspot);
    }
    result.column = (unsigned char)((tile_x & 0xF8u) >> 3);
    row = (unsigned char)((unsigned char)(tile_y - 0x40u) >> 3);
    offset = (unsigned short)(result.column * 22u + row);
    tile = world->columns[offset];
    if (dir & 0x0Cu) {
        unsigned char next = world->columns[offset + 22u];
        if (next >= tile) tile = next;
    }
    if (world->level == 0u) {
        if (k_walkable_tile[tile]) tile = 0x26u;
        if (is_player && world->room == 0x1Fu && (dir & 0x0Cu) &&
            x == 0x80u && y < 0x56u)
            tile = 0x26u;
    }
    result.tile = tile;
    return result;
}
