#include "enemy_runtime_private.h"
#include "legacy_bridge.h"

void enrt_hide_sprites_over_link(void) {
    ENEMY_OAM_HIDE_0 = 0xF8;
    ENEMY_OAM_HIDE_1 = 0xF8;
}

void enrt_play_secret_found_tune(void) {
    ENEMY_SFX_SECRET = 4;
}

void enrt_play_boss_death_cry(void) {
    ENEMY_SFX_BOSS_CRY = 2;
    ENEMY_SFX_BOSS_CRY_FLAGS = 0x80;
}

void enrt_gohma_play_parry_tune(void) {
    ENEMY_SFX_PARRY = 1;
}

void enrt_update_gibdo(unsigned int slot) {
    enrt_update_common_wanderer(0x80u, slot);
    c_check_monster_collisions(slot);
    z07_anim_advance_and_fetch(8, slot);
    z07_anim_set_obj_hflip(slot);
    c_draw_object_not_mirrored_with_frame(0, slot);
}

/* ---- Plan C: drained from z_04 (Zol + Gel family) --------------------- */

/* RoomObjCount — bumped when Zol splits so the kill-bookkeeping stays
 * balanced after the parent destroys itself but spawns two children.
 */
#define ROOM_OBJ_COUNT RAM(0x034E)

/* ZolGelDelays — index by (RNG & 3) for Zol, +4 for Gel. */
static const unsigned char enrt_zol_gel_delays[8] = {
    0x18, 0x28, 0x38, 0x48,
    0x08, 0x18, 0x28, 0x38
};

/* Forward declarations for state handlers / helpers. */
void          enrt_update_zol_state(unsigned int slot);
void          enrt_zol_check_collisions(unsigned int slot);
void          enrt_gel_move(unsigned int slot);
void          enrt_gel_check_collisions(unsigned int slot);
unsigned int  enrt_gel_move_splitting(unsigned int slot);
void          enrt_update_normal_zol_or_gel(unsigned char qspeed, unsigned int slot);
static void   enrt_update_zol_state_0_wander(unsigned int slot);
static void   enrt_update_zol_state_1_shove(unsigned int slot);
static void   enrt_update_zol_state_2_split(unsigned int slot);
static unsigned int enrt_create_child_gel(unsigned int slot);

/* UpdateZolState — dispatch on state timer (low 2 bits, like the
 * original 3-entry jump table; behaviour for state 3 is undefined in
 * the NES code so we keep it as a no-op).
 */
void enrt_update_zol_state(unsigned int slot) {
    unsigned char state = ENEMY_STATE_TIMER(slot);
    switch (state) {
        case 0: enrt_update_zol_state_0_wander(slot); break;
        case 1: enrt_update_zol_state_1_shove(slot); break;
        case 2: enrt_update_zol_state_2_split(slot); break;
        default: break;
    }
}

/* State 0: Wander like a normal slow Zol (qspeed = 24, ~3/8 px/frame). */
static void enrt_update_zol_state_0_wander(unsigned int slot) {
    enrt_update_normal_zol_or_gel(24, slot);
}

/* State 1: Big shove — move straight at full speed; if blocked, advance
 * to state 2 (split).
 */
static void enrt_update_zol_state_1_shove(unsigned int slot) {
    if (enrt_gel_move_splitting(slot) & CARRY_SET) {
        ENEMY_STATE_TIMER(slot) = (unsigned char)(ENEMY_STATE_TIMER(slot) + 1);
    }
}

/* State 2: Destroy the parent Zol and spawn two child Gels with
 * opposing directions. Counts: +1 to RoomObjCount because we lose 1
 * (the Zol) and gain 2 (the Gels).
 */
static void enrt_update_zol_state_2_split(unsigned int slot) {
    ROOM_OBJ_COUNT = (unsigned char)(ROOM_OBJ_COUNT + 1);
    z07_destroy_monster(slot);

    unsigned int child1 = enrt_create_child_gel(slot);

    /* If the Zol's direction was vertical (>= 4 i.e. UP/DOWN bits),
     * the first child gets LEFT (2); otherwise UP (8).
     */
    unsigned char zol_dir = ENEMY_DIR(slot);
    unsigned char first_dir = (zol_dir >= 0x04) ? 2u : 8u;
    ENEMY_DIR(child1) = first_dir;

    unsigned int child2 = enrt_create_child_gel(slot);
    /* Second child gets the opposite of the first (LSR by 1: 2->1,
     * 8->4). 1 = RIGHT, 4 = DOWN.
     */
    ENEMY_DIR(child2) = (unsigned char)(first_dir >> 1);
}

/* CreateChildGel — uses ShootLimited to spawn a child of type 20 (Gel),
 * resets it into state 0 (instead of the default shot state $10), and
 * inherits the parent's grid offset. Returns the new child slot.
 */
static unsigned int enrt_create_child_gel(unsigned int slot) {
    /* NES LDA #$14 / STA $00: the shot type goes in [00]; the parent
     * Zol keeps its own type (T-147). */
    RAM(0x0000u) = 0x14u;
    unsigned int result = c_shoot_limited(slot);
    unsigned int child = result & 0xFFu;

    /* New child starts in wander state and inherits grid offset. */
    ENEMY_STATE_TIMER(child) = 0;
    OBJ(NES_OBJ_GRID_OFFSET, child) = OBJ(NES_OBJ_GRID_OFFSET, slot);
    return child;
}

/* Zol_CheckCollisions — only meaningful in state 0; if hit hard enough
 * to require a state change, decide between "shove" (state 1) and
 * "immediate split" (state 2) based on facing-vs-grid alignment.
 */
void enrt_zol_check_collisions(unsigned int slot) {
    if (ENEMY_STATE_TIMER(slot) != 0)
        return;
    enrt_gel_check_collisions(slot);
    if (ENEMY_METASTATE(slot) != 0)
        return;
    if (ENEMY_HIT_REACTION(slot) == 0)
        return;

    /* Vertical-grid alignment: low nibble of Y is $D → use horizontal
     * direction mask (3 = LEFT|RIGHT bits).
     */
    unsigned char y_align_mask = ((ENEMY_Y(slot) & 0x0F) == 0x0D) ? 3u : 0u;
    /* Horizontal-grid alignment: low nibble of X is 0 → use vertical
     * direction mask ($C = UP|DOWN bits).
     */
    unsigned char x_align_mask = ((ENEMY_X(slot) & 0x0F) == 0x00) ? 0x0Cu : 0u;
    unsigned char align_mask = (unsigned char)(y_align_mask | x_align_mask);

    /* If the facing direction lines up with one of the grid axes, then
     * advance to state 1 (big shove); otherwise advance straight to
     * state 2 (split immediately). The original encodes this by always
     * doing one inc, plus a second inc when the AND is zero.
     */
    if ((ENEMY_DIR(slot) & align_mask) == 0) {
        ENEMY_STATE_TIMER(slot) = (unsigned char)(ENEMY_STATE_TIMER(slot) + 1);
    }
    ENEMY_STATE_TIMER(slot) = (unsigned char)(ENEMY_STATE_TIMER(slot) + 1);
}

/* Gel_Move — three-state move dispatcher used by Gels. */
void enrt_gel_move(unsigned int slot) {
    unsigned char state = ENEMY_STATE_TIMER(slot);
    if (state == 0) {
        /* State 0: kick off into state 1 with qspeed $20 (1/2 px/frame)
         * and a 5-frame timer.
         */
        ENEMY_WALK_SPEED(slot) = 32;
        ENEMY_MOVE_TIMER(slot) = 5;
        ENEMY_STATE_TIMER(slot) = (unsigned char)(ENEMY_STATE_TIMER(slot) + 1);
        return;
    }
    if (state == 1) {
        if (ENEMY_MOVE_TIMER(slot) != 0) {
            /* Timer not expired — try to move. If not blocked, return
             * (we'll keep gliding next frame).
             */
            if ((enrt_gel_move_splitting(slot) & CARRY_SET) == 0)
                return;
            /* Blocked — fall through to grid-snap + advance. */
        }

        /* Snap to nearest 16-px grid (X = (X+8)&$F0, Y = ((Y+8)&$F0)|$D),
         * clear grid offset, and advance to state 2.
         */
        ENEMY_X(slot) = (unsigned char)(((ENEMY_X(slot) + 8u) & 0xF0u));
        ENEMY_Y(slot) = (unsigned char)((((ENEMY_Y(slot) + 8u) & 0xF0u) | 0x0Du));
        OBJ(NES_OBJ_GRID_OFFSET, slot) = 0;
        ENEMY_STATE_TIMER(slot) = (unsigned char)(ENEMY_STATE_TIMER(slot) + 1);
        return;
    }
    /* state 2: standard wandering with qspeed $40 (1 px/frame). */
    enrt_update_normal_zol_or_gel(64, slot);
}

/* UpdateNormalZolOrGel — common Zol/Gel idle wandering: set walking
 * speed, target the player, and pick the next move-timer delay from
 * the ZolGelDelays table.
 */
void enrt_update_normal_zol_or_gel(unsigned char qspeed, unsigned int slot) {
    ENEMY_WALK_SPEED(slot) = qspeed;
    if (ENEMY_MOVE_TIMER(slot) >= 5)
        return;

    ENEMY_AIR_SPEED(slot) = 32;
    c_wanderer_target_player(slot);

    /* If between squares (grid offset != 0) or move timer is non-zero,
     * skip the new-delay assignment.
     */
    if ((OBJ(NES_OBJ_GRID_OFFSET, slot) | ENEMY_MOVE_TIMER(slot)) != 0)
        return;

    /* Pick a delay table index from the slot's RNG byte; Gels use the
     * second half of the table (+4 vs Zol).
     */
    unsigned int idx = ENEMY_RNG_A(slot) & 0x03u;
    if (ENEMY_TYPE(slot) != 0x13)   /* 0x13 = Zol */
        idx += 4;
    ENEMY_MOVE_TIMER(slot) = enrt_zol_gel_delays[idx];
}

/* Gel_MoveSplitting — sets max walking speed, locks moving direction
 * to the facing direction, then runs the standard tile-block /
 * room-bound / move sequence.
 *
 * Returns CARRY_SET (== 0x100) if blocked (by a tile or the room
 * boundary), 0 if it moved freely.
 */
unsigned int enrt_gel_move_splitting(unsigned int slot) {
    ENEMY_WALK_SPEED(slot) = 0xFF;
    /* Copy facing direction → global moving-direction cell ($000F). */
    RAM(NES_OBJ_DIR) = ENEMY_DIR(slot);

    /* If sitting at a tile boundary (grid offset == 0), check the next
     * tile we'd step onto.
     */
    if (OBJ(NES_OBJ_GRID_OFFSET, slot) == 0) {
        unsigned char tile = z07_get_colliding_tile_moving(slot);
        unsigned char floor = ENEMY_DUNGEON_TILE_FLOOR;  /* dungeon tile floor */
        /* CMP D1,D0; bcc → 6502 carry set when D0 (tile) >= D1 (floor),
         * which means "blocked" → return CARRY_SET.
         */
        if (tile >= floor)
            return CARRY_SET;
    }

    /* Room-edge check: returns 0 in D0/RAM[$0F] if blocked. */
    if (z01_bound_by_room(slot) == 0)
        return CARRY_SET;

    /* Free to move. */
    c_move_object((unsigned short)slot);

    /* Mask grid offset back into 0..$0F if it's snapped to a tile. */
    unsigned char off = OBJ(NES_OBJ_GRID_OFFSET, slot);
    unsigned char masked = (unsigned char)(off & 0x0Fu);
    if (masked == 0) {
        OBJ(NES_OBJ_GRID_OFFSET, slot) = 0;
    }
    return 0;
}

/* Gel_CheckCollisions — write current X/Y into the scratch cells the
 * collision routines read, then tail-call into CheckMonsterCollisions.
 *
 * The NES comment notes that GetObjectMiddle (called by
 * CheckMonsterCollisions) overwrites those scratch cells immediately,
 * but we mirror the original behaviour exactly.
 */
void enrt_gel_check_collisions(unsigned int slot) {
    RAM(NES_SCRATCH_2) = ENEMY_X(slot);
    RAM(NES_SCRATCH_3) = ENEMY_Y(slot);
    c_check_monster_collisions(slot);
}
