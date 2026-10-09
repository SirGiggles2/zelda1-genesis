#ifndef ROOMROM_HUD_H
#define ROOMROM_HUD_H

void roomrom_hud_upload_chr(void);
/* T-118: force the next roomrom_hud_draw to rebuild the whole window
 * (call after writing the Window plane outside this module). */
void roomrom_hud_invalidate(void);
/* T-132: blank the counts and hearts (level entry, NES modes 3/4). */
void roomrom_hud_set_counts_hidden(unsigned char hidden);
/* T-092: NES status-bar B selection; see hud_runtime.c. */
unsigned char hud_status_bar_b_item(unsigned char *slot_out);
/* Shared NES occupied-slot search: 1 forward, 2 backward, 0 validate. */
void hud_select_inventory_item(unsigned char direction);
void roomrom_hud_draw(unsigned char hud_id, unsigned char room_id,
                      unsigned char is_underworld);

/* Phase 6 Task 6.10.6 (Step A): per-frame live overlay of count/heart
 * cells from g_inventory. Cheap; safe to call every frame after the
 * first roomrom_hud_draw(). No-op until HUD has been drawn at least
 * once. */
void roomrom_hud_refresh_dynamic(void);
/* T-172: play-tick refresh on the NES status-bar cadence (even FrameCounter). */
void roomrom_hud_refresh_play(void);
void roomrom_hud_preset_counts_hidden(void);
void roomrom_hud_play_flush(void);
/* NES FormatStatusBarText ran this tick (World_ChangeRupees). */
void roomrom_hud_status_bar_formatted(void);
/* Selector $44 (status bar map transfer) drained: show the map. */
void roomrom_hud_status_bar_map_cue(void);

/* Plan v5c T6.5 — per-frame mini-map position marker + palette flash.
 * NES Z_01.asm:4095-4146 renders flashing marker sprite at computed
 * (x,y) from room_id every 16 frames. Port: BG tile $51 with palette
 * alternation. No-op for UW (redux automap path) or before HUD drawn. */
void roomrom_hud_refresh_marker(unsigned char room_id,
                                unsigned char is_underworld,
                                unsigned char frame_counter);

/* V2.4k (2026-05-26): HUD position toggle. NES Z1 gameplay HUD at TOP;
 * inventory subscreen HUD at BOTTOM. Caller toggles via this API.
 * Switches Window plane position via VDP + repositions HUD tile writes
 * via HUD_WIN_ROW_BASE + re-issues full HUD redraw at new row offset. */
void roomrom_hud_set_bottom_mode(unsigned char bottom);

#endif
