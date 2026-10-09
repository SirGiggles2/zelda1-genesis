/* Phase 9 Task 9.4 — Option consumer probe.
 *
 * Probe RAM contract ($FF7EB0..$FF7EBF):
 *   [0]   = 'C' (0x43) magic
 *   [1]   = 'N' (0x4E) magic
 *   [2]   = version (3 — negative-path coverage extension)
 *   [3]   = total tests run (34)
 *   [4]   = passes
 *   [5]   = bits  0..7  (1 = pass)
 *   [6]   = bits  8..15
 *   [7]   = bits 16..23
 *   [8]   = bits 24..31
 *   [9]   = bits 32..33  (only low 2 bits live)
 *   [10..15] reserved
 *
 * Tests by group:
 *   Group A (bits 0..4)   — apply_inventory_at_start (5 tests, v1).
 *   Group B (bits 5..18)  — set-then-get accessor coverage (14 tests, v2).
 *   Group C (bits 19..26) — bool set-zero returns zero (8 tests, v3).
 *   Group D (bits 27..31) — enum out-of-range rejected (5 tests, v3).
 *   Group E (bits 32..33) — start_hearts edge clamps (2 tests, v3).
 *
 * Group A:
 *   bit0:  defaults_apply_yields_3_hearts_8_bombs
 *   bit1:  start_hearts_7_yields_0x77
 *   bit2:  bomb_upgrade_plus4_yields_12
 *   bit3:  bomb_upgrade_plus8_yields_16
 *   bit4:  start_hearts_16_clamps_to_15
 *
 * Group B (positive-path getters):
 *   bit5:  getter_low_health_warning_set_returns_one
 *   bit6:  getter_automap_set_returns_one
 *   bit7:  getter_dungeon_colors_set_returns_one
 *   bit8:  getter_visible_secrets_set_returns_one
 *   bit9:  getter_diagonal_sword_set_returns_one
 *   bit10: getter_no_reduced_flashing_set_returns_one
 *   bit11: getter_ab_swap_set_returns_one
 *   bit12: getter_auto_collect_drops_set_returns_one
 *   bit13: getter_sword_style_beam_always_returns_two
 *   bit14: getter_like_like_no_eat_returns_one
 *   bit15: getter_bomb_upgrade_plus8_returns_two
 *   bit16: getter_lost_woods_relaxed_returns_one
 *   bit17: getter_dark_room_bright_returns_two
 *   bit18: getter_start_hearts_5_returns_5
 *
 * Group C (bool set-zero):
 *   bit19: getter_low_health_warning_set_zero_returns_zero
 *   bit20: getter_automap_set_zero_returns_zero
 *   bit21: getter_dungeon_colors_set_zero_returns_zero
 *   bit22: getter_visible_secrets_set_zero_returns_zero
 *   bit23: getter_diagonal_sword_set_zero_returns_zero
 *   bit24: getter_no_reduced_flashing_set_zero_returns_zero
 *   bit25: getter_ab_swap_set_zero_returns_zero
 *   bit26: getter_auto_collect_drops_set_zero_returns_zero
 *
 * Group D (enum out-of-range rejected — set keeps prior value):
 *   bit27: enum_sword_style_oob_rejected
 *   bit28: enum_like_like_oob_rejected
 *   bit29: enum_bomb_upgrade_oob_rejected
 *   bit30: enum_lost_woods_oob_rejected
 *   bit31: enum_dark_room_oob_rejected
 *
 * Group E (start_hearts clamps):
 *   bit32: start_hearts_zero_clamps_to_min
 *   bit33: start_hearts_default_is_min
 */

#ifndef SRC_GAME_OPTIONS_PROBES_OPTIONS_CONSUMER_PROBE_H
#define SRC_GAME_OPTIONS_PROBES_OPTIONS_CONSUMER_PROBE_H

#define OPTIONS_CONSUMER_PROBE_BASE  0x00FF7EB0UL

#ifdef __cplusplus
extern "C" {
#endif

void options_consumer_probe_run(void);

#ifdef __cplusplus
}
#endif

#endif /* SRC_GAME_OPTIONS_PROBES_OPTIONS_CONSUMER_PROBE_H */
