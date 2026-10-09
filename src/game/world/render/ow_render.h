#ifndef ROOMROM_OW_ROOM_RENDER_H
#define ROOMROM_OW_ROOM_RENDER_H

#define ROOMROM_MAP_ORIGINAL 0u
#define ROOMROM_MAP_REDUX    1u
#define ROOMROM_ROOM_COLS    32u
#define ROOMROM_ROOM_ROWS    22u
#define ROOMROM_HUD_ROWS     7u
#define ROOMROM_ROOM_FIRST_ROW ROOMROM_HUD_ROWS
#define ROOMROM_PLANE_ROWS   64u

void roomrom_ow_room_render_set_map(unsigned char map_id);
unsigned char roomrom_ow_room_render_get_map(void);
void roomrom_ow_room_render_load_palette(unsigned char room_id);
void roomrom_ow_room_render_upload_chr(void);
void roomrom_ow_room_render_fill_plane_a(unsigned char room_id);

/* Task #44 — NES cave-interior render (Z_05.asm InitModeB cave-room paint).
 *
 * Caves are NOT OW rooms; the NES Mode B InitModeB pipeline calls
 * LayoutCaveAndAvanceSubmode (Z_05.asm:6020) which overrides the OW
 * column-directory pointer with RoomLayoutOWCave0 (regular) or
 * RoomLayoutOWCave1 (shortcut). Palette pattern still comes from OW
 * room $44 (Z_05.asm:6628 InitModeB_Sub5_FillTileAttrsAndTransferTopHalf
 * comment "An OW room that has the same NT attributes as a cave.").
 *
 * Before this entry point existed, SCENE_CAVE rendered OW room cave_id
 * directly (lake/road tiles visible inside cave). This function plumbs
 * the cave column override + correct attrs source so the cave interior
 * matches NES Z1. cave_id determines regular vs shortcut layout:
 *   0x6A..0x7A → regular cave (k_cave_layout_regular)
 *   0x7B+     → shortcut cave (k_cave_layout_shortcut)
 *
 * Same raw-tile cache + stable-flag semantics as fill_plane_a. */
void roomrom_cave_room_render_fill_plane_a(unsigned char cave_id);
/* T-135: compute up to max_cols columns of OW room room_id ahead of a
 * load (RAM only); the room's next render uses them. Drop after the load. */
/* T-172: layout-summary cache (tile object, shortcut). */
void roomrom_ow_room_render_layout_drop(void);
void roomrom_ow_room_render_prepare_layout(unsigned char room_id);
void roomrom_ow_room_render_prepare(unsigned char room_id, unsigned char max_cols);
void roomrom_ow_room_render_prepare_drop(void);
/* T-172: fill the whole room from a complete precompute (plane metatile
 * columns dst_col0..+15); 0 = not available, fill per column. */
unsigned char roomrom_ow_room_render_fill_room_prepared(unsigned char room_id,
                                                        unsigned char dst_col0,
                                                        unsigned char dst_row_base);
/* T-134: compute up to max_cols cave metatile columns ahead of the swap
 * (RAM only, nothing drawn). */
void roomrom_cave_room_render_prepare(unsigned char cave_id,
                                      unsigned char max_cols);

/* S5 collision: returns non-zero if the metatile at (col, row) in the
 * current OW room is walkable. col 0..15, row 0..10. Out-of-bounds = 0. */
unsigned char roomrom_ow_room_render_walkable_at(unsigned char col,
                                                 unsigned char row);

/* T6 OW collision (NES tile-granularity): returns non-zero if the
 * BG TILE at (tile_col, tile_row) is walkable per NES Z1
 * GetCollidableTile WalkableTiles list. tile_col 0..31, tile_row 0..21.
 * Out-of-bounds = 0. Used by link_walkable_at to match NES sub-metatile
 * collision (the metatile-granularity walkable map blocks legal paths
 * like the $67->$57 north exit gap). */
unsigned char roomrom_ow_room_render_walkable_tile_at(unsigned char tile_col,
                                                      unsigned char tile_row);

/* Task 5.4: raw NES BG tile id for the active 32x22 playfield, populated
 * during fill_plane_a. tile_col 0..31, tile_row 0..21. Out-of-bounds = 0.
 *
 * SLICE-1 SEMANTICS:
 *   - Reflects the LAST full fill_plane_a render of the currently held
 *     room. Single-column scroll fills do NOT update cache contents and
 *     mark the cache unstable.
 *   - Tile mutations from secrets, bombed rocks, burnt bushes, pushed
 *     blocks are NOT republished here; future ticket adds the hook.
 *   - Callers that drive game-flow decisions (e.g. warp detection) must
 *     gate on roomrom_ow_room_render_is_stable() returning 1.
 */
unsigned char roomrom_ow_room_render_raw_tile_at(unsigned char tile_col,
                                                 unsigned char tile_row);

/* Returns 1 iff the raw-tile cache reflects a full fill_plane_a since the
 * last partial column write. Coordinator must check this before consuming
 * raw_tile_at output for warp-tile detection. */
unsigned char roomrom_ow_room_render_is_stable(void);

/* Task 5.4: explicit bracket for callers using fill_one_col_at in a
 * 16-column loop to paint the active slot (e.g. load_room). Wrap the
 * loop:
 *
 *   roomrom_ow_room_render_begin_full_fill();
 *   for (c = 0; c < 16; c++) roomrom_ow_room_render_fill_one_col_at(...);
 *   roomrom_ow_room_render_mark_stable();
 *
 * Skipping the bracket leaves the cache unstable; callers that paint
 * scroll-staging slots (where Link is NOT yet) must NOT mark stable. */
void roomrom_ow_room_render_begin_full_fill(void);
void roomrom_ow_room_render_mark_stable(void);

/* Task 5.4: copy the 32x22 raw-tile cache to RAM probe block at
 * 0xFF7400 ('TC' magic). Called by the debug-tick state-mirror
 * publisher so BizHawk Lua can scan the cache for warp-tile positions
 * without a per-cell accessor call. */
void roomrom_ow_room_render_publish_cache(void);

/* T0.1 cave entry: mirror the 32x22 raw-tile cache into nes_ram
 * PlayAreaTiles ($6530 + col*0x16 + row). Required so the existing
 * GetCollidableTile drain (collision_get_collidable_tile_still) returns
 * real tile IDs instead of zeros; without this, HandleWarpOW (which
 * gates on tile == $24/$88/$70-$73 for cave/stairs entry) never fires
 * and the player cannot enter caves. Call AFTER mark_stable when the
 * cache reflects the active slot. */
void roomrom_ow_room_render_publish_play_area_tiles(void);

/* S6.5 scroll: render one metatile column of room_id at plane metatile
 * column dst_col (0..15). src_col selects the source room's col layout
 * (palette uses src position so attributes match the source room). Plane
 * cols wrap mod 32 via SGDK. Used by the scroll state machine to overwrite
 * "just-left-visible" plane cols with incoming-room data. */
void roomrom_ow_room_render_fill_one_col(unsigned char room_id,
                                         unsigned char src_col,
                                         unsigned char dst_col);
void roomrom_ow_room_render_fill_one_col_at(unsigned char room_id,
                                            unsigned char src_col,
                                            unsigned char dst_col,
                                            unsigned char dst_row_base);

/* PR-2 V scroll staging: select target plane for nametable writes.
 * 0 = BG_A (default, current room slot). 1 = BG_B (incoming room
 * staging during V scroll). Caller must reset to 0 after staging. */
void roomrom_ow_room_render_set_target_plane(unsigned char plane);

/* T-050: tile object recorded by NES LayoutRoomOW / CheckTileObject for
 * an OW room (type $62-$67, X = col*$10, Y = row*$10 + $40; type 0 if
 * none), after the secret ($80) substitution and CheckShortcut. Reads the
 * live world flags. */
void roomrom_ow_room_tile_object(unsigned char room_id, unsigned char *type,
                                 unsigned char *x, unsigned char *y);

/* T-050: write one NES BG tile of the active room (the last full fill
 * marked stable) at play-area tile (col 0..31, row 0..21): plane cell,
 * raw-tile cache and walkability. Returns 0 if no room is active. */
unsigned char roomrom_ow_room_render_set_tile(unsigned char tile_col,
                                              unsigned char tile_row,
                                              unsigned char raw_tile);

void roomrom_cellar_render(void);
unsigned short roomrom_cellar_tile_word(unsigned char tile);

#endif
