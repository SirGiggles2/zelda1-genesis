#include "player_state.h"

/* Phase 6 Task 6.1 — definitions for the PlayerState array.
 *
 * `players[0]` mirrors NES Z1 object slot 0. The remaining slots are
 * zero-initialized and unused until Phase 13 (Optional 4-Player Mode)
 * fills them. `g_player_count` is 1 by default; Phase 13 bumps it after
 * lobby selection. */

PlayerState  players[PLAYER_MAX_COUNT];
unsigned char g_player_count = 1u;

/* player_state_size_invariant probe (compile-time).
 *
 * Genesis SRAM regions in this project are bounded by the Z1 save-state
 * footprint plus per-player progress. Z1 SaveData::profile is 0x40
 * bytes; budgeting 0x80 (128) bytes per slot for the typed PlayerState
 * + future Genesis-native progress fields keeps the 4-player save under
 * 512 bytes — well below the 8 KB SRAM available even on the lightest
 * NES-faithful cartridge layout.
 *
 * If `sizeof(PlayerState) * PLAYER_MAX_COUNT` ever drifts past the
 * envelope, this static_assert fails the build before silent SRAM
 * stomping can ship. */
#define PLAYER_STATE_SRAM_HEADROOM_BYTES   16u
#define PLAYER_STATE_SRAM_BUDGET_BYTES    512u

_Static_assert(
    (sizeof(PlayerState) * PLAYER_MAX_COUNT) + PLAYER_STATE_SRAM_HEADROOM_BYTES
        < PLAYER_STATE_SRAM_BUDGET_BYTES,
    "player_state_size_invariant: PlayerState[4] + headroom must fit in "
    "the SRAM multiplayer save budget"
);
