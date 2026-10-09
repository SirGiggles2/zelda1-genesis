/* src/intro_phase.c
 *
 * Phase dispatcher. Title phases (LOAD/DISPLAY/FADEOUT/BLACK_HOLD)
 * wired in Task 4. Story phases (LOAD/RUN) wired in Task 5.
 * Full attract loop: TITLE_LOAD -> TITLE_DISPLAY -> TITLE_FADEOUT
 *                    -> BLACK_HOLD -> STORY_LOAD -> STORY_RUN -> TITLE_LOAD
 */
#include "intro_phase.h"
#include "intro_title.h"
#include "intro_story.h"
#include "platform_abi.h"

static intro_phase_t s_phase;
static unsigned short s_phase_counter;

static void goto_phase(intro_phase_t next) {
    s_phase = next;
    s_phase_counter = 0;
    DBG_SENTINEL(0x10u) = (unsigned char)next;
}

void intro_phase_init(void) {
    s_phase = PHASE_TITLE_LOAD;
    s_phase_counter = 0;
    DBG_SENTINEL(0x10u) = (unsigned char)PHASE_TITLE_LOAD;
}

void intro_phase_step(void) {
    switch (s_phase) {
        case PHASE_TITLE_LOAD:
            intro_title_setup();
            goto_phase(PHASE_TITLE_DISPLAY);
            break;

        case PHASE_TITLE_DISPLAY:
            intro_title_step();
            s_phase_counter++;
            if (s_phase_counter >= TITLE_DISPLAY_FRAMES) {
                intro_title_fade_reset();
                intro_title_fade_apply(0);
                goto_phase(PHASE_TITLE_FADEOUT);
            }
            break;

        case PHASE_TITLE_FADEOUT:
            intro_title_step();
            intro_title_fade_step();
            if (intro_title_fade_done()) {
                intro_title_blackout();
                goto_phase(PHASE_BLACK_HOLD);
            }
            break;

        case PHASE_BLACK_HOLD:
            s_phase_counter++;
            if (s_phase_counter >= BLACK_HOLD_FRAMES) {
                goto_phase(PHASE_STORY_LOAD);
            }
            break;

        case PHASE_STORY_LOAD:
            intro_story_load();
            goto_phase(PHASE_STORY_RUN);
            break;

        case PHASE_STORY_RUN:
            intro_story_step();
            if (intro_story_at_end()) {
                intro_story_clear_end();
                goto_phase(PHASE_TITLE_LOAD);
            }
            break;

        default:
            __builtin_unreachable();
    }
}
