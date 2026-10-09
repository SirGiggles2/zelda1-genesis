/* Phase 9 Task 9.2 — SRAM byte-IO adapter for OptionsState.
 *
 * Adapter implementation: includes <genesis.h> (allowed in
 * src/sgdk_adapter/) and wraps SGDK SRAM_enable / SRAM_readByte /
 * SRAM_writeByte. Gameplay-side persistence layer uses the public
 * surface in sram_options_io.h.
 *
 * SRAM logical offset 0x800 = first byte of OptionsState region.
 * SGDK SRAM_readByte / SRAM_writeByte translate logical->physical
 * (offset * 2 + SRAM_BASE) internally; we pass logical offsets only.
 *
 * Save-slot region preservation: the public read/write entry points
 * only target offsets 0x800..(0x800+size-1). The _at variants accept
 * arbitrary offsets and are intended for the persistence probe.
 */

#include <genesis.h>

#include "sram_options_io.h"

#define SRAM_OPTIONS_BASE  0x800u

void sram_options_io_read_at(unsigned int offset, unsigned char *buf,
                             unsigned int size)
{
    unsigned int i;

    if (buf == 0) return;

    SRAM_enable();
    for (i = 0u; i < size; ++i) {
        buf[i] = SRAM_readByte((u32)offset + i);
    }
    SRAM_disable();
}

void sram_options_io_write_at(unsigned int offset, const unsigned char *buf,
                              unsigned int size)
{
    unsigned int i;

    if (buf == 0) return;

    SRAM_enable();
    for (i = 0u; i < size; ++i) {
        SRAM_writeByte((u32)offset + i, buf[i]);
    }
    SRAM_disable();
}

void sram_options_io_read(unsigned char *buf, unsigned int size)
{
    sram_options_io_read_at(SRAM_OPTIONS_BASE, buf, size);
}

void sram_options_io_write(const unsigned char *buf, unsigned int size)
{
    sram_options_io_write_at(SRAM_OPTIONS_BASE, buf, size);
}
