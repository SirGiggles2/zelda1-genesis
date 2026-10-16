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
unsigned char game_profile_is_remix(void);
#endif
