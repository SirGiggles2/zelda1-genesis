/* dyn_tile_dispatch.h — dynamic play-area tile editing primitives.
 *
 * NES sources:
 *   ChangeTileObjTiles   @ Z_07.asm:1114
 *   ChangePlayMapSquareOW @ Z_05.asm:6102
 *   WriteSquareOW        @ Z_05.asm:5942
 *
 * Used by armos secret reveals, push-block secrets, bombable walls,
 * burning brush — any object that needs to swap a 16x16 play-area
 * square AND queue the corresponding 4 nametable tile writes.
 *
 * Drain Rule D1 ADOPT (data tables) + EXTEND (NES-asm to C bodies, no
 * native drain in src/oracle/world/).
 */

#ifndef DYN_TILE_DISPATCH_H
#define DYN_TILE_DISPATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* NES ChangeTileObjTiles (Z_07.asm:1114). Cue 4 nametable tile writes
 * (2 records of 2 vertical tiles) for the object's screen position.
 * Then call change_play_map_square_ow to update the in-memory play
 * area map. tile is the first tile id (the >$46/<$F3 manipulation
 * patches the second record's bytes). */
void dyn_tile_change_tile_obj_tiles(unsigned int tile, unsigned int slot);

/* NES ChangePlayMapSquareOW (Z_05.asm:6102). Look up the play-area
 * column address by the object's X coordinate, add the row offset
 * derived from Y, then write the square. Picks type-1 vs type-3
 * square based on the primary-tile range. */
void dyn_tile_change_play_map_square_ow(unsigned int slot);

/* NES WriteSquareOW (Z_05.asm:5942). Write 4 tiles to a 16x16 square
 * via the [00:01] pointer + Y row offset. Square index in [0D] picks
 * type-1 (>= $10) or type-3 (< $10). [05] holds primary tile. */
void dyn_tile_write_square_ow(void);

#ifdef __cplusplus
}
#endif

#endif /* DYN_TILE_DISPATCH_H */
