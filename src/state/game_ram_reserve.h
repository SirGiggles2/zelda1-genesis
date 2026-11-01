/* T-368 Stage C: the native-state block reserved above the 2 KB NES image.
 * Hand-written. The generated game_ram header (C5) includes this file.
 *   image   [$FF8000, $FF8800)   NES RAM image
 *   native  [$FF8800, GR_END_ABS) GR_RESERVED bytes, zeroed at boot after the SGDK heap
 *                                 is walled and the DMA queue/buffer have been relocated above it
 * Evidence: tools/native_ram/probe_dma_wall.py prints the measured DMA bounds. */
#ifndef GAME_RAM_RESERVE_H
#define GAME_RAM_RESERVE_H

#define GR_OFF        0x0800u
#define GR_RESERVED   0x2000u
#define GR_END_ABS    (0xFF8000ul + GR_OFF + GR_RESERVED)

/* T-257: pause-map scratch, nes_ram offsets [$1000, $1C00) inside the native
 * block above every player record (PLAYER_DEFER_END, asserted in
 * player_defer.c). Owned by src/game/inventory/pause_map.c only while the
 * map page is open; nothing else reads or writes it. */
#define GR_MAP_SCRATCH_OFF  0x1000u
#define GR_MAP_SCRATCH_SIZE 0x0C00u

/* T-260: Remix B+C combo state (ComboRam, src/game/items/combo_ram.h), nes_ram
 * offsets [$1C00, $1D00), directly above the map scratch and below the native
 * end canary (GR_OFF + GR_RESERVED - 4 = $27FC). Zeroed at boot with the block;
 * written only in MD Remix. */
#define GR_COMBO_OFF        0x1C00u
#define GR_COMBO_SIZE       0x0100u
_Static_assert(GR_COMBO_OFF >= GR_MAP_SCRATCH_OFF + GR_MAP_SCRATCH_SIZE, "combo above map scratch");
_Static_assert(GR_COMBO_OFF + GR_COMBO_SIZE <= GR_OFF + GR_RESERVED - 4u, "combo below end canary");

#endif
