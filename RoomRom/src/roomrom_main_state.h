/* RoomRom main.c-internal state surface.
 *
 * The warp coordinator (roomrom_world_transition) needs to apply a
 * scene/level/quest/room/Link-position outcome atomically. main.c owns
 * those statics; this header exposes a single-call applier so the
 * coordinator never reads or writes main.c statics directly.
 *
 * Header intentionally lives outside RoomRom/src/atlas/ and is not
 * exported through render_abi.h — it is "internal RoomRom plumbing"
 * scoped to the gameplay harness.
 */

#ifndef ROOMROM_MAIN_STATE_H
#define ROOMROM_MAIN_STATE_H

/* Scene id constants — must match the scene_t enum in main.c. */
#define ROOMROM_MAIN_SCENE_OW    0u
#define ROOMROM_MAIN_SCENE_UW    1u
#define ROOMROM_MAIN_SCENE_CAVE  2u

/* Link face id constants — must match link_face_t in roomrom_sprites.h. */
#define ROOMROM_MAIN_LINK_FACE_DOWN  0u
#define ROOMROM_MAIN_LINK_FACE_UP    1u
#define ROOMROM_MAIN_LINK_FACE_LEFT  2u
#define ROOMROM_MAIN_LINK_FACE_RIGHT 3u

/* Outcome of a warp decision made by the coordinator. PREPARE writes
 * this struct once; LOAD reads it once and never mutates a field. */
typedef struct {
    unsigned char dest_scene;        /* SCENE_OW / SCENE_UW / SCENE_CAVE */
    unsigned char dest_level;        /* UW level 1..9; ignored for OW */
    unsigned char dest_quest;        /* UW quest 1..2; ignored for OW */
    unsigned char dest_room_id;
    short         dest_link_x;
    short         dest_link_y;
    unsigned char dest_link_face;    /* link_face_t value */
    unsigned char dest_redux_flag;   /* 0 = original, 1 = redux */
} rr_warp_outcome_t;

/* Apply a warp outcome atomically. Single-call boundary: state surface
 * is internally consistent before return. Order is documented inline
 * in main.c. */
void roomrom_main_apply_warp_outcome(const rr_warp_outcome_t *out);

/* T-132: OW -> dungeon level entry. Runs NES mode $10 (stairs), loads the
 * level (mode 2), unfurls the curtain (mode 3) and walks Link in (mode 4);
 * applies `out` itself when the stairs finish. */
void roomrom_main_begin_level_entry(const rr_warp_outcome_t *out);

/* Read the current redux flag for the active scene. The coordinator
 * reads this in PREPARE so it can populate dest_redux_flag without
 * depending on private main.c statics. */
unsigned char roomrom_main_current_redux_flag(void);

/* Read the current scene id (SCENE_OW / SCENE_UW / SCENE_CAVE). The
 * coordinator's check_warp gate uses this to short-circuit warp
 * detection when not in OW. */
unsigned char roomrom_main_current_scene(void);
/* Plane cell showing NES name-table cell (col, nt_row) in the current room
 * (scroll-aware; the Genesis frame starts at NES line 8). */
void roomrom_main_nt_cell_to_plane(unsigned char col, unsigned char nt_row,
                                   unsigned short *pc, unsigned short *pr);

/* Read the current OW room id. Coordinator passes this to the OW
 * metadata accessor for selector resolution. */
unsigned char roomrom_main_current_room_id(void);

/* Read Link's current position. Coordinator computes the warp tile
 * (col, row) from these. */
short roomrom_main_current_link_x(void);
short roomrom_main_current_link_y(void);

/* Story scripts that move Link use this typed-state/mirror boundary. */
void roomrom_main_set_link_story_pose(unsigned char x, unsigned char y,
                                     unsigned char face);

/* T-056: Link's typed state from the NES cells after NES object code
 * moved/turned him (UpdateDock). */
void roomrom_main_link_sync_from_nes(void);
/* T-056: Link_EndMoveAndAnimate called from an object update (UpdateDock):
 * ladder setup + CheckWarps (mode 5), AnimateLinkBase, Link redrawn. */
void roomrom_main_link_end_move_from_object(void);
/* T-056: start the OW scroll toward ObjDir now (UpdateDock's
 * GoToNextModeFromPlay, after its draw). */
void roomrom_main_ow_scroll_from_object(void);

/* Read Link's grid offset (NES ObjGridOffset). Coordinator gates rule 2. */
signed char roomrom_main_current_link_grid_offset(void);

/* Read Link's face. Coordinator latches this into the save state. */
unsigned char roomrom_main_current_link_face(void);

/* Read NES UndergroundExitType analogue. Used by the warp coordinator's
 * rule-1 precondition to block warp re-trigger immediately after a
 * scene transition. Phase C (2026-05-24): writer landed.
 *
 * NES values (Z_05.asm + Z_07.asm + Z_01.asm):
 *   0 = OW free (warp checks fire)
 *   1 = cave / cellar exit pending (block re-trigger on entrance tile)
 *   2 = dungeon level exit pending (same blocking semantics; NES uses
 *       this distinct value for EndGameMode12 path)
 *
 * Cleared on first OW grid-aligned step per Z_07.asm:3200 by
 * roomrom_world_transition_tick. */
unsigned char roomrom_main_underground_exit_type(void);
void          roomrom_main_set_underground_exit_type(unsigned char uet);

/* Read/write the master quest selector. NES Z1 analogue: QuestNumbers
 * ($62D) for the live quest plus SaveFileAQuestNumber{0,1,2}
 * ($651B-$651D) per save slot. Genesis mirror is a single byte:
 * 1 = Q1 (default), 2 = Q2. Coordinator stamps save_state.dest_quest
 * + outcome.dest_quest from the getter; the setter is invoked by the
 * save-slot loader (Phase 9) and a future quest-selector menu. */
unsigned char roomrom_main_current_quest(void);
void          roomrom_main_set_quest(unsigned char quest);

/* Read this-frame input dpad mask in NES `ObjInputDir` format:
 *   bit 0 = RIGHT (E)
 *   bit 1 = LEFT  (W)
 *   bit 2 = DOWN  (S)
 *   bit 3 = UP    (N)
 * Used by Task 5.7 push-block state machine. */
unsigned char roomrom_main_current_input_dir(void);

/* Read this-frame mode (MODE_WALK / MODE_TELEPORT). Used by Task 5.7
 * push-block state machine to bypass during TELEPORT. */
unsigned char roomrom_main_current_mode(void);

#define ROOMROM_MAIN_MODE_WALK     0u
#define ROOMROM_MAIN_MODE_TELEPORT 1u

#endif /* ROOMROM_MAIN_STATE_H */
