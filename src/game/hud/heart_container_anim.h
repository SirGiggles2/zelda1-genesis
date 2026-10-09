#ifndef ROOMROM_HUD_HEART_CONTAINER_ANIM_H
#define ROOMROM_HUD_HEART_CONTAINER_ANIM_H

/* Plan v5b T2.7 — 3-frame scale-up animation for newly added heart
 * container slot.
 *
 * NES Z_01.asm @TakeHeartContainer (line 4538) is instant — Items[$18]
 * gains $11 and the HUD repaints next vblank. RoomRom adds a 3-frame
 * fade-in over the new slot so the player can visually parse the
 * increase: EMPTY -> HALF -> FULL across ~30 frames (~500ms @ 60Hz).
 *
 * State is global; only one anim runs at a time (multiple back-to-back
 * heart container pickups overwrite the in-flight anim — the final
 * slot wins, which matches NES instant-update semantics anyway).
 *
 * Wiring:
 *   - inventory_add_heart_container() in inventory.c calls
 *     hud_heart_container_anim_start(new_slot_index).
 *   - roomrom_hud_refresh_dynamic() calls hud_heart_container_anim_tick()
 *     each vblank to advance the frame timer.
 *   - draw_hearts_row() in hud_runtime.c consults
 *     hud_heart_container_anim_override_tile() per slot.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Kick off animation. slot_index = 0..15 (the heart slot of the newly
 * gained container). Safe to call mid-anim (overrides the previous one). */
void hud_heart_container_anim_start(unsigned char slot_index);

/* Advance the per-frame timer. No-op when idle. */
void hud_heart_container_anim_tick(void);

/* Returns 1 while an animation is in flight (so the HUD refresh knows
 * to bypass its inventory-dirty gate). */
unsigned char hud_heart_container_anim_active(void);

/* When the anim is running and slot_index matches the target slot,
 * returns the animation frame tile (EMPTY -> HALF -> FULL). Otherwise
 * returns fallback. */
unsigned char hud_heart_container_anim_override_tile(unsigned char slot_index,
                                                     unsigned char fallback);

#ifdef __cplusplus
}
#endif

#endif /* ROOMROM_HUD_HEART_CONTAINER_ANIM_H */
