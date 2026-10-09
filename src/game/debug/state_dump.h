/* state_dump.h - A+B+C+Start: freeze the game and show all internal data.
 *
 * Holding A, B, C and Start together stops the game for good and turns the
 * screen into a paged dump of the machine state (docs/debug/state_dump.md):
 * readable pages (summary, objects, NES RAM, VDP) to photograph or
 * screenshot, and lossless image pages carrying all 64 KB of 68k RAM, all
 * 64 KB of VRAM, CRAM, VSRAM and the VDP register shadow for
 * tools/debug/decode_dump.py. Left / Right turn the pages.
 */
#ifndef STATE_DUMP_H
#define STATE_DUMP_H

/* Where the freeze came from (stored in the dump header). */
#define STATE_DUMP_CTX_TITLE 1u
#define STATE_DUMP_CTX_FS    2u
#define STATE_DUMP_CTX_GAME  3u
#define STATE_DUMP_CTX_HANG  4u   /* VInt: the main loop stopped polling */
#define STATE_DUMP_CTX_TILEGRID 5u

/* Main context, once per frame right after the VBlank process (the pad
 * state is fresh, the frame's VDP transfers are done). Does not return
 * when the chord is held. */
void state_dump_poll(unsigned char ctx);

/* Loops that read the pad port themselves (3-button bits, active high:
 * $01 up $02 down $04 left $08 right $10 B $20 C $40 A $80 Start). */
void state_dump_pad(unsigned char ctx, unsigned char held);

/* VBlank interrupt. When the main loop has not polled for a second (a
 * hang or a blocking loop), reads the pad itself and freezes on the
 * chord. */
void state_dump_vint(void);

#endif
