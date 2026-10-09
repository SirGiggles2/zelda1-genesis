#ifndef GAME_ENEMIES_BOSSES_BOSS_MANHANDLA_H
#define GAME_ENEMIES_BOSSES_BOSS_MANHANDLA_H

/* Phase 8 Task 8.4 — Manhandla public API.
 *
 * NES source: Z_07.asm:5295 row $3C -> Z_04.asm:7842 UpdateManhandla;
 *             Z_07.asm:5601 row $3C -> Z_04.asm:7747 InitManhandla.
 * Drained C:  src/oracle/enemies/enemy_manhandla_runtime.c
 *             (enrt_init_manhandla, enrt_update_manhandla,
 *              enrt_manhandla_set_all_segments_direction,
 *              enrt_manhandla_check_collisions, enrt_manhandla_move,
 *              enrt_manhandla_draw). Drain primary.
 * Coverage:   FULL — UpdateManhandla + InitManhandla bodies fully
 *             drained, including 5-segment loop / segment-died flag /
 *             bounce-direction / TurnTowardsPlayer8 vs TurnRandomlyDir8
 *             pick / fireball spawn gate / mirrored vs not-mirrored draw.
 * Stance:     ADOPT — drained Manhandla primitives consumed verbatim.
 *             This TU only carries 4 callee shim forwarders that resolve
 *             enemy_manhandla_runtime.c's c_* and z_* extern decls to
 *             the dispatcher entry points already in the link.
 */

/* No public API entry points — wiring lives in enemy_loop.c via the
 * drained enrt_init_manhandla / enrt_update_manhandla symbols
 * (declared in src/oracle/enemies/enemy_runtime.h). */

#endif /* GAME_ENEMIES_BOSSES_BOSS_MANHANDLA_H */
