/* Phase 9 Task 9.4 — Option consumer implementation. */

#include "options_consumer.h"
#include "options_runtime.h"
#include "options_state.h"
#include "../../state/inventory.h"

static unsigned char clamp_start_hearts_to_nibble(unsigned char v)
{
    /* heart_values nibble can hold 0..15. Options menu UI exposes
     * 3..16 per OPTIONS_START_HEARTS_MIN/MAX, but the storage nibble
     * caps at 15; clamp the upper edge so 16 -> 15 (vanilla cap). */
    if (v < OPTIONS_START_HEARTS_MIN) return OPTIONS_START_HEARTS_MIN;
    if (v > 15u) return 15u;
    return v;
}

static unsigned char bomb_cap_for_upgrade(unsigned char upgrade)
{
    switch (upgrade) {
    case OPTIONS_BOMBUPG_PLUS4:  return MAX_BOMBS_UPGRADE_1;  /* 12 */
    case OPTIONS_BOMBUPG_PLUS8:  return MAX_BOMBS_UPGRADE_2;  /* 16 */
    case OPTIONS_BOMBUPG_VANILLA:
    default:                     return MAX_BOMBS_DEFAULT;    /*  8 */
    }
}

void options_consumer_apply_inventory_at_start(void)
{
    unsigned char hearts = options_get((unsigned int)OPTION_ID_START_HEARTS);
    unsigned char bomb_upg = options_get((unsigned int)OPTION_ID_BOMB_UPGRADE);
    unsigned char clamped = clamp_start_hearts_to_nibble(hearts);

    /* Boot profile: max = clamped, current = max (full heart bar).
     * NES full health has HeartPartial >= $80; new files use $FF. */
    g_inventory.heart_values = heart_values_pack(clamped, clamped);
    g_inventory.heart_partial = 0xFFu;

    g_inventory.max_bombs = bomb_cap_for_upgrade(bomb_upg);
    inventory_hud_mark_dirty();
}

/* --- Bool getters --------------------------------------------------- */

unsigned char options_consumer_get_low_health_warning(void)
{
    return options_get((unsigned int)OPTION_ID_LOW_HEALTH_WARNING);
}

unsigned char options_consumer_get_automap(void)
{
    return options_get((unsigned int)OPTION_ID_AUTOMAP);
}

unsigned char options_consumer_get_dungeon_colors(void)
{
    return options_get((unsigned int)OPTION_ID_DUNGEON_COLORS);
}

unsigned char options_consumer_get_visible_secrets(void)
{
    return options_get((unsigned int)OPTION_ID_VISIBLE_SECRETS);
}

unsigned char options_consumer_get_diagonal_sword(void)
{
    return options_get((unsigned int)OPTION_ID_DIAGONAL_SWORD);
}

unsigned char options_consumer_get_no_reduced_flashing(void)
{
    return options_get((unsigned int)OPTION_ID_NO_REDUCED_FLASHING);
}

unsigned char options_consumer_get_ab_swap(void)
{
    return options_get((unsigned int)OPTION_ID_AB_SWAP);
}

unsigned char options_consumer_get_auto_collect_drops(void)
{
    return options_get((unsigned int)OPTION_ID_AUTO_COLLECT_DROPS);
}

/* --- Enum getters --------------------------------------------------- */

unsigned char options_consumer_get_sword_style(void)
{
    return options_get((unsigned int)OPTION_ID_SWORD_STYLE);
}

unsigned char options_consumer_get_like_like_behavior(void)
{
    return options_get((unsigned int)OPTION_ID_LIKE_LIKE_BEHAVIOR);
}

unsigned char options_consumer_get_bomb_upgrade(void)
{
    return options_get((unsigned int)OPTION_ID_BOMB_UPGRADE);
}

unsigned char options_consumer_get_lost_woods(void)
{
    return options_get((unsigned int)OPTION_ID_LOST_WOODS);
}

unsigned char options_consumer_get_dark_room_light(void)
{
    return options_get((unsigned int)OPTION_ID_DARK_ROOM_LIGHT);
}

unsigned char options_consumer_get_room_scroll(void)
{
    return options_get((unsigned int)OPTION_ID_ROOM_SCROLL);
}

/* --- Numeric ------------------------------------------------------- */

unsigned char options_consumer_get_start_hearts(void)
{
    return clamp_start_hearts_to_nibble(
        options_get((unsigned int)OPTION_ID_START_HEARTS));
}
