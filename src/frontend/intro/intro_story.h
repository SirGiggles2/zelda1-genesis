/* src/intro_story.h
 *
 * Story + items continuous-scroll runtime. PHASE_STORY_LOAD calls
 * intro_story_load(); PHASE_STORY_RUN calls intro_story_step() each
 * vblank. Story text and items are a single concatenated tilemap that
 * scrolls vertically; items animate via per-tile pal toggles inside
 * step().
 */
#ifndef INTRO_STORY_H
#define INTRO_STORY_H

void intro_story_load(void);            /* PHASE_STORY_LOAD */
void intro_story_step(void);            /* per vblank during STORY_RUN */
unsigned char intro_story_at_end(void); /* 1 once full content + end pause done */
void intro_story_clear_end(void);       /* reset end flag for next loop */

#endif
