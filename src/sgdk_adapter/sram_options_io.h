/* Phase 9 Task 9.2 — SRAM byte-IO adapter for OptionsState.
 *
 * Public surface for the GREENFIELD options persistence layer
 * (src/game/options/options_persistence.c). Wraps SGDK SRAM_enable /
 * SRAM_readByte / SRAM_writeByte so gameplay code stays free of
 * <genesis.h> per SGDK-1.
 *
 * Locked SRAM region: $800..$81F (32 bytes), reserved at S0 in
 * docs/audit/sram_map.md. Save slots at $000..$7FF are NOT touched
 * by this adapter; sram_save_load / sram_save_store
 * (src/abi/sram_abi.h) own that range.
 *
 * The _at variants take an explicit logical offset and exist so the
 * persistence probe can verify cross-region preservation (write
 * sentinel outside the locked range, commit options, read sentinel
 * back). They are not for general gameplay use.
 */

#ifndef SRC_SGDK_ADAPTER_SRAM_OPTIONS_IO_H
#define SRC_SGDK_ADAPTER_SRAM_OPTIONS_IO_H

#ifdef __cplusplus
extern "C" {
#endif

/* Read `size` bytes from SRAM logical offset 0x800 into `buf`. */
void sram_options_io_read(unsigned char *buf, unsigned int size);

/* Write `size` bytes from `buf` to SRAM logical offset 0x800.
 * Does not touch save-slot region $000..$7FF. */
void sram_options_io_write(const unsigned char *buf, unsigned int size);

/* Generic offset variants — used by the persistence probe to verify
 * region boundary preservation. */
void sram_options_io_read_at(unsigned int offset, unsigned char *buf,
                             unsigned int size);
void sram_options_io_write_at(unsigned int offset, const unsigned char *buf,
                              unsigned int size);

#ifdef __cplusplus
}
#endif

#endif /* SRC_SGDK_ADAPTER_SRAM_OPTIONS_IO_H */
