#ifndef SWORD_SHOT_H
#define SWORD_SHOT_H

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

#endif
