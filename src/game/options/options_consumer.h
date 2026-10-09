/* Phase 9 Task 9.4 — Option consumer wiring.
 *
 * Translates options_runtime live state into game-side behavior gates.
 * Each option id has a consumer site; some need init-time application
 * (start_hearts, bomb_upgrade affect inventory shape on game-start),
 * others are queried on-demand at runtime gate sites (ab_swap in input
 * dispatch, automap in HUD render, etc.).
 *
 * Drain Rule D1 stance: GREENFIELD (debate 004 — Redux extension; NES
 * Z1 has no options menu). No drain candidate.
 *
 * Hard rule: keep this header free of any RoomRom / game subsystem
 * includes. Consumers route through narrow accessors so the call sites
 * inside RoomRom remain the only places that touch both worlds.
 */

#ifndef SRC_GAME_OPTIONS_OPTIONS_CONSUMER_H
#define SRC_GAME_OPTIONS_OPTIONS_CONSUMER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Apply game-start option-driven seed values to the singleton inventory.
 *
 * Reads:
 *   OPTION_ID_START_HEARTS  — clamps to [1..15] and packs into
 *                              heart_values nibble pair.
 *   OPTION_ID_BOMB_UPGRADE  — VANILLA -> 8, PLUS4 -> 12, PLUS8 -> 16.
 *
 * Call once after options_persistence_load_or_default at game-start
 * (roomrom_debug_enter), BEFORE any inventory-displaying or
 * heart-consuming code runs. */
void options_consumer_apply_inventory_at_start(void);

/* Boolean-bit getters routed through options_get; centralizes id ->
 * runtime translation so consumer call sites read clearly.
 *
 * Returns 0 (off) or 1 (on) per the bool group bit. */
unsigned char options_consumer_get_low_health_warning(void);
unsigned char options_consumer_get_automap(void);
unsigned char options_consumer_get_dungeon_colors(void);
unsigned char options_consumer_get_visible_secrets(void);
unsigned char options_consumer_get_diagonal_sword(void);
unsigned char options_consumer_get_no_reduced_flashing(void);
unsigned char options_consumer_get_ab_swap(void);
unsigned char options_consumer_get_auto_collect_drops(void);

/* Enum getters route through the same accessor; values are
 * OPTIONS_<group>_<value> constants from options_state.h. */
unsigned char options_consumer_get_sword_style(void);
unsigned char options_consumer_get_like_like_behavior(void);
unsigned char options_consumer_get_bomb_upgrade(void);
unsigned char options_consumer_get_lost_woods(void);
unsigned char options_consumer_get_dark_room_light(void);
unsigned char options_consumer_get_room_scroll(void);

/* Numeric — clamped before return so callers don't need to re-clamp. */
unsigned char options_consumer_get_start_hearts(void);

#ifdef __cplusplus
}
#endif

#endif /* SRC_GAME_OPTIONS_OPTIONS_CONSUMER_H */
