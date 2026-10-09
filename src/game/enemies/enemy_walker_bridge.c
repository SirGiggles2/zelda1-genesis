/* enemy_walker_bridge.c -- Phase 7 Task 7.2 step 4/5 walker UPDATE
 * primitives bridge.
 *
 * Per debate 2026-05-09 verdict (Option C, Sonnet evidence): 6 of 7
 * walker UPDATE primitives already exist as drained native functions
 * linked into Debug.md. Step-5 closes the last gap with a native
 * Walker_Move drain composed from drained primitives.
 *
 * Drain Rule D1 stance: EXTEND. All callees here are drained C
 * (PRIMARY evidence). No NES asm linkage. No vasm/gas dialect bridge.
 *
 * Hard rule WT-5: lives at src/game/enemies/, not RoomRom/.
 *
 * Symbol map:
 *   c_walker_move                         -> NATIVE (this file, step 5)
 *                                            composes object_bound_by_room +
 *                                            object_move_object from
 *                                            world/object_dispatch.c
 *                                            (NES Z_07.asm:2555 Walker_Move)
 *   c_check_monster_collisions            -> link_collision_check_monster_collisions
 *                                            (link_collision_dispatch.c:263)
 *   c_check_link_collision                -> link_collision_check_link_collision
 *                                            (link_collision_dispatch.c)
 *   c_draw_object_not_mirrored_with_frame -> draw_object_not_mirrored_with_frame
 *                                            (draw_dispatch.c:426)
 *   c_wanderer_target_player              -> enrt_wanderer_target_player
 *                                            (enemy_wanderer_runtime.c:63)
 *   z07_anim_advance_and_fetch            -> sprite_anim_advance_and_fetch
 *                                            (sprite_dispatch.c:105)
 *   z01_anim_set_sprite_desc_attrs        -> core_anim_set_sprite_desc_attrs
 *                                            (core_dispatch.c:155)
 *   z01_abs                               -> trivial native one-liner
 */

#include "combat/link_collision_dispatch.h"
#include "combat/collision_dispatch.h"   /* collision_get_colliding_tile_moving */
#include "world/draw_dispatch.h"
#include "world/sprite_dispatch.h"
#include "world/object_dispatch.h"   /* object_bound_by_room, object_move_object */
#include "world/world_dispatch.h"    /* world_fill_tile_map */
#include "world/dyn_tile_dispatch.h" /* dyn_tile_change_tile_obj_tiles (step 6c) */
#include "core/core_dispatch.h"      /* core_get_opposite_dir, core_reset_moving_dir,
                                        core_reset_obj_metastate_and_timer */
#include "enemies/enemy_dispatch.h"  /* enemy_play_secret_found_tune (step 6c) */
#include "world/progress_dispatch.h" /* progress_get_room_flag_uw_item_state (step 6c) */
#include "combat_state.h"            /* ROOM_KILL_COUNT (step 20 drop conv) */
#include "room_state.h"              /* ROOM_OW_CUR_KILL_TOTAL ($034F NES RoomKillCount) */
#include "platform_abi.h"            /* RAM, OBJ, NES_OBJ_DIR, NES_SHOT_COLLISION_FLAG */
#include "roomrom_enemy_state.h"     /* ENEMY_* macros (re-export of state/enemy_state.h) */
#include "enemy_render.h"            /* Phase D: enemy_render_publish_meta */
#include "enemy_loop.h"              /* enemy_init_fn, ENEMY_LOOP_TYPE_MAX */
#include "../../../RoomRom/src/roomrom_main_state.h" /* Link story-pose owner */

/* NES non-Link offsets (cell-level; OBJ macro adds slot index).
 * ObjStunTimer  = $003D  (per-slot)
 * InvClock      = $066C  (global; pause/clock flag)
 * ObjInputDir   = $03F8  (per-slot; aliases ENEMY_PUSH_DIR_SCRATCH)
 * ObjShoveDir   = $00C0  (per-slot)
 * ObjGridOffset = $0394  (per-slot; per platform_abi NES_OBJ_GRID_OFFSET) */
#define NES_OBJ_STUN_TIMER   0x003D
#define NES_INV_CLOCK        0x066C
#define NES_OBJ_INPUT_DIR    0x03F8
#define NES_OBJ_SHOVE_DIR    0x00C0

extern void enrt_wanderer_target_player(unsigned int slot);
extern void enrt_update_goriya(unsigned int slot);     /* 7.4 step 6b ($1E armos) */
extern void enrt_draw_ghini_and_check_collisions(unsigned int slot); /* 7.4 step 6c ($22 ghini fade) */
extern void enrt_end_init_flyer(unsigned int slot);    /* 7.4 step 6c (ghini terminal init) */
extern void c_obj_shove(unsigned int slot);            /* knockback applier */
extern void enrt_try_shooting(unsigned char qspeed, unsigned char shot_type,
                              unsigned int slot);   /* enemy_walker_runtime.c */

/* Forward decl — defined after c_obj_shove block in this file (step 18). */
void c_walker_check_tile_collision(unsigned int slot);

void c_walker_move(unsigned int slot)
{
    /* NES Walker_Move (Z_07.asm:2555). Non-Link path only — slot 0
     * (Link) movement still runs through the RoomRom debug runtime so
     * Walker_Move is never called for it under Phase 7 Task 7.2.
     *
     * Drained scope (step 5):
     *   - ChooseObjDirOrInputDir  -> CheckStunned (non-Link branch)
     *   - CheckStunned             -> InvClock | ObjStunTimer gate
     *   - FilterInput              -> single-direction pick via
     *                                 core_get_opposite_dir + reverse table
     *   - SetMovingDir             -> mask $0F into NES_OBJ_DIR
     *   - CheckBoundary            -> object_bound_by_room
     *   - MoveObject               -> object_move_object
     *
     * Deferred (TODO; step 6 / next task):
     *   - Obj_Shove                — fires on damage knockback; not yet
     *                                wired to combat hit path so
     *                                ObjShoveDir(slot) is always 0 at
     *                                this point in Phase 7 Task 7.2.
     *   - Walker_CheckTileCollision — needs room tile data; without it
     *                                octoroks would freeze in walls
     *                                instead of bouncing. Defer until
     *                                room subsystem hooked into Debug.md.
     *
     * NES ReverseDirections table { $08, $04, $02, $01 } — selects the
     * lowest-bit single direction from a (possibly diagonal) input mask
     * to prevent objects from straddling two axes per frame.
     */

    /* Step 1 — Obj_Shove gate. NES Walker_Move (Z_07.asm:2555) checks
     * ObjShoveDir; if non-zero, jumps to Obj_Shove (Z_07.asm:2730)
     * which advances the slot $20 px along the shove direction over
     * ObjShoveDistance frames. Wired 2026-05-18 — c_obj_shove drained
     * at c_shims.asm:4447 + linked. Without this, knockback frames
     * collapsed to no-op and walkers kept moving through hit-react. */
    if (OBJ(NES_OBJ_SHOVE_DIR, slot) != 0u) {
        c_obj_shove(slot);
        return;
    }

    /* Step 2 — CheckStunned (non-Link path). */
    if ((RAM(NES_INV_CLOCK) | OBJ(NES_OBJ_STUN_TIMER, slot)) != 0u) {
        return;
    }

    /* Step 3 — FilterInput. ObjInputDir(slot) was seeded by
     * enrt_update_rope from ENEMY_DIR(slot) before the call. */
    unsigned char input = (unsigned char)OBJ(NES_OBJ_INPUT_DIR, slot);
    unsigned char dir;
    if (input == 0u) {
        dir = 0u;
    } else {
        /* core_get_opposite_dir returns (idx<<8)|opposite. NES then
         * indexes ReverseDirections[idx] = { $08, $04, $02, $01 }
         * which, combined with the lowest-set-bit scan inside
         * core_get_opposite_dir, picks one single-bit direction from
         * the input mask. Equivalent to (input & -input) for a single
         * lowest set bit but mapped through the NES table — match the
         * NES path exactly so behavior is bit-identical. */
        static const unsigned char k_reverse_dirs[4] = {
            0x08u, 0x04u, 0x02u, 0x01u
        };
        unsigned int packed = core_get_opposite_dir((unsigned int)input);
        unsigned char idx = (unsigned char)((packed >> 8) & 0x03u);
        dir = k_reverse_dirs[idx];
    }

    /* Step 4 — SetMovingDir. NES masks low nibble into $0F. */
    RAM(NES_OBJ_DIR) = (unsigned char)(dir & 0x0Fu);

    /* NES sets $0E = 0 (doorway scratch). Walker_CheckTileCollision
     * branch uses this; safe to clear unconditionally. */
    RAM(NES_SHOT_COLLISION_FLAG) = 0u;

    /* Step 5 — CheckBoundary. object_bound_by_room reads NES_OBJ_DIR,
     * runs both H/V bound tests, and returns the post-test DIR (0 if
     * a bound cleared it). The function's side effect on $0F is what
     * matters; we drop the return value because object_move_object
     * re-reads $0F. */
    (void)object_bound_by_room(slot);

    /* Step 6 — Walker_CheckTileCollision (step 18 native). NES path
     * BoundByRoom -> Walker_CheckTileCollision -> MoveObject. Non-Link
     * branch only — walker UPDATE rows always pass slot >= 1. */
    c_walker_check_tile_collision(slot);

    /* Step 7 — MoveObject. Reads NES_OBJ_DIR, advances X/Y for the
     * matching axis bit. */
    object_move_object((unsigned short)slot);
}

/* Step 17 collision-viz counters. Public so probe can read them.
 * - check_monster_collisions_calls: total times c_check_monster_collisions
 *   was invoked since boot (across all slots, all ticks).
 * - check_link_collision_calls: total times c_check_link_collision
 *   was invoked since boot. */
volatile unsigned long g_check_monster_collisions_calls = 0u;
volatile unsigned long g_check_link_collision_calls    = 0u;

void c_check_monster_collisions(unsigned int slot)
{
    g_check_monster_collisions_calls++;
    link_collision_check_monster_collisions(slot);
}

void c_check_link_collision(unsigned int slot)
{
    g_check_link_collision_calls++;
    link_collision_check_link_collision(slot);
}

void c_draw_object_not_mirrored_with_frame(unsigned int frame, unsigned int slot)
{
    draw_object_not_mirrored_with_frame((unsigned char)frame, slot);
}

void c_wanderer_target_player(unsigned int slot)
{
    enrt_wanderer_target_player(slot);
}

void z07_anim_advance_and_fetch(unsigned int val, unsigned int slot)
{
    sprite_anim_advance_and_fetch(val, slot);
}

void z07_anim_set_obj_hflip(unsigned int slot)
{
    /* Step 7: forwarder for enrt_animate_and_draw_common_object below.
     * Mirrors src/gen/z_07.c NATIVE_SPRITE branch — that file is not
     * linked into Debug.md so we route directly. */
    sprite_anim_set_obj_hflip(slot);
}

void enrt_animate_and_draw_common_object(unsigned int val, unsigned int slot)
{
    /* Step 7: native composition matching src/oracle/enemies/enemy_runtime.c.
     * Inlined here instead of linking enemy_runtime.c (avoids dragging in
     * legacy_bridge.h chain). Used by enrt_update_stalfos (and bubble/
     * standing fire / etc when those families wire later). */
    z07_anim_advance_and_fetch(val, slot);
    z07_anim_set_obj_hflip(slot);
    c_draw_object_not_mirrored_with_frame(0u, slot);
}

unsigned char z01_anim_set_sprite_desc_attrs(unsigned int val)
{
    return core_anim_set_sprite_desc_attrs(val);
}

unsigned char z01_abs(unsigned int val)
{
    /* NES Abs at z_01.asm. Sign-test on bit 7. */
    unsigned char v = (unsigned char)val;
    return (v < 0x80u) ? v : (unsigned char)(0u - (unsigned int)v);
}

/* -------- Step 6: walker family unblock stubs -------- */

/* Helper: NES EnsureObjectAligned (Z_07.asm:2086). Snap X/Y to the
 * 8-pixel grid when GridOffset is 0; Y also gets +5 vertical offset
 * (NES uses ObjY = (Y & $F8) | 5 for top-left alignment). */
static void shove_ensure_object_aligned(unsigned int slot)
{
    if (OBJ(NES_OBJ_GRID_OFFSET, slot) != 0u)
        return;
    OBJ(NES_OBJ_X, slot) = (unsigned char)(OBJ(NES_OBJ_X, slot) & 0xF8u);
    OBJ(NES_OBJ_Y, slot) =
        (unsigned char)((OBJ(NES_OBJ_Y, slot) & 0xF8u) | 0x05u);
}

/* Helper: NES CheckPersonBlocking (Z_01.asm:3108). Reads Link's Y;
 * if Link is high in room (Y < $8E) AND moving up (bit 3 of dir set),
 * zero $0F via core_reset_moving_dir to signal blocked. Otherwise
 * leave $0F alone. */
static void shove_check_person_blocking(void)
{
    /* NES uses absolute ObjY (slot 0 = Link). */
    if (OBJ(NES_OBJ_Y, 0u) >= 0x8Eu)
        return;                           /* Link too low; not blocking */
    if ((RAM(NES_LINK_MOVING_DIR) & 0x08u) == 0u)
        return;                           /* not moving up */
    (void)core_reset_moving_dir();        /* clears $0F */
}

void c_obj_shove(unsigned int slot)
{
    /* NES Obj_Shove (Z_07.asm:2274). Phase 7 Task 7.2 step 16 native
     * drain. Stance: REPLACE (was step-6 stub).
     *
     * Two phases:
     *   - Init phase (high bit of ObjShoveDir set): clear high bit,
     *     pick perpendicular policy from ObjDir vs new shove dir.
     *   - Move phase: try to walk up to 4 pixels in shove direction,
     *     respecting GridOffset alignment, tile collision, room edges,
     *     person blocking. Decrements ObjShoveDistance per pixel; on
     *     any block cause, ResetShoveInfo (clears dir + dist).
     *
     * NES $0F = LINK_MOVING_DIR scratch (z07_get_colliding_tile_moving
     * reads it as the direction byte). $03 = pixel counter (4..0).
     * $02 = +1/-1 delta for the moved axis.
     */

    unsigned char shove_dir = (unsigned char)OBJ(NES_OBJ_SHOVE_DIR, slot);

    /* ASL on shove_dir: bit-7 was the "init" flag. */
    if ((shove_dir & 0x80u) != 0u) {
        /* @InitPhase — clear high bit, then check perpendicular. */
        unsigned char dir_low = (unsigned char)(shove_dir & 0x7Fu);
        OBJ(NES_OBJ_SHOVE_DIR, slot) = dir_low;

        unsigned char obj_dir = (unsigned char)ENEMY_DIR(slot);
        if (obj_dir < 0x03u) {
            /* @FacingHorizontally: shove_dir & $0C nonzero -> perpendicular */
            if ((dir_low & 0x0Cu) == 0u)
                return;                  /* horizontal shove + horizontal facing -> OK */
        } else {
            /* facing vertical: shove_dir & $03 nonzero -> perpendicular */
            if ((dir_low & 0x03u) == 0u)
                return;                  /* vertical shove + vertical facing -> OK */
        }

        /* @CheckPerpendicularShove */
        if (OBJ(NES_OBJ_GRID_OFFSET, slot) == 0u)
            return;                      /* aligned -> allow it */
        if (slot != 0u) {
            /* not Link -> ResetShoveInfo */
            OBJ(NES_OBJ_SHOVE_DIR, slot)  = 0u;
            OBJ(NES_OBJ_SHOVE_DIST_BASE, slot) = 0u;
            return;
        }
        /* Link: bounce shove backward (opposite of facing). NES uses
         * absolute addresses ObjDir + ObjShoveDir (slot 0). */
        {
            unsigned int packed = core_get_opposite_dir((unsigned int)ENEMY_DIR(0u));
            OBJ(NES_OBJ_SHOVE_DIR_BASE, 0u) = (unsigned char)(packed & 0xFFu);
        }
        return;
    }

    /* @MoveIfNotDone */
    if (OBJ(NES_OBJ_SHOVE_DIST_BASE, slot) == 0u) {
        /* ResetShoveInfo (no distance left). */
        OBJ(NES_OBJ_SHOVE_DIR, slot)       = 0u;
        OBJ(NES_OBJ_SHOVE_DIST_BASE, slot) = 0u;
        return;
    }

    /* ShoveMoveMin: 4-pixel move loop. [03] is the NES pixel counter and
     * [02] the +1/-1 step; both stay in NES RAM as on the NES (T-171). */
    RAM(0x0003u) = 4u;
    do {
        /* @LoopShovePixel */
        unsigned char grid_off = (unsigned char)OBJ(NES_OBJ_GRID_OFFSET, slot);
        if (grid_off == 0u) {
            shove_ensure_object_aligned(slot);
            unsigned char dir = (unsigned char)(OBJ(NES_OBJ_SHOVE_DIR, slot) & 0x0Fu);
            RAM(NES_LINK_MOVING_DIR) = dir;
            unsigned char tile = collision_get_colliding_tile_moving(slot);
            if (tile >= RAM(0x034Au)) {
                /* ObjectFirstUnwalkableTile -> blocked. */
                OBJ(NES_OBJ_SHOVE_DIR, slot)       = 0u;
                OBJ(NES_OBJ_SHOVE_DIST_BASE, slot) = 0u;
                return;
            }
        }

        /* @CheckBoundary */
        {
            unsigned char dir = (unsigned char)(OBJ(NES_OBJ_SHOVE_DIR, slot) & 0x0Fu);
            unsigned char post = object_bound_by_room_with_dir(dir, slot);
            if (post == 0u) {
                OBJ(NES_OBJ_SHOVE_DIR, slot)       = 0u;
                OBJ(NES_OBJ_SHOVE_DIST_BASE, slot) = 0u;
                return;
            }
        }

        /* Person-blocking gate: only fires if slot 1's type is the
         * grumble moblin ($36) OR a person ($4B..$52). */
        {
            unsigned char t1 = (unsigned char)ENEMY_TYPE(1u);
            int is_person = (t1 == 0x36u) ||
                            ((t1 >= 0x4Bu) && (t1 < 0x53u));
            if (is_person) {
                shove_check_person_blocking();
                if (RAM(NES_LINK_MOVING_DIR) == 0u) {
                    OBJ(NES_OBJ_SHOVE_DIR, slot)       = 0u;
                    OBJ(NES_OBJ_SHOVE_DIST_BASE, slot) = 0u;
                    return;
                }
            }
        }

        /* @ChooseSpeed: $02 = +1 if dir bit-0 (right) or bit-2 (down) set,
         * else -1. NES uses (ShoveDir & $05) as the "positive" mask. */
        unsigned char delta;
        {
            unsigned char dir = (unsigned char)OBJ(NES_OBJ_SHOVE_DIR, slot);
            delta = ((dir & 0x05u) != 0u) ? 0x01u : 0xFFu;
        }
        RAM(0x0002u) = delta;

        /* Decrement remaining distance. */
        OBJ(NES_OBJ_SHOVE_DIST_BASE, slot) =
            (unsigned char)(OBJ(NES_OBJ_SHOVE_DIST_BASE, slot) - 1u);

        /* Advance grid offset by delta; wrap to 0 on multiple of $10
         * (or 8 for Link). */
        {
            unsigned char new_off =
                (unsigned char)(OBJ(NES_OBJ_GRID_OFFSET, slot) + delta);
            unsigned char masked  = (unsigned char)(new_off & 0x0Fu);
            if (masked == 0u) {
                OBJ(NES_OBJ_GRID_OFFSET, slot) = 0u;
            } else if (slot == 0u && (masked & 0x07u) == 0u) {
                OBJ(NES_OBJ_GRID_OFFSET, slot) = 0u;
            } else {
                OBJ(NES_OBJ_GRID_OFFSET, slot) = new_off;
            }
        }

        /* @ApplySpeed: horizontal bits 0-1 of ShoveDir set -> bump X,
         * else bump Y. */
        {
            unsigned char dir = (unsigned char)OBJ(NES_OBJ_SHOVE_DIR, slot);
            if ((dir & 0x03u) != 0u) {
                OBJ(NES_OBJ_X, slot) =
                    (unsigned char)(OBJ(NES_OBJ_X, slot) + delta);
            } else {
                OBJ(NES_OBJ_Y, slot) =
                    (unsigned char)(OBJ(NES_OBJ_Y, slot) + delta);
            }
        }
        RAM(0x0003u) = (unsigned char)(RAM(0x0003u) - 1u);
    } while (RAM(0x0003u) != 0u);
}

/* -------- Step 18: native c_walker_check_tile_collision -------- */

/* Helper: Walker_GetNextAltDir (Z_07.asm:3027). 4-step jump table.
 * NES sequence is `LDA $0E; INC $0E; TableJump(A)` so the dispatched
 * step uses the OLD $0E. EndLoop resets $0E = 0 explicitly.
 *  step 0 -> RandomObjPerpendicularDir
 *  step 1 -> MovingOppositeDir
 *  step 2 -> ReverseObjDir (also rewrites ObjDir + $0F)
 *  step 3 -> EndLoop (returns 0; $0E reset to 0)
 */
static unsigned char walker_get_next_alt_dir(unsigned int slot)
{
    static const unsigned char k_reverse_dirs[4] = {
        0x08u, 0x04u, 0x02u, 0x01u
    };

    unsigned char step = (unsigned char)RAM(NES_SHOT_COLLISION_FLAG); /* $0E */
    RAM(NES_SHOT_COLLISION_FLAG) = (unsigned char)(step + 1u);

    switch (step & 0x03u) {
    case 0u: {
        /* RandomObjPerpendicularDir (Z_07.asm:3037).
         * Y = (Random[slot] high bit set) ? 0 : 1
         * If ObjDir & $0C (V-facing) -> Y += 2 -> table picks H dir.
         * Else -> table picks V dir. Result is always perpendicular
         * to current facing axis. NES Random table base = $0018. */
        unsigned char r = (unsigned char)RAM(0x0018u + slot);
        unsigned char y = ((r & 0x80u) != 0u) ? 0u : 1u;
        if ((ENEMY_DIR(slot) & 0x0Cu) != 0u) {
            y = (unsigned char)(y + 2u);
        }
        return k_reverse_dirs[y & 0x03u];
    }
    case 1u: {
        /* MovingOppositeDir (Z_07.asm:3053).
         *   if ($0F & $0A) != 0  -> $0F >> 1
         *   else                 -> $0F << 1 (8-bit truncated)
         * $0A = bits {DOWN, RIGHT}; $05 = bits {UP, LEFT}. So shifting
         * right when on the "increasing" axis flips to the matching
         * decreasing direction (and vice versa). */
        unsigned char dir = (unsigned char)RAM(NES_LINK_MOVING_DIR);
        if ((dir & 0x0Au) != 0u) {
            return (unsigned char)(dir >> 1);
        }
        return (unsigned char)((dir << 1) & 0xFFu);
    }
    case 2u: {
        /* ReverseObjDir (Z_07.asm:3067):
         *   ObjDir = opposite(ObjDir); $0F = ObjDir; return A. */
        unsigned int packed =
            core_get_opposite_dir((unsigned int)ENEMY_DIR(slot));
        unsigned char opp = (unsigned char)(packed & 0xFFu);
        ENEMY_DIR(slot)          = opp;
        RAM(NES_LINK_MOVING_DIR) = opp;
        return opp;
    }
    default: {
        /* EndLoop (Z_07.asm:3077). $0E = 0; return 0. */
        RAM(NES_SHOT_COLLISION_FLAG) = 0u;
        return 0u;
    }
    }
}

/* _FaceUnblockedDir (Z_07.asm:3088). Aligned objects only: walk the
 * alt-dir sequence (perpendicular, opposite of [0F], reverse) and face the
 * first direction that is walkable and inside the room. [0E] = step,
 * [0F] = candidate, as on the NES. Used by UpdateRope's turn timer. */
void c_face_unblocked_dir(unsigned int slot)
{
    if (OBJ(NES_OBJ_GRID_OFFSET, slot) != 0u) return;
    RAM(NES_SHOT_COLLISION_FLAG) = 0u;
    for (;;) {
        unsigned char alt = walker_get_next_alt_dir(slot);
        RAM(NES_LINK_MOVING_DIR) = alt;
        if (alt == 0u) return;
        if (collision_get_colliding_tile_moving(slot) >= (unsigned char)RAM(0x034Au))
            continue;
        alt = object_bound_by_room_with_dir(alt, slot);
        if (alt == 0u) continue;
        ENEMY_DIR(slot) = alt;
        return;
    }
}

void c_walker_check_tile_collision(unsigned int slot)
{
    /* NES Walker_CheckTileCollision (Z_07.asm:2815). Phase 7 Task 7.2
     * step 18. Stance: REPLACE the prior c_walker_move TODO comment
     * that deferred this primitive.
     *
     * Drained scope: non-Link branch only. Walker UPDATE rows always
     * pass slot >= 1, so the X==0 Link branch (DoorwayDir read,
     * GameMode==5 ladder check, screen-edge handler,
     * GoToNextModeFromPlay) is not reachable from this call site and
     * is intentionally omitted.
     *
     * The Reverse branch (gated on ObjAttr bit $10) is omitted as
     * dead code — NES asm comment Z_07.asm:2862 records "$10 is not
     * used in the object attribute array at 07:FAEF". The table
     * never sets it, so Reverse never fires.
     *
     * NES $0E = SHOT_COLLISION_FLAG ($000E) = alt-dir loop step.
     * NES $0F = LINK_MOVING_DIR    ($000F) = moving direction scratch.
     */

    /* No ObjectFirstUnwalkableTile guard: room load writes $034A
     * (enemy_loop.c, $89 OW / $78 UW) before any walker updates, as on
     * the NES (T-171; the earlier `== 0 -> return` skip predated that). */

    /* @CheckGridOffset (X != 0 path): grid offset != 0 -> return. */
    if (OBJ(NES_OBJ_GRID_OFFSET, slot) != 0u) return;

    /* Reset alt-dir step ($0E = 0). */
    RAM(NES_SHOT_COLLISION_FLAG) = 0u;

    /* If $0F (moving dir) == 0, set from input dir + drop into
     * the TryNextDir loop. Else fall through to CheckTiles. */
    unsigned char moving_dir = (unsigned char)RAM(NES_LINK_MOVING_DIR);
    if (moving_dir == 0u) {
        moving_dir = (unsigned char)OBJ(NES_OBJ_INPUT_DIR, slot);
        RAM(NES_LINK_MOVING_DIR) = moving_dir;
        unsigned char alt = walker_get_next_alt_dir(slot);
        RAM(NES_LINK_MOVING_DIR) = alt;
        if (RAM(NES_SHOT_COLLISION_FLAG) == 0u) return;
        /* fall through into CheckTiles loop */
    }

    /* CheckTiles loop. */
    for (unsigned int guard = 0u; guard < 8u; guard++) {
        unsigned char tile = collision_get_colliding_tile_moving(slot);
        if (tile < (unsigned char)RAM(0x034Au)) {
            /* GoWalkableDir non-Link path -> CheckBoundary (Z_07.asm:3005).
             * BoundByRoom returns post-test dir; non-zero -> store as
             * facing dir and return. Zero -> TryNextDir loop. */
            unsigned char dir = (unsigned char)RAM(NES_LINK_MOVING_DIR);
            unsigned char post = object_bound_by_room_with_dir(dir, slot);
            if (post != 0u) {
                ENEMY_DIR(slot) = post;
                return;
            }
        }
        /* Unwalkable OR boundary failed -> next alt dir. */
        unsigned char alt = walker_get_next_alt_dir(slot);
        RAM(NES_LINK_MOVING_DIR) = alt;
        if (RAM(NES_SHOT_COLLISION_FLAG) == 0u) return;
    }
    /* guard exit: NES loop bounded by $0E reaching 4. The 8-iter
     * cap above is a belt-and-braces native guard — should never
     * fire because EndLoop returns 0 + clears $0E by step 4. */
}

unsigned int c_shoot_if_wanted(unsigned int shot_type, unsigned int slot)
{
    /* NES Z_04.asm:11351 _ShootIfWanted + Z_07.asm:5795 FindEmptyMonsterSlot
     * + Z_07.asm:5782 SetTypeAndClearObject + Z_01.asm:4026 DestroyObject_WRAM.
     * Phase 7 Task 7.2 step 9 — projectile hook native drain.
     * Stance: REPLACE (was step 6 stub).
     *
     * NES sequence:
     *   1. If ObjWantsToShoot == 0 -> return C=0.
     *   2. FindEmptyMonsterSlot: scan Y=$0B downto $01 for ObjType==0.
     *      None found -> return C=0.
     *   3. If shot_type >= $53 (true projectile, not melee):
     *        if ActiveMonsterShots >= 4 -> return C=0.
     *        else INC ActiveMonsterShots.
     *   4. SetTypeAndClearObject(shot_type, empty): writes ObjType[empty];
     *      DestroyObject_WRAM zeroes ShoveDir/ShoveDist/Timer/State/
     *      InvincibilityTimer + sets Uninitialized=$FF + Metastate=$01.
     *   5. ObjState[empty] = $10, ObjTimer[empty] = 0,
     *      Dir/X/Y[empty] = Dir/X/Y[shooter].
     *   6. Return C=1, Y=empty.
     *
     * This-project convention:
     *   - empty slot == ENEMY_TYPE(s)==0 AND ENEMY_ALIVE_FLAG(s)==0.
     *   - ENEMY_ALIVE_FLAG=1 marks the slot occupied.
     *   - Shot UPDATE rows ($53 flying rock etc) must be wired in
     *     enemy_update_fns[] separately for the shot to do anything;
     *     until then it'll spawn but stand still — visible regression
     *     surface for the next step. */

    RAM(0x0000u) = (unsigned char)shot_type;   /* _ShootIfWanted: STA $00 */
    if (ENEMY_PUSH_TIMER(slot) == 0u) return 0u;

    unsigned int empty = 0u;
    {
        unsigned int y = 0x0Bu;
        for (;;) {
            /* FindEmptyMonsterSlot: STY EmptyMonsterSlot ($59, T-171). */
            if (ENEMY_TYPE(y) == 0u) { empty = y; ENEMY_NEXT_SHOT_SLOT = (unsigned char)y; break; }
            if (y == 0x01u) break;
            y--;
        }
    }
    if (empty == 0u) return 0u;

    if (shot_type >= 0x53u) {
        if (ENEMY_SHOT_COUNT >= 0x04u) return 0u;
        ENEMY_SHOT_COUNT = (unsigned char)(ENEMY_SHOT_COUNT + 1u);
    }

    /* SetTypeAndClearObject + DestroyObject_WRAM compositional clear.
     * Mirrors clear_slot_scratch() in enemy_loop.c but written out so
     * the trampoline match to NES sequence is auditable. */
    ENEMY_TYPE(empty)              = (unsigned char)shot_type;
    ENEMY_OBJ_SHOVE_DIR(empty)     = 0u;
    OBJ(0x00D3u, empty)            = 0u;  /* ObjShoveDistance */
    ENEMY_MOVE_TIMER(empty)        = 0u;  /* ObjTimer ($0028) */
    ENEMY_STATE_TIMER(empty)       = 0u;  /* ObjState ($00AC) */
    ENEMY_HIT_REACTION(empty)      = 0u;  /* ObjInvincibilityTimer ($04F0) */
    ENEMY_METASTATE(empty)         = 0x01u;
    /* DestroyObject_WRAM: uninitialized; its first update runs
     * InitObject (T-012: the shot waits a frame, NES). */
    ENEMY_ALIVE_FLAG(empty)        = 0xFFu;

    /* Shoot block: state $10 = "shot active", copy dir/x/y from shooter. */
    ENEMY_STATE_TIMER(empty) = 0x10u;
    ENEMY_MOVE_TIMER(empty)  = 0u;
    ENEMY_DIR(empty)         = (unsigned char)ENEMY_DIR(slot);
    ENEMY_X(empty)           = (unsigned char)ENEMY_X(slot);
    ENEMY_Y(empty)           = (unsigned char)ENEMY_Y(slot);

    /* T-012: ObjAttr and the per-type init (InitMonsterShot: ObjQSpeed)
     * come from InitObject on the shot's first update (enemy_loop),
     * as on the NES; running them here moved the rock a frame early. */
    return CARRY_SET | empty;
}

/* -------- Step 6: native enrt_update_octorock -------- */

void enrt_update_octorock(unsigned int slot)
{
    /* NES UpdateOctorock (Z_04.asm:2966). Phase 7 Task 7.2 step 6.
     * Native composition over drained primitives. Stance: EXTEND.
     *
     * Replaces the earlier $07 dispatch row that reused enrt_update_rope
     * (semantically wrong — rope is type $29; enrt_update_rope's leever-
     * style speed-ramp is rope-only behavior). Octorok shape is simpler:
     * walker move + shoot-rock try + dir-based frame select.
     *
     * Steps:
     *   1. Turn rate (ENEMY_AIR_SPEED): blue ($09+) = $A0, red = $70.
     *   2. enrt_wanderer_target_player — runs c_walker_move + targeting.
     *   3. qspeed: $20 if slow ($07/$09), else $40 (fast $08/$0A).
     *   4. _TryShooting flying rock $53 (enrt_try_shooting). A failed
     *      allocation keeps qspeed; a successful shot pauses the Octorok.
     *   5. sprite_anim_fetch_obj_pos — primes draw scratch + clears
     *      ENEMY_FRAME_FLAGS (ZP_TMPF / $000F).
     *   6. dir-based frame_offset: UP=1, DOWN=2, LEFT=0, RIGHT=0+hflip.
     *   7. anim counter DEC; on 0 reload to 6 + toggle DRAW_FRAME ^ 3.
     *   8. final_frame = dir_offset + DRAW_FRAME.
     *   9. Draw mirrored if dir & $0C, else not mirrored.
     *  10. CheckMonsterCollisions.
     */

    /* Step 1 — Turn rate. */
    ENEMY_AIR_SPEED(slot) =
        (ENEMY_TYPE(slot) >= 0x09u) ? 0xA0u : 0x70u;

    /* Step 2 — Wanderer chain. */
    enrt_wanderer_target_player(slot);

    /* Step 3 — qspeed by color. */
    unsigned char qspeed;
    {
        unsigned char t = ENEMY_TYPE(slot);
        qspeed = (t == 0x07u || t == 0x09u) ? 0x20u : 0x40u;
    }

    /* Step 4 — _TryShooting flying rock $53 (shared NES helper). */
    enrt_try_shooting(qspeed, 0x53u, slot);

    /* Step 5 — Anim_FetchObjPosForSpriteDescriptor. */
    (void)sprite_anim_fetch_obj_pos(slot);

    /* Step 6 — Direction-based frame offset. */
    unsigned char dir_offset;
    {
        unsigned char dir = ENEMY_DIR(slot);
        if ((dir & 0x0Cu) != 0u) {
            /* Vertical: UP=$08 -> 1, DOWN=$04 -> 2. */
            dir_offset = (dir == 0x08u) ? 1u : 2u;
        } else {
            /* Horizontal: LEFT/RIGHT both use offset 0; RIGHT also
             * sets hflip via ENEMY_FRAME_FLAGS (NES INC $0F). */
            dir_offset = 0u;
            if (dir == 0x01u) {
                ENEMY_FRAME_FLAGS =
                    (unsigned char)(ENEMY_FRAME_FLAGS + 1u);
            }
        }
    }

    /* Step 7 — Animate counter. */
    {
        unsigned char ctr = (unsigned char)(ENEMY_ANIM_TIMER(slot) - 1u);
        if (ctr == 0u) {
            ctr = 0x06u;
            ENEMY_DRAW_FRAME(slot) =
                (unsigned char)(ENEMY_DRAW_FRAME(slot) ^ 0x03u);
        }
        ENEMY_ANIM_TIMER(slot) = ctr;
    }

    /* Step 8 — final = dir_offset + DRAW_FRAME. */
    unsigned char final_frame =
        (unsigned char)(dir_offset + ENEMY_DRAW_FRAME(slot));

    /* Step 9 — Draw mirrored vs not. */
    if ((ENEMY_DIR(slot) & 0x0Cu) != 0u) {
        draw_object_mirrored_with_frame(final_frame, slot);
    } else {
        draw_object_not_mirrored_with_frame(final_frame, slot);
    }

    /* Step 10 — CheckMonsterCollisions. */
    link_collision_check_monster_collisions(slot);
}

/* ---------------------------------------------------------------------------
 * Phase 7 Task 7.2 step 20 — UpdateMetaObject (NES Z_07.asm:5403).
 *
 * Drain Rule D1: NES asm SECONDARY (no drained C candidate exists for
 * UpdateMetaObject / AnimateAndDrawMetaObject / UpdateMetaObjectEnd).
 * Stance: ADOPT — transcribe the NES body verbatim, with two scoped
 * stubs:
 *
 *   - Anim_FetchObjPosForSpriteDescriptor + DrawCloud + Anim_WriteItemSprites:
 *     skipped (OAM router not wired; visible sparkle/cloud not observable
 *     this phase). Timer + metastate cells still update identically, so
 *     the metastate-progression timing matches NES.
 *
 *   - SetUpDroppedItem (Z_04.asm:11103): skipped. Drop-item id lookup +
 *     fairy-on-$10-kills + help-drop branch defer to a follow-up task
 *     (item subsystem hookup). Step 20 only verifies the OUTER drop
 *     conversion (ENEMY_TYPE -> $60) is observable; the dropped item's
 *     actual identity is not yet visible without the OAM router anyway.
 *
 * NES variable map:
 *   ObjTimer            = OBJ($0028, slot)  =  ENEMY_MOVE_TIMER
 *   ObjMetastate        = OBJ($0405, slot)  =  ENEMY_METASTATE
 *   ObjType             = OBJ($03A8, slot)  =  ENEMY_TYPE  (NES_OBJ_TYPE)
 *   ObjAttr             = OBJ($04BF, slot)
 *   ObjUninitialized    = OBJ($0492, slot)  ←  semantic conflict: in NES
 *                                              this is the "needs init"
 *                                              flag; in our code the
 *                                              same offset is named
 *                                              ENEMY_ALIVE_FLAG and uses
 *                                              the OPPOSITE polarity
 *                                              (1=alive, 0=empty). The
 *                                              enemy loop also reserves
 *                                              $FF for an occupied slot's
 *                                              one-time InitObject tick.
 *                                              Converted drops use that
 *                                              sentinel after setup so
 *                                              UpdateItem begins on the
 *                                              same frame as NES.
 *   Item_ObjMonsterType = OBJ($0412, slot)  ←  same offset as ENEMY_PUSH_TIMER;
 *                                              NES Z1 reuses the cell for
 *                                              dropped-item slots.
 *   WorldKillCycle      = RAM($052A)         (0..9 wrap)
 *   RoomKillCount       = RAM($0627)         = ROOM_KILL_COUNT
 *   NoDropMonsterTypes (single-byte branches): $5D (RupeeStash),
 *                                              $14 (ChildGel),
 *                                              $1C (RedKeese).
 */

/* NES Item_ObjMonsterType ($0412) aliases ENEMY_PUSH_TIMER. */
#define META_ITEM_MONSTER_TYPE(slot)   OBJ(0x0412, (slot))
#define META_OBJ_ATTR(slot)            OBJ(0x04BF, (slot))
#define META_WORLD_KILL_CYCLE          RAM(0x052A)
/* NES Item_ObjItemLifetime ($03A8): per-slot countdown for dropped item. */
#define META_ITEM_LIFETIME(slot)       OBJ(0x03A8, (slot))
/* NES Item_ObjItemId aliases OBJ_STATE ($00AC). */
#define META_ITEM_ID(slot)             OBJ(0x00AC, (slot))
/* NES help-drop runaway counters at ZP $50/$51. */
#define META_HELP_DROP_COUNT           RAM(0x0050)
#define META_HELP_DROP_VALUE           RAM(0x0051)
/* NES WorldKillCount at $0627 — fairy-on-$10-kills gate. */
#define META_WORLD_KILL_COUNT          RAM(0x0627)

/* NES drop tables (reference/aldonunez/Z_04.asm:11042-11101).
 * Extracted to src/data/drop_tables.inc; mirrored here as C const arrays
 * so SetUpDroppedItem can run without ASM linkage. */
static const unsigned char k_no_drop_types[7] = {
    0x5Du, 0x14u, 0x15u, 0x1Bu, 0x1Cu, 0x1Du, 0x17u
};

static const unsigned char k_drop_set0_types[6] = {
    0x07u, 0x08u, 0x0Eu, 0x04u, 0x0Fu, 0x23u
};
static const unsigned char k_drop_set1_types[9] = {
    0x21u, 0x22u, 0x0Du, 0x10u, 0x13u, 0x28u, 0x2Au, 0x27u, 0x16u
};
static const unsigned char k_drop_set2_types[9] = {
    0x09u, 0x0Au, 0x03u, 0x01u, 0x12u, 0x06u, 0x0Bu, 0x24u, 0x30u
};
static const unsigned char k_drop_set_base_offsets[4] = {
    0x00u, 0x0Au, 0x14u, 0x1Eu
};
static const unsigned char k_drop_rates[4] = {
    0x50u, 0x98u, 0x68u, 0x68u
};
static const unsigned char k_drop_item_table[40] = {
    0x22u, 0x18u, 0x22u, 0x18u, 0x23u, 0x18u, 0x22u, 0x22u,
    0x18u, 0x18u, 0x0Fu, 0x18u, 0x22u, 0x18u, 0x0Fu, 0x22u,
    0x21u, 0x18u, 0x18u, 0x18u, 0x22u, 0x00u, 0x18u, 0x21u,
    0x18u, 0x22u, 0x00u, 0x18u, 0x00u, 0x22u, 0x22u, 0x22u,
    0x23u, 0x18u, 0x22u, 0x23u, 0x22u, 0x22u, 0x22u, 0x18u
};

/* Fairy drop setup is shared with the drained flyer implementation. */
extern void enrt_set_up_fairy_object(unsigned int slot);

/* DestroyMonster_Bank4 (NES Z_04.asm:11327) — clear slot completely.
 * NES sets ObjType=0, SetShoveInfoWith0, ObjTimer=0, ObjState=0,
 * ObjInvincibilityTimer=0, ObjUninitialized=$FF, ObjMetastate=1.
 * Our codebase ALIVE_FLAG semantics inverse of NES uninit: 0=dead/free. */
/* DestroyMonster -> SetTypeAndClearObject -> DestroyObject_WRAM (Z_01.asm):
 * type, shove dir/distance, ObjTimer, ObjState and ObjInvincibilityTimer
 * ($4F0) cleared (lockstep tmp_shotHit: NES clears $C0/$D3/$4F0 when the
 * killed tektite is destroyed). ObjUninitialized ($492) is $FF on NES; the
 * Genesis loop uses the cell as an alive flag (0 = empty). The metastate
 * ends at 0 either way (@Reset after SetUpDroppedItem). */
static void native_destroy_monster(unsigned int slot)
{
    /* NES DestroyMonster = SetTypeAndClearObject(0) -> DestroyObject_WRAM
     * (metastate 1, T-171). */
    core_set_type_and_clear_object(0u, slot);
}

/* SetUpDroppedItem (NES Z_04.asm:11103) — drop-item id lookup + fairy gate +
 * help-drop randomization. Stance: EXTEND.
 *
 * Returns 1 if drop committed (slot converted to live $60 item), 0 if
 * slot was destroyed (no drop). */
static unsigned char native_set_up_dropped_item(unsigned int slot)
{
    unsigned char row = 0u;
    unsigned char monster_type = (unsigned char)META_ITEM_MONSTER_TYPE(slot);
    unsigned char item_id;
    unsigned char i;

    /* @FindNoDropType — destroy if type is in no-drop list. */
    for (i = 0u; i < 7u; ++i) {
        if (monster_type == k_no_drop_types[i]) {
            native_destroy_monster(slot);
            return 0u;
        }
    }

    /* @FindDrop0Type — row 0. */
    {
        unsigned char found = 0u;
        for (i = 0u; i < 6u; ++i) {
            if (monster_type == k_drop_set0_types[i]) { found = 1u; break; }
        }
        if (!found) {
            row = 1u;
            for (i = 0u; i < 9u; ++i) {
                if (monster_type == k_drop_set1_types[i]) { found = 1u; break; }
            }
        }
        if (!found) {
            row = 2u;
            for (i = 0u; i < 9u; ++i) {
                if (monster_type == k_drop_set2_types[i]) { found = 1u; break; }
            }
        }
        if (!found) row = 3u;
    }

    /* @Found — slot 1 + (Stalfos $2A or Gibdo $30) destroys (already has room item). */
    if (slot == 1u && (monster_type == 0x2Au || monster_type == 0x30u)) {
        native_destroy_monster(slot);
        return 0u;
    }

    /* @LookUpItem — base offset + WorldKillCycle = drop item id. */
    {
        unsigned char base   = k_drop_set_base_offsets[row];
        unsigned char cycle  = (unsigned char)META_WORLD_KILL_CYCLE;
        unsigned char idx    = (unsigned char)(base + cycle);
        if (idx >= 40u) idx = (unsigned char)(idx % 40u);
        item_id = k_drop_item_table[idx];
    }

    /* Fairy-on-$10-kills gate. */
    if ((unsigned char)META_WORLD_KILL_COUNT == 0x10u) {
        item_id = 0x23u;
        META_HELP_DROP_COUNT = 0u;
        META_HELP_DROP_VALUE = 0u;
    } else if ((unsigned char)META_HELP_DROP_COUNT < 0x0Au) {
        /* @RandomlyCancel — Random[slot] >= rate cancels drop. */
        unsigned char rnd = (unsigned char)ENEMY_RNG_A(slot);
        if (rnd >= k_drop_rates[row]) {
            native_destroy_monster(slot);
            return 0u;
        }
    } else {
        /* Help-drop runaway — guaranteed item, type from HelpDropValue. */
        if ((unsigned char)META_HELP_DROP_VALUE == 0u) {
            item_id = 0x0Fu;  /* 5 rupees */
        } else {
            item_id = 0x00u;  /* bomb */
        }
        META_HELP_DROP_COUNT = 0u;
        META_HELP_DROP_VALUE = 0u;
    }

    /* @Commit — store lifetime + id. NES jumps to SetUpFairyObject for $23. */
    META_ITEM_LIFETIME(slot) = 0xFFu;
    META_ITEM_ID(slot)       = item_id;
    if (item_id == 0x23u) {
        enrt_set_up_fairy_object(slot);
    }
    /* NES @DropItem (Z_07.asm:5457) stores $60 in ObjType and
     * ObjUninitialized before SetUpDroppedItem: the next object pass runs
     * InitObject, only the following one UpdateItem. */
    ENEMY_ALIVE_FLAG(slot) = 0x60u;
    return 1u;
}

void update_meta_object(unsigned int slot)
{
    unsigned char ms = (unsigned char)ENEMY_METASTATE(slot);

    /* AnimateAndDrawMetaObject (NES Z_07.asm:4977) — draw stub, timer
     * + metastate logic verbatim.
     *
     * if metastate >= $10: spark path
     *   if (metastate & $0F) == 0: skip draw, ALWAYS reset timer + INC
     *     metastate (the @AnimateSpark BEQ @IncMetastate branch).
     *   else: draw spark (skipped), then check timer:
     *     if ObjTimer != 0: exit
     *     else: reset ObjTimer=6, INC metastate
     * else (metastate < $10): cloud path. Metastate's lower nibble is
     *   the cloud frame (0..3). Draw cloud (skipped). Same timer check.
     */
    if (ms >= 0x10u) {
        unsigned char nibble = (unsigned char)(ms & 0x0Fu);
        if (nibble == 0u) {
            /* @AnimateSpark BEQ @IncMetastate — immediate INC, no
             * draw, no timer-zero gate. */
            ENEMY_MOVE_TIMER(slot) = 0x06u;
            ENEMY_METASTATE(slot) = (unsigned char)(ms + 1u);
            ms = (unsigned char)(ms + 1u);
        } else {
            /* Phase D 2026-05-15: publish spark frame to native cache so
             * the dying-enemy spark sprite is visible. NES Z_07.asm:5001
             * @AnimateSpark draws via Anim_WriteItemSprites Y=$24. */
            enemy_render_publish_meta(slot);
            if (ENEMY_MOVE_TIMER(slot) != 0u) {
                /* Timer still ticking — return without further work.
                 * Outer UpdateMetaObject post-call check below would
                 * see the unchanged metastate and still test it for
                 * end-state, so we fall through. */
            } else {
                ENEMY_MOVE_TIMER(slot) = 0x06u;
                ENEMY_METASTATE(slot) = (unsigned char)(ms + 1u);
                ms = (unsigned char)(ms + 1u);
            }
        }
    } else {
        /* Phase D 2026-05-15: publish cloud frame to native cache so the
         * spawning-enemy cloud puff is visible. NES Z_07.asm:4912
         * DrawCloud writes via Anim_WriteItemSprites Y=$01. */
        enemy_render_publish_meta(slot);
        if (ENEMY_MOVE_TIMER(slot) != 0u) {
            /* Same fall-through as spark path. */
        } else {
            ENEMY_MOVE_TIMER(slot) = 0x06u;
            ENEMY_METASTATE(slot) = (unsigned char)(ms + 1u);
            ms = (unsigned char)(ms + 1u);
        }
    }

    /* UpdateMetaObject post-call — check end metastate ($04 or $14).
     * NES Z_07.asm:5407-5410:
     *   LDA ObjMetastate, X
     *   AND #$0F
     *   CMP #$04
     *   BCS UpdateMetaObjectEnd
     */
    if ((unsigned char)(ms & 0x0Fu) < 0x04u) return;

    /* UpdateMetaObjectEnd (NES Z_07.asm:5414).
     *
     *   LDA ObjMetastate, X
     *   AND #$10
     *   BEQ @Reset            ; metastate $04 — done with cloud, reset
     *
     * Otherwise metastate is $14 (death-spark complete) — convert the
     * slot into a dropped item.
     */
    if ((unsigned char)(ms & 0x10u) == 0u) {
        /* @Reset: metastate $04 — reset metastate so slot is ready to
         * tick autonomously. (Cloud-end path; spawning monster.) */
        ENEMY_METASTATE(slot) = 0u;
        return;
    }

    /* Metastate $14 — drop conversion path.
     *
     * Copy ObjType to Item_ObjMonsterType so SetUpDroppedItem can
     * compute the drop. Three monster types (RupeeStash $5D,
     * ChildGel $14, RedKeese $1C) skip the world-kill-cycle bump
     * and let SetUpDroppedItem handle them.
     */
    {
        unsigned char obj_type = (unsigned char)ENEMY_TYPE(slot);
        unsigned char skip_kill_cycle = 0u;

        /* NES UpdateMetaObjectEnd applies the same kill accounting and
         * drop conversion to bosses. An early alive-flag clear leaves the
         * boss type/OAM stale and prevents the room-clear secret. */
        META_ITEM_MONSTER_TYPE(slot) = obj_type;

        if (obj_type == 0x5Du || obj_type == 0x14u || obj_type == 0x1Cu) {
            skip_kill_cycle = 1u;
        }

        if (skip_kill_cycle == 0u) {
            unsigned char cycle = (unsigned char)META_WORLD_KILL_CYCLE;
            cycle = (unsigned char)(cycle + 1u);
            if (cycle == 0x0Au) cycle = 0u;
            META_WORLD_KILL_CYCLE = cycle;

            /* Skip RoomKillCount bump for Zora ($11). NES UpdateMetaObjectEnd
             * (Z_07.asm:5453) writes RoomKillCount at $034F, NOT
             * WorldKillCount at $0627. The two are different counters —
             * combat_handle_monster_died (combat_dispatch.c:30) handles
             * the WorldKillCount bump separately. */
            if (obj_type != 0x11u) {
                ROOM_OW_CUR_KILL_TOTAL =
                    (unsigned char)(ROOM_OW_CUR_KILL_TOTAL + 1u);
            }
        }

        /* @DropItem: convert slot to dropped-item type ($60), then run
         * SetUpDroppedItem to pick the item id (or destroy if no drop). */
        ENEMY_TYPE(slot)       = 0x60u;
        ENEMY_ALIVE_FLAG(slot) = 0x60u;  /* STA ObjUninitialized: uninitialized */
        META_OBJ_ATTR(slot)    = 0x81u;

        /* SetUpDroppedItem (Z_04.asm:11103). Returns 0 if slot was
         * destroyed (no-drop type or random cancel) — fall through to
         * @Reset which is a no-op for cleared slots. */
        (void)native_set_up_dropped_item(slot);
    }

    /* @Reset path always runs after drop conversion. */
    ENEMY_METASTATE(slot) = 0u;
}

/* NES Z_04.asm:3332 DrawArmosAndCheckCollisions. Phase 7 Task 7.4
 * step 6b helper. Stance: ADOPT — verbatim transcription of NES body.
 *
 * NES sequence:
 *   Anim_FetchObjPosForSpriteDescriptor.
 *   if ObjDir == $08 (up):  frame_base = 1.
 *   else                  : frame_base = 0  (the NES code skips the LDA #$01
 *                                            via BNE; carry path uses frame_base 0
 *                                            but adds ObjAnimFrame, which is the
 *                                            "down/front" base).
 *
 *   Note: NES code has a subtle "default" — when ObjDir != $08 it falls
 *   through with whatever A held (initialized to $00). The visible
 *   behavior is: facing-up uses frame 1+ObjAnimFrame ("back"), other
 *   facings use frame 0+ObjAnimFrame ("front"). DrawObjectNotMirrored
 *   handles horizontal flip at the dispatch backend.
 *
 *   DrawObjectNotMirrored(frame).
 *   if ObjTimer != 0: jmp CheckLinkCollision (only — fading-in armos
 *                     don't take weapon damage yet).
 *   else:             CheckMonsterCollisions.
 *                     if ObjMetastate != 0: ObjType = $5D (DeadDummy).
 */
static void armos_draw_and_check_collisions(unsigned int slot)
{
    /* NES DrawArmosAndCheckCollisions: Anim_FetchObjPosForSpriteDescriptor
     * only; UpdateArmos animates before jumping here (T-147: the advance
     * ran the counter down while the armos faded in, t129 t4090). */
    (void)sprite_anim_fetch_obj_pos(slot);

    {
        unsigned char frame_base = ((unsigned char)ENEMY_DIR(slot) == 0x08u) ? 1u : 0u;
        unsigned char frame = (unsigned char)(frame_base + (unsigned char)ENEMY_DRAW_FRAME(slot));
        draw_object_not_mirrored_with_frame(frame, slot);
    }

    if ((unsigned char)ENEMY_MOVE_TIMER(slot) != 0u) {
        link_collision_check_link_collision(slot);
        return;
    }
    link_collision_check_monster_collisions(slot);
    if ((unsigned char)ENEMY_METASTATE(slot) != 0u) {
        ENEMY_TYPE(slot) = 0x5Du;     /* DeadDummy */
    }
}

/* NES Z_04.asm:3302 UpdateArmos. Phase 7 Task 7.4 step 6b native drain.
 * Stance: ADOPT — verbatim transcription.
 *
 * NES sequence:
 *   1. UpdateGoriya (already drained: enrt_update_goriya). Note armos
 *      shares goriya AI but UpdateGoriya special-cases type $1E to skip
 *      the shoot-delay early-out (enemy_wanderer_runtime.c:176).
 *   2. If ObjShoveDir != 0: jmp DrawArmosAndCheckCollisions.
 *   3. Decrement ObjAnimCounter; if non-zero: jmp DrawArmosAndCheckCollisions.
 *   4. ObjAnimCounter = $06.  ObjAnimFrame ^= $02  (advance to next pose pair).
 *   5. fall through to DrawArmosAndCheckCollisions.
 *
 * Cell map:
 *   ObjShoveDir    = ENEMY_OBJ_SHOVE_DIR ($00C0).
 *   ObjAnimCounter = ENEMY_ANIM_TIMER ($03B5 / aliased).
 *   ObjAnimFrame   = ENEMY_DRAW_FRAME ($03E4).
 */
void enrt_update_armos(unsigned int slot)
{
    enrt_update_goriya(slot);

    if ((unsigned char)OBJ(NES_OBJ_SHOVE_DIR, slot) != 0u) {
        armos_draw_and_check_collisions(slot);
        return;
    }

    {
        unsigned char anim = (unsigned char)(ENEMY_ANIM_TIMER(slot) - 1u);
        ENEMY_ANIM_TIMER(slot) = anim;
        if (anim != 0u) {
            armos_draw_and_check_collisions(slot);
            return;
        }
    }

    ENEMY_ANIM_TIMER(slot) = 0x06u;
    ENEMY_DRAW_FRAME(slot) =
        (unsigned char)((unsigned char)ENEMY_DRAW_FRAME(slot) ^ 0x02u);
    armos_draw_and_check_collisions(slot);
}

/* NES Z_04.asm:3144 SecretArmosRoomIds (7 bytes). */
static const unsigned char k_secret_armos_room_ids[7] = {
    0x24u, 0x0Bu, 0x1Cu, 0x22u, 0x34u, 0x3Du, 0x4Eu
};

/* NES Z_04.asm:3147 SecretArmosXs (7 bytes). */
static const unsigned char k_secret_armos_xs[7] = {
    0xE0u, 0xB0u, 0xB0u, 0x30u, 0x40u, 0x90u, 0xA0u
};

/* NES Z_04.asm:3150 InitArmosOrFlyingGhini. Phase 7 Task 7.4 step 6c
 * native drain. Stance: EXTEND — no oracle drain in src/oracle/enemies/.
 *
 * Sequence (NES asm summary):
 *   1. ObjUninitialized = ObjTimer (gate-flag for fade-in path).
 *   2. If ObjTimer != 0: jmp @FinishInit.
 *   3. If ObjType == $22 (FlyingGhini): jmp @FinishInit.
 *   4. Else (Armos $1E): scan SecretArmosRoomIds[6..0]:
 *        if RoomId match && ObjX match && ObjY == $80:
 *          if Y == 0: special bracelet path
 *            ObjY[19] = $80, ObjX[19] = ObjX, ObjState[19] = 0
 *            RoomItemId ($98+19 = $00AB) = $14 (PowerBracelet)
 *            if !room_flag_uw_item_state: PlaySecretFoundTune
 *            jmp @UseFloorTile (tile = $26)
 *          else: jmp @UseChosenTile (tile = $70 stairs)
 *        no match: tile = $26 floor.
 *      @UseChosenTile: if tile == $70: PlaySecretFoundTune.
 *      ReturnToBank4 = 1; ChangeTileObjTiles(tile, slot).
 *      ObjGridOffset[slot] = 3.
 *      ObjQSpeedFrac[slot] = $20 if RNG_A < $80 else $60.
 *   5. @FinishInit:
 *      ObjInputDir = $04, ObjDir = $04 (down).
 *      If ObjTimer & 1: skip draw (return without drawing this frame).
 *      Else if ObjType == $22: jmp L_EndInitFlyingGhini.
 *      Else: DrawArmosAndCheckCollisions; return.
 *   6. L_EndInitFlyingGhini:
 *      DrawGhiniAndCheckCollisions.
 *      If ObjUninitialized != 0: return (still fading).
 *      ResetObjMetastateAndTimer; EndInitFlyer.
 */
void enrt_init_armos_or_flying_ghini(unsigned int slot)
{
    /* Step 1: latch ObjTimer into ObjUninitialized (= ENEMY_ALIVE_FLAG). */
    const unsigned char timer = (unsigned char)ENEMY_MOVE_TIMER(slot);
    ENEMY_ALIVE_FLAG(slot) = timer;

    /* Step 2: still fading in -> skip to @FinishInit. */
    if (timer != 0u) {
        goto FinishInit;
    }

    {
        const unsigned char obj_type = (unsigned char)ENEMY_TYPE(slot);

        /* Step 3: FlyingGhini after fade -> skip armos secret logic. */
        if (obj_type == 0x22u) {
            goto FinishInit;
        }

        /* Step 4: Armos secret-room scan. */
        unsigned char chosen_tile = 0x70u;        /* default: stairs. */
        unsigned char matched = 0u;

        const unsigned char room_id = (unsigned char)RAM(NES_CUR_ROOM_ID);
        const unsigned char obj_x = (unsigned char)ENEMY_X(slot);
        const unsigned char obj_y = (unsigned char)ENEMY_Y(slot);

        signed char y = 6;
        while (y >= 0) {
            if (k_secret_armos_room_ids[y] == room_id &&
                k_secret_armos_xs[y] == obj_x &&
                obj_y == 0x80u) {
                matched = 1u;
                if (y == 0) {
                    /* Bracelet path: stage room item slot 19. */
                    ENEMY_Y(19u)            = 0x80u;
                    ENEMY_X(19u)            = obj_x;
                    ENEMY_STATE_TIMER(19u)  = 0u;       /* ObjState[19] = 0. */
                    OBJ(NES_OBJ_FLAG_BASE, 19u) = 0x14u;/* RoomItemId = PowerBracelet. */
                    if (progress_get_room_flag_uw_item_state() == 0u) {
                        enemy_play_secret_found_tune();
                    }
                    chosen_tile = 0x26u;                /* @UseFloorTile. */
                }
                /* y > 0 falls through with chosen_tile = $70 (stairs). */
                break;
            }
            y = (signed char)(y - 1);
        }
        if (!matched) {
            chosen_tile = 0x26u;                        /* @UseFloorTile. */
        }

        /* @UseChosenTile: PlaySecretFoundTune if stairs. */
        if (chosen_tile == 0x70u) {
            enemy_play_secret_found_tune();
        }

        /* INC ReturnToBank4 — flag for ChangeTileObjTiles. */
        RAM(0x00F7u) = (uint8_t)(RAM(0x00F7u) + 1u);
        dyn_tile_change_tile_obj_tiles(chosen_tile, slot);

        /* ObjGridOffset = 3 (square-grid alignment compensation). */
        OBJ(NES_OBJ_GRID_OFFSET, slot) = 0x03u;

        /* Q-speed: $20 if RNG_A < $80 else $60. */
        /* NES LDY Random+1,X (T-147: read Random+0,X). */
        ENEMY_WALK_SPEED(slot) =
            ((unsigned char)ENEMY_RNG_B(slot) < 0x80u) ? 0x20u : 0x60u;
    }

FinishInit:
    /* Facing + input dirs = down ($04). */
    ENEMY_PUSH_DIR_SCRATCH(slot) = 0x04u;       /* ObjInputDir. */
    ENEMY_DIR(slot)              = 0x04u;

    /* Every other frame: skip drawing (LSR + BCS in NES). */
    {
        const unsigned char tmr = (unsigned char)ENEMY_MOVE_TIMER(slot);
        if ((tmr & 1u) != 0u) {
            return;                              /* Carry set -> skip draw. */
        }
    }

    if ((unsigned char)ENEMY_TYPE(slot) == 0x22u) {
        /* L_EndInitFlyingGhini. */
        enrt_draw_ghini_and_check_collisions(slot);
        if ((unsigned char)ENEMY_ALIVE_FLAG(slot) != 0u) {
            return;                              /* still fading. */
        }
        core_reset_obj_metastate_and_timer(slot);
        enrt_end_init_flyer(slot);
        return;
    }

    /* Armos terminal: draw + check collisions. */
    armos_draw_and_check_collisions(slot);
}

/* Phase 7 Task 7.4 step 8 — UpdateGuardFire (NES Z_04.asm:9684).
 *
 * Drain Rule D1 EXTEND. No oracle drain candidate for UpdateGuardFire
 * in src/oracle/. NES body is 6 instructions:
 *   LDA #$06 / JSR AnimateAndDrawCommonObject
 *   JSR CheckMonsterCollisions
 *   LDA ObjMetastate, X / BEQ exit
 *   LDA #$5D / STA ObjType, X
 *   RTS
 *
 * Animation rate = 6 (slower than standing-fire which uses
 * z07_animate_object_walking). On kill (metastate != 0), convert
 * to $5D dead dummy. Used for $3F GuardFire dispatch row. */
void enrt_update_guard_fire(unsigned int slot)
{
    enrt_animate_and_draw_common_object(6u, slot);
    c_check_monster_collisions(slot);
    if ((unsigned char)ENEMY_METASTATE(slot) != 0u) {
        ENEMY_TYPE(slot) = 0x5Du;       /* DeadDummy. */
    }
}

/* NES source: Z_04.asm:InitZelda / UpdateZelda (9481, 9617).
 * Drained C: none; this bridge owns Zelda's room object and ending handoff.
 * Coverage: PARTIAL (Zelda encounter through Mode 13 entry).
 * Stance: EXTEND the existing object dispatch and Link-state boundary. */
void enrt_init_zelda(unsigned int slot)
{
    static const unsigned char xs[5] = { 0x78u, 0x60u, 0x70u, 0x80u, 0x90u };
    static const unsigned char ys[5] = { 0x88u, 0xB5u, 0x9Du, 0x9Du, 0xB5u };
    unsigned int i;
    (void)slot;
    for (i = 1u; i <= 5u; ++i) {
        ENEMY_X(i) = xs[i - 1u];
        ENEMY_Y(i) = ys[i - 1u];
        ENEMY_TYPE(i) = 0x3Fu;
    }
    ENEMY_TYPE(1u) = 0x37u;
}

void enrt_update_zelda(unsigned int slot)
{
    (void)sprite_anim_fetch_obj_pos(slot);
    draw_object_mirrored(0u, slot); /* Person_Draw, no combat collision. */
    if ((unsigned char)ENEMY_STATE_TIMER(slot) == 0u) {
        unsigned char x = (unsigned char)ENEMY_PLAYER_OBJ_X;
        if (x < 0x70u || x >= 0x81u ||
            (unsigned char)ENEMY_PLAYER_OBJ_Y != 0x95u) return;
        ENEMY_STATE_TIMER(slot) = 1u;
        RAM(NES_OBJ_STATE_BASE) = 0x40u;
        roomrom_main_set_link_story_pose(0x88u, 0x88u,
                                         ROOMROM_MAIN_LINK_FACE_LEFT);
        RAM(0x0600u) = 0x06u; /* Zelda fanfare request. */
        ENEMY_MOVE_TIMER(slot) = 0x80u;
        return;
    }
    /* Link_EndMoveAndDraw_Bank4: animation counter 6 (no animation), then
     * Link_EndMoveAndAnimate through the Link owner (T-171). */
    RAM(0x03D0u) = 0x06u;
    roomrom_main_link_end_move_from_object();
    if ((unsigned char)ENEMY_MOVE_TIMER(slot) != 0u) return;
    RAM(0x0011u) = 0u; /* IsUpdatingMode */
    RAM(NES_SUB_MODE) = 0u;
    RAM(NES_OBJ_STATE_BASE) = 0u;
    RAM(NES_GAME_MODE) = 0x13u;
    ENEMY_X(12u) = 0x20u;
    ENEMY_X(13u) = 0x01u;
    RAM(0x000Au) = 0x24u; /* FillTileMap blank tile selector. */
    world_fill_tile_map();  /* JMP FillTileMap (T-171) */
}
