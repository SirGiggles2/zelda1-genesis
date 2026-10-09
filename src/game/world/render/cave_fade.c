/* cave_fade.c — NES Link-descend cave-entry animation.
 *
 * NES Z_05.asm:1400 InitMode10 + Z_05.asm:2308 UpdateMode10Stairs_Full.
 * 64-frame descend (16 px × 4 frames/px). After descend, instant scene
 * swap to cave. Exit is instant (snap) — user-trigger via C+START chord.
 */

#include "cave_fade.h"
#include "cave_palette.h"
#include "ow_render.h"
#include "render_abi.h"  /* render_vram_read_word, render_set_plane_a_word */
#include "platform_abi.h"  /* RAM(): FrameCounter $15 */
#include "../../enemies/enemy_loop.h"  /* enemy_loop_enter_cave_objects (T-171) */
#include "../../combat/collision_dispatch.h"  /* InitMode10 GetCollidableTileStill (T-171) */

/* Genesis Plane A VRAM base + cell stride. Per src/sgdk_adapter
 * config (engine PR-2 H64xV32 layout): plane A at $C000, 64 cells
 * wide, 2 bytes per cell. Plane is 32 rows tall (PR-2 trimmed half
 * of V64 to free CHR space). */
#define CAVE_FADE_PLANE_A_BASE   0xC000u
#define CAVE_FADE_PLANE_COLS     64u
#define CAVE_FADE_PLANE_ROWS     64u   /* 64x64 plane (PR-2c) */
#define CAVE_FADE_HUD_ROW_OFFSET 7u  /* matches ROOMROM_ROOM_FIRST_ROW */

/* T-011: cells this module raised to high priority, so the NES
 * behind-background effect ends with the stairs animation: NES sets the
 * sprite priority bit on Link only while he walks in/out
 * (PutLinkBehindBackground / AnimateAndDrawLinkBehindBackground). Left
 * raised, a Link standing still at a cave mouth after StepOutside was
 * hidden behind the ground (user report, t011_exit_idle tick 1100). */
#define CAVE_FADE_ARCH_MAX 21u   /* 3 cols x 7 rows */
static unsigned char s_arch_col[CAVE_FADE_ARCH_MAX];
static unsigned char s_arch_row[CAVE_FADE_ARCH_MAX];
static unsigned char s_arch_n = 0u;

static void mark_cell_hi_prio_xy(unsigned char col, unsigned char row)
{
    if (col >= CAVE_FADE_PLANE_COLS || row >= CAVE_FADE_PLANE_ROWS) {
        return;
    }
    unsigned short vram_addr = (unsigned short)(
        CAVE_FADE_PLANE_A_BASE +
        ((unsigned short)row * CAVE_FADE_PLANE_COLS + (unsigned short)col) * 2u);
    unsigned short cur = render_vram_read_word(vram_addr);
    if ((cur & 0x8000u) != 0u) {
        return;  /* already high prio */
    }
    render_set_plane_a_word(col, row, (unsigned short)(cur | 0x8000u));
    if (s_arch_n < CAVE_FADE_ARCH_MAX) {
        s_arch_col[s_arch_n] = col;
        s_arch_row[s_arch_n] = row;
        ++s_arch_n;
    }
}

void cave_fade_restore_arch(void)
{
    unsigned char i;
    for (i = 0u; i < s_arch_n; ++i) {
        const unsigned short vram_addr = (unsigned short)(
            CAVE_FADE_PLANE_A_BASE +
            ((unsigned short)s_arch_row[i] * CAVE_FADE_PLANE_COLS +
             (unsigned short)s_arch_col[i]) * 2u);
        const unsigned short cur = render_vram_read_word(vram_addr);
        render_set_plane_a_word(s_arch_col[i], s_arch_row[i],
                                (unsigned short)(cur & 0x7FFFu));
    }
    s_arch_n = 0u;
}

void cave_fade_forget_arch(void)
{
    s_arch_n = 0u;   /* plane redrawn: the raised cells are gone */
}

/* NES InitMode10 + UpdateMode10Stairs: a $24 pad descends 16 pixels,
 * 1 px every 4 frames. Stairs and $88 skip that descent. */
#define CAVE_DESCEND_PIXELS   16u
#define CAVE_DESCEND_FRAMES_PER_PX 4u
#define CAVE_DESCEND_TOTAL_FRAMES \
    (CAVE_DESCEND_PIXELS * CAVE_DESCEND_FRAMES_PER_PX)

/* NES cave ENTRY emerge (InitMode_WalkCave, Z_05.asm:6643 + MoveObject
 * Z_07.asm:2719). After the cave loads, Link spawns at the cave bottom and
 * walks UP to the cave floor. Movement is NES MoveObject's quarter-speed
 * fraction applied 4x/frame: each application does ObjPosFrac -= ObjQSpeedFrac
 * and, on borrow, ObjY -= 1. Values byte-captured live from Z1 cave $6A
 * (probe dbg_frac): spawn ObjY=$DD, ObjPosFrac=$00, ObjGridOffset=$30,
 * ObjQSpeedFrac=$60 (const); Link halts at the floor ObjY=$D5 (collision
 * clears ObjGridOffset). The captured ObjY walk is $DD,$DB,$DA,$D8,$D7,$D5. */
#define CAVE_EMERGE_SPAWN_Y   0xDDu
#define CAVE_EMERGE_FLOOR_Y   0xD5u
#define CAVE_EMERGE_GRID0     0x30u
#define CAVE_EMERGE_QSPEED    0x60u

static cave_fade_phase_t     s_phase          = CAVE_FADE_IDLE;
static unsigned char         s_frame_counter  = 0u;  /* 0..63 for descend */
static unsigned char         s_step_idx       = 0u;  /* 0..15 px steps emitted */
static cave_id_t             s_pending_cid    = 0u;
static unsigned char         s_entrance_tile  = 0u;
static unsigned char         s_return_room_id = 0u;
static cave_fade_callbacks_t s_cb             = { 0, 0, 0, 0, 0, 0, 0, 0 };
/* LINK_EMERGE running state (NES MoveObject accumulator). */
static unsigned char         s_emerge_y       = 0u;
static unsigned char         s_emerge_posfrac = 0u;
static unsigned char         s_emerge_grid    = 0u;
/* NES walk-anim cadence (ObjAnimCounter $3D0 / ObjAnimFrame $3E4). Counter
 * down-counts; on roll past 1 it resets to 6 and toggles the frame — a
 * 6-frame walk-pose period (Z_07.asm:5045 AnimateObjectWalking). */
#define CAVE_ANIM_PERIOD  6u
static unsigned char         s_anim_counter   = CAVE_ANIM_PERIOD;
static unsigned char         s_anim_frame     = 0u;
/* Cave-load hold: NES holds Link at the descent-end Y while GameMode $0B
 * submodes 1-7 (LayoutCave, row transfers, InitCave) run before submode 8
 * (InitMode_WalkCave) repositions to $DD and emerges. The swap runs
 * HOLD + 2 frames after mode $0B submode 0; NES submode 8 is frame 31
 * (T-171 frame trace, OW $77 cave; the old 18 put the cave 11 frames
 * early, its FrameCounter caught up by nes_frames_catch_up). */
#define CAVE_LOAD_HOLD_FRAMES 29u
/* T-134: hold counter value at which the playfield goes black: NES
 * submode 2 is visible 2 frames after mode $0B starts (t120 f124 -> f126). */
#define CAVE_LOAD_BLANK_AT ((unsigned char)(CAVE_LOAD_HOLD_FRAMES - 1u))
static unsigned char         s_load_counter   = 0u;

void cave_fade_set_callbacks(const cave_fade_callbacks_t *cb)
{
    if (cb != 0) {
        s_cb = *cb;
    }
}

void cave_fade_begin_enter(cave_id_t cid, unsigned char entrance_tile)
{
    if (s_phase != CAVE_FADE_IDLE) {
        return;
    }
    /* Z_05.asm:SetTargetMode: non-cellar warps silence current song before
     * Mode $10 stairs. $50 selects Mode $0C shortcut; all other cave
     * selectors use Mode $0B. Let audio_requests consume the NES request
     * on the next tick, including when OW music belongs to XGM. */
    RAM(0x005Bu) = (cid == 0x6Eu) ? 0x0Cu : 0x0Bu;
    RAM(0x0604u) = 0x80u;
    s_pending_cid   = cid;
    s_entrance_tile = entrance_tile;
    s_frame_counter = 0u;
    s_step_idx      = 0u;
    s_phase         = CAVE_FADE_LINK_DESCEND;
}

void cave_fade_begin_exit(unsigned char return_room_id)
{
    if (s_phase != CAVE_FADE_IDLE) {
        return;
    }
    s_return_room_id = return_room_id;
    s_frame_counter  = 0u;
    s_step_idx       = 0u;
    s_anim_counter   = CAVE_ANIM_PERIOD;
    s_anim_frame     = 0u;
    s_phase          = CAVE_FADE_LINK_ASCEND;  /* descend-mirror exit anim */
}

unsigned char cave_fade_is_active(void)
{
    return (unsigned char)(s_phase != CAVE_FADE_IDLE);
}

cave_fade_phase_t cave_fade_phase_current(void)
{
    return s_phase;
}

unsigned char cave_fade_descend_step_idx(void)
{
    return (s_phase == CAVE_FADE_LINK_DESCEND) ? s_step_idx : 0u;
}

void cave_fade_mark_arch_hi_prio(unsigned char link_tile_col,
                                 unsigned char link_tile_row)
{
    /* Per APPENDIX plan revision (2026-05-22 systematic-debug rounds
     * 1-4 review): NES PutLinkBehindBackground (Z_05.asm:2326-2331)
     * sets behind-BG bit on BOTH Sprites+74 + Sprites+78 = ENTIRE
     * Link sprite (slots $12/$13 in 8x16 mode = 2 OAM entries = 16x16
     * Link). My previous "upper-only" assumption was wrong.
     *
     * Genesis architecture: single low-prio Link sprite + wide BG-prio
     * stamp. Tile content (CHR color-0 pixels) determines what shows
     * through: cells with non-color-0 pixels cover Link; cells with
     * color-0 (transparent) pixels let Link show through.
     *
     * Stamp range rows -2..+4: covers Link's full descend Y range
     * [$4D, $5D] (plane rows 16-20) + 2 buffer rows above for the
     * arch/ground that should cover Link's head. Math (Y >> 3) + 7
     * HUD offset assumes Plane A vscroll = 0 (confirmed via probe).
     * Cols -1..+1 covers Link's 16-px-wide sprite + 1 col buffer. */
    for (signed char dr = -2; dr <= 4; ++dr) {
        for (signed char dc = -1; dc <= 1; ++dc) {
            /* The plane wraps (T-132). */
            signed int row = ((signed int)link_tile_row + (signed int)dr) &
                             (signed int)(CAVE_FADE_PLANE_ROWS - 1u);
            signed int col = ((signed int)link_tile_col + (signed int)dc) &
                             (signed int)(CAVE_FADE_PLANE_COLS - 1u);
            mark_cell_hi_prio_xy((unsigned char)col, (unsigned char)row);
        }
    }
}

extern void roomrom_combat_animate_link_base(void);   /* combat_runtime.c */

/* T-171: Link's walk animation on the NES cells. UpdateMode10Stairs and
 * InitMode_WalkCave both end in Link_EndMoveAndAnimate -> AnimateLinkBase
 * on ObjAnimCounter/ObjAnimFrame ($3D0/$3E4), carried over from play (the
 * old fixed seeds matched only when Link's counter happened to be 4:
 * t012_route t140). Hand the result to the owner for the pose. */
static void animate_link_nes(void)
{
    roomrom_combat_animate_link_base();
    if (s_cb.on_anim_tick != 0)
        s_cb.on_anim_tick(RAM(0x03D0u), RAM(0x03E4u));
}

void cave_fade_tick(void)
{
    /* NES walk-anim runs EVERY frame Link is animating (descend/emerge/ascend),
     * independent of the position step. Emit the current (counter, frame) then
     * advance: counter down-counts, rolling 1->6 and toggling the frame — the
     * 6-frame walk-pose cadence (Z_07.asm:5045). */
    if (s_phase == CAVE_FADE_LINK_ASCEND) {
        if (s_cb.on_anim_tick != 0) {
            s_cb.on_anim_tick(s_anim_counter, s_anim_frame);
        }
        if (s_anim_counter <= 1u) {
            s_anim_counter = CAVE_ANIM_PERIOD;
            s_anim_frame   = (unsigned char)(s_anim_frame ^ 1u);
        } else {
            s_anim_counter = (unsigned char)(s_anim_counter - 1u);
        }
    }

    switch (s_phase) {
    case CAVE_FADE_LINK_DESCEND: {
        /* Z_05.asm:UpdateMode10Stairs_Full skips the 16-pixel descent
         * for stairs $70-$73 and rock-pile $88. Only the $24 pad walks
         * Link behind the background. The first mode-10 update still
         * enters the target cave mode before its load submodes. */
        if (s_entrance_tile != 0x24u) {
            /* T-171: the first call is InitMode10's own frame; the mode
             * switch is UpdateMode10's, the next frame (NES OW $78
             * stairs: mode $10 updating on frame 1, mode $0B on 2). */
            if (s_frame_counter == 0u) {
                s_frame_counter = 1u;
                (void)collision_get_collidable_tile_still(0u);   /* InitMode10 */
                break;
            }
            s_load_counter = CAVE_LOAD_HOLD_FRAMES;
            s_phase = CAVE_FADE_LOAD_HOLD;
            if (s_cb.on_load_blank != 0) s_cb.on_load_blank(0u);
            animate_link_nes();          /* after the mode switch, as NES */
            break;
        }
        /* NES UpdateMode10Stairs_Full (Z_05.asm:2314): INC ObjY when
         * FrameCounter & 3 == 0, from the frame after InitMode10's own
         * (T-012: a private 4-frame count was a frame late at fc $6C;
         * the init frame at fc $B8 must not move). After 16 steps,
         * mode $0B. */
        if (s_frame_counter < 2u) s_frame_counter++;
        /* InitMode10's own frame (first call) neither moves nor animates;
         * it sets StairsTargetY ($412) $10 below Link (Z_05.asm:1400). */
        if (s_frame_counter == 1u) {
            (void)collision_get_collidable_tile_still(0u);       /* InitMode10 */
            RAM(0x0412u) = (unsigned char)(RAM(0x0084u) + 0x10u);
        }
        if (s_frame_counter < 2u) break;
        if ((RAM(0x0015u) & 0x03u) == 0u) {
            if (s_cb.on_descend_step != 0) {
                s_cb.on_descend_step(s_step_idx);
            }
            s_step_idx = (unsigned char)(s_step_idx + 1u);
            if (s_step_idx >= CAVE_DESCEND_PIXELS) {
                /* NES holds Link at the descent-end Y for the cave-load
                 * submodes BEFORE the emerge — replicate the duration. */
                s_load_counter = CAVE_LOAD_HOLD_FRAMES;
                s_phase = CAVE_FADE_LOAD_HOLD;
                if (s_cb.on_load_blank != 0) s_cb.on_load_blank(0u);
            }
        }
        animate_link_nes();              /* AnimateAndDrawLinkBehindBackground */
        break;
    }

    case CAVE_FADE_LOAD_HOLD:
        /* Hold Link at the descent-end position (the descend handler left
         * nes_ram ObjY there; cave-play is gated off while cave_fade is active)
         * for the NES cave-load duration, then do the swap + emerge. */
        if (s_load_counter > 0u) {
            s_load_counter = (unsigned char)(s_load_counter - 1u);
            /* T-134: NES enters mode $0B at the descent end and its
             * submode 2 (t120 f124 -> f126) shows a black
             * playfield until the cave appears at once (submode 8). */
            if (s_load_counter == CAVE_LOAD_BLANK_AT &&
                s_cb.on_load_blank != 0) {
                s_cb.on_load_blank(1u);
            }
            /* Lay the cave out behind the black playfield (RAM only). */
            if (s_load_counter <= CAVE_LOAD_BLANK_AT)
                roomrom_cave_room_render_prepare((unsigned char)s_pending_cid, 2u);
        } else {
            s_phase = CAVE_FADE_SWAP_ENTRY;
        }
        break;

    case CAVE_FADE_SWAP_ENTRY:
        /* Instant: cave state init + cave plane fill + cave palette
         * stamp. Mirrors the three calls that pre-anim lived inline at
         * engine/src/main.c. Owner callback then handles Link
         * reposition (cave-bottom) + scene flip. */
        (void)cave_init(s_pending_cid);
        roomrom_cave_room_render_fill_plane_a((unsigned char)s_pending_cid);
        roomrom_ow_room_render_publish_play_area_tiles();
        /* InitMode_EnterRoom's object setup sees the cave layout in
         * PlayAreaTiles and Link still at the descent end; on_swap_entry
         * then puts Link at ($70, $DD) (InitModeB_EnterCave_Bank5 order). */
        enemy_loop_enter_cave_objects((unsigned char)RAM(0x00EBu));  /* RoomId: the OW room */
        cave_palette_apply();
        if (s_cb.on_swap_entry != 0) {
            s_cb.on_swap_entry(s_pending_cid);
        }
        /* NES: cave-load is followed by InitMode_WalkCave (the emerge), not an
         * idle. Spawn Link at the cave bottom ($DD) and hand off to the emerge
         * walk-up. on_swap_entry already placed Link at the spawn; seed the
         * MoveObject accumulator. */
        s_emerge_y       = CAVE_EMERGE_SPAWN_Y;
        s_emerge_posfrac = 0u;
        s_emerge_grid    = CAVE_EMERGE_GRID0;
        /* Re-seed the walk anim for the emerge (NES InitMode_WalkCave restarts
         * it): hold pose 0 through the walk-up, flip to pose 1 at the settle. */
        /* InitModeB_EnterCave: InitMode_EnterRoom leaves Link's
         * ObjAnimCounter at 4 and ObjInputDir 0, so its
         * Link_EndMoveAndAnimate does not step (NES t011 f155: $3D0 4). */
        RAM(0x03D0u) = 4u;
        /* T-202 NES source: Z_05.asm:InitMode_EnterRoom clears $0300..$051F.
         * Drained C: core_runtime.c:corert_clear_ram0300_up_to;
         * cave adapter: enemy_loop.c:enemy_loop_enter_cave_slots.
         * Coverage: PARTIAL (cave adapter resets enemy slots, not Link's frame).
         * Stance: EXTEND the native entry reset to include ObjAnimFrame+0.
         * Walking starts from the cleared frame; carrying the previous
         * OW pose inverts the settled cave pose when that pose was 1. */
        RAM(0x03E4u) = 0u;
        if (s_cb.on_anim_tick != 0)
            s_cb.on_anim_tick(RAM(0x03D0u), RAM(0x03E4u));
        s_phase          = CAVE_FADE_LINK_EMERGE;
        break;

    case CAVE_FADE_LINK_EMERGE: {
        /* NES MoveObject UP, quarter-speed applied 4x/frame
         * (Z_07.asm:2719-2749). Each application: ObjPosFrac -= ObjQSpeedFrac;
         * on borrow (underflow), ObjY -= 1. */
        unsigned char start_y = s_emerge_y;
        unsigned char i;
        for (i = 0u; i < 4u; ++i) {
            unsigned char nf = (unsigned char)(s_emerge_posfrac - CAVE_EMERGE_QSPEED);
            if (nf > s_emerge_posfrac) {           /* borrow (underflow) */
                s_emerge_y = (unsigned char)(s_emerge_y - 1u);
            }
            s_emerge_posfrac = nf;
        }
        /* ObjGridOffset -= pixels moved this frame. */
        {
            unsigned char moved = (unsigned char)(start_y - s_emerge_y);
            s_emerge_grid = (s_emerge_grid > moved)
                ? (unsigned char)(s_emerge_grid - moved) : 0u;
        }
        /* Cave-floor collision: NES halts Link at the floor and clears
         * ObjGridOffset there (it does not exhaust the full $30 budget). */
        if (s_emerge_y <= CAVE_EMERGE_FLOOR_Y) {
            s_emerge_y    = CAVE_EMERGE_FLOOR_Y;
            s_emerge_grid = 0u;
        }
        if (s_cb.on_emerge_step != 0) {
            s_cb.on_emerge_step(s_emerge_y, s_emerge_grid, s_emerge_posfrac);
        }
        animate_link_nes();              /* Link_EndMoveAndAnimateInRoom */
        /* T-012: NES shows the floor frame in submode 8, then one more
         * frame (ObjGridOffset 0) before the cave updates. */
        if (s_emerge_y <= CAVE_EMERGE_FLOOR_Y) {
            s_phase = CAVE_FADE_EMERGE_SETTLE;
            s_step_idx = 0u;
        }
        break;
    }

    case CAVE_FADE_EMERGE_SETTLE:
        /* Stays active through the settle tick (the owner runs no
         * objects while active); play resumes on the next one. */
        if (s_step_idx == 0u) {
            if (s_cb.on_walk_done != 0) s_cb.on_walk_done();
            s_step_idx = 1u;
        } else {
            s_phase = CAVE_FADE_IDLE;
        }
        break;

    case CAVE_FADE_LINK_ASCEND: {
        /* Mirror of LINK_DESCEND: Y -= 1 every 4 frames for 16 steps.
         * NES Z_05.asm:1603+ uses same Mode 10 path for cave-exit,
         * just with StairsTargetY = ObjY - $10 (= UP 16 px). After
         * 16 steps, SWAP_EXIT swaps plane back to OW. */
        s_frame_counter = (unsigned char)(s_frame_counter + 1u);
        if ((s_frame_counter & 0x03u) == 0u) {
            if (s_cb.on_ascend_step != 0) {
                s_cb.on_ascend_step(s_step_idx);
            }
            s_step_idx = (unsigned char)(s_step_idx + 1u);
            if (s_step_idx >= CAVE_DESCEND_PIXELS) {
                s_phase = CAVE_FADE_SWAP_EXIT;
            }
        }
        break;
    }

    case CAVE_FADE_SWAP_EXIT:
        /* Instant: cave teardown + OW plane refill + OW palette
         * restore. Owner callback handles Link reposition + HUD reset
         * + scene flip. */
        cave_exit();
        roomrom_ow_room_render_fill_plane_a(s_return_room_id);
        roomrom_ow_room_render_publish_play_area_tiles();
        roomrom_ow_room_render_load_palette(s_return_room_id);
        if (s_cb.on_swap_exit != 0) {
            s_cb.on_swap_exit();
        }
        s_phase = CAVE_FADE_IDLE;
        break;

    case CAVE_FADE_IDLE:
    default:
        break;
    }
}
