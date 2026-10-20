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
/* T-255: Remix second item slot. b_has/b_slot are this frame's B result. */
unsigned char hud_status_bar_c_item(unsigned char b_has, unsigned char b_slot,
                                    unsigned char *slot_out);
/* Shared NES occupied-slot search: 1 forward, 2 backward, 0 validate. */
void hud_select_inventory_item(unsigned char direction);
/* User-requested four-direction grid; direction uses SGDK D-pad bits.
 * Zero validates the existing selection. Letter shares potion cell7. */
void hud_move_inventory_item(unsigned char direction);
unsigned char hud_inventory_item_selectable(unsigned char slot);
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
/* Fixed Window owns these status-bar cells; native formatter still runs. */
unsigned char roomrom_hud_owns_status_bar(void);
/* Selector $44 (status bar map transfer) drained: show the map. */
void roomrom_hud_status_bar_map_cue(void);
/* A committed OW scroll reveals discovery on the following NMI. */
void roomrom_hud_overworld_discovery_cue(void);

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

/* Native NT0 HUD writes retain the installed HUD cell attributes. */
void roomrom_hud_transfer_tile(unsigned char col, unsigned char row, unsigned char tile);
/* Whole NT0 HUD record (rows 1..7, cols 0..31, count pre-clamped). */
void roomrom_hud_transfer_record(unsigned char col, unsigned char row,
                                 unsigned char count, unsigned char vertical,
                                 unsigned char repeat, const unsigned char *tiles);
/* T-172: HUD display epoch (changes with any HUD word/palette/force/clear)
 * and whether the word cache is authoritative (no forced redraw pending). */
unsigned char roomrom_hud_cache_epoch(void);
unsigned char roomrom_hud_cache_clean(void);


#endif
