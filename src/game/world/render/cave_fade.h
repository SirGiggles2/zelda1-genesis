/* cave_fade.h — NES cave entry/exit transition sequencer.
 *
 * Z_05.asm:InitMode10 and UpdateMode10Stairs_Full: entrance tile $24
 * requests the stairs sound and walks Link down 16 pixels at one pixel
 * per four frames. Other entrance tiles enter the target mode on the
 * next update without that descent. Both paths then load the cave and
 * walk Link into it. Exit ascends before restoring the overworld.
 */

#ifndef CAVE_FADE_H
#define CAVE_FADE_H

#include "../../cave/cave_dispatch.h"  /* cave_id_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CAVE_FADE_IDLE         = 0,
    CAVE_FADE_LINK_DESCEND = 1, /* $24 descends; other tiles skip to load */
    CAVE_FADE_SWAP_ENTRY   = 2,
    CAVE_FADE_LINK_ASCEND  = 3,  /* cave exit: Link walks UP, Y-=1 per 4 frames */
    CAVE_FADE_SWAP_EXIT    = 4,
    CAVE_FADE_LINK_EMERGE  = 5,  /* cave ENTRY emerge: Link walks UP from $DD to
                                  * the cave floor ($D5) via NES MoveObject
                                  * (InitMode_WalkCave, Z_05.asm:6643). */
    CAVE_FADE_LOAD_HOLD    = 6,  /* between descent-end and emerge: NES holds
                                  * Link at the descent-end Y while submodes 1-7
                                  * load the cave (~29 frames) before
                                  * InitModeB_EnterCave repositions to $DD. */
    CAVE_FADE_EMERGE_SETTLE = 7  /* T-012: InitMode_WalkCave's last frame
                                  * (ObjGridOffset 0): no move, no animation,
                                  * RunCrossRoomTasksAndBeginUpdateMode. */
} cave_fade_phase_t;

typedef struct {
    /* Called once per LINK_DESCEND tick when Link should move down 1 px
     * (every 4th frame). step_idx counts 0..15 (16 steps total). Owner
     * adjusts players[0].y += 1 and sets sprite priority. */
    void (*on_descend_step)(unsigned char step_idx);
    /* Called at SWAP_ENTRY. Owner sets scene = SCENE_CAVE +
     * Link reposition (NES $70,$DD = 112,221 face up; Z_01.asm:2965) +
     * any other RoomRom-local state bookkeeping. */
    void (*on_swap_entry)(cave_id_t cid);
    /* Called once per LINK_ASCEND tick (cave exit). Owner adjusts
     * players[0].y -= 1 + ticks walk-anim. 16 steps total. */
    void (*on_ascend_step)(unsigned char step_idx);
    /* Called at SWAP_EXIT. Owner sets scene = SCENE_OW + Link
     * reposition (16 px south of entrance facing down) + HUD reset. */
    void (*on_swap_exit)(void);
    /* Called once per LINK_EMERGE tick (cave ENTRY emerge). Owner writes
     * players[0].y = obj_y and mirrors nes_ram ObjY[0]=$84=obj_y,
     * ObjGridOffset[0]=$394=grid, ObjPosFrac[0]=$3A8=posfrac so the byte-diff
     * tracks the NES emerge (InitMode_WalkCave). obj_y walks $DD -> $D5
     * (cave floor). Appended last to keep the positional initializer order
     * of the existing four callbacks unchanged. */
    void (*on_emerge_step)(unsigned char obj_y, unsigned char grid,
                           unsigned char posfrac);
    /* Called EVERY frame during DESCEND/EMERGE/ASCEND (independent of the
     * position step) with the NES walk-anim state: counter = ObjAnimCounter
     * ($3D0, down-counts 6..1 rolling to 6), frame = ObjAnimFrame ($3E4,
     * toggles 0/1 at each roll — the 6-frame walk-pose cadence,
     * Z_07.asm:5045 AnimateObjectWalking). Owner sets s_link_frame=frame and
     * mirrors nes_ram $3D0=counter / $3E4=frame. Appended last to preserve the
     * existing positional initializer order. */
    void (*on_anim_tick)(unsigned char counter, unsigned char frame);
    /* T-134: NES mode $0B submode 2 blanks the playfield (the HUD stays)
     * until the cave appears. stage 0 at the descent end or immediate
     * stairs-mode handoff (owner hides
     * Link: sprite writes land a frame after plane writes), stage 1 on the
     * next tick (owner blanks the play area). Appended last. */
    void (*on_load_blank)(unsigned char stage);
    /* T-012: the walk-in's settle frame (submode 8 -> 0, mode $0B update
     * starts next frame). Appended last. */
    void (*on_walk_done)(void);
} cave_fade_callbacks_t;

void              cave_fade_set_callbacks(const cave_fade_callbacks_t *cb);

void              cave_fade_begin_enter(cave_id_t cid,
                                        unsigned char entrance_tile);
void              cave_fade_begin_exit(unsigned char return_room_id);

unsigned char     cave_fade_is_active(void);
cave_fade_phase_t cave_fade_phase_current(void);
void              cave_fade_tick(void);

/* Current descend step index (0..15). Returns 0 if not in
 * LINK_DESCEND phase. */
unsigned char     cave_fade_descend_step_idx(void);

/* Stamp the 2x2 BG cells above Link's standing tile with high priority
 * so the low-priority Link sprite renders BEHIND the cave entrance
 * arch — matches NES OAM sprite-priority $20 effect on the Link
 * upper-half sprite slots ($12, $13) during UpdateMode10Stairs.
 *
 * Genesis layering: low-prio sprite < high-prio BG; low-prio sprite >
 * low-prio BG. So this gives Link "covered by arch lip, visible inside
 * black interior". Call from owner at cave-entry trigger (BEFORE the
 * fade starts) so the prio bit is live across descend frames.
 *
 * The cave-plane fill at SWAP_ENTRY overwrites all cells with prio=0
 * cave tiles; on SWAP_EXIT, ow_render fill_plane_a repaints OW with
 * prio=0 defaults. So the prio bit is transient — only set during
 * the descend window. */
/* T-011: lower the cells raised by cave_fade_mark_arch_hi_prio (end of
 * a stairs walk); forget them when the plane is redrawn. */
void              cave_fade_restore_arch(void);
void              cave_fade_forget_arch(void);
void              cave_fade_mark_arch_hi_prio(unsigned char link_tile_col,
                                              unsigned char link_tile_row);

#ifdef __cplusplus
}
#endif

#endif /* CAVE_FADE_H */
