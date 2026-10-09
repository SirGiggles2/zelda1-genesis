/* src/fs_input.c — Genesis 3-button poll + edge detect.
 *
 * Genesis controller port 1 at $00A10003 (data) / $00A10009 (control).
 * Read sequence: TH=1 then TH=0; each yields 6 button bits. Combine for the
 * 3-button (Up/Down/Left/Right/A/B/C/Start) set. Active-low (0 = pressed).
 *
 * Boot-frame ignore: the first 4 frames after init may report spurious
 * presses due to controller line settling; suppress edges during this window.
 */
#include "fs_input.h"
#include <stdint.h>

static uint8_t s_prev;
static uint8_t s_boot_frames;

#define IO_PORT1_DATA (*(volatile uint8_t *)0x00A10003)
#define IO_PORT1_CTRL (*(volatile uint8_t *)0x00A10009)

void fs_input_init(void) {
    IO_PORT1_CTRL = 0x40;       /* TH = output, others = input */
    IO_PORT1_DATA = 0x40;
    s_prev = 0;
    s_boot_frames = 0;
}

static uint8_t read_pad(void) {
    /* TH=0 step: bits 5..0 = -- ST A 00 D U  (Start + A + Down + Up).
     * TH=1 step: bits 5..0 = C  B  R L D U  (C + B + Right + Left + Down + Up). */
    uint8_t lo, hi;
    IO_PORT1_DATA = 0x00; lo = (uint8_t)(IO_PORT1_DATA & 0x3F);
    IO_PORT1_DATA = 0x40; hi = (uint8_t)(IO_PORT1_DATA & 0x3F);

    uint8_t btn = 0;
    if (!(hi & 0x01)) btn |= FS_BTN_UP;
    if (!(hi & 0x02)) btn |= FS_BTN_DOWN;
    if (!(hi & 0x04)) btn |= FS_BTN_LEFT;
    if (!(hi & 0x08)) btn |= FS_BTN_RIGHT;
    if (!(hi & 0x10)) btn |= FS_BTN_B;
    if (!(hi & 0x20)) btn |= FS_BTN_A;       /* C button — used as NES A equivalent */
    if (!(lo & 0x10)) btn |= FS_BTN_A;       /* TH=0 A bit (real Genesis A) */
    if (!(lo & 0x20)) btn |= FS_BTN_START;
    return btn;
}

uint8_t fs_input_pressed(void) {
    if (s_boot_frames < 4u) {
        s_boot_frames++;
        s_prev = read_pad();
        return 0;
    }
    uint8_t cur = read_pad();
    uint8_t edge = (uint8_t)(cur & ~s_prev);
    s_prev = cur;
    return edge;
}
