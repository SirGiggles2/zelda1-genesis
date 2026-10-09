/* Phase 9 Task 9.4 — Option consumer probe (in-ROM tests). */

#include "options_consumer_probe.h"
#include "../options_runtime.h"
#include "../options_state.h"
#include "../options_consumer.h"
#include "../../../state/inventory.h"

#define PROBE  ((volatile unsigned char *)OPTIONS_CONSUMER_PROBE_BASE)

static void stamp_magic(void)
{
    unsigned int i;
    PROBE[0] = 'C';
    PROBE[1] = 'N';
    PROBE[2] = 0x03u;  /* version 3 — negative-path coverage */
    for (i = 3u; i < 16u; ++i) PROBE[i] = 0u;
}

/* --- Group A: apply_inventory_at_start behavior tests (v1) ---------- */

static unsigned char test_defaults_3_hearts_8_bombs(void)
{
    options_runtime_init();
    options_consumer_apply_inventory_at_start();
    return (g_inventory.heart_values == 0x33u
            && g_inventory.max_bombs == 8u) ? 1u : 0u;
}

static unsigned char test_start_hearts_7(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_START_HEARTS, 7u);
    options_consumer_apply_inventory_at_start();
    return (g_inventory.heart_values == 0x77u) ? 1u : 0u;
}

static unsigned char test_bomb_upgrade_plus4(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_BOMB_UPGRADE,
                OPTIONS_BOMBUPG_PLUS4);
    options_consumer_apply_inventory_at_start();
    return (g_inventory.max_bombs == 12u) ? 1u : 0u;
}

static unsigned char test_bomb_upgrade_plus8(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_BOMB_UPGRADE,
                OPTIONS_BOMBUPG_PLUS8);
    options_consumer_apply_inventory_at_start();
    return (g_inventory.max_bombs == 16u) ? 1u : 0u;
}

static unsigned char test_start_hearts_16_clamps_to_15(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_START_HEARTS, 16u);
    options_consumer_apply_inventory_at_start();
    return (g_inventory.heart_values == 0xFFu) ? 1u : 0u;
}

/* --- Group B: positive-path getter coverage (v2) -------------------- */

static unsigned char test_getter_low_health_warning(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_LOW_HEALTH_WARNING, 1u);
    return (options_consumer_get_low_health_warning() == 1u) ? 1u : 0u;
}

static unsigned char test_getter_automap(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_AUTOMAP, 1u);
    return (options_consumer_get_automap() == 1u) ? 1u : 0u;
}

static unsigned char test_getter_dungeon_colors(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_DUNGEON_COLORS, 1u);
    return (options_consumer_get_dungeon_colors() == 1u) ? 1u : 0u;
}

static unsigned char test_getter_visible_secrets(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_VISIBLE_SECRETS, 1u);
    return (options_consumer_get_visible_secrets() == 1u) ? 1u : 0u;
}

static unsigned char test_getter_diagonal_sword(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_DIAGONAL_SWORD, 1u);
    return (options_consumer_get_diagonal_sword() == 1u) ? 1u : 0u;
}

static unsigned char test_getter_no_reduced_flashing(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_NO_REDUCED_FLASHING, 1u);
    return (options_consumer_get_no_reduced_flashing() == 1u) ? 1u : 0u;
}

static unsigned char test_getter_ab_swap(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_AB_SWAP, 1u);
    return (options_consumer_get_ab_swap() == 1u) ? 1u : 0u;
}

static unsigned char test_getter_auto_collect_drops(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_AUTO_COLLECT_DROPS, 1u);
    return (options_consumer_get_auto_collect_drops() == 1u) ? 1u : 0u;
}

static unsigned char test_getter_sword_style_beam_always(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_SWORD_STYLE,
                OPTIONS_SWORD_BEAM_ALWAYS);
    return (options_consumer_get_sword_style()
            == OPTIONS_SWORD_BEAM_ALWAYS) ? 1u : 0u;
}

static unsigned char test_getter_like_like_no_eat(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_LIKE_LIKE_BEHAVIOR,
                OPTIONS_LIKELIKE_NO_EAT);
    return (options_consumer_get_like_like_behavior()
            == OPTIONS_LIKELIKE_NO_EAT) ? 1u : 0u;
}

static unsigned char test_getter_bomb_upgrade_plus8(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_BOMB_UPGRADE,
                OPTIONS_BOMBUPG_PLUS8);
    return (options_consumer_get_bomb_upgrade()
            == OPTIONS_BOMBUPG_PLUS8) ? 1u : 0u;
}

static unsigned char test_getter_lost_woods_relaxed(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_LOST_WOODS,
                OPTIONS_LWOODS_RELAXED);
    return (options_consumer_get_lost_woods()
            == OPTIONS_LWOODS_RELAXED) ? 1u : 0u;
}

static unsigned char test_getter_dark_room_bright(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_DARK_ROOM_LIGHT,
                OPTIONS_DARK_BRIGHT);
    return (options_consumer_get_dark_room_light()
            == OPTIONS_DARK_BRIGHT) ? 1u : 0u;
}

static unsigned char test_getter_start_hearts_5(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_START_HEARTS, 5u);
    return (options_consumer_get_start_hearts() == 5u) ? 1u : 0u;
}

/* --- Group C: bool set-zero returns zero (v3) ----------------------- */

static unsigned char bool_set_zero_returns_zero(unsigned int id,
    unsigned char (*getter)(void))
{
    options_runtime_init();
    options_set(id, 1u);          /* arm at one */
    if (getter() != 1u) return 0u;
    options_set(id, 0u);          /* clear */
    return (getter() == 0u) ? 1u : 0u;
}

static unsigned char test_set_zero_low_health_warning(void)
{ return bool_set_zero_returns_zero(
    (unsigned int)OPTION_ID_LOW_HEALTH_WARNING,
    options_consumer_get_low_health_warning); }

static unsigned char test_set_zero_automap(void)
{ return bool_set_zero_returns_zero(
    (unsigned int)OPTION_ID_AUTOMAP,
    options_consumer_get_automap); }

static unsigned char test_set_zero_dungeon_colors(void)
{ return bool_set_zero_returns_zero(
    (unsigned int)OPTION_ID_DUNGEON_COLORS,
    options_consumer_get_dungeon_colors); }

static unsigned char test_set_zero_visible_secrets(void)
{ return bool_set_zero_returns_zero(
    (unsigned int)OPTION_ID_VISIBLE_SECRETS,
    options_consumer_get_visible_secrets); }

static unsigned char test_set_zero_diagonal_sword(void)
{ return bool_set_zero_returns_zero(
    (unsigned int)OPTION_ID_DIAGONAL_SWORD,
    options_consumer_get_diagonal_sword); }

static unsigned char test_set_zero_no_reduced_flashing(void)
{ return bool_set_zero_returns_zero(
    (unsigned int)OPTION_ID_NO_REDUCED_FLASHING,
    options_consumer_get_no_reduced_flashing); }

static unsigned char test_set_zero_ab_swap(void)
{ return bool_set_zero_returns_zero(
    (unsigned int)OPTION_ID_AB_SWAP,
    options_consumer_get_ab_swap); }

static unsigned char test_set_zero_auto_collect_drops(void)
{ return bool_set_zero_returns_zero(
    (unsigned int)OPTION_ID_AUTO_COLLECT_DROPS,
    options_consumer_get_auto_collect_drops); }

/* --- Group D: enum out-of-range rejection (v3) ---------------------- */

/* Common pattern: set valid value, then attempt OOB; verify the
 * stored value is still the valid one (options_set must no-op). */

static unsigned char test_enum_sword_style_oob_rejected(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_SWORD_STYLE,
                OPTIONS_SWORD_BEAM_ALWAYS);
    options_set((unsigned int)OPTION_ID_SWORD_STYLE, OPTIONS_SWORD_COUNT);
    options_set((unsigned int)OPTION_ID_SWORD_STYLE, 0xFFu);
    return (options_consumer_get_sword_style()
            == OPTIONS_SWORD_BEAM_ALWAYS) ? 1u : 0u;
}

static unsigned char test_enum_like_like_oob_rejected(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_LIKE_LIKE_BEHAVIOR,
                OPTIONS_LIKELIKE_NO_EAT);
    options_set((unsigned int)OPTION_ID_LIKE_LIKE_BEHAVIOR,
                OPTIONS_LIKELIKE_COUNT);
    options_set((unsigned int)OPTION_ID_LIKE_LIKE_BEHAVIOR, 0xFFu);
    return (options_consumer_get_like_like_behavior()
            == OPTIONS_LIKELIKE_NO_EAT) ? 1u : 0u;
}

static unsigned char test_enum_bomb_upgrade_oob_rejected(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_BOMB_UPGRADE,
                OPTIONS_BOMBUPG_PLUS8);
    options_set((unsigned int)OPTION_ID_BOMB_UPGRADE,
                OPTIONS_BOMBUPG_COUNT);
    options_set((unsigned int)OPTION_ID_BOMB_UPGRADE, 0xFFu);
    return (options_consumer_get_bomb_upgrade()
            == OPTIONS_BOMBUPG_PLUS8) ? 1u : 0u;
}

static unsigned char test_enum_lost_woods_oob_rejected(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_LOST_WOODS,
                OPTIONS_LWOODS_RELAXED);
    options_set((unsigned int)OPTION_ID_LOST_WOODS,
                OPTIONS_LWOODS_COUNT);
    options_set((unsigned int)OPTION_ID_LOST_WOODS, 0xFFu);
    return (options_consumer_get_lost_woods()
            == OPTIONS_LWOODS_RELAXED) ? 1u : 0u;
}

static unsigned char test_enum_dark_room_oob_rejected(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_DARK_ROOM_LIGHT,
                OPTIONS_DARK_BRIGHT);
    options_set((unsigned int)OPTION_ID_DARK_ROOM_LIGHT,
                OPTIONS_DARK_COUNT);
    options_set((unsigned int)OPTION_ID_DARK_ROOM_LIGHT, 0xFFu);
    return (options_consumer_get_dark_room_light()
            == OPTIONS_DARK_BRIGHT) ? 1u : 0u;
}

/* --- Group E: start_hearts edge clamps (v3) ------------------------- */

static unsigned char test_start_hearts_zero_clamps_to_min(void)
{
    options_runtime_init();
    options_set((unsigned int)OPTION_ID_START_HEARTS, 0u);
    /* options_set clamps below MIN to MIN; getter clamps to nibble. */
    return (options_consumer_get_start_hearts()
            == OPTIONS_START_HEARTS_MIN) ? 1u : 0u;
}

static unsigned char test_start_hearts_default_is_min(void)
{
    options_runtime_init();
    /* No options_set call — defaults must equal MIN. */
    return (options_consumer_get_start_hearts()
            == OPTIONS_START_HEARTS_MIN) ? 1u : 0u;
}

/* --- Driver --------------------------------------------------------- */

static void mark(unsigned int bit_idx, unsigned char *passes_io,
                 unsigned char bits[5])
{
    unsigned int byte_idx = bit_idx >> 3;
    unsigned int bit_in_byte = bit_idx & 7u;
    bits[byte_idx] |= (unsigned char)(1u << bit_in_byte);
    ++(*passes_io);
}

void options_consumer_probe_run(void)
{
    /* Snapshot mutable state — tests poke g_options via options_set and
     * g_inventory via apply_inventory_at_start. Both are restored at
     * the end so the live gameplay session sees the real options state
     * after the probe runs. */
    unsigned char saved_heart_values  = g_inventory.heart_values;
    unsigned char saved_heart_partial = g_inventory.heart_partial;
    unsigned char saved_max_bombs     = g_inventory.max_bombs;

    unsigned char bits[5] = { 0u, 0u, 0u, 0u, 0u };
    unsigned char passes = 0u;
    unsigned char total  = 34u;

    stamp_magic();

    /* Group A (5 tests, bits 0..4). */
    if (test_defaults_3_hearts_8_bombs())          mark(0u,  &passes, bits);
    if (test_start_hearts_7())                     mark(1u,  &passes, bits);
    if (test_bomb_upgrade_plus4())                 mark(2u,  &passes, bits);
    if (test_bomb_upgrade_plus8())                 mark(3u,  &passes, bits);
    if (test_start_hearts_16_clamps_to_15())       mark(4u,  &passes, bits);

    /* Group B (14 tests, bits 5..18). */
    if (test_getter_low_health_warning())          mark(5u,  &passes, bits);
    if (test_getter_automap())                     mark(6u,  &passes, bits);
    if (test_getter_dungeon_colors())              mark(7u,  &passes, bits);
    if (test_getter_visible_secrets())             mark(8u,  &passes, bits);
    if (test_getter_diagonal_sword())              mark(9u,  &passes, bits);
    if (test_getter_no_reduced_flashing())         mark(10u, &passes, bits);
    if (test_getter_ab_swap())                     mark(11u, &passes, bits);
    if (test_getter_auto_collect_drops())          mark(12u, &passes, bits);
    if (test_getter_sword_style_beam_always())     mark(13u, &passes, bits);
    if (test_getter_like_like_no_eat())            mark(14u, &passes, bits);
    if (test_getter_bomb_upgrade_plus8())          mark(15u, &passes, bits);
    if (test_getter_lost_woods_relaxed())          mark(16u, &passes, bits);
    if (test_getter_dark_room_bright())            mark(17u, &passes, bits);
    if (test_getter_start_hearts_5())              mark(18u, &passes, bits);

    /* Group C (8 tests, bits 19..26). */
    if (test_set_zero_low_health_warning())        mark(19u, &passes, bits);
    if (test_set_zero_automap())                   mark(20u, &passes, bits);
    if (test_set_zero_dungeon_colors())            mark(21u, &passes, bits);
    if (test_set_zero_visible_secrets())           mark(22u, &passes, bits);
    if (test_set_zero_diagonal_sword())            mark(23u, &passes, bits);
    if (test_set_zero_no_reduced_flashing())       mark(24u, &passes, bits);
    if (test_set_zero_ab_swap())                   mark(25u, &passes, bits);
    if (test_set_zero_auto_collect_drops())        mark(26u, &passes, bits);

    /* Group D (5 tests, bits 27..31). */
    if (test_enum_sword_style_oob_rejected())      mark(27u, &passes, bits);
    if (test_enum_like_like_oob_rejected())        mark(28u, &passes, bits);
    if (test_enum_bomb_upgrade_oob_rejected())     mark(29u, &passes, bits);
    if (test_enum_lost_woods_oob_rejected())       mark(30u, &passes, bits);
    if (test_enum_dark_room_oob_rejected())        mark(31u, &passes, bits);

    /* Group E (2 tests, bits 32..33). */
    if (test_start_hearts_zero_clamps_to_min())    mark(32u, &passes, bits);
    if (test_start_hearts_default_is_min())        mark(33u, &passes, bits);

    PROBE[3] = total;
    PROBE[4] = passes;
    PROBE[5] = bits[0];
    PROBE[6] = bits[1];
    PROBE[7] = bits[2];
    PROBE[8] = bits[3];
    PROBE[9] = bits[4];

    /* Restore mutated state. options_runtime_init resets g_options to
     * defaults; the live game-start path in roomrom_debug_enter ran
     * BEFORE this probe so the post-restore state matches what the
     * debug-entry expected (defaults + apply_inventory_at_start
     * already populated g_inventory). */
    options_runtime_init();
    g_inventory.heart_values  = saved_heart_values;
    g_inventory.heart_partial = saved_heart_partial;
    g_inventory.max_bombs     = saved_max_bombs;
}
