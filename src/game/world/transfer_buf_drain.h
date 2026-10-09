#ifndef TRANSFER_BUF_DRAIN_H
#define TRANSFER_BUF_DRAIN_H

/* Plan v5b — TRANSFER_BUF → CRAM substrate bridge.
 *
 * NES Z_06.asm:555-643 TransferCurTileBuf / @TransferTileBuf walks
 * record-formatted bytes at DynTileBuf ($0301) and streams them to
 * PpuData_2007. On Genesis there is no PPU; without a C-side drain,
 * every TRANSFER_BUF writer (world_animate_world_fading, room
 * play-area copy, Mode 11 dead-Link palette cue, etc.) lands in a
 * dead buffer and the user sees no visual effect.
 *
 * This module consumes the buffer each tick and forwards $3Fxx
 * (palette) writes to Genesis CRAM via render_cram_write_color.
 * Nametable writes ($20-$2F) are recognised and skipped (drained
 * without rendering) so they do not pile up; a future task will wire
 * them through render_set_plane_a_word.
 *
 * Two source surfaces both follow the same record encoding:
 *
 *   A) Dynamic TRANSFER_BUF at nes_ram[$0302..], cursor at $0301
 *      (TRANSFER_BUF_POS). Writers: world_animate_world_fading,
 *      room column-attr copies, etc.
 *
 *   B) Static TileBufSelector at $0014 -> TransferBufAddrs[selector]
 *      -> const buffer baked into the asm (Z_06.asm:489+). Writers:
 *      Mode 11 Sub9 dead-Link palette cue, Mode 12 EndLevel attr
 *      cues, room mode transition palette/attr cues, etc.
 *
 * Record format per NES TransferTileBuf:
 *   [hi, lo, ctrl, ...bytes, hi_of_next_record_or_$FF]
 *   ctrl bottom 6 bits = byte count (0 means 64 per NES).
 *   ctrl bit 6 / bit 7 = repeat-tile / vert auto-inc — ignored for
 *   $3F since palette writes are flat byte streams.
 *
 * Drain mapping for $3F (NES palram 0..31 -> Genesis CRAM slot 0..31):
 *   NES $3F00..$3F0F (BG palette) ->  Genesis PAL0 colors 0..15
 *   NES $3F10..$3F1F (SPR palette) -> Genesis PAL1 colors 0..15
 * (Identity mapping; NES color bytes go through
 *  roomrom_bg_palette_nes_to_cram for the BGR-444 conversion.)
 *
 * NES source : reference/aldonunez/Z_06.asm:489-643 TransferBufAddrs
 *              + TransferCurTileBuf + TransferTileBuf
 * Drained C  : NEW (this file)
 * Coverage   : PARTIAL (\$3F palette via both dynamic + static paths;
 *              static selectors limited to Mode11DeadLinkPalette \$2C
 *              today, extend resolve_static_buffer for more;
 *              \$20-\$2F nametable writes skipped pending plane bridge)
 * Stance     : EXTEND
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Drain pending TRANSFER_BUF records. Resets TRANSFER_BUF_POS to 0
 * and stamps a $FF sentinel at byte 0 so a re-entry with no new
 * writes is a no-op. Safe to call per tick. */
/* T-172: native stand-in for an NES DynTileBuf record this tick. */
void transfer_buf_note_native_record(void);
/* 1 while DynTileBuf holds a record (NES or native) until the drain. */
unsigned char transfer_buf_dyn_busy(void);
void transfer_buf_drain(void);

/* T-097: forget attribute writes (a new room was laid out). */
void transfer_buf_attr_shadow_reset(void);
/* NES TurnOffVideoAndClearArtifacts: the name table is blank (then holds
 * its mode's text), no longer PlayAreaTiles, until the next room load. */
void transfer_buf_nt_cleared(void);

#ifdef __cplusplus
}
#endif

#endif /* TRANSFER_BUF_DRAIN_H */
