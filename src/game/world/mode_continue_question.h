/* GameMode 8 CONTINUE / SAVE / RETRY (mode_continue_question.c).
 *
 * NES source: reference/aldonunez/Z_05.asm InitMode8 +
 * UpdateMode8ContinueQuestion_Full. Reads ButtonsPressed ($F8); the caller
 * (engine/src/main.c roomrom_debug_tick) reads the pads first.
 */

#ifndef MODE_CONTINUE_QUESTION_H
#define MODE_CONTINUE_QUESTION_H

void mode8_continue_question_update(void);

#endif
