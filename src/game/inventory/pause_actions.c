/* pause_actions.c — see pause_actions.h.
 *
 * Layout (user 2026-10-29): the Remix sheet no longer needs the NES
 * "USE B BUTTON" box (B/C show on the status bar), so its left block,
 * NES rows 13..19 cols 0..14, holds a four-entry list:
 *   row13 OPTIONS, row15 MAP, row17 SAVE, row19 SAVE AND QUIT.
 * Left from the item grid's left edge enters the list; Right or B returns.
 * The focused entry is red (sheet sub-pal 1), the rest white (sub-pal 0).
 * A activates. OPTIONS and MAP cover the sheet with a page; Start does not
 * close the pause while a page is open (inventory_subscreen_menu_active).
 */
#include "pause_actions.h"
#include "pause_map.h"
#include "inventory_pause_internal.h"
#include "platform_abi.h"
#include "render_abi.h"
#include "save_game.h"
#include "../../state/game_profile.h"
#include "../hud/hud_runtime.h"
#include "../options/options_menu_model.h"

/* SGDK pad low byte. */
#define PAD_UP    0x01u
#define PAD_DOWN  0x02u
#define PAD_LEFT  0x04u
#define PAD_RIGHT 0x08u
#define PAD_B     0x10u
#define PAD_C     0x20u
#define PAD_A     0x40u
#define PAD_START 0x80u

#define PAGE_NONE    0u
#define PAGE_OPTIONS 1u
#define PAGE_MUSIC   2u
#define PAGE_MAP     3u

#define MSG_NONE   0u
#define MSG_SAVED  1u
#define MSG_FAILED 2u
#define MSG_TICKS  120u

static unsigned char s_list_focus;
static unsigned char s_index;
static unsigned char s_page;
static unsigned char s_confirm;
static unsigned char s_msg;
static unsigned char s_msg_timer;
static unsigned char s_quit;
static unsigned char s_opt_cursor;
static unsigned char s_music_cursor;

void pause_actions_open(void)
{
    s_list_focus = 0u;
    s_index = PAUSE_ACTION_OPTIONS;
    s_page = PAGE_NONE;
    s_confirm = 0u;
    s_msg = MSG_NONE;
    s_msg_timer = 0u;
    s_quit = 0u;
}

unsigned char pause_actions_list_focused(void) { return s_list_focus; }
unsigned char pause_actions_page_open(void) { return (unsigned char)(s_page != PAGE_NONE); }
unsigned char pause_actions_return_row(void) { return (unsigned char)(s_index >= 2u); }

unsigned char pause_actions_take_quit(void)
{
    unsigned char q = s_quit;
    s_quit = 0u;
    return q;
}

static const char *entry_text(unsigned char i)
{
    switch (i) {
    case PAUSE_ACTION_OPTIONS: return "OPTIONS";
    case PAUSE_ACTION_MAP:     return "MAP";
    case PAUSE_ACTION_SAVE:
        if (s_msg == MSG_SAVED) return "SAVED";
        if (s_msg == MSG_FAILED) return "SAVE FAILED";
        return "SAVE";
    default:
        return s_confirm ? "PRESS A AGAIN" : "SAVE AND QUIT";
    }
}

unsigned short pause_actions_cell(unsigned char nes_row, unsigned char col)
{
    unsigned char i, n;
    const char *text;
    if (nes_row < PAUSE_ACTIONS_FIRST_ROW || nes_row > PAUSE_ACTIONS_LAST_ROW ||
        ((nes_row - PAUSE_ACTIONS_FIRST_ROW) & 1u) || col < PAUSE_ACTIONS_COL)
        return inventory_pause_glyph(' ', 0u);
    i = (unsigned char)((nes_row - PAUSE_ACTIONS_FIRST_ROW) >> 1);
    text = entry_text(i);
    for (n = 0u; text[n] != '\0'; ++n)
        if (n == (unsigned char)(col - PAUSE_ACTIONS_COL))
            return inventory_pause_glyph(text[n], (s_list_focus && s_index == i) ? 1u : 0u);
    return inventory_pause_glyph(' ', 0u);
}

void pause_actions_focus_from_grid(unsigned char grid_row)
{
    s_list_focus = 1u;
    s_index = grid_row ? PAUSE_ACTION_SAVE : PAUSE_ACTION_OPTIONS;
    s_confirm = 0u;
    inventory_pause_refresh_actions();
}

/* ---- page text ------------------------------------------------------ */

static void cells_blank(unsigned short *cells)
{
    unsigned char i;
    unsigned short w = inventory_pause_glyph(' ', 0u);
    for (i = 0u; i < 32u; ++i) cells[i] = w;
}

static void cells_text(unsigned short *cells, unsigned char col, const char *s,
                       unsigned char sub_pal)
{
    while (*s && col < 32u) cells[col++] = inventory_pause_glyph(*s++, sub_pal);
}

static void page_line(unsigned char row, unsigned char col, const char *s,
                      unsigned char sub_pal)
{
    unsigned short cells[32];
    cells_blank(cells);
    if (s) cells_text(cells, col, s, sub_pal);
    inventory_pause_write_row(row, cells);
}

static void page_clear(void)
{
    unsigned char r;
    for (r = 0u; r < INVENTORY_PAUSE_ROWS; ++r) page_line(r, 0u, 0, 0u);
}

/* ---- options page (File Select rows, options_menu_model) ------------- */

#define OPT_TITLE_ROW 1u
#define OPT_BODY_ROW  3u
#define OPT_HELP_ROW  14u
#define OPT_HINT_ROW  18u
#define OPT_LABEL_COL 3u
#define OPT_VALUE_COL 19u

static void options_row(unsigned char row)
{
    unsigned short cells[32];
    unsigned char sp = (row == s_opt_cursor) ? 1u : 0u;
    cells_blank(cells);
    cells_text(cells, OPT_LABEL_COL, options_menu_label(row), sp);
    cells_text(cells, OPT_VALUE_COL, options_menu_value(row), sp);
    inventory_pause_write_row((unsigned char)(OPT_BODY_ROW + row), cells);
}

static void options_help(void)
{
    unsigned char line;
    for (line = 0u; line < 3u; ++line)
        page_line((unsigned char)(OPT_HELP_ROW + line), OPT_LABEL_COL,
                  options_menu_help(s_opt_cursor, line), 0u);
}

static void options_draw(void)
{
    unsigned char r;
    page_clear();
    page_line(OPT_TITLE_ROW, 9u, "REMIX OPTIONS", 1u);
    for (r = 0u; r < OPTIONS_MENU_ROW_COUNT; ++r) options_row(r);
    options_help();
    page_line(OPT_HINT_ROW, OPT_LABEL_COL, "A EDIT START SAVE B BACK", 0u);
}

/* ---- music page ------------------------------------------------------ */

#define MUSIC_BODY_ROW  3u
#define MUSIC_HELP_ROW  16u
#define MUSIC_HINT_ROW  20u
#define MUSIC_VALUE_COL 18u

static void music_row(unsigned char row)
{
    unsigned short cells[32];
    char buf[13];
    unsigned char sp = (row == s_music_cursor) ? 1u : 0u;
    cells_blank(cells);
    cells_text(cells, OPT_LABEL_COL, options_music_label(row, buf), sp);
    cells_text(cells, MUSIC_VALUE_COL, options_music_value(row), sp);
    inventory_pause_write_row((unsigned char)(MUSIC_BODY_ROW + row), cells);
}

static void music_help(void)
{
    unsigned char line;
    for (line = 0u; line < 3u; ++line)
        page_line((unsigned char)(MUSIC_HELP_ROW + line), OPT_LABEL_COL,
                  options_music_help(s_music_cursor, line), 0u);
}

static void music_draw(void)
{
    unsigned char r;
    page_clear();
    page_line(OPT_TITLE_ROW, 10u, "REMIX MUSIC", 1u);
    for (r = 0u; r < OPTIONS_MUSIC_ROW_COUNT; ++r) music_row(r);
    music_help();
    page_line(MUSIC_HINT_ROW, OPT_LABEL_COL, "A NEXT START SAVE B BACK", 0u);
}

/* ---- map page: pause_map.c (T-257) ---------------------------------- */

/* ---- actions --------------------------------------------------------- */

/* Same file bytes the mode 8 Save would write at this moment (Continue
 * question handle_activated: InvClock 0, Redux three-heart minimum, full
 * partial heart; then UpdateModeDSave Sub0), but the live game keeps its
 * own clock and hearts. $C0-$CF are not touched (no save_pointer_scratch). */
static unsigned char save_in_place(void)
{
    const unsigned char hv = F_ram_066F_R, hp = F_ram_0670_R, clock = F_ram_066C_R;
    unsigned char ok;
    F_ram_066C_R = 0u;
    if ((hv & 0x0Fu) < 2u) F_ram_066F_R = (unsigned char)((hv & 0xF0u) | 0x02u);
    F_ram_0670_R = 0xFFu;
    ok = save_game_save_current();
    F_ram_066F_R = hv;
    F_ram_0670_R = hp;
    F_ram_066C_R = clock;
    return ok;
}

static void options_close(unsigned char commit)
{
    unsigned char slot = game_profile_current()->slot;
    if (commit) save_game_options_store(slot);
    else save_game_options_load(slot);
    s_page = PAGE_NONE;
    inventory_pause_redraw_sheet();
}

static void options_tick(unsigned char pressed)
{
    unsigned char old = s_opt_cursor;
    if (pressed & (PAD_B | PAD_C)) { options_close(0u); return; }
    if (pressed & PAD_START) { options_close(1u); return; }
    if ((pressed & PAD_UP) && s_opt_cursor > 0u) --s_opt_cursor;
    if ((pressed & PAD_DOWN) && s_opt_cursor + 1u < OPTIONS_MENU_ROW_COUNT) ++s_opt_cursor;
    if (s_opt_cursor != old) {
        options_row(old);
        options_row(s_opt_cursor);
        options_help();
        return;
    }
    if (pressed & PAD_A) {
        if (s_opt_cursor == OPTIONS_MENU_MUSIC_ROW) {
            s_page = PAGE_MUSIC;
            s_music_cursor = 0u;
            music_draw();
            return;
        }
        if (s_opt_cursor == OPTIONS_MENU_SAVE_ROW) { options_close(1u); return; }
    }
    if (s_opt_cursor < OPTIONS_MENU_SAVE_ROW && (pressed & (PAD_LEFT | PAD_RIGHT | PAD_A))) {
        options_menu_step(s_opt_cursor, (pressed & PAD_LEFT) ? 0u : 1u);
        options_row(s_opt_cursor);
    }
}

static void music_tick(unsigned char pressed)
{
    unsigned char old = s_music_cursor, r;
    if ((pressed & (PAD_B | PAD_C)) ||
        (s_music_cursor == OPTIONS_MUSIC_BACK_ROW && (pressed & (PAD_A | PAD_START)))) {
        s_page = PAGE_OPTIONS;
        options_draw();
        return;
    }
    if (pressed & PAD_START) { options_close(1u); return; }
    if ((pressed & PAD_UP) && s_music_cursor) --s_music_cursor;
    if ((pressed & PAD_DOWN) && s_music_cursor < OPTIONS_MUSIC_BACK_ROW) ++s_music_cursor;
    if (s_music_cursor < OPTIONS_MUSIC_BACK_ROW && (pressed & (PAD_LEFT | PAD_RIGHT | PAD_A))) {
        if (options_music_step(s_music_cursor, (pressed & PAD_LEFT) ? 1u : 0u))
            for (r = 2u; r < OPTIONS_MUSIC_BACK_ROW; ++r) music_row(r);
        music_row(1u);
        music_row(s_music_cursor);
        music_help();
    }
    if (old != s_music_cursor) {
        music_row(old);
        music_row(s_music_cursor);
        music_help();
    }
}

static void activate(void)
{
    switch (s_index) {
    case PAUSE_ACTION_OPTIONS:
        s_page = PAGE_OPTIONS;
        s_opt_cursor = 0u;     /* as fs_options_enter */
        options_draw();
        return;
    case PAUSE_ACTION_MAP:
        s_page = PAGE_MAP;
        pause_map_open();
        return;
    case PAUSE_ACTION_SAVE:
        s_msg = save_in_place() ? MSG_SAVED : MSG_FAILED;
        s_msg_timer = MSG_TICKS;
        F_ram_0602_R = 1u;
        inventory_pause_refresh_actions();
        return;
    default:
        if (!s_confirm) {
            s_confirm = 1u;
            F_ram_0602_R = 1u;
            inventory_pause_refresh_actions();
            return;
        }
        s_quit = 1u;
        return;
    }
}

static unsigned char list_tick(unsigned char pressed);

/* Pages own the sprite chain: the HUD markers, plus the map's dot. */
static void page_sprites(void)
{
    /* The map publishes its own sprites and scroll. */
    if (s_page != PAGE_NONE && s_page != PAGE_MAP) inventory_pause_page_sprites(0u, 0u, 0u);
}

unsigned char pause_actions_tick(unsigned char joy, unsigned char pressed)
{
    unsigned char result = PAUSE_ACTIONS_STAY;
    if (s_msg_timer && --s_msg_timer == 0u) {
        s_msg = MSG_NONE;
        if (s_page == PAGE_NONE) inventory_pause_refresh_actions();
    }
    /* T-258: the flute's whirlwind opens the map picker as soon as the
     * menu is down. */
    if (s_page == PAGE_NONE && pause_map_flute_pending()) {
        s_page = PAGE_MAP;
        pause_map_open();
        return result;
    }
    if (s_page == PAGE_OPTIONS) options_tick(pressed);
    else if (s_page == PAGE_MUSIC) music_tick(pressed);
    else if (s_page == PAGE_MAP) {
        if (pause_map_tick(joy, pressed)) s_page = PAGE_NONE;   /* sheet already back */
    } else if (s_list_focus) result = list_tick(pressed);
    page_sprites();
    return result;
}

static unsigned char list_tick(unsigned char pressed)
{
    if (pressed & (PAD_RIGHT | PAD_B | PAD_C)) {
        if ((pressed & (PAD_B | PAD_C)) && s_confirm) {
            s_confirm = 0u;
            inventory_pause_refresh_actions();
            return PAUSE_ACTIONS_STAY;
        }
        s_list_focus = 0u;
        s_confirm = 0u;
        inventory_pause_refresh_actions();
        return PAUSE_ACTIONS_TO_GRID;
    }
    if (pressed & (PAD_UP | PAD_DOWN)) {
        s_index = (unsigned char)((pressed & PAD_UP)
            ? (s_index ? s_index - 1u : PAUSE_ACTION_COUNT - 1u)
            : ((s_index + 1u) % PAUSE_ACTION_COUNT));
        s_confirm = 0u;
        F_ram_0602_R = 1u;
        inventory_pause_refresh_actions();
        return PAUSE_ACTIONS_STAY;
    }
    if (pressed & PAD_A) activate();
    return PAUSE_ACTIONS_STAY;
}
