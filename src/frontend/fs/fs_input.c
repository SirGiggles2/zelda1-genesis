/* File Select input uses SGDK's sampled primary party controller.
 * Convert its BUTTON bits to FS bits, retaining boot suppression and edges.
 * Protocol detection/polling belongs to the controller adapter, including taps.
 */
#include "fs_input.h"
#include <stdint.h>
#include "controller_abi.h"

static uint8_t s_prev;
static uint8_t s_boot_frames;

/* T-377: the host polls through SGDK before fs_tick. Direct TH strobes
 * cannot read TeamPlayer and disrupt its protocol. Preserve FS edge policy. */
void fs_input_init(void) {
    s_prev = 0;
    s_boot_frames = 0;
}

static uint8_t read_pad(void) {
    const unsigned short raw = controller_read_party(0u);
    return (uint8_t)((raw & 0x000Fu) |
                     ((raw & 0x0040u) >> 2) | ((raw & 0x0010u) << 1) |
                     ((raw & 0x0080u) >> 1) | ((raw & 0x0020u) << 2));
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
