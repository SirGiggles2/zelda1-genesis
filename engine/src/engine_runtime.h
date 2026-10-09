#ifndef ROOMROM_DEBUG_RUNTIME_H
#define ROOMROM_DEBUG_RUNTIME_H

/* engine Debug RAM Map (P0-3 canonical table; both ROMs link these)
 * ===================================================================
 *   $FF7200..$FF7277  120 B   state mirror (warp+door+cellar+push+dark+inv)
 *   $FF7300..$FF732F   48 B   Gate D metadata probe (Task 5.4 + 5.6)
 *   $FF7400..$FF76C3  708 B   OW raw-tile cache (Task 5.4)
 *   $FF76D0..$FF77CF  256 B   UW door persistence table (Task 5.5)
 *   $FF7800..$FF7AC3  708 B   UW BG-tile walkability cache (Task 5.5)
 *   $FF7B00..$FF7BFF  256 B   Push-block persistence (Task 5.7)
 *   $FF7C00..$FF7CFF  256 B   Dark-room candle-lit persistence (Task 5.8)
 *   $FF7D00..$FF7DFF  256 B   Item-taken persistence (Task 5.9)
 *
 * All offsets are within the 64KB Genesis 68K work RAM
 * ($FF0000..$FFFFFF). Probes read via "68K RAM" BizHawk domain,
 * subtract $FF0000 to get the domain offset.
 */

void roomrom_debug_enter(void);
void roomrom_debug_tick(void);
unsigned char roomrom_debug_get_scene(void);
unsigned char roomrom_debug_get_room_id(void);
short roomrom_debug_get_link_x(void);
short roomrom_debug_get_link_y(void);

/* Task 5.4: warp coordinator state surface. Both Zelda.md and
 * Zelda.md link these exports; BizHawk Lua probes read them
 * during Gate B/C verification. */
unsigned char roomrom_debug_warp_is_active(void);
unsigned char roomrom_debug_warp_unsupported_count(void);

/* Task 5.4: passive RAM state mirror for BizHawk Lua probes.
 *
 * The Genesis 68k has no script-host symbol resolution, so each tick we
 * mirror the gate-B field set into a fixed RAM block. Lua reads the
 * block; user drives input manually. Probes log transitions and dump
 * snapshots without needing scripted joypad input.
 *
 * Layout (little-endian byte ordering on Genesis but Lua uses the same
 * accessor pattern as A4 probe — big-endian publish, byte-swap reads):
 *
 *   off  size  field
 *   ---  ----  ----------------------------------------------------
 *   0    1     magic byte 'W' (0x57)
 *   1    1     magic byte 'P' (0x50)
 *   2    2     frame_counter (u16, BE)
 *   4    1     scene (SCENE_OW=0, SCENE_UW=1, SCENE_CAVE=2)
 *   5    1     room_id
 *   6    2     link_x (s16, BE)
 *   8    2     link_y (s16, BE)
 *   10   1     link_face
 *   11   1     link_dir
 *   12   1     link_grid_offset (s8)
 *   13   1     doorway_dir
 *   14   1     warp_is_active
 *   15   1     warp_unsupported_count
 *   16   1     uw_level (0 if scene != UW)
 *   17   1     uw_quest (0 if scene != UW)
 *   18   1     ow_raw_tile_stable (0/1)
 *   19   1     link_pos_frac
 *   20   1     underground_exit_type (slice-1 stub: always 0)
 *   21   1     tile_under_link_foot (raw NES BG tile id; 0 if cache unstable)
 *   22   1     save.version
 *   23   1     save.source_room_id
 *   24   1     save.source_underground_entrance_tile (post-collapse)
 *   25   1     save.source_underground_entrance_tile_raw
 *   26   2     save.source_link_x (s16, BE)
 *   28   2     save.source_link_y (s16, BE)
 *   30   1     save.source_link_face
 *   31   1     save.dest_level
 *   32   1     save.dest_quest
 *   33   1     save.dest_room_id *   34   1     save.dest_link_face
 *   35   1     walkable_at_link_metatile (s_walkable[col][row], 0/1)
 *   36   1     link_walkable_north (collision probe result for dir UP)
 *   37   1     link_metatile_col (0..15, OW only)
 *   38   1     link_metatile_row (0..10, OW only)
 *   39   1     reserved
 *
 * Task 5.5 extension (offsets 40..71, total 72 bytes):
 *   40   1     uw_door_type[E]   (DOOR_TYPE_*)
 *   41   1     uw_door_type[W]
 *   42   1     uw_door_type[S]
 *   43   1     uw_door_type[N]
 *   44   1     uw_door_opened_mask (DOOR_BIT_E/W/S/N OR'd)
 *   45   1     uw_door_false_timer (NES ObjTimer-equivalent, slice-1 mirror)
 *   46   1     uw_door_has_shutters
 *   47   1     uw_door_shutter_trigger_count (counts debug-chord fires)
 *   48   1     s_link_keys (current count)
 *   49   1     s_link_keys_pre_touch
 *   50   1     s_link_keys_post_touch
 *   51   1     last_touch_dir (DOOR_DIR_*; 0xFF if none yet)
 *   52   1     last_touch_result (0=blocked, 1=passable, 0xFF=none)
 *   53   1     last_touch_door_type
 *   54..71   18  reserved (room id history ring + future fields)
 *
 * Task 5.6 extension (offsets 72..79):
 *   72   1     uw_cellar_state (0=outside, 1=in_cellar; tracks current room)
 *   73   1     uw_cellar_entry_count (debug)
 *   74   1     uw_cellar_exit_count (debug)
 *   75   1     uw_cellar_pending_exit (1 if next LOAD exits cellar)
 *   76..79  4  reserved
 *
 * Task 5.7 extension (offsets 80..95):
 *   80   1     pushblock_state_for_current_room (0=idle 1=pushed 2=secret)
 *   81   1     pushblock_active_dir ($08/$04/$02/$01 per BlockPushDirections)
 *   82   1     pushblock_active_timer (0..$10 in TIMING)
 *   83   1     pushblock_active_offset (0..$10 in MOVING)
 *   84   1     pushblock_active_block_col_mt
 *   85   1     pushblock_active_block_row_mt
 *   86   1     pushblock_complete_count (debug; total pushes)
 *   87   1     pushblock_room_all_dead (gate state for probe; slice-1 = 1)
 *   88   1     pushblock_internal_state (0=IDLE 1=TIMING 2=MOVING 3=DONE)
 *   89..95  7  reserved
 *
 * Task 5.8 extension (offsets 96..103):
 *   96   1     uw_room_is_dark (current room flag, 0/1)
 *   97   1     uw_room_lit (current room flag, 0/1)
 *   98   1     candle_used_count (debug)
 *   99..103  5 reserved
 *
 * Task 5.9 extension (offsets 104..119, total 120 bytes):
 *   104  1     s_link_keys (duplicate of offset 48; convenience)
 *   105  1     INVENTORY_VALUE(16) compass byte
 *   106  1     INVENTORY_VALUE(17) map byte
 *   107  1     triforce_pickup_active (slice-1 stub flag)
 *   108  1     item_id_for_current_room (0 if none)
 *   109  1     s_item_taken[current_room]
 *   110  1     s_room_visited_count (5.10b deferral; slice-1 = 0)
 *   111  1     s_triforce_pickup_active duplicate (probe convenience)
 *
 * PR-4a extension (offsets 112..119, Task 5.9 reserved bytes):
 *   112  1     level_chr_swap_state (0=IDLE,1=REQUESTED,2=BLANK,
 *                                    3=DMA_SCENE_A,4=DMA_SCENE_B,5=READY)
 *   113  1     level_chr_swap_active_scene (roomrom_scene_id_t)
 *   114  2     level_chr_swap_request_count (u16, BE)
 *   116  4     level_chr_swap_total_bytes_dma (u32, BE)
 */
#define ROOMROM_DEBUG_STATE_MIRROR_BASE  0x00FF7200UL
#define ROOMROM_DEBUG_STATE_MIRROR_BYTES 120u

#define ROOMROM_DEBUG_PROBE_CONTROL_BASE 0x00FF73F8UL
#define ROOMROM_DEBUG_PROBE_ARM0         0x52u  /* 'R' */
#define ROOMROM_DEBUG_PROBE_ARM1         0x50u  /* 'P' */
#define ROOMROM_DEBUG_PROBE_FLAGS_OFF    2u
#define ROOMROM_DEBUG_PROBE_HEAVY_MIRROR 0x01u
#define ROOMROM_DEBUG_PROBE_ENEMY_STRESS 0x02u
#define ROOMROM_DEBUG_PROBE_BOSS_TRIGGER 0x04u
/* Boot self-tests in roomrom_debug_enter (metadata, warp routes, dungeon
 * round-trip, options x3, HUD format, save serializer). They write test
 * patterns into live NES RAM/SRAM (T-091: save serializer left $30..$57 in
 * Items $0657..$067E on every New Game), so they run only when a probe
 * arms this flag before gameplay entry. */
#define ROOMROM_DEBUG_PROBE_SELFTEST     0x08u

void roomrom_debug_publish_state_mirror(void);

/* Task 5.5: persistence-table dump block at $FF76D0 (256 B, one byte
 * per UW room id; value = DOOR_BIT_* mask of opened doors).
 * Published once per tick alongside the state mirror. */
#define ROOMROM_DEBUG_UW_PERSIST_BASE  0x00FF76D0UL
#define ROOMROM_DEBUG_UW_PERSIST_BYTES 256u

void roomrom_debug_publish_uw_persist(void);

#endif
