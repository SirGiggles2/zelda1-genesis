/* Phase 9 Task 9.3 — File Select OPTIONS submenu rendering. */

#ifndef SRC_FRONTEND_FS_FS_OPTIONS_RENDER_H
#define SRC_FRONTEND_FS_FS_OPTIONS_RENDER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Clear FS scene + paint OPTIONS title, all rows, and cursor. */
void fs_options_render_full(uint8_t cursor_row);

/* Re-paint a single row's value cell after a toggle / cycle / clamp. */
void fs_options_render_row_value(uint8_t row);

/* Move cursor sprite to the given row. Hides slot/cursor sprites used
 * by FS_NAV (they sit at SAT entries 0..3); OPTIONS cursor uses SAT 4. */
void fs_options_render_cursor(uint8_t cursor_row);

/* Restore FS_NAV scene (called when leaving OPTIONS). */
void fs_options_render_leave(uint8_t fs_nav_cursor);

#ifdef __cplusplus
}
#endif

#endif /* SRC_FRONTEND_FS_FS_OPTIONS_RENDER_H */
