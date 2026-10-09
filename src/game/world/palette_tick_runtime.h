#ifndef ROOMROM_PALETTE_TICK_H
#define ROOMROM_PALETTE_TICK_H

/* Phase 2.6.5: NES frame-cadence palette toggle framework for RoomRom.
 *
 * Substrate: palette_state.h + palette_tick.c (substrate, compiled into
 * RoomRom per build.bat step 3). Pure C, no SGDK API calls.
 *
 * RoomRom Phase 2 toggle inventory (see docs/audit/palette_toggle_inventory.md):
 *   intro_item_flash_8f  — Debug.md scope; not wired here.
 *   link_hit_invuln      — sprite suppression (roomrom_sprites.c), NOT palette.
 *   low_health_flash     — audio beep only in NES Z_07; no palette toggle.
 *   boss_aquamentus      — (InvTimer & 3) XOR 3 per-sprite attr; Phase 8 scope.
 *   heart_container      — screen flash at pickup; Phase 6 scope.
 *   triforce_clear       — screen flash at dungeon clear; Phase 5 scope.
 *
 * Phase 2 wires the plumbing with an empty toggle table. Phase 5+ register
 * entries via palette_register_toggle() before calling roomrom_palette_tick_init.
 *
 * API:
 *   roomrom_palette_tick_init(palram32)        — call at scene load
 *   roomrom_palette_tick_frame(frame_counter)  — call once per VBlank frame
 */

void roomrom_palette_tick_init(const unsigned char *palram32);
void roomrom_palette_tick_frame(unsigned short frame_counter);

#endif
