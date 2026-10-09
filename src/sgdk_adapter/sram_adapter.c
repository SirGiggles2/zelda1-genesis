/*
 * SRAM adapter implementation (S1 Phase D, Task D4).
 *
 * Forwards save-slot I/O to the bulk helpers in src/nes_io.asm and
 * maps the OptionsState region via the per-byte helpers. The work-RAM
 * mirror at $FF6000 holds all three NES save slots in flat layout;
 * sram_save_load and sram_save_store copy one slot's worth of bytes
 * between that mirror and the caller's buffer.
 *
 * Phase F may replace the per-byte helpers with SGDK SRAM_readByte /
 * SRAM_writeByte once the SGDK link is fully live. The adapter exists
 * now so call sites can be retargeted without changing signatures.
 *
 * Compile-only at S1 Phase D: no live caller exists until F-phase
 * frontend cutover. The .o is produced and dropped (not in LD_RESP).
 * Plan D4 step 3 (retarget save-menu C call sites) deferred to Phase F.
 *
 * SRAM mirror layout (docs/audit/sram_map.md, locked at S0):
 *   work-RAM mirror base: $FF6000
 *   slot 0: bytes   0 .. 681
 *   slot 1: bytes 682 .. 1363
 *   slot 2: bytes 1364 .. 2045
 *   OptionsState: SRAM logical offset 0x800, 32 bytes
 */

#include "sram_adapter.h"

/* Genesis work-RAM address of the NES SRAM mirror (3 save slots).
 * _sram_load_save_slots copies cart SRAM 0x000..0x7FF here at boot.
 * _sram_commit_save_slots writes it back to cart. */
#define SRAM_MIRROR_BASE  ((volatile unsigned char *)0x00FF6000u)

/* Forward declarations of existing ASM helpers. Definitions live in
 * src/nes_io.asm. Declared here to avoid pulling in ASM-specific
 * headers at the adapter layer. */

/* Load all 3 save slots from cart SRAM into work-RAM mirror. */
extern void _sram_load_save_slots(void);

/* Commit all 3 save slots from work-RAM mirror to cart SRAM. */
extern void _sram_commit_save_slots(void);

/* Enable cart SRAM mapper ($A130F1 := 1). */
extern void _sram_enable(void);

/* Read one byte from logical SRAM offset passed in D0.w.
 * Returns byte in D0.b (GCC ABI: D0 is first return register). */
extern unsigned char _sram_read_byte(unsigned short offset);

/* Write one byte to logical SRAM offset in D0.w; byte in D1.b.
 * GCC ABI: first arg D0, second arg D1. */
extern void _sram_write_byte(unsigned short offset, unsigned char val);

/* ---- Internal helpers ---- */

/* memcpy substitute: copy n bytes from src to dst.
 * Avoids pulling in libc; the adapter is -ffreestanding -nostdlib. */
static void adapter_memcpy(void *dst, const void *src, unsigned int n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    unsigned int i;
    for (i = 0u; i < n; i++) {
        d[i] = s[i];
    }
}

/* ---- Public API ---- */

void sram_save_load(unsigned char slot, void *dst)
{
    unsigned int base;

    if (slot > 2u) {
        return;
    }

    /* Refresh work-RAM mirror from cart SRAM so we have the latest data.
     * _sram_load_save_slots loads all three slots (0x000..0x7FF). */
    _sram_load_save_slots();

    /* Copy the requested slot from the mirror into dst. */
    base = (unsigned int)slot * SRAM_SAVE_SLOT_BYTES;
    adapter_memcpy(dst, (const void *)(SRAM_MIRROR_BASE + base),
                   SRAM_SAVE_SLOT_BYTES);
}

void sram_save_store(unsigned char slot, const void *src)
{
    unsigned int base;

    if (slot > 2u) {
        return;
    }

    /* Refresh mirror first so we don't clobber slots we aren't writing.
     * Then overwrite the requested slot in the mirror and commit all. */
    _sram_load_save_slots();

    base = (unsigned int)slot * SRAM_SAVE_SLOT_BYTES;
    adapter_memcpy((void *)(SRAM_MIRROR_BASE + base), src,
                   SRAM_SAVE_SLOT_BYTES);

    _sram_commit_save_slots();
}

void sram_options_load(struct OptionsState *out)
{
    /* S8a closed: struct OptionsState lives at src/game/options/
     * options_state.h:127. SRAM_OPTIONS_STATE_SIZE is the canonical
     * byte length; caller is responsible for passing a properly-sized
     * struct OptionsState pointer. */
    unsigned short i;
    unsigned char *p = (unsigned char *)out;

    if (!p) {
        return;
    }

    _sram_enable();
    for (i = 0u; i < (unsigned short)SRAM_OPTIONS_STATE_SIZE; i++) {
        p[i] = _sram_read_byte(
                   (unsigned short)(SRAM_OPTIONS_STATE_OFFSET + i));
    }
}

void sram_options_store(const struct OptionsState *in)
{
    /* TODO(S8a): same note as sram_options_load. */
    unsigned short i;
    const unsigned char *p = (const unsigned char *)in;

    if (!p) {
        return;
    }

    _sram_enable();
    for (i = 0u; i < (unsigned short)SRAM_OPTIONS_STATE_SIZE; i++) {
        _sram_write_byte(
            (unsigned short)(SRAM_OPTIONS_STATE_OFFSET + i),
            p[i]);
    }
}
