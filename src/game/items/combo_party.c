/* T-260: Remix combo input glue for 1-4 players.
 *
 * Runs the pure resolver on each player's gameplay edges after the item
 * ring filter and before any item use reads them; held/re-emitted B/C
 * presses are written back through input_state_filter_game. A combo fire
 * is latched for this update only and spent at the player's item-use point
 * under the same idle gate as ordinary items. Button masks are SGDK joy.h
 * post-remap values (this TU stays free of SGDK IO). */
#include "combo_party.h"
#include "combo_input.h"
#include "combo_recipes.h"
#include "combo_ram.h"
#include "../../abi/platform_abi.h"
#include "../../state/input_state.h"
#include "../../state/loadout.h"
#include "../../state/game_profile.h"
#include "../inventory/item_ring_runtime.h"

#define PAD_B 0x0010u
#define PAD_C 0x0020u
#define PAD_Z 0x0100u
#define PAD_Y 0x0200u
#define PAD_X 0x0400u

#define CR COMBO_RAM

static unsigned char party_b(unsigned char p) { return p ? loadout_party_b(p) : F_ram_0656; }
static unsigned char party_c(unsigned char p) { return p ? loadout_party_c(p) : loadout_c_slot(); }

void combo_party_reset_all(void)
{
    unsigned char i;
    combo_input_reset_all();
    for (i = 0u; i < COMBO_PLAYERS; ++i) {
        CR->fire[i] = 0u;
        CR->tap[i] = 0u;
        if (!loadout_ring_visible(&g_item_rings[i])) loadout_ring_reset(&g_item_rings[i]);
    }
}

void combo_party_filter(unsigned char player_count)
{
    unsigned char p;
    if (!game_profile_is_remix()) return;
    combo_input_set_masks(PAD_B, PAD_C, PAD_X);
    if (F_ram_00EB_N3 != CR->party_room || F_ram_0010_N2 != CR->party_level) {
        CR->party_room = F_ram_00EB_N3;
        CR->party_level = F_ram_0010_N2;
        combo_party_reset_all();
    }
    for (p = 0u; p < player_count && p < COMBO_PLAYERS; ++p) {
        const unsigned short pressed = input_state_game_pressed(p);
        const unsigned short raw = input_state_pressed(p);
        const unsigned char recipe = (unsigned char)(combo_recipe_lookup(party_b(p), party_c(p)) != COMBO_NONE);
        ComboInputOut out;
        unsigned short ordinary = pressed;
        CR->fire[p] = 0u;
        if (loadout_ring_visible(&g_item_rings[p]) || (raw & (PAD_Y | PAD_Z | 0x80u))) {
            combo_input_reset(p);
            CR->tap[p] = 0u;
            input_state_filter_game(p, input_state_game_held(p),
                (unsigned short)(pressed & (unsigned short)~(PAD_B | PAD_C | PAD_X)));
            continue;
        }
        /* The ring owns ordinary tap/hold timing. Recognize chords from
         * physical edges, retaining a released tap only until its window
         * ends. A held button never becomes an ordinary press on expiry. */
        out = combo_input_step(p, raw, recipe);
        if (recipe) {
            CR->tap[p] |= (unsigned short)(pressed & (PAD_B | PAD_C));
            ordinary &= (unsigned short)~(PAD_B | PAD_C | PAD_X);
            if (out.fire) {
                CR->tap[p] = 0u;
                loadout_ring_reset(&g_item_rings[p]);
            } else {
                unsigned short ready = CR->pending[p] ? out.pressed : (PAD_B | PAD_C);
                ready &= CR->tap[p];
                ordinary |= ready;
                CR->tap[p] &= (unsigned short)~ready;
            }
        } else {
            CR->tap[p] = 0u;
            ordinary &= (unsigned short)~PAD_X;
        }
        out.pressed = ordinary;
        if (out.pressed != pressed)
            input_state_filter_game(p, input_state_game_held(p), out.pressed);
        CR->fire[p] = out.fire;
    }
}

unsigned char combo_party_pending_fire(unsigned char player)
{
    return (unsigned char)(game_profile_is_remix() && player < COMBO_PLAYERS && CR->fire[player]);
}

void combo_party_fire(unsigned char player)
{
    if (player >= COMBO_PLAYERS || !CR->fire[player]) return;
    CR->fire[player] = 0u;
    (void)combo_recipe_try(player, party_b(player), party_c(player));
}
