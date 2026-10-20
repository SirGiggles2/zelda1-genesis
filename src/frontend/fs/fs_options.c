/* Phase 9 Task 9.3 — File Select OPTIONS submenu phase logic.
 *
 * Drain Rule D1 stance: GREENFIELD. Layered on top of options_runtime
 * (Task 9.1) and options_persistence (Task 9.2). Owns no SRAM region;
 * commits live state via options_persistence_commit() when user
 * accepts SAVE with A or Start; B returns without committing.
 */

#include "fs_options.h"
#include "fs_options_render.h"
#include "fs_input.h"
#include "fs_phase.h"
#include "fs_main.h"
#include "../../game/options/options_runtime.h"
#include "../../game/options/options_state.h"
#include "../../game/options/options_persistence.h"

#define FS_ROW_OPTIONS 6u   /* mirrors fs_main.c FS_ROW_OPTIONS */

static uint8_t s_cursor;
static uint8_t s_music_cursor;
static uint8_t s_music_page;

/* User-approved MD menu; preserve OptionId and all saved byte meanings.
 * No placeholder/core switch is offered as an optional modifier. */
unsigned int fs_options_id_for_row(uint8_t row)
{
    static const unsigned char ids[FS_OPTIONS_ROW_COUNT] = {
        OPTION_ID_COUNT, OPTION_ID_AB_SWAP, OPTION_ID_NO_REDUCED_FLASHING,
        OPTION_ID_SWORD_STYLE, OPTION_ID_LIKE_LIKE_BEHAVIOR,
        OPTION_ID_ROOM_SCROLL, OPTION_ID_COUNT, OPTION_ID_COUNT
    };
    return row < FS_OPTIONS_ROW_COUNT ? ids[row] : OPTION_ID_COUNT;
}

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
    s_music_page = 0u;
    PROBE[7] = s_cursor;
    fs_options_render_full(s_cursor);
}

static void exit_to_nav(unsigned char committed)
{
    PROBE[4] = (unsigned char)(PROBE[4] + 1u);
    if (committed) PROBE[5] = (unsigned char)(PROBE[5] + 1u);
    else PROBE[6] = (unsigned char)(PROBE[6] + 1u);

    fs_file_options_exit(committed);
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
    case OPTION_ID_UW_MUSIC:           return OPTIONS_UWMUSIC_COUNT;
    default: return 0u;
    }
}

static void apply_left(uint8_t row)
{
    row = (uint8_t)fs_options_id_for_row(row);
    if (row >= OPTION_ID_COUNT) return;
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
    row = (uint8_t)fs_options_id_for_row(row);
    if (row >= OPTION_ID_COUNT) return;
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
    if (s_music_page) {
        uint8_t old_cursor=s_music_cursor;
        if (!edge) return;
        if ((edge & (FS_BTN_B|FS_BTN_C)) ||
            (s_music_cursor == FS_MUSIC_BACK_ROW && (edge & (FS_BTN_A|FS_BTN_START)))) {
            s_music_page=0u;
            fs_options_render_full(s_cursor);
            return;
        }
        if (edge & FS_BTN_START) { exit_to_nav(1u); return; }
        if ((edge & FS_BTN_UP) && s_music_cursor) --s_music_cursor;
        if ((edge & FS_BTN_DOWN) && s_music_cursor < FS_MUSIC_BACK_ROW) ++s_music_cursor;
        if (s_music_cursor < FS_MUSIC_BACK_ROW && (edge & (FS_BTN_LEFT|FS_BTN_RIGHT|FS_BTN_A))) {
            if (s_music_cursor==0u) {
                options_music_set(1u,options_music_get(1u)==4u ? 1u : 4u);
            } else {
                unsigned char choice=s_music_cursor==1u ? options_dungeon_music_preset() :
                    options_dungeon_music_get((unsigned char)(s_music_cursor-1u));
                choice=(edge&FS_BTN_LEFT) ? (choice<=1u ? 3u : choice-1u) :
                    (choice>=3u ? 1u : choice+1u);
                if (s_music_cursor==1u) {
                    options_dungeon_music_set_all(choice);
                    for (uint8_t row=2u; row<FS_MUSIC_BACK_ROW; ++row) fs_music_render_row(row);
                } else options_dungeon_music_set((unsigned char)(s_music_cursor-1u),choice);
                fs_music_render_row(1u);
            }
            fs_music_render_row(s_music_cursor);
        }
        if (old_cursor!=s_music_cursor || (edge&(FS_BTN_LEFT|FS_BTN_RIGHT|FS_BTN_A)))
            fs_music_render_cursor(s_music_cursor);
        return;
    }
    if (edge & (FS_BTN_B|FS_BTN_C)) {
        if (edge & FS_BTN_B) PROBE[11] = (unsigned char)(PROBE[11] + 1u);
        exit_to_nav(0u); return;
    }
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
        if (s_cursor == FS_OPTIONS_MUSIC_ROW) {
            s_music_page=1u; s_music_cursor=0u;
            fs_music_render_full(s_music_cursor);
            return;
        }
        if (s_cursor == FS_OPTIONS_SAVE_ROW) {
            exit_to_nav(1u);
            return;
        }
        if (s_cursor < FS_OPTIONS_SAVE_ROW) {
            apply_right(s_cursor);
            fs_options_render_row_value(s_cursor);
        }
    }
    if (edge & FS_BTN_START) {
        PROBE[12] = (unsigned char)(PROBE[12] + 1u);
        exit_to_nav(1u);
        return;
    }
}
