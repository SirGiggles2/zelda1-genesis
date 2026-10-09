/*
 * SRAM API public surface (S1 Phase D, Task D4).
 *
 * Owned C in src/frontend/ and src/game/ calls sram_* functions here
 * instead of touching cart SRAM or the work-RAM mirror directly. The
 * implementation (src/sgdk_adapter/sram_adapter.c) forwards to the
 * existing hand-rolled helpers in src/nes_io.asm; Phase F may migrate
 * to SGDK SRAM_readByte / SRAM_writeByte per call site.
 *
 * SRAM layout (docs/audit/sram_map.md, locked at S0):
 *   0x000 - 0x7FF: 3 NES save slots (2048 bytes total)
 *   0x800 - 0x81F: OptionsState (32 bytes, struct defined at S8a)
 *   0x820 - 0x1FF8: unallocated reserved
 *   0x1FF9 - 0x1FFF: boot smoke-test sentinel
 *
 * Spec ref: 2026-04-27-native-genesis-rewrite-design.md Section 4.6
 * Plan retarget (step 3) deferred to Phase F - D4 is compile-only.
 */

#ifndef SRAM_ABI_H
#define SRAM_ABI_H

/* Forward declaration. struct OptionsState lives at
 * src/game/options/options_state.h (defined at S8a close); ABI header
 * keeps fwd-decl to avoid pulling game/-side header into the
 * substrate.  */
struct OptionsState;

/*
 * sram_save_load: copy one NES save slot from cart SRAM into dst.
 *   slot: 0, 1, or 2
 *   dst:  caller-allocated buffer of at least SRAM_SAVE_SLOT_BYTES bytes
 *
 * Internally calls _sram_load_save_slots (loads all three slots into the
 * work-RAM mirror at $FF6000) then copies the requested slot's bytes.
 */
void sram_save_load(unsigned char slot, void *dst);

/*
 * sram_save_store: write one NES save slot from src into cart SRAM.
 *   slot: 0, 1, or 2
 *   src:  caller buffer of SRAM_SAVE_SLOT_BYTES bytes
 *
 * Copies src into the work-RAM mirror slot, then calls
 * _sram_commit_save_slots to flush all three slots to cart.
 */
void sram_save_store(unsigned char slot, const void *src);

/*
 * sram_options_load: read the OptionsState region (SRAM 0x800..0x81F)
 * into *out. Live since S8a close.
 */
void sram_options_load(struct OptionsState *out);

/*
 * sram_options_store: write *in to the OptionsState region
 * (SRAM 0x800..0x81F). Live since S8a close.
 */
void sram_options_store(const struct OptionsState *in);

/* Size of one NES save slot in the 2 KB slot region.
 * 2048 bytes / 3 slots = 682 bytes each with 2 bytes remainder;
 * the layout uses a 682-byte stride (slot 0: 0..681, 1: 682..1363,
 * 2: 1364..2045, bytes 2046..2047 unused). */
#define SRAM_SAVE_SLOT_BYTES  682u

/* SRAM region offsets (mirrors docs/audit/sram_map.md). */
#define SRAM_SAVE_SLOTS_OFFSET     0x000u
#define SRAM_SAVE_SLOTS_SIZE       0x800u
#define SRAM_OPTIONS_STATE_OFFSET  0x800u
#define SRAM_OPTIONS_STATE_SIZE    0x020u

#endif /* SRAM_ABI_H */
