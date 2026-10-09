/* uw_map_builder.h — dynamic dungeon-map glyph builder.
 *
 * NES source (authoritative): Z_05.asm
 *   Submenu_WriteSheetMapRowTransferRecord (7530-7619)
 *   Submenu_WriteScanningMapRoomMark        (7659-7713)
 *   CalcOpenDoorwayMask                      (7726-7761)
 *   FindDoorAttrByDoorBit                    (4520-4560)
 *
 * Produces the 8-row x 16-col underworld map glyph grid for the current
 * dungeon, reading LIVE visited + door state exactly as the NES does:
 *   - unvisited room (world-flag bit $20 clear) -> $F5 (blank)
 *   - visited room -> 4-bit open-doorway mask (up/down/left/right
 *     walkability) -> glyph $E2 + mask  (range $E2..$F1)
 *   - each row rotated right by LevelInfo_SubmenuMapRotation
 *   - cols blanked where LevelInfo_SubmenuMapMask[col] & MapRowMasks[row]==0
 *
 * Data sources: installed LevelBlockAttrsA/B ($687E/$68FE), room flags
 * through $6BAF/$6BB0, and installed LevelInfo rotation/mask/Triforce.
 * Entry installs these for the current level and quest before menu use.
 */
#ifndef UW_MAP_BUILDER_H
#define UW_MAP_BUILDER_H

/* Fill out[8][16] with the live dungeon-map glyph grid for `level` (1..9).
 * out[row][col] indexes room (row<<4 | col) in display space (post-rotate,
 * post-mask). Levels out of range -> all-blank ($F5). */
void uw_map_build(unsigned char level, unsigned char out[8][16]);

/* Current installed LevelInfo fields; level 1..9, otherwise zero. */
unsigned char uw_map_rotation(unsigned char level);       /* low nibble */
unsigned char uw_map_triforce_room(unsigned char level);  /* $6BAE */

#endif /* UW_MAP_BUILDER_H */
