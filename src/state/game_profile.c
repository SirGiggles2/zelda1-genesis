/* NES source: Redux code/main.asm; native per-file mode from T-249.
 * Drained C: NONE (NES has no cross-ROM per-file game profile).
 * Coverage: NONE.
 * Stance: GREENFIELD, native immutable profile selection only. */
#include "game_profile.h"
#include "save_game.h"
static GameProfile s_profile;
const GameProfile *game_profile_current(void) { return &s_profile; }
void game_profile_clear(void)
{
    save_game_discovery_clear();
    s_profile.active = 0u;
    s_profile.slot = 0u;
    s_profile.quest = 0u;
    s_profile.mode = SAVE_MODE_ORIGINAL;
}
unsigned char game_profile_activate(unsigned char slot)
{
    if (!save_game_slot_active(slot)) { game_profile_clear(); return 0u; }
    s_profile.slot = slot;
    s_profile.quest = save_game_slot_quest(slot) ? 1u : 0u;
    s_profile.mode = save_game_slot_mode(slot);
    s_profile.active = 1u;
    save_game_discovery_load(slot,s_profile.quest);
    return 1u;
}
unsigned char game_profile_is_remix(void)
{
    return s_profile.active && s_profile.mode == SAVE_MODE_MD_REMIX;
}
