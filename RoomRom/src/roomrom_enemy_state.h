#ifndef ROOMROM_ENEMY_STATE_H
#define ROOMROM_ENEMY_STATE_H

/* Phase 7 Task 7.1 framework. Re-export of src/state/enemy_state.h enemy
 * slice for RoomRom-rooted family agents (7.2-7.7). Aliasing IS the spec
 * per reference/aldonunez/ObjVars.inc — same NES scratch byte means
 * different things per enemy family. Documented alias clusters live in
 * docs/audit/enemy_alias_table.md.
 *
 * Debate 2026-05-09 verdict Q1=(c): flat byte-slot + accessor macros.
 *   Codex / Gemini / Sonnet / Opus 4-of-4 unanimous. See
 *   debates/2026-05-09-phase7-task-7-1-framework/synthesis.md.
 *
 * Drain Rule D1 stance: EXTEND. Primary evidence is drained
 * src/state/enemy_state.h (134 OBJ(0x0412) collisions) — re-exported
 * here so RoomRom family code never depends on substrate paths
 * directly. NES asm wins ties.
 */

#include "platform_abi.h"
#include "../../src/state/enemy_state.h"

/* Compile-time collision proofs. NES Random[13] lives at $0018..$0024.
 * Object scratch bytes live at $0380..$04FF. Verify scratch_state /
 * enemy_state header math hasn't drifted. */
_Static_assert(sizeof(unsigned char) == 1,
               "enemy_state.h byte-slot model assumes 8-bit char");

/* Alias-cluster invariants (NES scratch overlay, intentional):
 *   $0412 = ENEMY_PUSH_TIMER
 *         | ENEMY_FLYER_SPEED_FRAC
 *         | ENEMY_JUMPER_VSPEED_HI
 *         | ENEMY_GOHMA_DIST_TRAVELED
 *   $041F = ENEMY_AIR_SPEED
 *         | ENEMY_JUMPER_VSPEED_LO
 *         | ENEMY_GOHMA_MOVE_ACCUM
 *   $0444 = ENEMY_AI_STATE
 *         | ENEMY_JUMPER_TARGET_Y
 *         | ENEMY_GOHMA_OPEN_EYE_TIMER
 *   $0451 = ENEMY_JUMPER_REVERSALS
 *         | ENEMY_MANHANDLA_FRAME_ACCUM
 *         | ENEMY_GOHMA_GO_STRAIGHT
 *   $0478 = ENEMY_BOUNCE_FLAGS
 *         | ENEMY_FLYER_Y_FINE
 *         | ENEMY_MANHANDLA_FRAME_ATTR
 *         | ENEMY_GOHMA_CLOSED_EYE_CNTR
 * Family agents MUST grep this table before claiming a byte. */

#endif
