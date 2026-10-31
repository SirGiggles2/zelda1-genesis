/* options_menu_model.h — MD Remix options menu rows, values and edits.
 *
 * NES source: NONE (user-approved custom MD Remix menu).
 * Drained C: fs_options.c / fs_options_render.c row tables and edit rules,
 * moved here unchanged so the File Select and the in-game pause (T-256)
 * share one menu model. Each caller keeps its own renderer and font.
 * Stance: EXTEND. Row identity is independent of the wire-stable OptionId.
 */
#ifndef SRC_GAME_OPTIONS_OPTIONS_MENU_MODEL_H
#define SRC_GAME_OPTIONS_OPTIONS_MENU_MODEL_H

#define OPTIONS_MENU_ROW_COUNT   10u
#define OPTIONS_MENU_SAVE_ROW    9u
#define OPTIONS_MENU_CORE_ROW    0u
#define OPTIONS_MENU_MUSIC_ROW   8u
#define OPTIONS_MUSIC_BACK_ROW   11u
#define OPTIONS_MUSIC_ROW_COUNT  12u

/* OPTION_ID_COUNT for informational/action rows. */
unsigned int options_menu_id_for_row(unsigned char row);
const char *options_menu_label(unsigned char row);
const char *options_menu_value(unsigned char row);
const char *options_menu_help(unsigned char row, unsigned char line);
/* Step the row's option one value left (dir 0) or right (dir 1). */
void options_menu_step(unsigned char row, unsigned char right);

/* Music page: row0 overworld, row1 all dungeons, rows2..10 dungeon1..9,
 * row11 back. Label is written into buf (at least 13 bytes). */
const char *options_music_label(unsigned char row, char *buf);
const char *options_music_value(unsigned char row);
/* Left/Right/A on a music row; returns 1 when every dungeon row changed. */
unsigned char options_music_step(unsigned char row, unsigned char left);
/* Three description lines for the cursor row. */
const char *options_music_help(unsigned char cursor, unsigned char line);

#endif /* SRC_GAME_OPTIONS_OPTIONS_MENU_MODEL_H */
