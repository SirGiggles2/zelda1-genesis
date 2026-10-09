/* src/fs_phase.h - FS phase machine + globals.
 *
 * Phase sequence: FS_LOAD (one-shot init) -> FS_NAV (idle, input-driven) ->
 *   FS_COPY_x / FS_ERASE_x / FS_OPTIONS in v3+ -> FS_HANDOFF when game starts.
 *
 * RAM probe contract (visible to BizHawk Lua test rig):
 *   0x07F0 = current phase byte
 *   0x07F1 = current cursor row (0..4 = slot0/slot1/slot2/COPY/ERASE)
 */
#ifndef FS_PHASE_H
#define FS_PHASE_H
#include <stdint.h>

typedef enum {
    FS_LOAD = 0,
    FS_NAV,
    FS_COPY_SRC,
    FS_COPY_DST,
    FS_ERASE_PICK,
    FS_ERASE_CONFIRM,
    FS_OPTIONS,
    FS_HANDOFF,
    FS_REGISTER      /* T-099: NES Mode $E name entry for an empty slot */
} fs_phase_t;

#define FS_CURSOR_MAX 6u   /* v3: 7 positions (slot0..OPTIONS), index range 0..6 */

extern uint8_t s_fs_phase;
extern uint8_t s_fs_cursor;
extern uint8_t s_fs_players_value;   /* 1..4 — PLAYERS L/R cycle */

void fs_phase_init(void);
void fs_phase_step(void);

#endif
