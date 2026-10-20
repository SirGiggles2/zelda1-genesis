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
/* A free atlas tile the remapped copy is uploaded into (not mapped by
 * k_nes_item_tile_to_atlas_idx). */
#define GREEN_RUPEE_ATLAS_IDX       24u
/* Unused NES tile id in the narrow range; remapped by translate_tile. */
#define GREEN_RUPEE_NES_TILE        0x51u

/* PAL3 CRAM entries used by the green copy. */
#define GREEN_RUPEE_PAL_BODY        13u
#define GREEN_RUPEE_PAL_SHADE       14u
/* NES color indices for the two greens. */
#define GREEN_RUPEE_NES_BODY        0x29u   /* Link tunic green */
#define GREEN_RUPEE_NES_SHADE       0x1Au   /* dark green */

#endif /* GREEN_RUPEE_H */
