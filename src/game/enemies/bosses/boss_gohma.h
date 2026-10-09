#ifndef GAME_ENEMIES_BOSSES_BOSS_GOHMA_H
#define GAME_ENEMIES_BOSSES_BOSS_GOHMA_H

/* Phase 8 Task 8.7 — Gohma public API.
 *
 * NES source: Z_07.asm:5653/5654 rows $33/$34 -> InitGohma (Z_04.asm:7814)
 *             Z_07.asm:5347/5348 rows $33/$34 -> UpdateGohma (Z_04.asm:8207)
 *             Z_01.asm:5878/Z_01.asm:6629 -> Gohma_HandleWeaponCollision
 *             (arrow-only damage gate via @Gohma path: only damages when
 *             eye state/frame == 3 i.e. fully open).
 * Drained C:  src/oracle/enemies/enemy_boss_runtime.c
 *             (enrt_init_gohma + enrt_update_gohma + enrt_gohma_set_sprite_attributes
 *              already shipped; full UpdateGohma body + eye-state
 *              animation + shoot-timer + sprint state machine).
 * Coverage:   FULL — InitGohma (sfx + INVINCIBILITY=$FB +
 *             SHOOT_TIMER=1 (alias BOSS_HP_PHASE++) + X=$80 + Y=$70 +
 *             ResetObjMetastateAndTimer) + UpdateGohma body (movement
 *             accumulator + 0x20-pixel sprint + reverse / random direction +
 *             eye state machine (open / half / closed cycle with
 *             0xC0|RNG reload) + shoot-timer rollover firing fireball
 *             type 86 + tail-call to AnimateAndDraw + CheckCollisions).
 *             Arrow-only damage handled by Gohma_HandleWeaponCollision
 *             reachable through c_gohma_check_collisions asm shim
 *             (Z_04 body still linked into Debug.md).
 * Stance:     ADOPT — drained Gohma primitives consumed verbatim. All
 *             callee primitives (c_reverse_obj_dir8 / c_shoot_fireball /
 *             c_gohma_animate_and_draw / c_gohma_check_collisions)
 *             already linked via c_shims.asm + prior bridge plumbing.
 *             No new shims needed.
 */

/* No public API entry points — wiring lives in enemy_loop.c via the
 * drained enrt_init_gohma / enrt_update_gohma symbols (declared in
 * src/oracle/enemies/enemy_runtime.h). */

#endif /* GAME_ENEMIES_BOSSES_BOSS_GOHMA_H */
