/* src/fs_input.h — Genesis 3-button controller poll + edge-trigger detect. */
#ifndef FS_INPUT_H
#define FS_INPUT_H
#include <stdint.h>

#define FS_BTN_UP     0x01
#define FS_BTN_DOWN   0x02
#define FS_BTN_LEFT   0x04
#define FS_BTN_RIGHT  0x08
#define FS_BTN_A      0x10  /* Genesis A (or C in 3-button-as-NES-A mapping) */
#define FS_BTN_B      0x20
#define FS_BTN_START  0x40

void fs_input_init(void);
uint8_t fs_input_pressed(void);   /* edge-triggered (release-then-press) */

#endif
