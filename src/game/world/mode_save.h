/* mode_save.h — GameMode $0D (Save) native body.
 * NES source: reference/aldonunez/Z_02.asm:2779 UpdateModeDSave.
 */
#ifndef MODE_SAVE_H
#define MODE_SAVE_H

/* Commit the current save slot (CurSaveSlot, NES $16) to cart SRAM and
 * return to Play. Sets g_mode_save_last_result. */
void mode13_save_update(void);

/* 0 = no save attempted, 1 = committed, 2 = refused (bad slot index). */
extern unsigned char g_mode_save_last_result;

#endif /* MODE_SAVE_H */
