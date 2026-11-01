/* pause_actions.h — T-256 Remix pause actions: OPTIONS, MAP, SAVE,
 * SAVE AND QUIT, in the left block the NES "USE B BUTTON" box used.
 *
 * NES source: NONE for the actions (Redux/Genesis extension). The save
 * reuses Z_02.asm UpdateModeDSave (mode_save.c / save_game_save_current)
 * and Redux menus/save_hearts.asm via the mode 8 commit path.
 * Drained C: mode_continue_question.c (Save selection), save_game.c,
 * options_menu_model.c (File Select options rows), hud discovery map art.
 * Coverage: PARTIAL (presentation is new; save bytes come from the
 * existing owners). Stance: EXTEND, Remix-only; Original pause unchanged.
 */
#ifndef PAUSE_ACTIONS_H
#define PAUSE_ACTIONS_H

#define PAUSE_ACTION_OPTIONS 0u
#define PAUSE_ACTION_MAP     1u
#define PAUSE_ACTION_SAVE    2u
#define PAUSE_ACTION_QUIT    3u
#define PAUSE_ACTION_COUNT   4u

/* NES name-table rows/column of the list (Remix sheet, left block). */
#define PAUSE_ACTIONS_FIRST_ROW 13u
#define PAUSE_ACTIONS_LAST_ROW  19u
#define PAUSE_ACTIONS_COL       2u
#define PAUSE_ACTIONS_COLS      15u   /* cols 0..14; col15 is the item box */

/* Each pause open: focus on the item grid, no page, no message. */
void pause_actions_open(void);

/* Cell word for NES row/col of the list block, or 0 when the caller keeps
 * its blank cell (row outside the list or column past the text). */
unsigned short pause_actions_cell(unsigned char nes_row, unsigned char col);

/* 1 while the list (not the item grid) has focus. */
unsigned char pause_actions_list_focused(void);
/* 1 while a full page (options, music, map) covers the sheet. */
unsigned char pause_actions_page_open(void);

/* Item grid Left at its edge: move focus into the list. row 0/1 = the
 * grid row the cursor left, choosing the nearest list entry. */
void pause_actions_focus_from_grid(unsigned char grid_row);

#define PAUSE_ACTIONS_STAY    0u
#define PAUSE_ACTIONS_TO_GRID 1u
/* One active-menu tick while the list or a page has focus. joy = held
 * buttons (SGDK low byte), pressed = new edges this tick. */
unsigned char pause_actions_tick(unsigned char joy, unsigned char pressed);

/* 1 once after SAVE AND QUIT was confirmed (caller leaves the pause and
 * starts the existing GameMode $0D save). */
unsigned char pause_actions_take_quit(void);

/* Grid row (0/1) the list should return to on Right. */
unsigned char pause_actions_return_row(void);

#endif /* PAUSE_ACTIONS_H */
