/* debug_tilegrid.h — title-screen tile-grid debug scene.
 *
 * Entry: pressing MODE at the title screen jumps here (never returns).
 * Renders the Z1 atlas (bg_sparse_tile_lut + sprite atlas) as a
 * deterministic tile grid so a byte-diff against the matching custom
 * NES test ROM (tools/gen_chr_viewer_rom.py output) audits every
 * (tile_id, sub_pal) atlas entry.
 *
 * Layout mirrors chr_viewer_rom.nes:
 *   Rows  0..15  cols 0..15  — BG 16x16 tile-id grid using current sub_pal
 *   Rows 16..23  cols 0..7   — SPR 8x8 grid showing current sprite-page tiles
 *   Outside grid             — blank fill (tile 0 with backdrop color)
 *
 * Controls:
 *   B button     cycle BG sub_pal 0..3 (rewrites attribute-equivalent route)
 *   Start        cycle sprite-page 0..3 (tile_ids $00-3F .. $C0-FF)
 *   Select       toggle 8x16 sprite mode (matches Z1 runtime)
 *   A button     no-op (Genesis has one atlas; A=bank cycle on NES side has
 *                no Genesis analog since all banks coexist in VRAM)
 *
 * Build: linked into Zelda.md via tools/build/build_rom.py TITLE_C_SOURCES.
 */
#ifndef GAME_DEBUG_TILEGRID_H
#define GAME_DEBUG_TILEGRID_H

/* Enter the tile-grid scene. Does NOT return — loops forever rendering
 * and polling input. Caller (intro_main) must transition cleanly: stop
 * music if desired, then jump here. */
void debug_tilegrid_main(void);

#endif /* GAME_DEBUG_TILEGRID_H */
