/* Phase 9 Task 9.1 — Options runtime in-ROM probe.
 *
 * Boot-time self-test for the GREENFIELD options runtime. Drives
 * options_runtime_init -> options_set / options_get -> serialize ->
 * apply round-trip + invalid-image rejection. Publishes a 16-byte
 * result block at OPTIONS_PROBE_BASE for tools/debug/probes/
 * probe_options_runtime.lua to verify.
 *
 * Hard rule WT-5: lives at src/game/options/probes/, not RoomRom/.
 *
 * Block layout @ OPTIONS_PROBE_BASE = 0xFF7E80 (free per RoomRom
 * Debug RAM Map; OW raw-tile + UW door persistence at $7400..$77CF;
 * enemy probe blocks own $7E00..$7FE7):
 *   [0]   = 'O'  (0x4F) magic
 *   [1]   = 'P'  (0x50) magic
 *   [2]   = OPTIONS_PROBE_VERSION (1)
 *   [3]   = test_count (number of tests run)
 *   [4]   = test_pass_count (number that passed)
 *   [5..14] = per-test bit (1=pass / 0=fail). Test ids:
 *     0  defaults_validate
 *     1  bool_set_get_roundtrip
 *     2  enum_clamp_invalid
 *     3  start_hearts_clamp_low
 *     4  start_hearts_clamp_high
 *     5  serialize_apply_roundtrip
 *     6  apply_rejects_bad_magic
 *     7  apply_rejects_bad_checksum
 *     8  version_after_init
 *     9  reserved_zero_after_defaults
 *   [15] = reserved
 */

#include "../options_runtime.h"
#include "../options_state.h"

#define OPTIONS_PROBE_BASE         0x00FF7E80UL
#define OPTIONS_PROBE_VERSION      0x01u
#define OPTIONS_PROBE_TEST_COUNT   10u

static unsigned char run_test(unsigned int id);

void options_probe_run(void)
{
    volatile unsigned char *block =
        (volatile unsigned char *)OPTIONS_PROBE_BASE;
    unsigned int i;
    unsigned int passes = 0u;

    block[0] = 0x4Fu;                      /* 'O' */
    block[1] = 0x50u;                      /* 'P' */
    block[2] = OPTIONS_PROBE_VERSION;
    block[3] = (unsigned char)OPTIONS_PROBE_TEST_COUNT;
    block[4] = 0u;                          /* filled below */

    for (i = 0u; i < OPTIONS_PROBE_TEST_COUNT; ++i) {
        unsigned char ok = run_test(i);
        block[5u + i] = ok;
        if (ok != 0u) ++passes;
    }
    block[4] = (unsigned char)passes;
    block[15] = 0u;
}

static unsigned char test_defaults_validate(void)
{
    options_runtime_init();
    return options_runtime_validate();
}

static unsigned char test_bool_set_get_roundtrip(void)
{
    options_runtime_init();
    options_set(OPTION_ID_AUTOMAP, 1u);
    if (options_get(OPTION_ID_AUTOMAP) != 1u) return 0u;
    options_set(OPTION_ID_AUTOMAP, 0u);
    if (options_get(OPTION_ID_AUTOMAP) != 0u) return 0u;
    options_set(OPTION_ID_DIAGONAL_SWORD, 1u);
    if (options_get(OPTION_ID_DIAGONAL_SWORD) != 1u) return 0u;
    if (options_get(OPTION_ID_AUTOMAP) != 0u) return 0u; /* no spillover */
    return 1u;
}

static unsigned char test_enum_clamp_invalid(void)
{
    options_runtime_init();
    options_set(OPTION_ID_SWORD_STYLE, 99u);
    if (options_get(OPTION_ID_SWORD_STYLE) != OPTIONS_SWORD_VANILLA) return 0u;
    options_set(OPTION_ID_SWORD_STYLE, OPTIONS_SWORD_BEAM_ALWAYS);
    if (options_get(OPTION_ID_SWORD_STYLE) != OPTIONS_SWORD_BEAM_ALWAYS) return 0u;
    return 1u;
}

static unsigned char test_start_hearts_clamp_low(void)
{
    options_runtime_init();
    options_set(OPTION_ID_START_HEARTS, 0u);
    return (options_get(OPTION_ID_START_HEARTS) == OPTIONS_START_HEARTS_MIN) ? 1u : 0u;
}

static unsigned char test_start_hearts_clamp_high(void)
{
    options_runtime_init();
    options_set(OPTION_ID_START_HEARTS, 200u);
    return (options_get(OPTION_ID_START_HEARTS) == OPTIONS_START_HEARTS_MAX) ? 1u : 0u;
}

static unsigned char test_serialize_apply_roundtrip(void)
{
    unsigned char buf[OPTIONS_STATE_SIZE];
    unsigned int i;
    unsigned int written;

    options_runtime_init();
    options_set(OPTION_ID_AUTOMAP, 1u);
    options_set(OPTION_ID_DUNGEON_COLORS, 1u);
    options_set(OPTION_ID_SWORD_STYLE, OPTIONS_SWORD_BEAM_ALWAYS);
    options_set(OPTION_ID_START_HEARTS, 5u);
    written = options_runtime_serialize(buf, OPTIONS_STATE_SIZE);
    if (written != OPTIONS_STATE_SIZE) return 0u;

    /* Wipe live state. */
    options_runtime_init();
    if (options_get(OPTION_ID_AUTOMAP) != 0u) return 0u;

    if (options_runtime_apply(buf, OPTIONS_STATE_SIZE) == 0u) return 0u;
    if (options_get(OPTION_ID_AUTOMAP) != 1u) return 0u;
    if (options_get(OPTION_ID_DUNGEON_COLORS) != 1u) return 0u;
    if (options_get(OPTION_ID_SWORD_STYLE) != OPTIONS_SWORD_BEAM_ALWAYS) return 0u;
    if (options_get(OPTION_ID_START_HEARTS) != 5u) return 0u;
    (void)i;
    return 1u;
}

static unsigned char test_apply_rejects_bad_magic(void)
{
    unsigned char buf[OPTIONS_STATE_SIZE];
    options_runtime_init();
    (void)options_runtime_serialize(buf, OPTIONS_STATE_SIZE);
    buf[0] = 0xAAu; /* corrupt magic */
    return (options_runtime_apply(buf, OPTIONS_STATE_SIZE) == 0u) ? 1u : 0u;
}

static unsigned char test_apply_rejects_bad_checksum(void)
{
    unsigned char buf[OPTIONS_STATE_SIZE];
    options_runtime_init();
    (void)options_runtime_serialize(buf, OPTIONS_STATE_SIZE);
    buf[31] ^= 0xFFu; /* flip low byte of checksum */
    return (options_runtime_apply(buf, OPTIONS_STATE_SIZE) == 0u) ? 1u : 0u;
}

static unsigned char test_version_after_init(void)
{
    options_runtime_init();
    return (options_get_version() == OPTIONS_VERSION_CURRENT) ? 1u : 0u;
}

static unsigned char test_reserved_zero_after_defaults(void)
{
    unsigned int i;
    options_runtime_init();
    /* Reserved spans struct offsets 11..29. */
    for (i = 11u; i < 30u; ++i) {
        if (options_runtime_peek(i) != 0u) return 0u;
    }
    return 1u;
}

static unsigned char run_test(unsigned int id)
{
    switch (id) {
    case 0: return test_defaults_validate();
    case 1: return test_bool_set_get_roundtrip();
    case 2: return test_enum_clamp_invalid();
    case 3: return test_start_hearts_clamp_low();
    case 4: return test_start_hearts_clamp_high();
    case 5: return test_serialize_apply_roundtrip();
    case 6: return test_apply_rejects_bad_magic();
    case 7: return test_apply_rejects_bad_checksum();
    case 8: return test_version_after_init();
    case 9: return test_reserved_zero_after_defaults();
    default: return 0u;
    }
}
