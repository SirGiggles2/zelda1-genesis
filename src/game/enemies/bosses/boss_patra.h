#ifndef GAME_ENEMIES_BOSSES_BOSS_PATRA_H
#define GAME_ENEMIES_BOSSES_BOSS_PATRA_H

/* Phase 8 Task 8.8 — Patra public API.
 *
 * NES source: Z_07.asm rows $47/$48 -> InitPatra (Z_04.asm:9552)
 *             Z_07.asm rows $47/$48 -> UpdatePatra (Z_04.asm:10070)
 *             Z_07.asm rows $25/$26 -> UpdatePatraChild (Z_04.asm:10164)
 *             Z_04.asm:10124 ControlPatraFlight
 *             Z_04.asm:10133 Flyer_PatraDecideState
 *             Z_04.asm:11898 PatraSines / 11911 RotateObjectLocation /
 *             12025 ShiftMultiply / 12055 DecreaseObjectAngle.
 *
 * Drained C:  src/oracle/enemies/enemy_patra_runtime.c
 *             (enrt_init_patra + enrt_update_patra_child + math helpers
 *              landed in this phase).
 *             src/oracle/enemies/enemy_flyer_runtime.c
 *             (enrt_flyer_speed_up + enrt_flyer_patra_decide_state +
 *              enrt_move_flyer — already shipped).
 * Coverage:   PARTIAL — UpdatePatra orchestrator (ControlPatraFlight JT
 *             over states 0..3 with state 0 = Flyer_SpeedUp,
 *             state 1 = Flyer_PatraDecideState, states 2/3 routed
 *             through the keese-flight bridge that already exposes
 *             Flyer_Chase / Flyer_Wander) + reset Flyer offsets +
 *             MoveFlyer + AnimateAndDrawCommonObject(2) +
 *             child-loop (slots 9..2) deciding LinkOnly vs Monster
 *             collision + TryChangeManeuver flip with the documented
 *             post-collision Y=0 timer lookup. Red fight route tested;
 *             blue variant/death-at-reload remain unverified.
 * Stance:     ADOPT — drained primitives consumed verbatim. State 2/3
 *             routing reuses c_control_keese_flight per the same
 *             Flyer_Chase / Flyer_Wander entries the keese head uses,
 *             matching the gleeok-head pattern (boss_gleeok.c:674).
 */

void boss_patra_update(unsigned int slot);

#endif /* GAME_ENEMIES_BOSSES_BOSS_PATRA_H */
