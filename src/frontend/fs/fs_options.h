/* Phase 9 Task 9.3 — File Select OPTIONS submenu phase logic.
 *
 * Drain Rule D1 stance: GREENFIELD (sanctioned per debate 004 — Redux
 * extension; NES Z1 has no options menu).
 *
 * Lifecycle:
 *   FS_NAV : cursor on OPTIONS row + A pressed -> fs_options_enter()
 *   FS_OPTIONS : fs_options_step() drives input every vblank
 *   A on SAVE row OR START: commit the selected file's live settings.
 *   B OR C: restore the selected file's committed settings.
 *
 * Probe RAM contract ($FF7EA0..$FF7EAF):
 *   [0]   = 'O'  (0x4F) magic
 *   [1]   = 'M'  (0x4D) magic
 *   [2]   = version (1)
 *   [3]   = enter_count        — # times fs_options_enter called
 *   [4]   = exit_count         — # times fs_options_exit_to_nav called
 *   [5]   = commit_count       — # times commit branch hit
 *   [6]   = abandon_count      — # times abandon branch hit
 *   [7]   = last_cursor_row
 *   [8]   = last_left_count    — # left edges seen in OPTIONS phase
 *   [9]   = last_right_count   — # right edges seen
 *   [10]  = last_a_count       — # A edges seen
 *   [11]  = last_b_count       — # B edges seen
 *   [12]  = last_start_count   — # Start edges seen
 *   [13..15] reserved
 */

#ifndef SRC_FRONTEND_FS_FS_OPTIONS_H
#define SRC_FRONTEND_FS_FS_OPTIONS_H

#include <stdint.h>

#define FS_OPTIONS_PROBE_BASE  0x00FF7EA0UL

/* Core is informational; only implemented preferences/modifiers are editable.
 * Row identity is independent of the wire-stable OptionId/save layout. */
#define FS_OPTIONS_ROW_COUNT   8u
#define FS_OPTIONS_SAVE_ROW    7u
#define FS_OPTIONS_CORE_ROW    0u
#define FS_OPTIONS_MUSIC_ROW   6u
#define FS_MUSIC_BACK_ROW      11u
#define FS_MUSIC_ROW_COUNT     12u

#ifdef __cplusplus
extern "C" {
#endif

/* Init runtime state and stamp probe magic. Called once from fs_init. */
void fs_options_probe_init(void);

/* Enter phase from FS_NAV (caller flips s_fs_phase to FS_OPTIONS first
 * to keep state-machine ownership in fs_phase.c). */
void fs_options_enter(void);

/* Returns OPTION_ID_COUNT for informational/action rows. */
unsigned int fs_options_id_for_row(uint8_t row);

/* One-vblank step. Reads input edges, updates live options state via
 * options_set, and may commit + return to FS_NAV. */
void fs_options_step(uint8_t edge);

#ifdef __cplusplus
}
#endif

#endif /* SRC_FRONTEND_FS_FS_OPTIONS_H */
