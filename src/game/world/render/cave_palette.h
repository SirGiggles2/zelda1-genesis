#ifndef ROOMROM_CAVE_PALETTE_H
#define ROOMROM_CAVE_PALETTE_H

/* Cave BG palette swap.
 *
 * NES source: reference/aldonunez/Z_06.asm:714 CaveBgPaletteRowsTransferBuf
 *   .BYTE $3F, $08, $08, $0F, $30, $00, $12, $0F, $07, $0F, $17, $FF
 *
 * Format: [hi=$3F][lo=$08][len=$08][8 bytes data][$FF term]
 * Writes 8 NES palette bytes to PPU $3F08..$3F0F:
 *   $3F08-$3F0B BG subpal 2: $0F (black) $30 (white) $00 (gray) $12 (blue)
 *   $3F0C-$3F0F BG subpal 3: $0F (black) $07 (brown) $0F (black) $17 (orange)
 *
 * On Genesis, PAL0 colors 8..15 hold BG subpal 2+3. Apply via
 * render_cram_subrange_upload(8, ...) to swap subpals without
 * rewriting the full 16-color PAL0.
 *
 * Drained C: NONE (new — no _runtime.c equivalent in src/game/)
 * Coverage : NONE (NES transfer-buf consumer was native PPU writer)
 * Stance   : EXTEND (new Genesis-side cave palette path) */

void cave_palette_apply(void);

#endif
