/* UW dark-room manifest accessor + lit-state (Task 5.8).
 *
 * NES authority: reference/aldonunez/Z_05.asm:IsDarkRoom_Bank5
 *                (line 7795-7801) — dark = (AttrsE[room] & $80).
 * Drained C:     src/game/room/room_dispatch.c:room_is_dark_room (75)
 *                — generator-time only call site (G1: $0A7E OOB on
 *                RoomRom 2 KB nes_ram).
 *
 * Slice-1 Phase 5.8 ships the master `uw_dark_rooms` table for all
 * L1-L9 × Q1-Q2 quest-levels.
 */

#ifndef ROOMROM_UW_DARK_META_H
#define ROOMROM_UW_DARK_META_H

/* Returns 1 if (level, quest, room_id) is a dark room. */
unsigned char roomrom_uw_room_is_dark(unsigned char level,
                                      unsigned char quest,
                                      unsigned char room_id);

/* Per-room candle-lit state (RoomRom-local extension, NES-mirroring
 * UsedCandle but per-room indexed for slice-1 persistence). */
unsigned char roomrom_uw_room_lit(unsigned char room_id);
void          roomrom_uw_room_set_lit(unsigned char room_id);
void          roomrom_uw_room_clear_lit(void);  /* full reset */

/* Probe accessors for state mirror + persistence publish. */
unsigned char roomrom_uw_dark_candle_used_count(void);
void          roomrom_uw_dark_publish_persist(void);

#define ROOMROM_DEBUG_DARK_LIT_BASE   0x00FF7C00UL
#define ROOMROM_DEBUG_DARK_LIT_BYTES  256u

/* Increments candle-used counter — called by main.c B-button hook. */
void roomrom_uw_dark_note_candle_used(void);

#endif /* ROOMROM_UW_DARK_META_H */
