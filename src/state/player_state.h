#ifndef PLAYER_STATE_H
#define PLAYER_STATE_H

#include "link_state.h"
#include "player_ram.h"

/* ---------------------------------------------------------------------------
 * Phase 6 Task 6.1 — PlayerState array shape.
 *
 * NES Z1 is 1-player and stores Link in object slot 0. Phase 6 only ever
 * reads `players[0]`. Phase 13 (Optional 4-Player Genesis Mode) fills
 * `players[1..3]` and bumps `g_player_count`.
 *
 * The shape lands NOW so Phase 6 sprite/render/input/collision code is
 * multiplayer-ready by construction (debate 002 synthesis: shape in Phase
 * 6, implementation deferred post-Phase-17). Retrofitting would require
 * rewriting every Phase 6 system that touched `players[0]` directly.
 *
 * Sizing rationale: 4 is the hard cap because the Genesis VDP sprite
 * budget (64 sprites total, 16 per scanline in active H32 mode) cannot sustain more
 * simultaneous Links + their projectiles + enemies. See render_budget.h
 * for the per-player sprite reservation.
 * ------------------------------------------------------------------------ */

#define PLAYER_MAX_COUNT  4u

/* T-367 unit 4b: actors live in the native PlayerRam records ($FF8800 block, 128-byte
 * stride, A4-relative). PLAYER(i) is the actor lvalue; there is no flat PlayerState array. */
#define PLAYER(i) (G_PLAYERS[(i)].actor)
_Static_assert(PLAYER_MAX_COUNT == PLAYER_RAM_COUNT, "one PlayerRam record per player");
extern unsigned char g_player_count;

#endif /* PLAYER_STATE_H */
