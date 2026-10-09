/* inventory_render.h — Phase 6 P6.2 pause subscreen renderer.
 *
 * Draws the NES Z1 inventory subscreen to Genesis Plane A when the
 * pause flag transitions to active. V1 = static BG tilemap layout
 * + sprite-based item icons + B-item cursor.
 *
 * Hook points:
 *   - inventory_subscreen_enter() — call when pause transitions OFF->ON
 *     (clears Plane A, writes subscreen tilemap, populates SAT with
 *     item icon sprites for owned items).
 *   - inventory_subscreen_exit()  — call when pause transitions ON->OFF
 *     (signals room renderer to re-paint Plane A from gameplay state).
 *   - inventory_subscreen_tick(joy_state) — call per-frame while paused
 *     (handles D-pad cursor movement + A-press B-item selection).
 *
 * NES reference: Z_05.asm:152 UpdateMenuAndMeters; :156 UpdateMenu;
 * Z_07.asm:868 DrawItemInInventory.
 */
#ifndef INVENTORY_RENDER_H
#define INVENTORY_RENDER_H

void inventory_subscreen_enter(void);
/* T-172: build the menu's static cells (both variants) ahead of the first pause. */
void inventory_menu_cells_build(void);
void inventory_subscreen_exit(void);
void inventory_subscreen_tick(unsigned char joy_state);

/* Query: is the subscreen currently being displayed? */
unsigned char inventory_subscreen_is_active(void);

/* Query: scroll-out finished (gameplay should resume + room reload). */
unsigned char inventory_subscreen_scrolled_out(void);

/* NES UpdateMenuActive state: menu fully down, taking input. */
unsigned char inventory_subscreen_menu_active(void);
/* Drop the subscreen at once (GameMode 8 from the menu, T-013). */
void inventory_subscreen_abort(void);

/* Phase 8: NES HUD B-item slot. Look up Genesis VRAM tile for given
 * cursor slot 0..8. Returns 0xFFFF if slot is unmapped (e.g. raft/ring
 * not extracted). Caller emits SAT entry at HUD B-box position. */
unsigned short inventory_get_vram_tile_for_slot(unsigned char cursor_slot);

/* Genesis PAL register (0..3) per NES sub_pal for inventory slot. */
unsigned char inventory_get_pal_for_slot(unsigned char cursor_slot);

#endif /* INVENTORY_RENDER_H */
