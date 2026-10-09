/* src/fs_render.h
 *
 * VDP primitives for File Select. Each function writes plane bytes
 * via render_* helpers from render_abi.h (S1.F4: vdp_* removed).
 */
#ifndef FS_RENDER_H
#define FS_RENDER_H

#include <stdint.h>

void fs_render_clear_screen(void);
void fs_render_static_layout(void);          /* border, "-SELECT-", NAME/LIFE headers,
                                                COPY/ERASE row labels (NES capture) */
void fs_render_extra_rows(void);             /* v3 Redux: PLAYERS + OPTIONS labels */
void fs_render_slot(uint8_t slot_idx);       /* Link sprite (bright/dim), name, hearts */
void fs_render_all_slots(void);
void fs_render_cursor(uint8_t row);          /* heart sprite at row Y (rows 0..6) */
void fs_render_players_row(uint8_t value);   /* PLAYERS digit cell with current 1..4 */

/* T-099: NES slot info text, name registration board and cursors. */
void fs_render_slot_text(uint8_t slot);
void fs_render_name_cursor(uint8_t show, uint8_t col, uint16_t row);
void fs_render_register_board(void);
void fs_render_name_field(uint8_t slot, const uint8_t *name);
void fs_render_board_cursor(uint8_t idx);
uint8_t fs_board_char(uint8_t idx);
void fs_board_cell(uint8_t idx, uint8_t *col, uint16_t *row);
uint16_t fs_slot_name_row(uint8_t slot);

#endif
