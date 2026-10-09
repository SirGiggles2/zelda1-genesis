/* src/intro_title.h
 *
 * Title runtime: load (CHR/CRAM/plane/sprites), per-vblank step
 * (glow + waterfall), fade-out driver. Setup blanks the display
 * during upload and re-enables it before returning.
 */
#ifndef INTRO_TITLE_H
#define INTRO_TITLE_H

/* Title timing — tuned vs NES f0035..f0785 reference but compressed
 * for snappier feel per user feedback. NES bright-hold ~520 fr, fade
 * ~230 fr, black ~255 fr; we keep fade at 230 (preserves 14-cycle
 * NES color progression) and tighten the holds. */
#define TITLE_DISPLAY_FRAMES 400u
#define TITLE_FADE_CYCLES    14u
#define BLACK_HOLD_FRAMES    180u

void intro_title_setup(void);                   /* PHASE_TITLE_LOAD */
void intro_title_step(void);                    /* PHASE_TITLE_DISPLAY */
void intro_title_fade_apply(unsigned char idx); /* paint cycle <idx> CRAM */
void intro_title_fade_step(void);               /* PHASE_TITLE_FADEOUT each vblank */
unsigned char intro_title_fade_done(void);      /* 1 once last cycle complete */
void intro_title_fade_reset(void);              /* re-arm fade for next loop */
void intro_title_blackout(void);                /* CRAM all black (entry to BLACK_HOLD) */

#endif /* INTRO_TITLE_H */
