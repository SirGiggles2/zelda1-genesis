#ifndef GAME_ENEMIES_BOSSES_BOSS_DODONGO_H
#define GAME_ENEMIES_BOSSES_BOSS_DODONGO_H

/* Phase 8 Task 8.3 — Dodongo public API.
 *
 * NES source: Z_07.asm:5295 row $31/$32 -> Z_04.asm:5856 UpdateDodongo;
 *             Z_04.asm:5856-5919 (UpdateDodongo, UpdateDodongoState,
 *             UpdateDodongoState0_Move) + 5920-6005
 *             (State1_Bloated dispatcher + Sub_Wait + Sub_Die +
 *             Sub_End) + 5919 (State2_Stunned).
 * Drained C:  src/oracle/enemies/enemy_dodongo_runtime.c — INIT
 *             (enrt_init_dodongo) + collision/draw/bloated-end/stunned
 *             primitives are PRIMARY; State0_Move + Bloated_Sub_Wait
 *             + state/bloated dispatchers are NOT drained, ported
 *             native here per Drain Rule D1 PARTIAL coverage.
 * Coverage:   FULL — every Z_04 UpdateDodongo branch routed through
 *             this bridge, mixing drained primitives with
 *             native-ported state/sub_wait.
 * Stance:     EXTEND — drained Dodongo primitives consumed verbatim;
 *             missing Move + Sub_Wait + dispatchers transcribed
 *             from NES asm into this TU.
 */

void boss_dodongo_update(unsigned int slot);

#endif /* GAME_ENEMIES_BOSSES_BOSS_DODONGO_H */
