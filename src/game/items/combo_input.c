/* T-260: Remix ordered B+C combo press resolver. See combo_input.h.
 * State lives in ComboRam (native block): pending 0 none / 1 B / 2 C. */
#include "combo_input.h"
#include "combo_ram.h"

#define CR COMBO_RAM

void combo_input_set_masks(unsigned short b, unsigned short c, unsigned short x)
{
    CR->mask_b = b; CR->mask_c = c; CR->mask_x = x;
}

void combo_input_reset(unsigned char player)
{
    if (player < COMBO_PLAYERS) CR->pending[player] = 0u;
}

void combo_input_reset_all(void)
{
    unsigned char i;
    for (i = 0u; i < COMBO_PLAYERS; ++i) CR->pending[i] = 0u;
}

static unsigned short pending_mask(unsigned char code)
{
    return code == 1u ? CR->mask_b : code == 2u ? CR->mask_c : 0u;
}

static unsigned char pending_code(unsigned short fresh)
{
    return (fresh & CR->mask_b) ? 1u : 2u;   /* fresh is exactly one of B or C */
}

ComboInputOut combo_input_step(unsigned char player, unsigned short pressed,
                               unsigned char has_recipe)
{
    ComboInputOut out;
    const unsigned char p = (unsigned char)(player & (COMBO_PLAYERS - 1u));
    const unsigned short bc = (unsigned short)(CR->mask_b | CR->mask_c);
    unsigned short fresh = (unsigned short)(pressed & bc);
    const unsigned short held = pending_mask(CR->pending[p]);

    out.fire = 0u;
    out.pressed = (unsigned short)(pressed & (unsigned short)~CR->mask_x);

    /* X fires the current pair's recipe and cancels any held press; with no
     * recipe it does nothing and never becomes two ordinary items. */
    if (pressed & CR->mask_x) {
        CR->pending[p] = 0u;
        if (has_recipe) {
            out.fire = 1u;
            out.pressed = (unsigned short)(out.pressed & (unsigned short)~bc);
        }
        return out;
    }
    if (!has_recipe) {
        /* No recipe: ordinary items, no delay; flush anything held. */
        out.pressed |= held;
        CR->pending[p] = 0u;
        return out;
    }
    if (held) {
        CR->age[p] = (unsigned char)(CR->age[p] + 1u);
        if (CR->age[p] >= COMBO_WINDOW_UPDATES) {
            /* Window over: the held press becomes ordinary this update and a
             * press arriving now starts a new window, never pairs late. */
            CR->pending[p] = 0u;
            out.pressed |= held;
            fresh = (unsigned short)(fresh & (unsigned short)~held);
            if (fresh) {
                CR->pending[p] = pending_code(fresh);
                CR->age[p] = 0u;
                out.pressed = (unsigned short)(out.pressed & (unsigned short)~fresh);
            }
            return out;
        }
        if (fresh & (unsigned short)(bc & (unsigned short)~held)) {
            CR->pending[p] = 0u;
            out.fire = 1u;
            out.pressed = (unsigned short)(out.pressed & (unsigned short)~bc);
            return out;
        }
        /* Repeat of the held button inside the window is absorbed. */
        out.pressed = (unsigned short)(out.pressed & (unsigned short)~bc);
        return out;
    }
    if (fresh == bc) {
        out.fire = 1u;
        out.pressed = (unsigned short)(out.pressed & (unsigned short)~bc);
        return out;
    }
    if (fresh) {
        CR->pending[p] = pending_code(fresh);
        CR->age[p] = 0u;
        out.pressed = (unsigned short)(out.pressed & (unsigned short)~bc);
    }
    return out;
}
