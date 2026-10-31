#include "weapon_state.h"

_Static_assert(sizeof(PlayerSwordState) == 10u,
               "PlayerSwordState layout is part of the native capture ABI");
_Static_assert(__builtin_offsetof(PlayerSwordState, face) == 3u,
               "weapon.sword.face schema offset");
_Static_assert(__builtin_offsetof(PlayerSwordState, x) == 6u,
               "weapon.sword.x schema offset");
_Static_assert(__builtin_offsetof(PlayerSwordState, y) == 8u,
               "weapon.sword.y schema offset");
_Static_assert(sizeof(PlayerBeamState) == 8u,
               "PlayerBeamState layout is part of the native capture ABI");
_Static_assert(__builtin_offsetof(PlayerBeamState, x) == 5u,
               "weapon.beam.x schema offset");
_Static_assert(__builtin_offsetof(PlayerBeamState, y) == 6u,
               "weapon.beam.y schema offset");
_Static_assert(__builtin_offsetof(PlayerBeamState, pos_frac) == 7u,
               "weapon.beam.pos_frac schema offset");
_Static_assert(sizeof(PlayerBoomerangState) == 11u,
               "PlayerBoomerangState layout is part of the native capture ABI");
_Static_assert(__builtin_offsetof(PlayerBoomerangState, pos_frac) == 10u,
               "weapon.boomerang.pos_frac schema offset");
_Static_assert(__builtin_offsetof(PlayerBoomerangState, x) == 5u,
               "weapon.boomerang.x schema offset");
_Static_assert(__builtin_offsetof(PlayerBoomerangState, y) == 6u,
               "weapon.boomerang.y schema offset");

PlayerSwordState g_player_sword[4];
PlayerBeamState g_player_beam[4];
PlayerBoomerangState g_player_boomerang;
SharedEffectState g_shared_effect[2];
_Static_assert(sizeof(SharedEffectState) == 2u, "shared effect capture stride");
_Static_assert(__builtin_offsetof(SharedEffectState, timer) == 1u,
               "shared effect timer capture offset");

/* NES source: WieldBomb/UpdateBombOrFire/UpdateTimers/ResetObjState.
 * Drained C: existing item/core/NMI producers. Coverage: state/timer only.
 * Stance: EXTEND native authority; legacy publication is one-way and immediate. */
void shared_effect_set_state(unsigned char slot, unsigned char value)
{
    if (slot < 16u || slot > 17u) return;
    g_shared_effect[slot - 16u].state = value;
    F_ram_00AC_X2(slot) = value;
}

void shared_effect_set_timer(unsigned char slot, unsigned char value)
{
    if (slot < 16u || slot > 17u) return;
    g_shared_effect[slot - 16u].timer = value;
    F_ram_0028_X(slot) = value;
}

unsigned char shared_effect_tick_timer(unsigned char slot)
{
    SharedEffectState *effect;
    if (slot < 16u || slot > 17u) return 0u;
    effect = &g_shared_effect[slot - 16u];
    if (effect->timer != 0u) --effect->timer;
    return effect->timer;
}

void player_sword_reset(unsigned char player)
{
    if (player >= 4u) {
        return;
    }
    g_player_sword[player].state = 0u;
    g_player_sword[player].timer = 0u;
    g_player_sword[player].dir = 0u;
    g_player_sword[player].face = 0u;
    g_player_sword[player].owner = player;
    g_player_sword[player].x = 0;
    g_player_sword[player].y = 0;
}

void player_sword_reset_all(void)
{
    unsigned char i;
    for (i = 0u; i < 4u; ++i) {
        player_sword_reset(i);
    }
}

void player_beam_reset(unsigned char player)
{
    /* Scene/warp reinitialization retains the movement fraction, just as
     * SetUpWeaponWithState and ResetObjState do. Cold BSS starts at zero;
     * the broad room movement clear resets it explicitly below. */
    if (player >= 4u) {
        return;
    }
    g_player_beam[player].state = 0u;
    g_player_beam[player].dir = 0u;
    g_player_beam[player].grid = 0u;
    g_player_beam[player].qspeed = 0u;
    g_player_beam[player].owner = player;
    g_player_beam[player].x = 0u;
    g_player_beam[player].y = 0u;
}

void player_beam_reset_all(void)
{
    unsigned char i;
    for (i = 0u; i < 4u; ++i) {
        player_beam_reset(i);
    }
}

void player_beam_clear_fractions(void)
{
    unsigned char i;
    for (i = 0u; i < 4u; ++i) g_player_beam[i].pos_frac = 0u;
}

void player_boomerang_reset(void)
{
    /* NES ResetObjState/scene init retain ObjPosFrac[$0F]. Cold BSS is
     * zero; ClearRam0300UpTo owns the explicit broad movement clear. */
    g_player_boomerang.state = 0u;
    g_player_boomerang.dir = 0u;
    g_player_boomerang.grid = 0u;
    g_player_boomerang.qspeed = 0u;
    g_player_boomerang.owner = 0u;
    g_player_boomerang.x = 0u;
    g_player_boomerang.y = 0u;
    g_player_boomerang.count = 0u;
    g_player_boomerang.limit = 0u;
    g_player_boomerang.timer = 0u;
}

unsigned char player_boomerang_tick_timer(void)
{
    if (g_player_boomerang.timer != 0u) --g_player_boomerang.timer;
    return g_player_boomerang.timer;
}
