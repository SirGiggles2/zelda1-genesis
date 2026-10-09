/* sram_backend.c — cart SRAM primitives for Debug.md, on SGDK.
 *
 * WHY THIS EXISTS
 * ---------------
 * sram_adapter.c declares four externs — _sram_enable, _sram_read_byte,
 * _sram_write_byte, _sram_load_save_slots, _sram_commit_save_slots — and
 * says they come from src/nes_io.asm. That file is part of the legacy
 * transpiled build and is NOT linked into Debug.md (grep build_debug.py:
 * zero references to nes_io). genesis_shell.asm, which called
 * _sram_load_save_slots at boot, is not linked either.
 *
 * Net effect before this file: Debug.md had no cart SRAM support at all.
 * sram_adapter.c had never been compiled, so nothing noticed. Linking
 * 142 KB of transpiled ASM to get five small routines would be the wrong
 * trade, so they are implemented here on SGDK's SRAM API instead.
 *
 * Per SGDK-1 this belongs in src/sgdk_adapter/: it is the only layer
 * permitted to include <genesis.h> and touch hardware directly.
 *
 * LAYOUT: three save slots of SRAM_SAVE_SLOT_BYTES (682) at SRAM offset
 * 0, matching sram_adapter.c's mirror window and save_serializer.h's
 * SAVE_SLOT_STRIDE. The mirror lives at SRAM_MIRROR_BASE ($FF6000).
 */

#include <genesis.h>
#include "sram_abi.h"

#define SRAM_MIRROR_BASE  ((volatile unsigned char *)0x00FF6000u)
#define SRAM_SLOTS_BYTES  (3u * SRAM_SAVE_SLOT_BYTES)   /* 2046 */

void _sram_enable(void)
{
    SRAM_enable();
}

unsigned char _sram_read_byte(unsigned short offset)
{
    unsigned char v;
    SRAM_enableRO();
    v = SRAM_readByte((u32)offset);
    SRAM_disable();
    return v;
}

void _sram_write_byte(unsigned short offset, unsigned char val)
{
    SRAM_enable();
    SRAM_writeByte((u32)offset, val);
    SRAM_disable();
}

/* T-100: the NES save block (nes_ram[$6000..$652F]) at cart SRAM logical
 * $000.., one byte per logical offset (SGDK maps it to the odd cart
 * bytes). Options live at $800 and are not touched. */
void sram_nes_save_block_load(volatile unsigned char *dst, unsigned short bytes)
{
    unsigned short i;
    SRAM_enableRO();
    for (i = 0u; i < bytes; ++i) {
        dst[i] = SRAM_readByte((u32)i);
    }
    SRAM_disable();
}

void sram_nes_save_block_store(const volatile unsigned char *src, unsigned short bytes)
{
    unsigned short i;
    SRAM_enable();
    for (i = 0u; i < bytes; ++i) {
        SRAM_writeByte((u32)i, src[i]);
    }
    SRAM_disable();
}

/* Cart SRAM -> work-RAM mirror. Called before any read of a slot so the
 * mirror reflects what is actually on the cart. */
void _sram_load_save_slots(void)
{
    unsigned int i;

    SRAM_enableRO();
    for (i = 0u; i < SRAM_SLOTS_BYTES; ++i) {
        SRAM_MIRROR_BASE[i] = SRAM_readByte((u32)i);
    }
    SRAM_disable();
}

/* Work-RAM mirror -> cart SRAM. This is the write that has to survive
 * power-off; everything upstream of it is volatile work RAM.
 *
 * Enable/disable brackets the whole sweep rather than each byte: leaving
 * SRAM mapped for the duration is both faster and avoids a torn window
 * mid-commit. */
void _sram_commit_save_slots(void)
{
    unsigned int i;

    SRAM_enable();
    for (i = 0u; i < SRAM_SLOTS_BYTES; ++i) {
        SRAM_writeByte((u32)i, SRAM_MIRROR_BASE[i]);
    }
    SRAM_disable();
}
