/* Active cartridge profile is derived from the immutable selected file.
 * No renderer/debug map or Options toggle owns production mode selection. */
#ifndef GAME_PROFILE_H
#define GAME_PROFILE_H
typedef struct GameProfile {
    unsigned char active;
    unsigned char slot;
    unsigned char quest; /* NES quest number: 0 or 1 */
    unsigned char mode;  /* SAVE_MODE_* */
} GameProfile;
const GameProfile *game_profile_current(void);
unsigned char game_profile_activate(unsigned char slot);
void game_profile_clear(void);
/* T-332/T-358: per-frame render selectors read the cached flag directly.
 * g_profile_remix is cleared by game_profile_clear(), so it already carries
 * the old `active && remix` result; keep the getter in the header so every
 * call site inlines instead of paying a JSR/RTS per monster/frame. */
extern unsigned char g_profile_remix;
static inline unsigned char game_profile_is_remix(void)
{
    return g_profile_remix;
}
#endif
