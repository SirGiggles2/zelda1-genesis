#ifndef ROOMROM_UW_ROOM_RENDER_H
#define ROOMROM_UW_ROOM_RENDER_H

#include "../world/render/ow_render.h"  /* Phase 12.2 promoted */

#define ROOMROM_UW_LEVEL_MIN 1u
#define ROOMROM_UW_LEVEL_MAX 9u
#define ROOMROM_UW_QUEST_MIN 1u
#define ROOMROM_UW_QUEST_MAX 2u

void roomrom_uw_room_render_set_map(unsigned char map_id);
unsigned char roomrom_uw_room_render_get_map(void);

void roomrom_uw_room_render_set_level(unsigned char level);
unsigned char roomrom_uw_room_render_get_level(void);

void roomrom_uw_room_render_set_quest(unsigned char quest);
unsigned char roomrom_uw_room_render_get_quest(void);

void roomrom_uw_room_render_load_palette(unsigned char room_id);
void roomrom_uw_room_render_upload_chr(void);
void roomrom_uw_room_render_fill_plane_a(unsigned char room_id);

/* Task 5.8: paint plane A play area cells (32x22) with a black-tile
 * VDP word (tile $00 + PAL0). HUD on WINDOW is unaffected. Used by
 * load_room when (is_dark && !lit). Walkability cache is left
 * untouched so Link movement isn't altered visually-only. */
void roomrom_uw_room_render_fill_plane_a_dark(void);

/* S6.5 scroll: render one metatile col (2 plane cols) of room_id from blob
 * at plane metatile col dst_col. */
void roomrom_uw_room_render_fill_one_col(unsigned char room_id,
                                         unsigned char src_col,
                                         unsigned char dst_col);
/* T-172: precompute up to max_cols metatile columns of room_id's tile
 * words into buf (22 x 32 words, plane-column order) for a later fill;
 * drop before buf is reused. */
void roomrom_uw_room_render_prepare(unsigned char room_id, unsigned short *buf,
                                    unsigned char max_cols);
void roomrom_uw_room_render_prepare_drop(void);
/* T-172: fill the whole room from a complete, valid precompute (plane
 * metatile columns dst_col0..+15); 0 = not available, fill per column. */
unsigned char roomrom_uw_room_render_fill_room_prepared(unsigned char room_id,
                                                        unsigned char dst_col0,
                                                        unsigned char dst_row_base);
/* 1 when room_id has a blob layout (a fill writes every play-area cell). */
unsigned char roomrom_uw_room_render_has_layout(unsigned char room_id);
void roomrom_uw_room_render_fill_one_col_at(unsigned char room_id,
                                            unsigned char src_col,
                                            unsigned char dst_col,
                                            unsigned char dst_row_base);

/* Vertical scroll HUD fix: door-art cells are high-priority BG tiles during
 * normal gameplay so Link can pass behind arches. While a vertical room slide
 * is active Link is hidden and the fixed HUD underlay must cover those cells.
 * Toggle only live nametable words whose raw tile ID is known door art. */
void roomrom_uw_room_render_set_live_door_priority(unsigned char slot_x,
                                                   unsigned char row_base,
                                                   unsigned char enabled);

/* S5.5 collision: returns non-zero if metatile (col, row) of the current
 * UW room is walkable. col 0..15, row 0..10. Out-of-bounds = 0. */
unsigned char roomrom_uw_room_render_walkable_at(unsigned char col,
                                                 unsigned char row);

/* NES-granular UW collision: returns non-zero if 8px tile (col, row) of the
 * current UW play area is walkable. col 0..31, row 0..21. Out-of-bounds = 0. */
unsigned char roomrom_uw_room_render_walkable_tile_at(unsigned char col,
                                                      unsigned char row);

/* Task 5.6: read raw NES BG tile id from blob NT entry for a specific
 * (level, room_id) lookup. Used by warp coordinator's UW-stair branch
 * (rule 5: raw_tile ∈ {$70..$73}). Returns 0 if blob entry missing or
 * out of bounds. col 0..31, row 0..21. */
unsigned char roomrom_uw_room_render_raw_tile_at_room(unsigned char level,
                                                      unsigned char quest,
                                                      unsigned char room_id,
                                                      unsigned char col,
                                                      unsigned char row);

/* Ph5.3 door-state layer: write tile, override walkability, query AT palette.
 * Call these AFTER fill_plane_a / fill_one_col_at (s_cur_attr must be set). */
void roomrom_uw_room_render_write_tile(unsigned char col, unsigned char row,
                                       unsigned char raw_tile, unsigned char pal);
void roomrom_uw_room_render_write_tile_pat(unsigned char col, unsigned char row,
                                           unsigned char raw_tile);
void roomrom_uw_room_render_write_tile_nt(unsigned char col, unsigned char row,
                                          unsigned char raw_tile, unsigned char pal);
void roomrom_uw_room_render_refresh_square(unsigned char col, unsigned char row);
void roomrom_uw_room_render_set_walkable(unsigned char col, unsigned char row,
                                         unsigned char val);
void roomrom_uw_room_render_set_walkable_tile(unsigned char col,
                                              unsigned char row,
                                              unsigned char val);
unsigned char roomrom_uw_room_render_palette_at(unsigned char nt_col,
                                                unsigned char nt_row);

/* PR-2 V scroll staging: select target plane for nametable writes.
 * 0 = BG_A (default, current room slot). 1 = BG_B (incoming room
 * staging during V scroll). Caller must reset to 0 after staging. */
void roomrom_uw_room_render_set_target_plane(unsigned char plane);

/* Task 5.5 debug: copy 32x22 BG-tile walkability cache to a fixed RAM
 * block for probe inspection. Layout:
 *   off 0,1: magic 'U','W'
 *   off 2: cols=32
 *   off 3: rows=22
 *   off 4..707: column-major cache[col*22 + row], 1=walkable, 0=blocked. */
#define ROOMROM_DEBUG_UW_WALKABLE_BASE  0x00FF7800UL
#define ROOMROM_DEBUG_UW_WALKABLE_BYTES 708u

void roomrom_uw_room_render_publish_walkable(void);

#endif
