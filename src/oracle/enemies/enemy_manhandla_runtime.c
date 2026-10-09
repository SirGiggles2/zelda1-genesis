#include "enemy_runtime_private.h"
#include "legacy_bridge.h"
#include "combat_state.h"
#include "room_state.h"
#include "sprite_state.h"

static const unsigned char kManhandlaBaseFrameImagesAndAttrs[5] = {
    0x00, 0x80, 0x02, 0x42, 0x04,
};

/* In ROM OffsetsX and OffsetsY are adjacent; ASM reads past OffsetsX into
 * OffsetsY for D3=2..4. These arrays reproduce the exact 5-byte window. */
static const unsigned char kManhandlaSegmentOffsetsX[5] = {
    0x00, 0x00, 0xF0, 0x10, 0x00,
};
static const unsigned char kManhandlaSegmentOffsetsY[5] = {
    0xF0, 0x10, 0x00, 0x00, 0x00,
};

void enrt_init_manhandla(unsigned int slot) {
    unsigned char rand_idx;
    signed char seg;

    ENEMY_SFX_BOSS_CRY = 64;
    rand_idx = (unsigned char)(ENEMY_RNG_A(slot) & 0x07u);
    ENEMY_DIR(slot) = Directions8[rand_idx];

    for (seg = 4; seg >= 0; --seg) {
        unsigned char uidx = (unsigned char)seg;
        RAM(0x0099u + uidx) = RAM(0x0099u);
        RAM(0x0350u + uidx) = 60;
        RAM(0x04B3u + uidx) = 0xE2;
        RAM(0x0479u + uidx) = kManhandlaBaseFrameImagesAndAttrs[uidx];
        RAM(0x0406u + uidx) = 0;
        RAM(0x0493u + uidx) = 0;
        RAM(0x04C0u + uidx) = RAM(0x04C0u);
        RAM(0x0486u + uidx) = RAM(0x0486u);
        RAM(0x0071u + uidx) = (unsigned char)(RAM(0x0075u) + kManhandlaSegmentOffsetsX[uidx]);
        RAM(0x0085u + uidx) = (unsigned char)(RAM(0x0089u) + kManhandlaSegmentOffsetsY[uidx]);
        RAM(0x0420u + uidx) = 0x80;
    }
}

void enrt_update_manhandla(unsigned int slot) {
    unsigned char frame;
    unsigned char new_frame_attrs;
    unsigned char old_frame_attrs;
    signed char seg;

    if (slot == 5) {
        if (ENEMY_MANHANDLA_SEGMENT_DIED_FLAG != 0) {
            for (seg = 4; seg >= 0; --seg) {
                unsigned char uidx = (unsigned char)seg;
                unsigned int sum = (unsigned int)RAM(0x0420u + uidx) + 0x80u;
                RAM(0x0420u + uidx) = (unsigned char)sum;
                RAM(0x042Du + uidx) = (unsigned char)(RAM(0x042Du + uidx)
                                                   + (unsigned char)(sum >> 8));
            }
            ENEMY_MANHANDLA_SEGMENT_DIED_FLAG = 0;
        }

        if (RAM(0x0385u) != 0) {
            enrt_manhandla_set_all_segments_direction(RAM(0x0385u));
        }

        if (ENEMY_MOVE_TIMER(slot) == 0) {
            unsigned char new_dir;
            ENEMY_MOVE_TIMER(slot) = 16;
            if (ENEMY_RNG_A(slot) >= 0x80u) {
                c_turn_towards_player8();
            } else {
                c_turn_randomly_dir8(slot);
            }
            new_dir = ENEMY_DIR(slot);
            RAM(0x0385u) = new_dir;
            enrt_manhandla_set_all_segments_direction(new_dir);
        }
    }

    if (slot == 5) {
        RAM(0x0384u) = ENEMY_DIR(slot);
    }
    enrt_manhandla_move(slot);
    enrt_manhandla_check_collisions(slot);

    {
        unsigned char post_dir = ENEMY_DIR(slot);
        if (post_dir != RAM(0x0384u)) {
            RAM(0x0385u) = post_dir;
        }
    }

    frame = (unsigned char)((ENEMY_MANHANDLA_FRAME_ACCUM(slot) & 0x10u) >> 4);
    RAM(0x0000u) = frame;
    new_frame_attrs = (unsigned char)((ENEMY_MANHANDLA_FRAME_ATTR(slot) & 0xFEu) | frame);
    ENEMY_MANHANDLA_FRAME_ATTR(slot) = new_frame_attrs;

    if (slot == 5) {
        enrt_manhandla_draw(slot);
        return;
    }

    old_frame_attrs = OBJ(0x0437u, slot);
    if (new_frame_attrs == old_frame_attrs) {
        enrt_manhandla_draw(slot);
        return;
    }
    OBJ(0x0437u, slot) = new_frame_attrs;

    if (new_frame_attrs & 0x01u) {
        enrt_manhandla_draw(slot);
        return;
    }

    if (ENEMY_RNG_B(slot) < 0xE0u) {
        enrt_manhandla_draw(slot);
        return;
    }
    if (ENEMY_TYPE(7) != 0) {
        enrt_manhandla_draw(slot);
        return;
    }
    c_shoot_fireball(86, slot);
    enrt_manhandla_draw(slot);
}

void enrt_manhandla_set_all_segments_direction(unsigned int val) {
    signed char i;
    for (i = 4; i >= 0; --i)
        ENEMY_MANHANDLA_SEG_DIR((unsigned char)i) = (unsigned char)val;
}

void enrt_manhandla_check_collisions(unsigned int slot) {
    unsigned char saved_dir = ENEMY_DIR(slot);
    unsigned char saved_move_timer = ENEMY_MOVE_TIMER(slot);
    signed char hand_slot;
    unsigned char hand_count;

    c_check_monster_collisions(slot);
    ENEMY_MOVE_TIMER(slot) = saved_move_timer;
    ENEMY_DIR(slot) = saved_dir;

    if (slot == 5)
        ENEMY_HIT_REACTION(slot) = 0;

    c_play_boss_hit_cry_if_needed(slot);
    if (ENEMY_METASTATE(slot) == 0)
        return;

    c_reset_shove_info(slot);
    if (slot == 5) {
        /* The base only dies with the last hand: ResetShoveInfo leaves
         * A = 0 and the NES stores it in ObjMetastate+5. Returning with
         * the death metastate killed the base on a sword hit (T-171
         * t171_manhandla_sword t541). */
        ENEMY_METASTATE(slot) = 0;
        return;
    }

    /* Hands are ObjType+1..+4 (LDA ObjType+1, Y for Y = 3..0); the
     * count includes this dying hand, then DEC: > 0 = another hand lives. */
    hand_count = 0;
    for (hand_slot = 3; hand_slot >= 0; --hand_slot) {
        if (ENEMY_TYPE((unsigned char)(hand_slot + 1)) == 0x3C)
            hand_count++;
    }
    hand_count--;

    if ((signed char)hand_count > 0) {
        ENEMY_TYPE(slot) = 93;
        ENEMY_MANHANDLA_SEGMENT_DIED_FLAG++;
        return;
    }

    c_play_boss_death_cry();
    ENEMY_TYPE(5) = 93;
    ENEMY_METASTATE(5) = 16;
    ENEMY_MOVE_TIMER(5) = 16;      /* STA ObjTimer+5 ($2D), not ObjState */
    ENEMY_MANHANDLA_SEGMENT_DIED_FLAG++;
}

void enrt_manhandla_move(unsigned int slot) {
    unsigned int speed_sum;
    unsigned char step;
    unsigned char dir = ENEMY_DIR(slot);
    unsigned int frame_sum;

    speed_sum = (unsigned int)ENEMY_PUSH_TIMER(slot)
              + ((unsigned int)ENEMY_AIR_SPEED(slot) & 0xE0u);
    ENEMY_PUSH_TIMER(slot) = (unsigned char)speed_sum;
    step = (unsigned char)(ENEMY_TURN_TIMER(slot) + (unsigned char)(speed_sum >> 8));
    RAM(0x0003u) = step;            /* NES [03] step, [02] mask $A1 << 3 (T-171) */
    RAM(0x0002u) = 0x08u;

    if (dir & 0x01)
        ENEMY_X(slot) = (unsigned char)(ENEMY_X(slot) + step);
    if (dir & 0x02)
        ENEMY_X(slot) = (unsigned char)(ENEMY_X(slot) - step);
    if (dir & 0x04)
        ENEMY_Y(slot) = (unsigned char)(ENEMY_Y(slot) + step);
    if (dir & 0x08)
        ENEMY_Y(slot) = (unsigned char)(ENEMY_Y(slot) - step);

    frame_sum = (unsigned int)ENEMY_MANHANDLA_FRAME_ACCUM(slot)
              + (unsigned int)(ENEMY_RNG_B(slot) & 0x03)
              + (unsigned int)step;
    ENEMY_MANHANDLA_FRAME_ACCUM(slot) = (unsigned char)frame_sum;

    c_bound_flyer(slot);
    z07_anim_fetch_obj_pos(slot);
}

void enrt_manhandla_draw(unsigned int slot) {
    unsigned char frame_attrs;
    unsigned char frame_idx;

    z07_anim_fetch_obj_pos(slot);
    frame_attrs = ENEMY_MANHANDLA_FRAME_ATTR(slot);
    z01_anim_set_sprite_desc_attrs((unsigned char)((frame_attrs & 0x80) | 0x01));
    if (frame_attrs & 0x40)
        ENEMY_FRAME_FLAGS++;

    /* T-171: A = frame image (low nibble) is DrawObject*'s frame
     * parameter (Z_04.asm Manhandla_Draw); passing 0 drew every segment
     * as the base (t171_boss_l3: NES tiles $E0-$EC, Genesis $E8/$EA). */
    frame_idx = (unsigned char)(frame_attrs & 0x0F);
    if (frame_idx == 2 || frame_idx == 3) {
        c_draw_object_not_mirrored_with_frame(frame_idx, slot);
        return;
    }
    c_draw_object_mirrored_with_frame(frame_idx, slot);
}
