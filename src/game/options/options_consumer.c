/* Phase 9 Task 9.4 — Option consumer implementation. */

#include "options_consumer.h"
#include "options_runtime.h"
#include "options_state.h"
#include "../../state/inventory.h"
#include "../../state/game_profile.h"

/* NES source: Redux main.asm unconditional core / optional.asm; Original
 * Z_07 MakeSwordShot and original inventory/maze/secret rules.
 * Drained C: existing options_consumer_get_* below.
 * Coverage: PARTIAL(profile policy; lifecycle verified separately).
 * Stance: EXTEND. Saved preferences never own the production profile.
 * T-298's visible Remix modifiers remain optional. Hidden historical fields
 * stay wire-compatible but cannot enable unverified rules in either profile. */
static unsigned char effective_option(unsigned int id)
{
    /* The chord-only diagnostics still exercise raw option consumers. */
    if (!game_profile_current()->active) return options_get(id);
    switch (id) {
    case OPTION_ID_AB_SWAP:
    case OPTION_ID_ROOM_SCROLL:
    case OPTION_ID_UW_MUSIC:
        return options_get(id); /* Existing Genesis presentation preferences. */
    case OPTION_ID_AUTOMAP:
    case OPTION_ID_DUNGEON_COLORS:
    case OPTION_ID_VISIBLE_SECRETS:
    case OPTION_ID_DIAGONAL_SWORD:
        return game_profile_is_remix();
    case OPTION_ID_NO_REDUCED_FLASHING:
    case OPTION_ID_SWORD_STYLE:
    case OPTION_ID_LIKE_LIKE_BEHAVIOR:
        return game_profile_is_remix() ? options_get(id) : 0u;
    case OPTION_ID_START_HEARTS:
        return OPTIONS_START_HEARTS_MIN;
    default:
        return 0u;
    }
}

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
    unsigned char hearts = effective_option((unsigned int)OPTION_ID_START_HEARTS);
    unsigned char bomb_upg = effective_option((unsigned int)OPTION_ID_BOMB_UPGRADE);
    unsigned char clamped = clamp_start_hearts_to_nibble(hearts);

    /* Boot profile: max = clamped, current = max (full heart bar).
     * NES full health has HeartPartial >= $80; new files use $FF. */
    g_inventory.heart_values = heart_values_pack(clamped, clamped);
    g_inventory.heart_partial = 0xFFu;

    /* Production save_file_a_load subsequently restores earned capacities.
     * The temporary boot inventory follows the selected profile too. */
    g_inventory.max_bombs = game_profile_is_remix() ? 10u : bomb_cap_for_upgrade(bomb_upg);
    inventory_hud_mark_dirty();
}

/* --- Bool getters --------------------------------------------------- */

unsigned char options_consumer_get_low_health_warning(void)
{
    return effective_option((unsigned int)OPTION_ID_LOW_HEALTH_WARNING);
}

unsigned char options_consumer_get_automap(void)
{
    return effective_option((unsigned int)OPTION_ID_AUTOMAP);
}

unsigned char options_consumer_get_dungeon_colors(void)
{
    return effective_option((unsigned int)OPTION_ID_DUNGEON_COLORS);
}

unsigned char options_consumer_get_visible_secrets(void)
{
    return effective_option((unsigned int)OPTION_ID_VISIBLE_SECRETS);
}

unsigned char options_consumer_get_diagonal_sword(void)
{
    return effective_option((unsigned int)OPTION_ID_DIAGONAL_SWORD);
}

unsigned char options_consumer_get_no_reduced_flashing(void)
{
    return effective_option((unsigned int)OPTION_ID_NO_REDUCED_FLASHING);
}

unsigned char options_consumer_get_ab_swap(void)
{
    return effective_option((unsigned int)OPTION_ID_AB_SWAP);
}

unsigned char options_consumer_get_auto_collect_drops(void)
{
    return effective_option((unsigned int)OPTION_ID_AUTO_COLLECT_DROPS);
}

/* --- Enum getters --------------------------------------------------- */

unsigned char options_consumer_get_sword_style(void)
{
    return effective_option((unsigned int)OPTION_ID_SWORD_STYLE);
}

unsigned char options_consumer_get_like_like_behavior(void)
{
    return effective_option((unsigned int)OPTION_ID_LIKE_LIKE_BEHAVIOR);
}

unsigned char options_consumer_get_bomb_upgrade(void)
{
    return effective_option((unsigned int)OPTION_ID_BOMB_UPGRADE);
}

unsigned char options_consumer_get_lost_woods(void)
{
    return effective_option((unsigned int)OPTION_ID_LOST_WOODS);
}

unsigned char options_consumer_get_dark_room_light(void)
{
    return effective_option((unsigned int)OPTION_ID_DARK_ROOM_LIGHT);
}

unsigned char options_consumer_get_room_scroll(void)
{
    return effective_option((unsigned int)OPTION_ID_ROOM_SCROLL);
}

unsigned char options_consumer_get_uw_music(void)
{
    return effective_option((unsigned int)OPTION_ID_UW_MUSIC);
}

/* --- Numeric ------------------------------------------------------- */

unsigned char options_consumer_get_start_hearts(void)
{
    return clamp_start_hearts_to_nibble(
        effective_option((unsigned int)OPTION_ID_START_HEARTS));
}
