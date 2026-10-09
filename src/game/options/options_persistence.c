/* Phase 9 Task 9.2 — Options SRAM persistence (GREENFIELD).
 *
 * Implementation: bridges options_runtime (Task 9.1) and the SGDK
 * adapter src/sgdk_adapter/sram_options_io.{h,c}. Lives under
 * src/game/ so SGDK-1 boundary is respected (no <genesis.h> include).
 *
 * Storage region: SRAM logical offset 0x800..0x81F (32 bytes), locked
 * at S0 in docs/audit/sram_map.md. Save slots $000..$7FF are owned by
 * sram_save_load / sram_save_store and are NOT touched here.
 */

#include "options_persistence.h"
#include "options_runtime.h"
#include "options_state.h"
#include "../../sgdk_adapter/sram_options_io.h"

unsigned char options_persistence_image_is_blank(const unsigned char *buf,
                                                 unsigned int size)
{
    unsigned int i;
    unsigned char zero_only = 1u;
    unsigned char ff_only = 1u;

    if (buf == 0 || size == 0u) return 1u;

    for (i = 0u; i < size; ++i) {
        if (buf[i] != 0x00u) zero_only = 0u;
        if (buf[i] != 0xFFu) ff_only = 0u;
        if (zero_only == 0u && ff_only == 0u) return 0u;
    }
    return 1u;
}

unsigned char options_persistence_load_or_default(void)
{
    unsigned char buf[OPTIONS_STATE_SIZE];

    /* Always seed runtime defaults first so a blank or corrupt SRAM
     * cell still leaves g_options in a valid (vanilla) state. Without
     * this, options_get() returns garbage from .bss zero-fill, and
     * downstream consumers like options_consumer_apply_inventory_at_start
     * would set heart_values=0x00 -> 0 hearts. */
    options_runtime_init();

    sram_options_io_read(buf, OPTIONS_STATE_SIZE);

    if (options_persistence_image_is_blank(buf, OPTIONS_STATE_SIZE) != 0u) {
        /* Uninitialized SRAM. Defaults already loaded above. Caller can
         * commit() later to seed real bytes; we do not auto-write here
         * so a fresh boot remains observable in the persistence probe. */
        return 0u;
    }

    if (options_runtime_apply(buf, OPTIONS_STATE_SIZE) == 0u) {
        /* Bad magic / version / checksum / range. Defaults survive from
         * options_runtime_init above. */
        return 0u;
    }

    return 1u;
}

void options_persistence_commit(void)
{
    unsigned char buf[OPTIONS_STATE_SIZE];
    unsigned int written;

    written = options_runtime_serialize(buf, OPTIONS_STATE_SIZE);
    if (written != OPTIONS_STATE_SIZE) return;

    sram_options_io_write(buf, OPTIONS_STATE_SIZE);
}
