#ifndef GAME_ENEMIES_BOSSES_BOSS_DIGDOGGER_H
#define GAME_ENEMIES_BOSSES_BOSS_DIGDOGGER_H

/* Phase 8 Task 8.6 — Digdogger public API.
 *
 * NES source: Z_07.asm:5658/5659 rows $38/$39 -> InitDigdogger1/2 (Z_04.asm:4860+)
 *             Z_07.asm:5320/5352/5353 rows $18/$38/$39 -> UpdateDigdogger
 *             (Z_04.asm:5265 + ChangeSpeed/Move/Draw/AfterFlute helpers).
 * Drained C:  src/oracle/enemies/enemy_boss_runtime.c
 *             (enrt_init_digdogger1, enrt_init_digdogger2 already shipped;
 *              enrt_update_digdogger + 6 statics drained this task).
 * Coverage:   FULL — UpdateDigdogger body + Digdogger_ChangeSpeed +
 *             Digdogger_Move + Digdogger_Draw (big + little) +
 *             L_Digdogger_AfterFlute (states 1 + 2) +
 *             CheckBigDigdoggerCollisions 4-corner loop +
 *             L_Digdogger_DrawAsLittle inset draw +
 *             @MakeChildren split path all drained.
 * Stance:     ADOPT — drained Digdogger primitives consumed verbatim.
 *             All callee primitives (c_turn_towards_player8 /
 *             c_turn_randomly_dir8 / c_bound_flyer /
 *             c_check_monster_collisions / c_play_boss_death_cry /
 *             c_draw_object_mirrored / c_draw_object_not_mirrored /
 *             z07_anim_advance_and_fetch / z07_anim_fetch_obj_pos /
 *             z01_anim_set_sprite_desc_attrs /
 *             enrt_anim_set_sprite_desc_level_palette_row) already
 *             resolved by prior bridges (boss_manhandla, boss_dodongo,
 *             flyer / jumper / projectile bridges) — no new shims.
 */

/* No public API entry points — wiring lives in enemy_loop.c via the
 * drained enrt_init_digdogger1 / enrt_init_digdogger2 / enrt_update_digdogger
 * symbols (declared in src/oracle/enemies/enemy_runtime.h). */

#endif /* GAME_ENEMIES_BOSSES_BOSS_DIGDOGGER_H */
