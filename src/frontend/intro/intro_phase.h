/* src/intro_phase.h
 *
 * Top-level phase state machine for the native intro. intro_main
 * calls intro_phase_init() once at boot and intro_phase_step()
 * each vblank.
 */
#ifndef INTRO_PHASE_H
#define INTRO_PHASE_H

typedef enum {
    PHASE_TITLE_LOAD = 0,
    PHASE_TITLE_DISPLAY,
    PHASE_TITLE_FADEOUT,
    PHASE_BLACK_HOLD,
    PHASE_STORY_LOAD,
    PHASE_STORY_RUN,    /* covers story text + items as one continuous
                         * scroll; intro_story owns sub-state internally */
} intro_phase_t;

void intro_phase_init(void);
void intro_phase_step(void);

#endif
