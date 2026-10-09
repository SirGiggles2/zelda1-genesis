/* enemy_wizzrobe_runtime.h — Wizzrobe family drain header.
 * NES sources Z_04.asm:7034 UpdateBlueWizzrobe, 7474 UpdateRedWizzrobe.
 */
#ifndef SRC_ORACLE_ENEMIES_ENEMY_WIZZROBE_RUNTIME_H
#define SRC_ORACLE_ENEMIES_ENEMY_WIZZROBE_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

void enrt_update_blue_wizzrobe(unsigned int slot);
void enrt_update_red_wizzrobe(unsigned int slot);
/* Shared NES BlueWizzrobe primitives also used by Ganon and his burst rays. */
void enrt_blue_wizzrobe_move(unsigned int slot);
void enrt_blue_wizzrobe_turn_sometimes_and_move_and_check_tile(unsigned int slot);

#ifdef __cplusplus
}
#endif

#endif /* SRC_ORACLE_ENEMIES_ENEMY_WIZZROBE_RUNTIME_H */
