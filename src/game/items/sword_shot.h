#ifndef SWORD_SHOT_H
#define SWORD_SHOT_H
#include "../../state/weapon_state.h"

/* Call-local saved publication, never a second persistent gameplay owner. */
typedef struct {
    PlayerBeamState beam;
    unsigned char inv_timer;
} BeamLegacyContext;

/* T-116: Link's sword shot / magic shot as the NES object in slot $0E. */

/* Room / boot reset. */
void sword_shot_init(void);

/* MakeSwordShot (Z_07.asm:4581), called by the sword at state 3.
 * allowed = hearts / $0529 / sword-style option gate (caller). */
void sword_shot_make(unsigned char allowed);

/* @MakeMagicShot (Z_07.asm:4521), called by the rod at state 3. */
void magic_shot_make(void);

/* UpdateSwordShotOrMagicShot (Z_07.asm:3408). */
void sword_shot_update(void);

/* Commit only the outputs of a legacy collision call made with this
 * player's beam published in slot14. Never infer ownership from rendering. */
void sword_shot_commit_collision(unsigned char player);
void sword_shot_make_for_player(unsigned char player, unsigned char allowed,
    unsigned char x, unsigned char y, unsigned char dir, unsigned char grid);
void magic_shot_make_for_player(unsigned char player,
    unsigned char x, unsigned char y, unsigned char dir, unsigned char grid);
void sword_shot_update_for_player(unsigned char player);
void sword_shot_hide_player(unsigned char player);
void sword_shot_swap_in(unsigned char player, BeamLegacyContext *saved);
void sword_shot_swap_out(const BeamLegacyContext *saved);

#endif
