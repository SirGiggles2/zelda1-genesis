#include "enemy_runtime_private.h"
#include "legacy_bridge.h"
#include "combat_state.h"
#include "room_state.h"
#include "sprite_state.h"

void enrt_gleeok_dec_head_timer(void) {
    ENEMY_GLEEOK_HEAD_TIMER--;
}

void enrt_gleeok_set_segment_x(unsigned int val, unsigned int slot) {
    ENEMY_GLEEOK_SEG_X(slot) = (unsigned char)val;
}

void enrt_gleeok_contract_segment_x(unsigned int slot) {
    unsigned char cur_x = ENEMY_GLEEOK_SEG_X(slot);
    unsigned char target_x = ENEMY_GLEEOK_SEG_X_TARGET(slot);
    unsigned char new_x = (unsigned char)(cur_x + 2);
    if (cur_x < target_x)
        new_x = (unsigned char)(new_x - 4);
    z04_gleeok_set_segment_x(new_x, slot);
}

void enrt_gleeok_contract_segment_y(unsigned int slot) {
    unsigned char cur_y = ENEMY_GLEEOK_SEG_Y(slot);
    unsigned char target_y = ENEMY_GLEEOK_SEG_Y_TARGET(slot);
    unsigned char new_y = (unsigned char)(cur_y + 2);
    if (cur_y > target_y)
        new_y = (unsigned char)(new_y - 4);
    z04_gleeok_set_segment_y(new_y, slot);
}

void enrt_gleeok_contract_segment(unsigned int slot) {
    if (ENEMY_RNG_A(0) & 0x80)
        enrt_gleeok_contract_segment_y(slot);
    else
        enrt_gleeok_contract_segment_x(slot);
}

void enrt_gleeok_ignore_segment(void) {}

void enrt_gleeok_set_segment_y(unsigned int val3, unsigned int slot) {
    ENEMY_GLEEOK_SEG_Y(slot) = (unsigned char)val3;
}

void enrt_init_gleeok_head(unsigned int slot) {
    z04_init_blue_keese(slot);
    ENEMY_MAX_AIR_SPEED = 0xE0;
    ENEMY_AIR_SPEED(slot) = 0xBF;
}

static unsigned int gleeok_ptr(unsigned int ptr_addr) {
    return (unsigned int)RAM(ptr_addr) | ((unsigned int)RAM(ptr_addr + 1) << 8);
}

void enrt_update_gleeok(unsigned int slot) {
    unsigned char obj_type;
    int neck_index;
    int seg;

    c_gleeok_draw_body();

    obj_type = OBJ(NES_OBJ_TYPE, 1);            /* ($0350,A4) = type of slot 1 */
    ENEMY_GLEEOK_NECK_INDEX = (unsigned char)(obj_type - 0x42);

    for (neck_index = (int)(unsigned char)ENEMY_GLEEOK_NECK_INDEX;
         neck_index >= 0;
         neck_index--) {
        unsigned char level_bit;
        ENEMY_GLEEOK_NECK_INDEX = (unsigned char)neck_index;

        level_bit = LevelMasks[(unsigned char)neck_index];
        if (ENEMY_GLEEOK_DEAD_NECK_MASK & level_bit)
            continue;

        c_gleeok_fetch_neck_addrs();

        /* Load this neck's segment bytes from external NES arrays into the
         * working object slots (X→OBJ(0x70,1..6), Y→OBJ(0x84,1..6), misc→
         * RAM[0x0413+1..0x0413+6]). Loop seg = 5..0. */
        /* T-125: resolve the three (ptr),Y bases once per neck (the
         * per-byte pointer loads cost ~1k instructions a frame with 3
         * necks). The arrays never overlap the $00-$05 pointers. */
        {
            const unsigned char *sx = &nes_ram[gleeok_ptr(0x0000)];
            const unsigned char *sy = &nes_ram[gleeok_ptr(0x0002)];
            const unsigned char *sm = &nes_ram[gleeok_ptr(0x0004)];
            for (seg = 5; seg >= 0; seg--) {
                RAM(0x0071 + seg) = sx[seg];
                RAM(0x0085 + seg) = sy[seg];
                RAM(0x0413 + seg) = sm[seg];
            }
        }

        /* One neck per frame moves + may shoot; selected by frame_tick & 3. */
        {
            unsigned char chosen = (unsigned char)(ENEMY_CUR_SPRITE_ATTR_ROW & 0x03u);
            ENEMY_SHOT_TYPE_SCRATCH = chosen;       /* RAM[$00] */
            /* NES Gleeok_FetchNeckAddrs leaves X = GleeokCurNeck. */
            if ((unsigned char)neck_index == chosen) {
                c_gleeok_move_neck();
                c_gleeok_move_head();
                /* Switch focus to the head slot (5). If RNG <0x20 and slot
                 * 0xB has no fireball, shoot fireball 86. */
                {
                    unsigned int head_slot = 5;
                    unsigned char rng = ENEMY_RNG_A(head_slot);
                    if (rng < 0x20 && OBJ(NES_OBJ_TYPE, 0x0B) == 0)
                        c_shoot_fireball(86, head_slot);
                }
            }
        }

        c_gleeok_draw_head_and_check_collisions();

        /* Save segment data back to the external NES arrays. */
        c_gleeok_fetch_neck_addrs();
        {
            unsigned char *dx = &nes_ram[gleeok_ptr(0x0000)];
            unsigned char *dy = &nes_ram[gleeok_ptr(0x0002)];
            unsigned char *dm = &nes_ram[gleeok_ptr(0x0004)];
            for (seg = 5; seg >= 0; seg--) {
                dx[seg] = RAM(0x0071 + seg);
                dy[seg] = RAM(0x0085 + seg);
                dm[seg] = RAM(0x0413 + seg);
            }
        }
    }
    /* NES @NextLoopNeck decrements the shared byte past zero. */
    ENEMY_GLEEOK_NECK_INDEX = 0xFFu;
}

/*--------------------------------------------------------------------
 * L_Gleeok_StoreRefSegDistance  (drained from z_04.asm:9834)
 *
 * Continuation of Gleeok_MoveNeck: given the signed reference segment
 * distance (head→base divided by 4) in D0, this routine:
 *   1. Stores the signed dist into ENEMY_GLEEOK_REF_SEG_DIST.
 *   2. Computes 2nd & 3rd tier H/V reference limits via
 *      Gleeok_CalcSegmentLimits (axis=0 horizontal, axis=1 vertical).
 *   3. For each of segments 0..3, if the gap to the previous segment
 *      exceeds the 3rd-tier limit, pulls it 2 pixels closer (X then Y).
 *   4. Calls Gleeok_StretchNeck for segments 0..2.
 *   5. For each middle segment 2..0, computes the reference X coord
 *      (base_X + ref_dist * (i+1)) and nudges the segment's X by ±1
 *      toward it.
 *   6. For segments 4 and 3, keeps the segment's Y between its
 *      neighbors (slots 4-3 in OBJ(0x86)).
 *
 * The asm entry is reached by JMP from Gleeok_MoveNeck (and from
 * L_Gleeok_UDiv4) with D0 = signed reference distance.
 *------------------------------------------------------------------*/
void enrt_gleeok_store_ref_seg_distance(unsigned int signed_ref_dist) {
    unsigned int seg;
    unsigned char abs_dist;
    int i;

    ENEMY_GLEEOK_REF_SEG_DIST = (unsigned char)signed_ref_dist;

    /* Horizontal tier limits from |signed_ref_dist|. */
    abs_dist = z01_abs(signed_ref_dist);
    c_gleeok_calc_segment_limits((unsigned int)abs_dist, 0u);

    /* Vertical tier limits: |head_y - base_y| / 4. */
    abs_dist = z01_abs((unsigned int)(unsigned char)
                       (ENEMY_GLEEOK_HEAD_Y - ENEMY_GLEEOK_BASE_Y));
    abs_dist = (unsigned char)(abs_dist >> 2);
    c_gleeok_calc_segment_limits((unsigned int)abs_dist, 1u);

    /* Keep adjacent segments within the 3rd tier reference distance.
     * For each seg in [0..3], compute |prev_x - cur_x| and |prev_y - cur_y|
     * (slots 0..3 vs 1..4 mapped onto OBJ(0x71,seg) etc). When the gap
     * exceeds the limit, nudge cur 2 pixels toward prev. */
    for (seg = 0; seg < 4; seg++) {
        unsigned char prev_x = RAM(0x0071 + seg);
        unsigned char cur_x  = ENEMY_GLEEOK_SEG_X(seg);
        unsigned char dx = z01_abs((unsigned int)(unsigned char)(prev_x - cur_x));
        unsigned char prev_y, cur_y, dy;

        if (dx >= ENEMY_GLEEOK_REF_LIMIT_H_3) {
            unsigned char new_x = (unsigned char)(cur_x + 2);
            if (cur_x >= prev_x)
                new_x = (unsigned char)(new_x - 4);
            ENEMY_GLEEOK_SEG_X(seg) = new_x;
        }

        prev_y = RAM(0x0085 + seg);
        cur_y  = ENEMY_GLEEOK_SEG_Y(seg);
        dy = z01_abs((unsigned int)(unsigned char)(prev_y - cur_y));

        if (dy >= ENEMY_GLEEOK_REF_LIMIT_V_3) {
            unsigned char new_y = (unsigned char)(cur_y + 2);
            if (cur_y >= prev_y)
                new_y = (unsigned char)(new_y - 4);
            ENEMY_GLEEOK_SEG_Y(seg) = new_y;
        }
    }

    /* Stretch / contract per segment for segs 0..2. */
    for (seg = 0; seg < 3; seg++)
        c_gleeok_stretch_neck(seg);

    /* Pull each middle segment X toward its computed reference X
     * (base_X + ref_dist * (i+1)) by 1. */
    for (i = 2; i >= 0; i--) {
        unsigned char ref_x = ENEMY_GLEEOK_BASE_X;       /* OBJ(0x70, 1) */
        int j;
        for (j = i; j >= 0; j--)
            ref_x = (unsigned char)(ref_x + ENEMY_GLEEOK_REF_SEG_DIST);
        {
            unsigned char cur = ENEMY_GLEEOK_SEG_X((unsigned int)i);
            unsigned char nudged = (unsigned char)(cur + 1);
            /* NES CMP cur / BCS keeps +1 when reference equals cur. */
            if (cur > ref_x)
                nudged = (unsigned char)(nudged - 2);
            ENEMY_GLEEOK_SEG_X((unsigned int)i) = nudged;
        }
    }

    /* Keep the Y of segments 4 and 3 (OBJ(0x87,1..2)) between their
     * vertical neighbors. The asm walks D2=1..0 over OBJ(0x86..0x88). */
    for (i = 1; i >= 0; i--) {
        unsigned char cur = ENEMY_GLEEOK_SEG_Y_TARGET((unsigned int)i);
        unsigned char above = ENEMY_GLEEOK_SEG_Y((unsigned int)i);
        unsigned char below = RAM(0x0088 + (unsigned int)i);

        if (cur < above) {
            /* cur < above: if also cur < below, nudge up (++) toward middle */
            if (cur < below)
                ENEMY_GLEEOK_SEG_Y_TARGET((unsigned int)i)++;
        } else {
            /* cur >= above: if also cur >= below, nudge down (--) toward middle */
            if (cur >= below)
                ENEMY_GLEEOK_SEG_Y_TARGET((unsigned int)i)--;
        }
    }
}

/*--------------------------------------------------------------------
 * Gleeok_CheckCollisions  (drained from z_04.asm:10285)
 *
 * Per-segment collision step. Called with D2 = current segment slot
 * from Gleeok_DrawSegmentAndCheckCollisions; returns when slot drops
 * below 1 (whole loop done) or when the boss dies.
 *
 * For non-head/base segments (slot != 5 and != 1), no collision check;
 * just continue to the next segment.
 *
 * For head (slot 5) and base (slot 1):
 *   - CheckMonsterCollisions, set writhe + low anim cntr if hit.
 *   - ResetShoveInfo.
 *   - Base: ResetObjMetastate, continue.
 *   - Head/upper: PlayBossHitCryIfNeeded; if not killed, continue.
 *   - If head was killed: spawn a flying head in slot (neck+7),
 *     hide both head & base sprites, OR this neck into the dead mask,
 *     count dead necks; if all dead -> boss died.
 *
 * Loop continues by tail-call to Gleeok_DrawSegmentAndCheckCollisions
 * (which then JMPs back into here). We use bounded recursion (max 5
 * levels) which mirrors the asm tail-jump chain.
 *------------------------------------------------------------------*/
void enrt_gleeok_check_collisions(unsigned int slot) {
    unsigned int next_slot;

    if (slot != 5 && slot != 1)
        goto next_segment;

    c_check_monster_collisions(slot);

    /* ENEMY_OBJ_SHOVE_DIR != 0 means the segment was hit this frame */
    if (ENEMY_OBJ_SHOVE_DIR(slot) != 0) {
        ENEMY_GLEEOK_WRITHE_CNTR = 6;
        ENEMY_GLEEOK_ANIM_CNTR   = 6;
    }
    c_reset_shove_info(slot);

    /* The base segment can writhe but never dies. */
    if (slot == 1) {
        c_reset_obj_metastate(slot);
        goto next_segment;
    }

    c_play_boss_hit_cry_if_needed(slot);

    /* If the metastate didn't transition to "killed" yet, continue. */
    if (ENEMY_METASTATE(slot) == 0)
        goto next_segment;

    /* This neck died. Prepare to make a flying head. */
    ENEMY_CHARGE_SPEED(slot) = 96;
    {
        unsigned int saved_slot = slot;     /* mirrors PHA */
        unsigned int destroy_slot = slot;

        if (slot == 5) {
            /* Spawn a flying head in slot (neck_index + 7). */
            unsigned int flying = (unsigned int)
                (unsigned char)(ENEMY_GLEEOK_NECK_INDEX + 7);
            ENEMY_ALIVE_FLAG(flying) = 0xFF;       /* OBJ(0x0492) */
            ENEMY_X(flying) = ENEMY_GLEEOK_HEAD_X;
            ENEMY_Y(flying) = ENEMY_GLEEOK_HEAD_Y;
            OBJ(NES_OBJ_TYPE, flying) = 70;        /* type 0x46 = flying head */
        }

        destroy_slot = saved_slot;             /* PLA -> D2 */

        /* Hide the original head's sprite and the base segment's sprite.
         * sprite_offset = neck_index << 3; OAM bytes at $200 and $220. */
        {
            unsigned int sprite_off =
                (unsigned int)((unsigned char)(ENEMY_GLEEOK_NECK_INDEX << 3));
            OAM_BYTE(sprite_off) = 0xF8;
            OAM_BYTE(0x20 + sprite_off) = 0xF8;
        }

        /* Add this neck's bit to the dead-neck mask, then count bits. */
        {
            unsigned char level_bit = LevelMasks[(unsigned char)ENEMY_GLEEOK_NECK_INDEX];
            unsigned char new_mask =
                (unsigned char)(ENEMY_GLEEOK_DEAD_NECK_MASK | level_bit);
            unsigned char shifter = new_mask;
            unsigned char dead_count = 0;
            int b;

            ENEMY_GLEEOK_DEAD_NECK_MASK = new_mask;

            for (b = 0; b < 4; b++) {
                if (shifter & 1u)
                    dead_count++;
                shifter = (unsigned char)(shifter >> 1);
            }

            /* Compare to original neck count (type - 0x41). */
            {
                unsigned char total_necks =
                    (unsigned char)(OBJ(NES_OBJ_TYPE, 1) - 0x41);
                if (dead_count == total_necks)
                    goto boss_died;
            }
        }

        c_reset_obj_metastate(destroy_slot);
        return;
    }

next_segment:
    if (slot < 1 || slot - 1 < 1)
        return;
    next_slot = slot - 1;
    c_gleeok_draw_segment_and_check_collisions(next_slot);
    return;

boss_died:
    /* Whole gleeok died. Hide the first 0x10 priority sprites, play death
     * cry, ObjMetastate+1 ($406) = 0x11 for the death spark, and clear
     * types of slots 2..10. */
    c_write_blank_priority_sprites();
    c_play_boss_death_cry();
    ROOM_OBJ_STUN_TIMER(0) = 17;
    {
        /* STA ObjType+1, Y for Y = 1..9: slots 2..10. Slot 1 (the body)
         * keeps its type so its death spark ($11) runs; the drain cleared
         * slots 1..9, removing the body at once (T-171 t171_gleeok_sword
         * t1353). Slot 11 (a fireball) is left, as the NES notes. */
        unsigned int s;
        for (s = 2; s <= 0x0A; s++)
            OBJ(NES_OBJ_TYPE, s) = 0;
    }
}
