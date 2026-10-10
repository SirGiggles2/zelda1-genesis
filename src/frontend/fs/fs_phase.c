/* src/fs_phase.c — phase dispatcher + RAM probe writes.
 *
 * RAM probe at $00FF07F0/F1 readable from BizHawk Lua via "68K RAM" domain
 * at offsets $07F0/$07F1 (the 68K RAM window starts at Genesis $FF0000).
 */
#include "fs_phase.h"
#include "fs_render.h"
#include "fs_handoff.h"
#include <stdint.h>

uint8_t s_fs_phase;
uint8_t s_fs_cursor;
uint8_t s_fs_players_value;

#define M68K_RAM ((volatile uint8_t *)0x00FF0000)

void fs_phase_init(void) {
    s_fs_phase = FS_LOAD;
    s_fs_cursor = 0;
    s_fs_players_value = 1;   /* default 1 player; persistence deferred to SRAM follow-up */
}

void fs_phase_step(void) {
    /* RAM probe contract: $07F0=phase, $07F1=cursor, $07F2=players value */
    M68K_RAM[0x07F0] = s_fs_phase;
    M68K_RAM[0x07F1] = s_fs_cursor;
    M68K_RAM[0x07F2] = s_fs_players_value;
    switch (s_fs_phase) {
        case FS_LOAD:
            fs_render_clear_screen();
            fs_render_static_layout();
            fs_render_all_slots();
            fs_render_cursor(s_fs_cursor);
            s_fs_phase = FS_NAV;
            break;
        case FS_NAV:
            /* Idle — input dispatch in fs_main moves cursor + cycles PLAYERS. */
            break;
        case FS_HANDOFF:
            /* A pressed on a slot row.
             *
             * This used to hardcode slot 3, because the File Select was
             * written in "v6.minimal SRAM-less mode" before cart SRAM
             * existed: slot >= 3 meant "go to register-name" in the old
             * transpiled path. Its own comment said real slot routing
             * would land "when SRAM is wired". That happened 2026-08-04
             * (sram_backend.c + save_game.c), so the cursor row is now
             * the slot, and out-of-range rows fall back to slot 0 rather
             * than handing the save layer an index it must reject.
             *
             * Cursor rows: 0/1/2 = save slots, 3 = COPY, 4 = ERASE. Only
             * a slot row reaches FS_HANDOFF, but the clamp keeps a future
             * cursor change from silently producing an invalid slot. */
            fs_handoff_to_transpiled(
                (uint8_t)((s_fs_cursor <= 2u) ? s_fs_cursor : 0u));
            break;
        default:
            break;  /* FS_COPY_*, FS_ERASE_*, FS_OPTIONS land in v4+ */
    }
}
