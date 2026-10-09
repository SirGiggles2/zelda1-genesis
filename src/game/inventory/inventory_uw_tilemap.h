#ifndef INVENTORY_UW_TILEMAP_H
#define INVENTORY_UW_TILEMAP_H
/* NES UW (dungeon) subscreen NT2 tilemap + per-cell sub-pal, captured live
 * (pause_golden nes_uw/active.bin, L1Q1). The map-sheet cells are STATIC
 * here (captured state); G4 dynamic builder overrides them at runtime. */
extern const unsigned char k_inventory_uw_tilemap[30][32];
extern const unsigned char k_inventory_uw_subpal[30][32];
#endif
