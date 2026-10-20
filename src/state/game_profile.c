/* NES source: Redux code/main.asm; native per-file mode from T-249.
 * Drained C: NONE (NES has no cross-ROM per-file game profile).
 * Coverage: NONE.
 * Stance: GREENFIELD, native immutable profile selection only. */
#include "game_profile.h"
#include "save_game.h"
#include "player_state.h"
static GameProfile s_profile;
unsigned char g_profile_remix;
const GameProfile *game_profile_current(void) { return &s_profile; }
void game_profile_clear(void)
{
    save_game_discovery_clear();
    s_profile.active = 0u;
    s_profile.slot = 0u;
    s_profile.quest = 0u;
    s_profile.mode = SAVE_MODE_ORIGINAL;
    g_profile_remix = 0u;
    g_player_count = 1u;
}
unsigned char game_profile_activate(unsigned char slot)
{
    if (!save_game_slot_active(slot)) { game_profile_clear(); return 0u; }
    s_profile.slot = slot;
    s_profile.quest = save_game_slot_quest(slot) ? 1u : 0u;
    s_profile.mode = save_game_slot_mode(slot);
    s_profile.active = 1u;
    g_profile_remix = (unsigned char)(s_profile.mode == SAVE_MODE_MD_REMIX);
    g_player_count = save_game_slot_players(slot);
    if (g_player_count < 1u || g_player_count > PLAYER_MAX_COUNT)
        g_player_count = 1u;
    save_game_discovery_load(slot,s_profile.quest);
    return 1u;
}
/* game_profile_is_remix() is a header inline over g_profile_remix; see
 * game_profile.h. g_profile_remix is cleared with the profile, so the old
 * `active && remix` result is preserved without a per-call JSR/RTS. */
