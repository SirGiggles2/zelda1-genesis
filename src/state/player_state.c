#include "player_state.h"

/* Phase 6 Task 6.1 — definitions for the PlayerState array.
 *
 * `players[0]` mirrors NES Z1 object slot 0. The remaining slots are
 * zero-initialized and unused until Phase 13 (Optional 4-Player Mode)
 * fills them. `g_player_count` is 1 by default; Phase 13 bumps it after
 * lobby selection. */

unsigned char g_player_count = 1u;

/* Transient work-RAM record budget, not a save format. Save serialization
 * remains the explicit compatible codec. Build.bat independently checks the
 * complete linked BSS against the physical A4 fence. */
#define PLAYER_STATE_WORK_RAM_HEADROOM_BYTES   16u
#define PLAYER_STATE_WORK_RAM_BUDGET_BYTES    512u
_Static_assert(
    (sizeof(PlayerState) * PLAYER_MAX_COUNT) + PLAYER_STATE_WORK_RAM_HEADROOM_BYTES
        < PLAYER_STATE_WORK_RAM_BUDGET_BYTES,
    "player_state_size_invariant: native records exceed work-RAM budget"
);
_Static_assert(sizeof(PlayerState) == 28u &&
               __builtin_offsetof(PlayerState, go_straight_when_diag) == 26u,
               "update native player schema/probes with the current layout");
