/* T-260: Remix ordered B+C item recipes. See combo_recipes.h. */
#include "combo_recipes.h"
#include "arrow.h"
#include "bomb.h"
#include "../../abi/platform_abi.h"
#include "../../state/inventory.h"
#include "../../state/game_profile.h"

combo_kind_t combo_recipe_lookup(unsigned char b_slot, unsigned char c_slot)
{
    if (!game_profile_is_remix()) return COMBO_NONE;
    if (b_slot == COMBO_SLOT_ARROW && c_slot == COMBO_SLOT_BOMBS) return COMBO_BOMB_ARROW;
    if (b_slot == COMBO_SLOT_BOMBS && c_slot == COMBO_SLOT_ARROW) return COMBO_ARROW_BURST;
    return COMBO_NONE;
}

static combo_result_t combo_check(unsigned char player, combo_kind_t kind)
{
    /* One bomb arrow / one burst at a time; both need a bomb slot for the blast. */
    if (kind == COMBO_BOMB_ARROW &&
        (roomrom_arrow_active() || roomrom_arrow_explosive_live(player))) return COMBO_BUSY;
    if (kind == COMBO_ARROW_BURST && combo_burst_live()) return COMBO_BUSY;
    if (!roomrom_bomb_slot_free()) return COMBO_BUSY;
    if (F_ram_065A_N == 0u) return COMBO_NO_BOW;
    if (F_ram_0677_R == 0u) return COMBO_NO_ARROW;   /* Remix arrow ammo (T-291) */
    if (F_ram_0658_N == 0u) return COMBO_NO_BOMB;
    return COMBO_OK;
}

combo_result_t combo_recipe_try(unsigned char player, unsigned char b_slot,
                                unsigned char c_slot)
{
    const combo_kind_t kind = combo_recipe_lookup(b_slot, c_slot);
    combo_result_t r;
    if (kind == COMBO_NONE) return COMBO_NO_RECIPE;
    r = combo_check(player, kind);
    if (r != COMBO_OK) {
        F_ram_0604_N = COMBO_ERROR_TUNE0;   /* PlayParryTune */
        return r;
    }
    --F_ram_0677_R;
    --F_ram_0658_N;
    g_inventory.bombs = F_ram_0658_N;
    inventory_hud_mark_dirty();
    if (kind == COMBO_BOMB_ARROW) roomrom_arrow_fire_explosive(player);
    else combo_arrow_burst_place(player);
    return COMBO_OK;
}
