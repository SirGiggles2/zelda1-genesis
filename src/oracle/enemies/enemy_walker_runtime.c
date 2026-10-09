#include "enemy_runtime_private.h"
#include "legacy_bridge.h"
#include "sprite_state.h"

static void enrt_octorock_common(unsigned int slot, unsigned char speed) {
    ENEMY_WALK_SPEED(slot) = speed;
    ENEMY_MOVE_TIMER(slot) = (unsigned char)((slot + 1u) << 4);
    (void)z07_reset_obj_state(slot);
    ENEMY_DRAW_FRAME(slot) = 0;
    ENEMY_ANIM_TIMER(slot) = 6;
    enrt_init_walker(slot);
}

void enrt_update_bubble(unsigned int slot) {
    wanderer_update_common(64, slot);
    {
        unsigned char obj_type = ENEMY_TYPE(slot);
        unsigned char palette;
        if (obj_type == 0x2B) {
            palette = ENEMY_CUR_SPRITE_ATTR_ROW & 3;
        } else {
            palette = obj_type - 0x2B;
        }
        z01_anim_set_sprite_desc_attrs(palette);
    }
    enrt_animate_and_draw_common_object(1, slot);
    z01_check_link_collision(slot);
    if (!ENEMY_COLLISION_FLAG)
        return;
    if (ENEMY_TYPE(slot) == 0x2B) {
        ENEMY_BUBBLE_EFFECT = 16;
        return;
    }
    ENEMY_BUBBLE_STATUS = (unsigned char)(ENEMY_TYPE(slot) - 0x2C);
}

void enrt_init_leever(unsigned int slot) {
    ENEMY_LEEVER_TIMER = 5;
    z07_reset_obj_metastate_and_timer(slot);
    /* NES InitLeever falls through into InitSlowOctorockOrGhini
     * (Z_04.asm): q-speed $20, timer (slot+1)*$10, InitWalker. */
    enrt_octorock_common(slot, 32);
}

void enrt_init_walker(unsigned int slot) {
    if (ENEMY_DIR(slot) != 0)
        return;
    {
        unsigned char link_x = CHASE_TARGET_X;
        unsigned char obj_x = ENEMY_X(slot);
        unsigned char diff_x = (unsigned char)(link_x - obj_x);
        unsigned char h_dir = (link_x >= obj_x) ? 2u : 1u;
        ENEMY_SHOT_TYPE_SCRATCH = diff_x;
        ENEMY_SCRATCH_Y = h_dir;
        ENEMY_DIR(slot) = h_dir;
    }
    {
        unsigned char link_y = CHASE_TARGET_Y;
        unsigned char obj_y = ENEMY_Y(slot);
        unsigned char diff_y = (unsigned char)(link_y - obj_y);
        unsigned char v_dir = (link_y >= obj_y) ? 4u : 8u;
        ENEMY_DIR(slot) = v_dir;
        if (diff_y < ENEMY_SHOT_TYPE_SCRATCH)
            return;
    }
    ENEMY_DIR(slot) = ENEMY_SCRATCH_Y;
}

void enrt_init_bubble(unsigned int slot) {
    ENEMY_WALK_SPEED(slot) = 64;
    enrt_init_walker(slot);
}

void enrt_init_rope(unsigned int slot) {
    ENEMY_CHARGE_SPEED(slot) = 16;
    if (SAVE_SLOT_QUEST(SAVE_SLOT_INDEX) != 0)
        ENEMY_CHARGE_SPEED(slot) = 64;
    enrt_init_walker(slot);
}

void enrt_init_darknut(unsigned int slot) {
    ENEMY_INVINCIBILITY(slot) = 0xF6;
    ENEMY_WALK_SPEED(slot) = (ENEMY_TYPE(slot) == 0x0B) ? 32u : 40u;
    enrt_init_walker(slot);
}

void enrt_init_slow_octorock_or_ghini(unsigned int slot) {
    enrt_octorock_common(slot, 32);
}

void enrt_init_fast_octorock(unsigned int slot) {
    enrt_octorock_common(slot, 48);
}

void enrt_init_gel(unsigned int slot) {
    ENEMY_STATE_TIMER(slot) = 2;
    enrt_init_walker(slot);
}

void enrt_update_rope(unsigned int slot) {
    unsigned char old_dir = ENEMY_DIR(slot);
    ENEMY_PUSH_DIR_SCRATCH(slot) = old_dir;

    if (!(ENEMY_PAUSE_FLAG | ENEMY_STUN_TIMER(slot))) {
        c_walker_move(slot);

        if ((OBJ(NES_OBJ_GRID_OFFSET, slot) & 0x0F) == 0)
            OBJ(NES_OBJ_GRID_OFFSET, slot) = 0;

        if (ENEMY_WALK_SPEED(slot) != 0x60 && ENEMY_MOVE_TIMER(slot) == 0) {
            ENEMY_MOVE_TIMER(slot) = ENEMY_RNG_A(slot) & 0x3F;
            c_face_unblocked_dir(slot);
        }
    }

    if (ENEMY_DIR(slot) != old_dir)
        ENEMY_WALK_SPEED(slot) = 0x20;

    /* NES UpdateRope lines up on Link's own position (ObjX/ObjY slot 0),
     * not the chase target (T-171). */
    if (ENEMY_WALK_SPEED(slot) == 0x20 && OBJ(NES_OBJ_GRID_OFFSET, slot) == 0) {
        unsigned char x_dist = z01_abs((unsigned char)(ENEMY_X(0) - ENEMY_X(slot)));
        if (x_dist < 8) {
            ENEMY_DIR(slot) = 8;
            if (ENEMY_Y(0) >= ENEMY_Y(slot))
                ENEMY_DIR(slot) >>= 1;
            ENEMY_WALK_SPEED(slot) = 0x60;
        } else {
            unsigned char y_dist = z01_abs((unsigned char)(ENEMY_Y(0) - ENEMY_Y(slot)));
            if (y_dist < 8) {
                ENEMY_DIR(slot) = 2;
                if (ENEMY_X(0) >= ENEMY_X(slot))
                    ENEMY_DIR(slot) >>= 1;
                ENEMY_WALK_SPEED(slot) = 0x60;
            }
        }
    }

    z07_anim_advance_and_fetch(10, slot);
    ENEMY_FRAME_FLAGS = (ENEMY_DIR(slot) & 0x02) >> 1;
    z01_anim_set_sprite_desc_attrs(2);

    if (SAVE_SLOT_QUEST(SAVE_SLOT_INDEX) != 0)
        z01_anim_set_sprite_desc_attrs(ENEMY_CUR_SPRITE_ATTR_ROW & 0x03);

    c_draw_object_not_mirrored_with_frame(ENEMY_DRAW_FRAME(slot), slot);
    c_check_monster_collisions(slot);
}

void enrt_update_standing_fire(unsigned int slot) {
    c_check_link_collision(slot);
    z01_anim_set_sprite_desc_attrs(2);
    ENEMY_DIR(slot) = 8;
    z07_animate_object_walking(slot);
    if (ENEMY_TYPE(slot) != 0x40)
        ENEMY_FRAME_FLAGS = 0;
    c_draw_object_not_mirrored_with_frame(0, slot);
}

void enrt_update_zol(unsigned int slot) {
    c_update_zol_state(slot);
    c_zol_check_collisions(slot);
    z07_anim_fetch_obj_pos(slot);
    c_draw_object_mirrored_with_frame((ENEMY_CUR_SPRITE_ATTR_ROW & 0x08) ? 0 : 1, slot);
}

void enrt_update_gel(unsigned int slot) {
    unsigned char orig_x;
    c_gel_move(slot);
    c_gel_check_collisions(slot);
    /* NES UpdateGel saves ObjX after Gel_Move (T-013: saved before, the
     * restore undid every step and gels never moved). */
    orig_x = ENEMY_X(slot);
    ENEMY_X(slot) = orig_x + 4;
    z07_anim_fetch_obj_pos(slot);
    z01_anim_set_sprite_desc_attrs(3);
    c_draw_object_not_mirrored_with_frame((ENEMY_CUR_SPRITE_ATTR_ROW & 0x02) ? 0 : 1, slot);
    ENEMY_X(slot) = orig_x;
}

void enrt_update_zora(unsigned int slot) {
    if (ENEMY_PAUSE_FLAG)
        return;
    c_update_burrower(slot);
    if (ENEMY_STATE_TIMER(slot) == 3 && ENEMY_MOVE_TIMER(slot) == 0xFD) {
        enrt_shoot_fireball(85, slot);
        ENEMY_MOVE_TIMER(slot) = 32;
    }
    if (ENEMY_STATE_TIMER(slot) == 0) {
        ENEMY_ROOM_MONSTER_FLAG--;
        z07_destroy_monster(slot);
    }
}

/* NES Z_04.asm @TryShootingNow body, shared by _TryShooting and
 * UpdateStalfos @PrepareToShoot. qspeed: the speed to keep when not in
 * shooting time ([01] on the NES). */
void enrt_try_shooting_body(unsigned char qspeed, unsigned char shot_type, unsigned int slot)
{
    unsigned char new_timer;

    if (ENEMY_HIT_REACTION(slot) != 0) {
        new_timer = 0;                          /* temporarily invincible */
    } else {
        /* LDY ObjShootTimer / DEY / BPL: 0 (or >= $81) reads as expired. */
        new_timer = (unsigned char)(OBJ(0x0451, slot) - 1u);
        if (new_timer & 0x80u) {
            if (OBJ(0x0412, slot) == 0) {       /* ObjWantsToShoot */
                ENEMY_WALK_SPEED(slot) = qspeed;
                return;
            }
            new_timer = 0x30;
        }
    }

    OBJ(0x0451, slot) = new_timer;

    if (new_timer == 0) {
        ENEMY_WALK_SPEED(slot) = qspeed;
        return;
    }
    if (new_timer != 0x10 || (ENEMY_PAUSE_FLAG | ENEMY_STUN_TIMER(slot)) != 0) {
        ENEMY_WALK_SPEED(slot) = 0;
        return;
    }

    unsigned int result = c_shoot_if_wanted(shot_type, slot);
    if ((result & CARRY_SET) == 0) {
        ENEMY_WALK_SPEED(slot) = qspeed;
        return;
    }

    /* _ShootIfWanted2 tail, then the success path. */
    ENEMY_MOVE_TIMER(slot) = 0x80;
    OBJ(0x0437, slot) = (unsigned char)(OBJ(0x0437, slot) - 1);
    OBJ(0x0412, slot) = 0;
    ENEMY_WALK_SPEED(slot) = 0;
}

/* NES Z_04.asm:1975 _TryShooting. [01] = qspeed on entry; blue walkers
 * ($01/$03/$09/$0A) skip the gate, others need ShootTimer != 0 or
 * Random,X >= $F8; then [00] = shot type and the shared body. */
void enrt_try_shooting(unsigned char qspeed, unsigned char shot_type, unsigned int slot)
{
    unsigned char type = (unsigned char)ENEMY_TYPE(slot);

    RAM(0x0001) = qspeed;
    if (!(type == 0x01u || type == 0x03u || type == 0x09u || type == 0x0Au)
        && OBJ(0x0451, slot) == 0u && ENEMY_RNG_A(slot) < 0xF8u)
        return;                                 /* @Exit: speed unchanged */
    RAM(0x0000) = shot_type;
    enrt_try_shooting_body(qspeed, shot_type, slot);
}

void enrt_update_moblin(unsigned int slot) {
    ENEMY_AIR_SPEED(slot) = 0xA0;
    c_wanderer_target_player(slot);
    enrt_try_shooting(0x20, 0x5Bu, slot);
}

/* NES Z_04.asm:1965 UpdateLynel — falls into Goriya update body, then
 * shoots a sword-shot ($57) at qspeed $20. Mirrors UpdateMoblin but
 * with Goriya AI + sword projectile instead of arrow ($5B). */
void enrt_update_lynel(unsigned int slot) {
    enrt_update_goriya(slot);
    enrt_try_shooting(0x20, 0x57u, slot);
}

void enrt_update_stalfos(unsigned int slot) {
    enrt_update_common_wanderer(0x80u, slot);
    c_check_monster_collisions(slot);
    enrt_animate_and_draw_common_object(8u, slot);

    RAM(0x0001) = 0x20;

    if (SAVE_SLOT_QUEST(SAVE_SLOT_INDEX) == 0)
        return;

    if (OBJ(0x0451, slot) == 0 && ENEMY_RNG_A(slot) < 0xF8)
        return;

    /* @PrepareToShoot: the _TryShooting body without its [00] store; the
     * sword-shot type reaches [00] only through _ShootIfWanted. */
    enrt_try_shooting_body(0x20, 0x57u, slot);
}

/* Phase 7 Task 7.2 step 11 — drain NES UpdateDarknut (Z_04.asm:6474).
 *
 *   LDA #$80                                     ; turn rate $80
 *   JSR UpdateCommonWanderer
 *   JSR CheckMonsterCollisions
 *   LDA #$00 / STA ObjStunTimer,X                ; never stunned
 *   LDA #$08 / JSR Anim_AdvanceAnimCounterAndSetObjPosForSpriteDescriptor
 *   ; A=0 on return; reload ObjDir and derive frame
 *   LDA ObjDir,X / CMP #$02 / BNE :+ / INC $0F   ; hflip if facing left
 *   LSR / LSR                                    ; 2 up, 1 down, 0 horizontal
 *   LDY ObjAnimFrame,X / BEQ :+
 *     CLC / ADC #$03                             ; frame 1 = base + 3
 *     LDY ObjDir,X / CPY #$08 / BNE :+ / INC $0F ; hflip if facing up + frame=1
 *   JSR DrawObjectNotMirrored / RTS
 *
 * Custom frame derivation forbids reusing enrt_animate_and_draw_common_object
 * (which always passes frame=0 to the draw call). */
void enrt_update_darknut(unsigned int slot) {
    enrt_update_common_wanderer(0x80u, slot);
    c_check_monster_collisions(slot);
    ENEMY_STUN_TIMER(slot) = 0u;
    z07_anim_advance_and_fetch(8u, slot);

    unsigned char dir = (unsigned char)ENEMY_DIR(slot);

    if (dir == 0x02u)
        RAM(0x000F) = (unsigned char)(RAM(0x000F) + 1u);

    unsigned char frame = (unsigned char)(dir >> 2);

    if (ENEMY_DRAW_FRAME(slot) != 0u) {
        frame = (unsigned char)(frame + 3u);
        if (dir == 0x08u)
            RAM(0x000F) = (unsigned char)(RAM(0x000F) + 1u);
    }

    c_draw_object_not_mirrored_with_frame((unsigned int)frame, slot);
}

void enrt_draw_ghini_and_check_collisions(unsigned int slot) {
    unsigned char frame = z07_anim_fetch_obj_pos(slot);
    RAM(0x000D) = frame;
    unsigned char dir = ENEMY_DIR(slot);
    if ((dir & 0x08) == 0) {
        RAM(0x000D) = (unsigned char)(RAM(0x000D) + 1);
        if ((dir & 0x01) != 0) {
            RAM(0x000F) = (unsigned char)(RAM(0x000F) + 1);
        }
    } else {
        if ((dir & 0x02) != 0) {
            RAM(0x000F) = (unsigned char)(RAM(0x000F) + 1);
        }
    }
    c_draw_object_not_mirrored_with_frame((unsigned int)RAM(0x000D), slot);
    c_check_link_collision(slot);
}

void enrt_update_ghini(unsigned int slot) {
    enrt_update_common_wanderer(0xFFu, slot);
    enrt_draw_ghini_and_check_collisions(slot);
    c_check_monster_collisions(slot);
    if (ENEMY_METASTATE(slot) == 0) return;
    unsigned int y = 0x0B;
    while (y != 0) {
        if (OBJ(0x034F, y) == 0x22) {
            OBJ(0x0405, y) = 0x11;
        }
        y--;
    }
}
