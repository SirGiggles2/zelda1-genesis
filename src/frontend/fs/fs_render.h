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
void fs_render_slot(uint8_t slot_idx);       /* Link sprite (bright/dim), name, hearts */
void fs_render_all_slots(void);
void fs_render_cursor(uint8_t row);          /* heart sprite at row Y (rows 0..4) */

/* T-099: NES slot info text, name registration board and cursors. */
void fs_render_slot_text(uint8_t slot);
void fs_render_name_cursor(uint8_t show, uint8_t col, uint16_t row);
void fs_render_register_board(uint8_t slot);
void fs_render_misc_menu(uint8_t copy);
void fs_render_misc_cursor(uint8_t row);
void fs_render_prompt(const char *prompt);
void fs_render_copy_destination(uint8_t source);
void fs_render_confirmation(const char *prompt, uint8_t choice, uint8_t copy, uint8_t source, uint8_t destination);
void fs_render_tick(uint8_t selected, uint8_t animate);
void fs_render_rename_menu(void);
void fs_render_rename_board(uint8_t slot);
void fs_render_page(const char *title);
void fs_render_text(uint16_t row,uint8_t col,const char *text);
void fs_render_page_cursor(uint16_t row);
void fs_render_quest_stats(uint8_t slot,uint8_t quest,uint16_t row);
void fs_render_file_identity(uint8_t slot);
void fs_render_name_field(uint8_t slot, const uint8_t *name);
void fs_render_board_cursor(uint8_t idx);
uint8_t fs_board_char(uint8_t idx);
void fs_board_cell(uint8_t idx, uint8_t *col, uint16_t *row);
uint16_t fs_slot_name_row(uint8_t slot);

#endif
