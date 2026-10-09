#include "enemy_runtime_private.h"
#include "legacy_bridge.h"
#include "combat_state.h"
#include "room_state.h"
#include "sprite_state.h"

void enrt_dodongo_dec_bloated_timer(unsigned int slot) {
    ENEMY_BLOATED_TIMER(slot)--;
}

void enrt_update_dodongo_state2_stunned(unsigned int slot) {
    unsigned char timer = ENEMY_STUN_TIMER(slot);
    if (timer == 1) {
        z04_update_dodongo_bloated_sub_end(slot);
        return;
    }
    /* LDY / DEY / BPL: 0 (or >= $81) reloads $20 (T-171). */
    if ((unsigned char)(timer - 1u) & 0x80u)
        ENEMY_STUN_TIMER(slot) = 32;
}

void enrt_init_dodongo(unsigned int slot) {
    ENEMY_SFX_BOSS_CRY = 32;
    ENEMY_DIR(slot) = (ENEMY_RNG_A(slot) < 0x80) ? 1u : 2u;
}

void enrt_update_dodongo_state1_bloated_sub_die(unsigned int slot) {
    z07_update_dead_dummy(slot);
    enrt_play_boss_death_cry();
    enrt_update_dodongo_bloated_sub_end(slot);
}

void enrt_update_dodongo_bloated_sub_end(unsigned int slot) {
    z07_reset_obj_state(slot);
    ENEMY_TURN_TIMER(slot) = 0;
}

/*====================================================================*
 * Dodongo family (drained from z_04.asm:6001+). The Dodongo boss
 * logic — collisions with the player's weapons, eating bombs (bloat
 * state machine), and the draw routine that picks frame images from
 * per-direction tables. Mouth/bomb hotspot limits live here as
 * ROM-equivalent constant tables.
 *====================================================================*/

/* Left-side mouth limits keyed by (direction >> 1).
 * 5-way direction index: 0=right, 1=left, 2=down, 3=unused, 4=up. */
static const unsigned char DodongoMouthNegativeLimits0[] = {
    0xF0, 0x00, 0xF8, 0xFF, 0xF8
};
static const unsigned char DodongoMouthPositiveLimits0[] = {
    0x00, 0x10, 0x08, 0xFF, 0x08
};
static const unsigned char DodongoMouthNegativeLimits1[] = {
    0xFC, 0xFC, 0xF0, 0xFF, 0x00
};
static const unsigned char DodongoMouthPositiveLimits1[] = {
    0x04, 0x04, 0x00, 0xFF, 0x10
};

/* Bomb hotspot limits indexed by D3 (0=dust cloud, 1=bomb). */
static const unsigned char DodongoBombPositiveLimits[] = { 0x0C, 0x11 };
static const unsigned char DodongoBombNegativeLimits[] = { 0xF4, 0xF0 };

/* 5-way direction index -> frame-image number.
 * Two animation frames: 0..4 and 5..9 (index 3 is unused/FF). */
static const unsigned char DodongoFrameImages[] = {
    0x00, 0x01, 0x06, 0xFF, 0x08,
    0x02, 0x03, 0x06, 0xFF, 0x08
};
static const unsigned char DodongoFrameHFlips[] = {
    0x00, 0x40, 0x00, 0xFF, 0x00,
    0x00, 0x40, 0x40, 0xFF, 0x40
};
static const unsigned char DodongoFrameImagesBloated[] = {
    0x04, 0x05, 0x07, 0xFF, 0x09,
    0x04, 0x05, 0x07, 0xFF, 0x09
};
static const unsigned char DodongoFrameHFlipsBloated[] = {
    0x00, 0x40, 0x00, 0xFF, 0x00,
    0x00, 0x40, 0x00, 0xFF, 0x00
};

/*--------------------------------------------------------------------
 * Dodongo_CheckCollisionsStandardSize (drained from z_04.asm:6052)
 *
 * Sets full invincibility ($FF), runs the shared monster-collision
 * pass, and — only when the Dodongo is stunned (state 2) — re-runs a
 * sword-only collision at the object middle. Damage counts 13 (bomb
 * shots inflict the sword-damage bit).
 *------------------------------------------------------------------*/
void enrt_dodongo_check_collisions_standard_size(unsigned int slot) {
    ENEMY_INVINCIBILITY(slot) = 0xFF;
    c_check_monster_collisions(slot);
    if (ENEMY_STATE_TIMER(slot) != 2)
        return;
    /* Stunned: allow sword hits only. */
    ENEMY_INVINCIBILITY(slot) = 0xFE;
    c_get_object_middle(slot);
    c_check_monster_sword_collision(slot, 13);
}

/*--------------------------------------------------------------------
 * Dodongo_CheckCollisions (drained from z_04.asm:6001)
 *
 * Runs the standard-size collision pass. If the Dodongo wasn't hurt
 * and it is facing horizontally (direction < 4), shifts the hitbox
 * right by $10 pixels and runs the pass again so both halves of the
 * long sprite can take hits. If either pass hurt it, dies via the
 * bloated sub-die path and drops a 10-bomb/10-rupee stash.
 *------------------------------------------------------------------*/
void enrt_dodongo_check_collisions(unsigned int slot) {
    enrt_dodongo_check_collisions_standard_size(slot);
    if (ENEMY_HIT_REACTION(slot) != 0)
        goto die;

    /* Vertical orientation (dir >= 4): no right-half recheck, return. */
    if (ENEMY_DIR(slot) >= 4)
        return;

    /* Save X, shift right $10, re-run, restore X. */
    {
        unsigned char saved_x = ENEMY_X(slot);
        ENEMY_X(slot) = (unsigned char)(saved_x + 0x10);
        enrt_dodongo_check_collisions_standard_size(slot);
        ENEMY_X(slot) = saved_x;
    }
    if (ENEMY_HIT_REACTION(slot) == 0)
        return;

die:
    enrt_update_dodongo_state1_bloated_sub_die(slot);
    /* Drop bomb-slot counters at RAM[$50]/[$51] = 10 (NES meta). */
    ROOM_CHAIN_KILL_COUNT = 10;
    ROOM_CHAIN_KILL_BONUS = 10;
}

/*--------------------------------------------------------------------
 * Dodongo_IsBombInRange (drained from z_04.asm:6270)
 *
 * Checks a bomb/dust-cloud hotspot against the monster's hotspot
 * stored at RAM[0]/[1] vs RAM[2]/[3]. Indexed by limit_idx (0=dust
 * cloud, 1=live bomb).
 *
 * Returns 0 when the bomb is in range on both axes (D0=0/Z=1 in the
 * NES convention); RAM[4]=dx, RAM[5]=dy when in-range on that axis.
 *------------------------------------------------------------------*/
unsigned int enrt_dodongo_is_bomb_in_range(unsigned int limit_idx) {
    unsigned char pos_limit = DodongoBombPositiveLimits[limit_idx & 1];
    unsigned char neg_limit = DodongoBombNegativeLimits[limit_idx & 1];
    unsigned char mask = 3;         /* bit 0 = Y close, bit 1 = X close */
    int axis;

    ENEMY_COLLISION_FLAG = pos_limit;
    COMBAT_DAMAGE_AMOUNT = neg_limit;
    COMBAT_SHOVE_DIR = mask;

    /* axis=1 first (Y), then axis=0 (X) — matches 6502 LDY #1 / dey loop.
     * Scratch layout: RAM[0],[1] = monster X/Y; RAM[2],[3] = bomb X/Y. */
    for (axis = 1; axis >= 0; axis--) {
        unsigned char monster = RAM(0x0000 + axis);
        unsigned char bomb    = RAM(0x0002 + axis);
        signed char  dist     = (signed char)(monster - bomb);

        if ((signed char)dist >= (signed char)pos_limit)
            continue;
        if ((signed char)dist <  (signed char)neg_limit)
            continue;

        /* In-range on this axis. */
        RAM(0x0004 + axis) = (unsigned char)dist;
        mask = (unsigned char)(mask >> 1);
        COMBAT_SHOVE_DIR = mask;
    }
    /* mask == 0 means in-range on both axes; return it as the A reg. */
    return (unsigned int)COMBAT_SHOVE_DIR;
}

/*--------------------------------------------------------------------
 * Dodongo_TryEatBomb (drained from z_04.asm:6168)
 *
 * Precondition: bomb is close enough for the coarse test. Checks
 * whether the bomb is near the mouth — the mouth position depends on
 * the Dodongo's orientation. On a match, advances to bloated state
 * (state_timer++), deactivates the bomb, and resets bloated substate.
 *
 * The 6502 version uses a tiny 1→0 loop over the two axes: first
 * horizontal, then vertical. For each axis it looks up signed
 * [neg_limit, pos_limit) for the 5-way direction index.
 *------------------------------------------------------------------*/
void enrt_dodongo_try_eat_bomb(unsigned int slot) {
    unsigned char dir_idx;
    int loop_counter;
    unsigned char dist;
    const unsigned char *neg_table = DodongoMouthNegativeLimits0;
    const unsigned char *pos_table = DodongoMouthPositiveLimits0;

    /* Coarse precheck (full sprite bounding). */
    if (enrt_dodongo_is_bomb_in_range(1) != 0)
        return;

    /* Loop counter in NES RAM[0]: starts at 1, decrements after each axis. */
    ENEMY_GLEEOK_NECK_X_PTR_LO = 1;
    loop_counter = 1;

    /* 5-way direction index for tables: dir >> 1. */
    dir_idx = (unsigned char)(ENEMY_DIR(slot) >> 1);

    /* First iteration: horizontal distance in RAM[4]. */
    dist = ENEMY_GLEEOK_NECK_M_PTR_LO;
    for (;;) {
        if ((signed char)dist < (signed char)neg_table[dir_idx])
            return;
        if ((signed char)dist >= (signed char)pos_table[dir_idx])
            return;

        /* Advance to vertical table set (Mouth*1). */
        neg_table = DodongoMouthNegativeLimits1;
        pos_table = DodongoMouthPositiveLimits1;
        /* Load vertical distance and decrement loop counter. */
        dist = ENEMY_GLEEOK_NECK_M_PTR_HI;
        ENEMY_GLEEOK_NECK_X_PTR_LO = (unsigned char)(--loop_counter);
        if (loop_counter < 0)
            break;
    }

    /* Bomb is in range of the mouth — advance to bloated state. */
    ENEMY_STATE_TIMER(slot)++;
    /* Deactivate first bomb slot (16) and reset bloated substate. */
    OBJ(0x00AC, 16) = 0;
    ENEMY_TURN_TIMER(slot) = 0;
}

/*--------------------------------------------------------------------
 * Dodongo_CheckBombHit (drained from z_04.asm:6092)
 *
 * When the Dodongo is in state 0 (moving), computes its mouth hotspot
 * (X+8 for vertical, X+$10 for horizontal; Y+8, +1 when vertical) and
 * the first bomb's hotspot (X+8, Y+8, plus the chained carries), then:
 *   - if bomb state == $12 (live): tail to TryEatBomb;
 *   - if bomb state >= $20 (fire): ignore;
 *   - else (dust cloud): if IsBombInRange(0) matches, stun (state=2).
 *------------------------------------------------------------------*/
void enrt_dodongo_check_bomb_hit(unsigned int slot) {
    unsigned char bomb_state;

    /* Only check while moving (state 0). */
    if (ENEMY_STATE_TIMER(slot) != 0)
        return;

    /* Monster hotspot X + 8 (+8 more when horizontal), Y + 8; bomb
     * hotspot X + 8, Y + 8. Only the first ADC has a CLC: CPY #$04 sets
     * the carry for a vertical Dodongo (so its Y hotspot is Y + 9), and
     * each later ADC takes the carry out of the previous one. */
    {
        unsigned int t;
        unsigned int c;
        if (ENEMY_DIR(slot) < 4) {
            t = (unsigned int)(unsigned char)(ENEMY_X(slot) + 8) + 8u;   /* CPY: C=0 */
            c = t >> 8;
        } else {
            t = (unsigned int)ENEMY_X(slot) + 8u;
            c = 1u;                                                      /* CPY: C=1 */
        }
        ENEMY_GLEEOK_NECK_X_PTR_LO = (unsigned char)t;
        t = (unsigned int)(unsigned char)ENEMY_Y(slot) + 8u + c;
        c = t >> 8;
        ENEMY_GLEEOK_NECK_X_PTR_HI = (unsigned char)t;

        /* First bomb slot is fixed at $10 (16). */
        t = (unsigned int)(unsigned char)OBJ(NES_OBJ_X, 16) + 8u + c;
        c = t >> 8;
        ENEMY_GLEEOK_NECK_Y_PTR_LO = (unsigned char)t;
        t = (unsigned int)(unsigned char)OBJ(NES_OBJ_Y, 16) + 8u + c;
        ENEMY_GLEEOK_NECK_Y_PTR_HI = (unsigned char)t;
    }

    bomb_state = OBJ(0x00AC, 16);
    if (bomb_state == 0)
        return;

    if (bomb_state == 0x12) {
        /* Live bomb — tail-call TryEatBomb. */
        enrt_dodongo_try_eat_bomb(slot);
        return;
    }
    if (bomb_state >= 0x20)
        return;

    /* Dust cloud from an exploded bomb: if it's in range, stun. */
    if (enrt_dodongo_is_bomb_in_range(0) != 0)
        return;
    ENEMY_STATE_TIMER(slot) = 2;
}

/*--------------------------------------------------------------------
 * Dodongo_Draw (drained from z_04.asm:6354)
 *
 * Picks an animation frame image from per-direction tables based on
 * state (0=walk, 1=bloated, 2=stunned) and draws the left half, and
 * — if facing horizontally — the right half at X+$10. The two frame
 * numbers come from DodongoFrameImages with an XOR-1 swap between
 * halves. Frame numbers 7 and 9 (vertical bloated images) are drawn
 * mirrored; others are drawn non-mirrored.
 *------------------------------------------------------------------*/
void enrt_dodongo_draw(unsigned int slot) {
    unsigned char dir_idx;      /* dir >> 1 — 5-way direction index. */
    unsigned char state;
    unsigned char turn_timer;   /* bloated substate when state==1. */
    unsigned char frame_index;  /* into DodongoFrameImages. */
    unsigned char frame_img;
    unsigned char mask_frames = 8;  /* default: switch every 8 frames. */
    unsigned char saved_frame_index;
    unsigned char saved_x;
    int draw_right_side = 0;

    enrt_anim_set_sprite_desc_level_palette_row();
    dir_idx = (unsigned char)(ENEMY_DIR(slot) >> 1);
    ENEMY_GLEEOK_NECK_X_PTR_LO = dir_idx;

    state = ENEMY_STATE_TIMER(slot);
    if (state == 0)
        goto draw_walking_fast;

    if (state > 1) {
        /* Stunned — switch every $20 frames. */
        mask_frames = 32;
        goto draw_walking;
    }

    /* state == 1 (bloated). */
    turn_timer = ENEMY_TURN_TIMER(slot);
    if (turn_timer == 0)
        goto draw_walking_fast;      /* substate 0: walk animation. */
    if (turn_timer == 2 || turn_timer == 3)
        goto draw_faded;

    /* State 1 substate 1 — bloated still frame set. */
    frame_index = (unsigned char)(dir_idx + 0x14);
    goto prepare_to_draw;

draw_faded:
    /* Every 2 frames, skip drawing for 2 frames. */
    if ((ENEMY_CUR_SPRITE_ATTR_ROW & 0x02) == 0)
        return;
    frame_index = dir_idx;
    goto prepare_to_draw;

draw_walking_fast:
    mask_frames = 8;
draw_walking:
    frame_index = dir_idx;
    if ((ENEMY_CUR_SPRITE_ATTR_ROW & mask_frames) != 0)
        frame_index = (unsigned char)(frame_index + 5);
    /* fall through */

prepare_to_draw:
    z07_anim_fetch_obj_pos(slot);
    saved_frame_index = frame_index;

    /* Left-half flip + frame image lookup. */
    if (frame_index >= 0x14) {
        ENEMY_FRAME_FLAGS = DodongoFrameHFlipsBloated[frame_index - 0x14];
        frame_img = DodongoFrameImagesBloated[frame_index - 0x14];
    } else {
        ENEMY_FRAME_FLAGS = DodongoFrameHFlips[frame_index];
        frame_img = DodongoFrameImages[frame_index];
    }

    /* Frame images 7 and 9 are vertical bloated frames drawn mirrored. */
    if (frame_img == 7 || frame_img == 9) {
        c_draw_object_mirrored_with_frame((unsigned int)frame_img, slot);
        draw_right_side = 1;
    } else {
        c_draw_object_not_mirrored_with_frame((unsigned int)frame_img, slot);
    }
    (void)draw_right_side;  /* both paths fall through to right-half logic. */

    /* If facing vertically, skip the right-side draw. */
    if ((ENEMY_DIR(slot) & 3) == 0)
        return;

    /* Right half: shift X by $10 and draw with the frame-XOR-1 image.
     * The H-flip already applied on the left half carries over. */
    /* NES: Y stays in [01], [00] = ObjX + $10 (the draw reads [00]; the
     * old ObjX shift left [00] as the left-half draw had it, T-171). */
    saved_x = ENEMY_X(slot);
    RAM(0x0000u) = (unsigned char)(saved_x + 0x10u);
    enrt_anim_set_sprite_desc_level_palette_row();
    if (saved_frame_index >= 0x14)
        frame_img = DodongoFrameImagesBloated[saved_frame_index - 0x14];
    else
        frame_img = DodongoFrameImages[saved_frame_index];
    c_draw_object_not_mirrored_with_frame(
        (unsigned int)(frame_img ^ 0x01u), slot);
}
