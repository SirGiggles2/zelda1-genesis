/* Phase 9 Task 9.2 — Options SRAM persistence in-ROM probe.
 *
 * Boot-time self-test for the GREENFIELD options persistence layer.
 * Drives blank-detection logic + commit/load roundtrip against live
 * cart SRAM (writes to logical 0x800..0x81F + a sentinel slot
 * outside the locked range to verify boundary preservation).
 *
 * Hard rule WT-5: lives at src/game/options/probes/, not engine/.
 *
 * Block layout @ OPTIONS_PERSISTENCE_PROBE_BASE = 0xFF7E90
 * (the slot adjacent to the Task 9.1 options_probe at 0xFF7E80;
 * enemy probe blocks own $7E00..$7FE7, so $7E80..$7E9F is two free
 * 16-byte slots reserved for the options subsystem):
 *   [0]   = 'P'  (0x50) magic
 *   [1]   = 'S'  (0x53) magic
 *   [2]   = OPTIONS_PERSISTENCE_PROBE_VERSION (1)
 *   [3]   = test_count
 *   [4]   = pass_count
 *   [5..12] = per-test pass-bit. Test ids:
 *     0  blank_detector_zeros           (logic)
 *     1  blank_detector_ffs             (logic)
 *     2  blank_detector_mixed_returns_0 (logic)
 *     3  commit_then_load_roundtrip     (SRAM)
 *     4  commit_preserves_outside_region(SRAM boundary)
 *     5  load_with_blank_returns_zero   (SRAM)
 *     6  load_with_corrupt_returns_zero (SRAM)
 *     7  load_with_valid_returns_one    (SRAM)
 *   [13..15] = reserved
 */

#include "../options_persistence.h"
#include "../options_runtime.h"
#include "../options_state.h"
#include "../../../sgdk_adapter/sram_options_io.h"

#define OPTIONS_PERSISTENCE_PROBE_BASE     0x00FF7E90UL
#define OPTIONS_PERSISTENCE_PROBE_VERSION  0x01u
#define OPTIONS_PERSISTENCE_PROBE_COUNT    8u

/* Sentinel offset just outside the locked OptionsState region.
 * Used by test 4 to verify commit() doesn't smear past 0x81F. */
#define SENTINEL_OFFSET   0x900u
#define SENTINEL_LEN      4u

static unsigned char run_test(unsigned int id);

void options_persistence_probe_run(void)
{
    volatile unsigned char *block =
        (volatile unsigned char *)OPTIONS_PERSISTENCE_PROBE_BASE;
    unsigned char saved_options[OPTIONS_STATE_SIZE];
    unsigned char saved_sentinel[SENTINEL_LEN];
    unsigned int i;
    unsigned int passes = 0u;

    sram_options_io_read(saved_options, OPTIONS_STATE_SIZE);
    sram_options_io_read_at(SENTINEL_OFFSET, saved_sentinel, SENTINEL_LEN);

    block[0] = 0x50u; /* 'P' */
    block[1] = 0x53u; /* 'S' */
    block[2] = OPTIONS_PERSISTENCE_PROBE_VERSION;
    block[3] = (unsigned char)OPTIONS_PERSISTENCE_PROBE_COUNT;
    block[4] = 0u;

    for (i = 0u; i < OPTIONS_PERSISTENCE_PROBE_COUNT; ++i) {
        unsigned char ok = run_test(i);
        block[5u + i] = ok;
        if (ok != 0u) ++passes;
    }
    block[4]  = (unsigned char)passes;
    block[13] = 0u;
    block[14] = 0u;
    block[15] = 0u;

    sram_options_io_write(saved_options, OPTIONS_STATE_SIZE);
    sram_options_io_write_at(SENTINEL_OFFSET, saved_sentinel, SENTINEL_LEN);
}

static unsigned char test_blank_detector_zeros(void)
{
    unsigned char buf[OPTIONS_STATE_SIZE];
    unsigned int i;
    for (i = 0u; i < OPTIONS_STATE_SIZE; ++i) buf[i] = 0x00u;
    return options_persistence_image_is_blank(buf, OPTIONS_STATE_SIZE);
}

static unsigned char test_blank_detector_ffs(void)
{
    unsigned char buf[OPTIONS_STATE_SIZE];
    unsigned int i;
    for (i = 0u; i < OPTIONS_STATE_SIZE; ++i) buf[i] = 0xFFu;
    return options_persistence_image_is_blank(buf, OPTIONS_STATE_SIZE);
}

static unsigned char test_blank_detector_mixed_returns_0(void)
{
    unsigned char buf[OPTIONS_STATE_SIZE];
    unsigned int i;
    for (i = 0u; i < OPTIONS_STATE_SIZE; ++i) buf[i] = 0x00u;
    buf[5] = 0x42u; /* one non-zero byte makes it non-blank */
    return (options_persistence_image_is_blank(buf, OPTIONS_STATE_SIZE) == 0u)
           ? 1u : 0u;
}

static unsigned char test_commit_then_load_roundtrip(void)
{
    options_runtime_init();
    options_set(OPTION_ID_AUTOMAP, 1u);
    options_set(OPTION_ID_DUNGEON_COLORS, 1u);
    options_set(OPTION_ID_SWORD_STYLE, OPTIONS_SWORD_BEAM_ALWAYS);
    options_set(OPTION_ID_START_HEARTS, 7u);
    options_persistence_commit();

    /* Wipe live state. */
    options_runtime_init();
    if (options_get(OPTION_ID_AUTOMAP) != 0u) return 0u;

    if (options_persistence_load_or_default() == 0u) return 0u;
    if (options_get(OPTION_ID_AUTOMAP) != 1u) return 0u;
    if (options_get(OPTION_ID_DUNGEON_COLORS) != 1u) return 0u;
    if (options_get(OPTION_ID_SWORD_STYLE) != OPTIONS_SWORD_BEAM_ALWAYS) return 0u;
    if (options_get(OPTION_ID_START_HEARTS) != 7u) return 0u;
    return 1u;
}

static unsigned char test_commit_preserves_outside_region(void)
{
    unsigned char sentinel[SENTINEL_LEN] = { 0xDEu, 0xADu, 0xBEu, 0xEFu };
    unsigned char readback[SENTINEL_LEN];
    unsigned int i;

    /* Stamp sentinel outside locked OptionsState region. */
    sram_options_io_write_at(SENTINEL_OFFSET, sentinel, SENTINEL_LEN);

    /* Commit options — must touch only 0x800..0x81F. */
    options_runtime_init();
    options_set(OPTION_ID_AUTOMAP, 1u);
    options_persistence_commit();

    /* Sentinel survives. */
    for (i = 0u; i < SENTINEL_LEN; ++i) readback[i] = 0u;
    sram_options_io_read_at(SENTINEL_OFFSET, readback, SENTINEL_LEN);
    for (i = 0u; i < SENTINEL_LEN; ++i) {
        if (readback[i] != sentinel[i]) return 0u;
    }
    return 1u;
}

static unsigned char test_load_with_blank_returns_zero(void)
{
    unsigned char blank[OPTIONS_STATE_SIZE];
    unsigned int i;

    for (i = 0u; i < OPTIONS_STATE_SIZE; ++i) blank[i] = 0xFFu;
    sram_options_io_write(blank, OPTIONS_STATE_SIZE);

    options_runtime_init();
    /* Dirty the live state: load_or_default must reset it to defaults
     * (it seeds options_runtime_init() first so a blank cart never leaves
     * garbage such as 0 start hearts) and report "nothing loaded". */
    options_set(OPTION_ID_AUTOMAP, 1u);

    if (options_persistence_load_or_default() != 0u) return 0u;
    if (options_get(OPTION_ID_AUTOMAP) != 0u) return 0u;
    return 1u;
}

static unsigned char test_load_with_corrupt_returns_zero(void)
{
    unsigned char buf[OPTIONS_STATE_SIZE];

    /* Seed valid image then corrupt it. */
    options_runtime_init();
    options_set(OPTION_ID_AUTOMAP, 1u);
    (void)options_runtime_serialize(buf, OPTIONS_STATE_SIZE);
    buf[0] = 0xAAu; /* bad magic */
    sram_options_io_write(buf, OPTIONS_STATE_SIZE);

    options_runtime_init();
    options_set(OPTION_ID_DUNGEON_COLORS, 1u);

    if (options_persistence_load_or_default() != 0u) return 0u;
    /* Corrupt image rejected; live state reset to defaults, and the
     * automap value inside the corrupt image was not applied. */
    if (options_get(OPTION_ID_DUNGEON_COLORS) != 0u) return 0u;
    if (options_get(OPTION_ID_AUTOMAP) != 0u) return 0u;
    return 1u;
}

static unsigned char test_load_with_valid_returns_one(void)
{
    unsigned char buf[OPTIONS_STATE_SIZE];

    options_runtime_init();
    options_set(OPTION_ID_DIAGONAL_SWORD, 1u);
    options_set(OPTION_ID_START_HEARTS, 9u);
    (void)options_runtime_serialize(buf, OPTIONS_STATE_SIZE);
    sram_options_io_write(buf, OPTIONS_STATE_SIZE);

    /* Power-cycle simulation: wipe live state, reload from SRAM. */
    options_runtime_init();
    if (options_get(OPTION_ID_DIAGONAL_SWORD) != 0u) return 0u;
    if (options_get(OPTION_ID_START_HEARTS)
        != OPTIONS_START_HEARTS_MIN) return 0u;

    if (options_persistence_load_or_default() != 1u) return 0u;
    if (options_get(OPTION_ID_DIAGONAL_SWORD) != 1u) return 0u;
    if (options_get(OPTION_ID_START_HEARTS) != 9u) return 0u;
    return 1u;
}

static unsigned char run_test(unsigned int id)
{
    switch (id) {
    case 0: return test_blank_detector_zeros();
    case 1: return test_blank_detector_ffs();
    case 2: return test_blank_detector_mixed_returns_0();
    case 3: return test_commit_then_load_roundtrip();
    case 4: return test_commit_preserves_outside_region();
    case 5: return test_load_with_blank_returns_zero();
    case 6: return test_load_with_corrupt_returns_zero();
    case 7: return test_load_with_valid_returns_one();
    default: return 0u;
    }
}
