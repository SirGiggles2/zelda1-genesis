#ifndef GAME_ENEMIES_BOSSES_BOSS_FRAMEWORK_H
#define GAME_ENEMIES_BOSSES_BOSS_FRAMEWORK_H

/* Phase 8 Task 8.1 — Boss Framework public API.
 *
 * NES source: Z_05.asm:8154-8250 (CreateRoomObjects),
 *             Z_07.asm:5453 (RoomKillCount inc on monster death),
 *             Z_04.asm:10999 (Ganon_ActivateRoomItem — re-arms slot 19).
 * Drained C:  src/oracle/enemies/enemy_boss_runtime.c (per-boss bodies),
 *             src/oracle/world/world_runtime.c
 *             (worldrt_get_shortcut_or_item_xy_for_room).
 * Coverage:   PARTIAL — CreateRoomObjects + push-block branch
 *             (FindAndCreatePushBlockObject @ Z_05.asm:5461) NOT yet
 *             drained; per-boss INIT/UPDATE bodies are PRIMARY.
 * Stance:     PARTIAL — boss_create_room_objects body is REPLACE
 *             (transpiled CreateRoomObjects exists at
 *             src/zelda_translated/z_05.asm:9033, replaced with native
 *             body); per-boss forwarders are ADOPT (thin call into
 *             drained enrt_*); push-block branch DEFERRED to follow-up.
 */

void boss_framework_room_init(unsigned char room_id);

#endif /* GAME_ENEMIES_BOSSES_BOSS_FRAMEWORK_H */
