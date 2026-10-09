/* Task 5.7: UW push-block state machine.
 *
 * NES source: reference/aldonunez/Z_04.asm:UpdateBlock (618-746).
 *             BlockPushDirections at 615-616.
 *
 * State machine mirrors NES `ObjState` for the BlockObj slot:
 *
 *   PB_IDLE    = 0   (NES UpdateBlock0Idle, before timer reaches $10)
 *   PB_TIMING  = 1   (slice-1 sub-state of IDLE — push timer 0..$10)
 *   PB_MOVING  = 2   (NES UpdateBlock1Moving, offset 0..$10)
 *   PB_DONE    = 3   (NES UpdateBlock2Done, latched)
 *
 * NES uses ObjState directly $00/$01/$02 with the timer as a separate
 * field. Slice-1 splits TIMING out so the state mirror can show the
 * timer phase distinctly from the no-input idle phase. State $03 maps
 * back to NES BlockPushComplete-driven semantics on room re-entry.
 *
 * Per-room debug mirror: s_pb_state_per_room[room_id] ∈ {0, 1}.
 * It resets on room entry; persistent door state has a separate owner.
 */

#ifndef ROOMROM_PUSHBLOCK_H
#define ROOMROM_PUSHBLOCK_H

void roomrom_pushblock_init(void);
void roomrom_pushblock_room_load(unsigned char level,
                                 unsigned char quest,
                                 unsigned char room_id);
void roomrom_pushblock_tick(void);
void roomrom_pushblock_update(unsigned int slot);

/* Mirror surface (used by main.c publish and probe). */
unsigned char roomrom_pushblock_state_for_room(unsigned char room_id);
unsigned char roomrom_pushblock_active_state(void);
unsigned char roomrom_pushblock_active_dir(void);
unsigned char roomrom_pushblock_active_timer(void);
unsigned char roomrom_pushblock_active_offset(void);
unsigned char roomrom_pushblock_active_block_col(void);
unsigned char roomrom_pushblock_active_block_row(void);
unsigned char roomrom_pushblock_complete_count(void);
unsigned char roomrom_pushblock_room_all_dead(void);

/* Persistence dump publish: copies s_pb_state_per_room[256] to
 * ROOMROM_DEBUG_PUSHBLOCK_PERSIST_BASE. Called once per tick. */
void roomrom_pushblock_publish_persist(void);

/* Sits after the UW walkability cache ($FF7800..$FF7AC3, 708 B).
 * Plan said $FF77D0 but that range is only 48 B before the
 * walkability cache starts; relocated to $FF7B00 to preserve
 * 256 B of room indexed storage. */
#define ROOMROM_DEBUG_PUSHBLOCK_PERSIST_BASE  0x00FF7B00UL
#define ROOMROM_DEBUG_PUSHBLOCK_PERSIST_BYTES 256u

#endif /* ROOMROM_PUSHBLOCK_H */
