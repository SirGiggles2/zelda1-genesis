/* hud_dispatch.h — native HUD subsystem dispatch (Phase 4).
 *
 * Native rewrite of src/oracle/hud/hud_runtime.c. Both ROMs link.
 *
 * Note: RoomRom has its own roomrom_hud.c for active HUD rendering;
 * this dispatch is the NES gameplay-tier formatter that fills the
 * NES OAM/transfer-buf mirror (used by Debug.md). RoomRom may bind
 * these natively once UW dialog/HUD-update paths port.
 */

#ifndef HUD_DISPATCH_H
#define HUD_DISPATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Pure native tile choice; index0 is the first heart. */
unsigned char hud_heart_tile(unsigned char hearts, unsigned char partial, unsigned char index);

/* Format heart icons into the dynamic transfer buf at start_off.
 * Reads RAM($000E)=hearts, RAM($000F)=partial. drain at hud_runtime.c:6-50. */
void hud_format_hearts_in_text_buf(unsigned char start_off);

/* Copy ZP $01/$02/$03 into TRANSFER_BUF_BYTE(base-2/-1/0). drain at
 * hud_runtime.c:52-57. */
void hud_copy_triplet_to_text_buf(void);

/* Format val into 3-digit BCD via cave_format_decimal_byte; clamp
 * leading $24 → 33; if tens is space, format doublet. drain at
 * hud_runtime.c:59-68. */
void hud_format_decimal_count_byte(unsigned char val);

/* RAM($00) = buf_offset; format_decimal_count_byte(val); copy_triplet.
 * drain at hud_runtime.c:70-74. */
void hud_format_decimal_count_byte_in_text_buf(unsigned char val,
                                                unsigned char buf_offset);

/* Fill 41-byte status bar transfer buf from template + LINK_HEARTS +
 * LINK_RUPEES + LINK_BOMB_COUNT + key count. drain at hud_runtime.c:76-93. */
void hud_format_status_bar_text(void);

/* Per-frame rupee animation tick: drain $067D credit accumulator into
 * LINK_RUPEES, drain CAVE_DOOR_REPAIR_RUPEE_DELTA into LINK_RUPEES--,
 * play sfx, refresh status bar. drain at hud_runtime.c:95-122. */
void hud_world_change_rupees(void);
void hud_world_fill_hearts(void);

/* Native currency state only; caller owns display scheduling. */
void hud_tick_native_rupees(unsigned char frame_counter);

#ifdef __cplusplus
}
#endif

#endif /* HUD_DISPATCH_H */
