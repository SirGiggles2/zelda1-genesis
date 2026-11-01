/* T-317 green rupee tile.
 *
 * The NES rupee tile ($32/$33) uses palette indices 2/3 only. No existing
 * Genesis sprite row maps those to green, so the world 1-rupee draws a
 * remapped copy of the rupee tile whose body/shade use the free PAL3
 * entries 13/14. PAL3 entries 0..3 stay the NES red ramp; entries 4..15
 * are unused by NES-derived sprites. Remix only.
 */
#ifndef GREEN_RUPEE_H
#define GREEN_RUPEE_H

/* Atlas index of the source rupee tile (NES tile $32 -> atlas $38). */
#define GREEN_RUPEE_SRC_ATLAS_IDX   56u
/* A free atlas pair the remapped 8x16 copy is uploaded into (24/25, not
 * mapped by k_nes_item_tile_to_atlas_idx). */
#define GREEN_RUPEE_ATLAS_IDX       24u
/* Unused NES tile id in the narrow range; remapped by translate_tile. */
#define GREEN_RUPEE_NES_TILE        0x51u

/* PAL3 CRAM entries used by the green copy. Rupee art pixel 1 = facet
 * lines (Redux), 2 = body, 3 = highlight edge (both profiles). */
#define GREEN_RUPEE_PAL_SHADE       13u
#define GREEN_RUPEE_PAL_BODY        14u
#define GREEN_RUPEE_PAL_LIGHT       15u
/* NES color indices for the three greens. */
#define GREEN_RUPEE_NES_SHADE       0x1Au   /* dark green */
#define GREEN_RUPEE_NES_BODY        0x29u   /* Link tunic green */
#define GREEN_RUPEE_NES_LIGHT       0x3Au   /* pale green */

/* Red 20-rupee (user 2026-10-29: "red like 2P Link"): its own remapped copy,
 * so it no longer takes the scene's sprite sub-palette 2 colours. */
#define RED_RUPEE_ATLAS_IDX         28u     /* free pair 28/29 (8x16) */
#define RED_RUPEE_NES_TILE          0x53u   /* unused narrow tile id */
#define RED_RUPEE_PAL_SHADE         10u
#define RED_RUPEE_PAL_BODY          11u
#define RED_RUPEE_PAL_LIGHT         12u
#define RED_RUPEE_NES_SHADE         0x06u   /* dark red */
#define RED_RUPEE_NES_BODY          0x16u   /* P2 identity red (bg_palette.c) */
#define RED_RUPEE_NES_LIGHT         0x26u   /* light red */

#endif /* GREEN_RUPEE_H */
