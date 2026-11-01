#ifndef ARROW_DIAG_CHR_H
#define ARROW_DIAG_CHR_H

/* T-260 user-drawn up-right arrow: two 8x16 columns (left top/bottom, then
 * right top/bottom), 4bpp item-atlas colour indexes 1 shaft / 2 head /
 * 3 feathers. Uploaded into free item atlas pairs; NES-style tile ids route
 * there through translate_tile. Other diagonals are flips. */
#define ARROW_DIAG_CHR_BYTES     128u
#define ARROW_DIAG_LEFT_ATLAS    32u     /* free pair 32/33 */
#define ARROW_DIAG_RIGHT_ATLAS   60u     /* free pair 60/61 */
#define ARROW_DIAG_LEFT_NES_TILE  0xF8u  /* not an item atlas tile */
#define ARROW_DIAG_RIGHT_NES_TILE 0xFAu  /* not an item atlas tile */

extern const unsigned char arrow_diag_chr[ARROW_DIAG_CHR_BYTES];

#endif /* ARROW_DIAG_CHR_H */
