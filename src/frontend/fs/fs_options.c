/* Phase 9 Task 9.3 — File Select OPTIONS submenu phase logic.
 *
 * Drain Rule D1 stance: GREENFIELD. Layered on top of options_runtime
 * (Task 9.1) and options_persistence (Task 9.2). Owns no SRAM region;
 * commits live state via options_persistence_commit() when user
 * presses B or A on the SAVE row.
 */

#include "fs_options.h"
#include "fs_options_render.h"
#include "fs_input.h"
#include "fs_phase.h"
#include "../../game/options/options_runtime.h"
#include "../../game/options/options_state.h"
#include "../../game/options/options_persistence.h"

#define FS_ROW_OPTIONS 6u   /* mirrors fs_main.c FS_ROW_OPTIONS */

static uint8_t s_cursor;

#define PROBE  ((volatile unsigned char *)FS_OPTIONS_PROBE_BASE)

void fs_options_probe_init(void)
{
    unsigned int i;
    PROBE[0]  = 0x4Fu; /* 'O' */
    PROBE[1]  = 0x4Du; /* 'M' */
    PROBE[2]  = 0x01u;
    for (i = 3u; i < 16u; ++i) PROBE[i] = 0u;
    s_cursor = 0u;
}

void fs_options_enter(void)
{
    PROBE[3] = (unsigned char)(PROBE[3] + 1u);
    s_cursor = 0u;
    PROBE[7] = s_cursor;
    fs_options_render_full(s_cursor);
}

static void exit_to_nav(unsigned char committed)
{
    PROBE[4] = (unsigned char)(PROBE[4] + 1u);
    if (committed) PROBE[5] = (unsigned char)(PROBE[5] + 1u);
    else PROBE[6] = (unsigned char)(PROBE[6] + 1u);

    fs_options_render_leave(FS_ROW_OPTIONS);
    s_fs_phase = FS_NAV;
    s_fs_cursor = FS_ROW_OPTIONS;
}

static unsigned char enum_count_for_row(uint8_t row)
{
    switch (row) {
    case OPTION_ID_SWORD_STYLE:        return OPTIONS_SWORD_COUNT;
    case OPTION_ID_LIKE_LIKE_BEHAVIOR: return OPTIONS_LIKELIKE_COUNT;
    case OPTION_ID_BOMB_UPGRADE:       return OPTIONS_BOMBUPG_COUNT;
    case OPTION_ID_LOST_WOODS:         return OPTIONS_LWOODS_COUNT;
    case OPTION_ID_DARK_ROOM_LIGHT:    return OPTIONS_DARK_COUNT;
    case OPTION_ID_ROOM_SCROLL:        return OPTIONS_SCROLL_COUNT;
    default: return 0u;
    }
}

static void apply_left(uint8_t row)
{
    unsigned char v = options_get((unsigned int)row);
    unsigned char count;
    unsigned char nv;

    if (row <= OPTION_ID_AUTO_COLLECT_DROPS) {
        options_set((unsigned int)row, (v != 0u) ? 0u : 1u);
        return;
    }
    if (row == OPTION_ID_START_HEARTS) {
        if (v > OPTIONS_START_HEARTS_MIN) {
            options_set((unsigned int)row, (unsigned char)(v - 1u));
        }
        return;
    }
    count = enum_count_for_row(row);
    if (count == 0u) return;
    nv = (v == 0u) ? (unsigned char)(count - 1u) : (unsigned char)(v - 1u);
    options_set((unsigned int)row, nv);
}

static void apply_right(uint8_t row)
{
    unsigned char v = options_get((unsigned int)row);
    unsigned char count;
    unsigned char nv;

    if (row <= OPTION_ID_AUTO_COLLECT_DROPS) {
        options_set((unsigned int)row, (v != 0u) ? 0u : 1u);
        return;
    }
    if (row == OPTION_ID_START_HEARTS) {
        if (v < OPTIONS_START_HEARTS_MAX) {
            options_set((unsigned int)row, (unsigned char)(v + 1u));
        }
        return;
    }
    count = enum_count_for_row(row);
    if (count == 0u) return;
    nv = (unsigned char)((v + 1u) % count);
    options_set((unsigned int)row, nv);
}

void fs_options_step(uint8_t edge)
{
    if (edge & FS_BTN_UP) {
        if (s_cursor > 0u) {
            s_cursor--;
            PROBE[7] = s_cursor;
            fs_options_render_cursor(s_cursor);
        }
    }
    if (edge & FS_BTN_DOWN) {
        if (s_cursor + 1u < FS_OPTIONS_ROW_COUNT) {
            s_cursor++;
            PROBE[7] = s_cursor;
            fs_options_render_cursor(s_cursor);
        }
    }
    if (edge & FS_BTN_LEFT) {
        PROBE[8] = (unsigned char)(PROBE[8] + 1u);
        if (s_cursor < FS_OPTIONS_SAVE_ROW) {
            apply_left(s_cursor);
            fs_options_render_row_value(s_cursor);
        }
    }
    if (edge & FS_BTN_RIGHT) {
        PROBE[9] = (unsigned char)(PROBE[9] + 1u);
        if (s_cursor < FS_OPTIONS_SAVE_ROW) {
            apply_right(s_cursor);
            fs_options_render_row_value(s_cursor);
        }
    }
    if (edge & FS_BTN_A) {
        PROBE[10] = (unsigned char)(PROBE[10] + 1u);
        if (s_cursor == FS_OPTIONS_SAVE_ROW) {
            options_persistence_commit();
            exit_to_nav(1u);
            return;
        }
        if (s_cursor < FS_OPTIONS_SAVE_ROW) {
            apply_right(s_cursor);
            fs_options_render_row_value(s_cursor);
        }
    }
    if (edge & FS_BTN_B) {
        PROBE[11] = (unsigned char)(PROBE[11] + 1u);
        options_persistence_commit();
        exit_to_nav(1u);
        return;
    }
    if (edge & FS_BTN_START) {
        PROBE[12] = (unsigned char)(PROBE[12] + 1u);
        exit_to_nav(0u);
        return;
    }
}
