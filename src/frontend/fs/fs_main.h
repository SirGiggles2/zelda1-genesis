/* src/fs_main.h
 *
 * Boot entry from boot.asm (proof ROM) or intro_handoff.c (main ROM v6).
 * Owns main loop while native File Select is active. Returns by jumping
 * to fs_to_transpiled_trampoline (v6); never returns normally.
 */
#ifndef FS_MAIN_H
#define FS_MAIN_H

void fs_main(void);

/* Frame-driven alternative to fs_main() for hosts that own the main loop.
 * fs_enter() does the one-shot init; fs_tick() advances exactly one frame
 * and does NOT wait for vblank — the caller does. */
void fs_enter(void);
void fs_tick(void);
extern unsigned char g_fs_back_requested;
void fs_file_options_exit(unsigned char committed);

#endif
