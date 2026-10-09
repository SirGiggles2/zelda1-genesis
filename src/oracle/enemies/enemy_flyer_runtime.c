#include "enemy_runtime_private.h"
#include "legacy_bridge.h"

static void enrt_flyer_adjust_speed_threshold(unsigned int slot) {
    unsigned char speed = ENEMY_AIR_SPEED(slot);
    if (speed & 0xE0) {
        /* NES Flyer_CompareMaxSpeed compares (speed & $E0). Same result for
         * the ROM's maxima ($40/$80/$A0/$C0/$E0), exact for any (T-171). */
        z04_flyer_compare_max_speed((unsigned char)(speed & 0xE0), slot);
        return;
    }
    {
        unsigned char rnd = ENEMY_RNG_A(slot);
        ENEMY_MOVE_TIMER(slot) = (rnd & 0x3F) | 0x40;
    }
    z04_flyer_set_flying_state(5, slot);
}

void enrt_flyer_slow_down(unsigned int slot) {
    ENEMY_AIR_SPEED(slot)--;
    enrt_flyer_adjust_speed_threshold(slot);
}

void enrt_flyer_speed_up(unsigned int slot) {
    ENEMY_AIR_SPEED(slot)++;
    enrt_flyer_adjust_speed_threshold(slot);
}

void enrt_set_flying_state_1(unsigned int slot) {
    z04_flyer_set_flying_state(1, slot);
}

void enrt_flyer_fairy_decide_state(unsigned int slot) {
    ENEMY_AI_STATE(slot) = 3;
    ENEMY_TURN_TIMER(slot) = 6;
}

void enrt_defer_bounce(unsigned int slot, unsigned int dir_idx) {
    if (slot != 5 && slot != 10)
        return;
    ENEMY_WALK_SPEED(slot) = Directions8[dir_idx];
}

void enrt_flyer_do_nothing(void) {}

void enrt_flyer_ghini_decide_state(unsigned int slot) {
    unsigned char rnd = ENEMY_RNG_A(slot);
    unsigned int state = (rnd >= 0xA0) ? 2 : (rnd >= 0x08) ? 3 : 4;
    z04_flyer_set_state_and_turns(state, slot);
}

void enrt_flyer_gleeok_head_decide_state(unsigned int slot) {
    unsigned char rnd = ENEMY_RNG_B(slot);
    unsigned int state = (rnd < 0xD0) ? 2 : 3;
    z04_flyer_set_state_and_turns(state, slot);
}

void enrt_flyer_moldorm_decide_state(unsigned int slot) {
    unsigned char rnd = ENEMY_RNG_A(slot);
    unsigned int state = (rnd >= 0x40) ? 2 : 3;
    ENEMY_AI_STATE(slot) = (unsigned char)state;
    ENEMY_TURN_TIMER(slot) = 8;
}

void enrt_flyer_patra_decide_state(unsigned int slot) {
    unsigned char rnd = ENEMY_RNG_A(slot);
    unsigned int state = (rnd >= 0x40) ? 2 : 3;
    ENEMY_AI_STATE(slot) = (unsigned char)state;
    ENEMY_TURN_TIMER(slot) = 8;
}

void enrt_flyer_compare_max_speed(unsigned char speed, unsigned int slot) {
    if (speed < ENEMY_MAX_AIR_SPEED)
        return;
    z04_flyer_set_flying_state(1, slot);
}

void enrt_flyer_keese_decide_state(unsigned int slot) {
    unsigned char rnd = ENEMY_RNG_B(slot);
    unsigned char state;
    if (rnd >= 0xA0)
        state = 2;
    else if (rnd >= 0x20)
        state = 3;
    else
        state = 4;
    z04_flyer_set_state_and_turns(state, slot);
}

void enrt_flyer_peahat_decide_state(unsigned int slot) {
    unsigned char rnd = ENEMY_RNG_A(slot);
    unsigned char state;
    if (rnd >= 0xB0)
        state = 2;
    else if (rnd >= 0x20)
        state = 3;
    else
        state = 4;
    z04_flyer_set_state_and_turns(state, slot);
}

void enrt_init_peahat(unsigned int slot) {
    z07_reset_obj_metastate_and_timer(slot);
    ENEMY_DIR(slot) = 8;
    z04_end_init_flyer(slot);
}

void enrt_init_pond_fairy(unsigned int slot) {
    ENEMY_SFX_SECRET = 8;
    ENEMY_X(slot) = 120;
    ENEMY_Y(slot) = 125;
}

void enrt_end_init_flyer(unsigned int slot) {
    enrt_reset_flyer_state(slot);
    ENEMY_MAX_AIR_SPEED = 0xA0;
    ENEMY_AIR_SPEED(slot) = 31;
}

void enrt_set_up_fairy_object(unsigned int slot) {
    ENEMY_SFX_SECRET = 8;
    enrt_reset_flyer_state(slot);
    ENEMY_DIR(slot) = 8;
    ENEMY_AIR_SPEED(slot) = 127;
    ENEMY_MAX_AIR_SPEED = 0xA0;
}

void enrt_flyer_delay(unsigned int slot) {
    if (ENEMY_MOVE_TIMER(slot) == 0)
        ENEMY_AI_STATE(slot) = 0;
}

void enrt_reset_flyer_state(unsigned int slot) {
    ENEMY_PUSH_TIMER(slot) = 0;
    ENEMY_TURN_TIMER(slot) = 0;
    ENEMY_FLAP_PHASE(slot) = 0;
    ENEMY_AI_STATE(slot) = 0;
    ENEMY_HIT_REACTION(slot) = 0;
}

void enrt_reset_push_timer(unsigned int slot) {
    ENEMY_PUSH_TIMER(slot) = 0;
}

void enrt_jumper_reset_vspeed_frac(unsigned int slot) {
    ENEMY_AIR_SPEED(slot) = 0;
}

void enrt_flyer_set_flying_state(unsigned int val, unsigned int slot) {
    ENEMY_AI_STATE(slot) = (unsigned char)val;
}

void enrt_init_blue_keese(unsigned int slot) {
    unsigned char rnd = ENEMY_RNG_A(slot) & 0x07;
    ENEMY_DIR(slot) = Directions8[rnd];
    enrt_reset_flyer_state(slot);
    ENEMY_MAX_AIR_SPEED = 0xC0;
    ENEMY_AIR_SPEED(slot) = 31;
}

void enrt_init_red_or_black_keese(unsigned int slot) {
    enrt_init_blue_keese(slot);
    ENEMY_AIR_SPEED(slot) = 127;
}

void enrt_update_keese(unsigned int slot) {
    if (!(ENEMY_PAUSE_FLAG | ENEMY_FREEZE_FLAG)) {
        c_control_keese_flight(slot);
        c_move_flyer(slot);
    }
    z07_anim_fetch_obj_pos(slot);
    c_draw_object_mirrored_with_frame((ENEMY_FLAP_PHASE(slot) >> 1) & 1, slot);
    c_check_monster_collisions(slot);
    c_reset_shove_info(slot);
}

void enrt_move_flyer(unsigned int slot) {
    unsigned int sum = (unsigned int)(ENEMY_AIR_SPEED(slot) & 0xE0u) + ENEMY_FLYER_SPEED_FRAC(slot);
    unsigned char dir = ENEMY_DIR(slot);
    unsigned char mask = 1;

    ENEMY_FLYER_SPEED_FRAC(slot) = (unsigned char)sum;
    if ((sum & 0x100u) == 0)
        return;

    if (dir & mask) {
        ENEMY_X(slot)++;
        ENEMY_FLYER_X_FINE(slot)++;
    }
    mask <<= 1;
    if (dir & mask) {
        ENEMY_X(slot)--;
        ENEMY_FLYER_X_FINE(slot)--;
    }
    mask <<= 1;
    if (dir & mask) {
        ENEMY_Y(slot)++;
        ENEMY_FLYER_Y_FINE(slot)++;
    }
    mask <<= 1;
    if (dir & mask) {
        ENEMY_Y(slot)--;
        ENEMY_FLYER_Y_FINE(slot)--;
    }

    /* NES Z_04.asm:MoveFlyer @End runs only after the fractional speed
     * carries into a whole-pixel move. $0437 drives Keese/Peahat frames;
     * BoundFlyer reverses movement at the room edge. */
    ENEMY_FLAP_PHASE(slot)++;
    enrt_bound_flyer(slot);
}

void enrt_bound_flyer(unsigned int slot) {
    /* Drain Rule D1 (NES asm wins ties): per Z_04.asm:11649 BoundFlyer
     * the post-bounds check is `LDA $0F; BNE Exit` — NOT BLOCKED ($0F
     * != 0) exits without reversing direction. BLOCKED ($0F == 0)
     * falls through to ReverseObjDir8. Original drained code had the
     * sense inverted (returned on blocked, reversed on not-blocked)
     * which caused Tektites/jumpers to reverse direction every frame
     * even in open rooms — they stayed pinned because consecutive
     * reversals cancelled motion. Fix 2026-05-15. */
    ENEMY_FRAME_FLAGS = ENEMY_DIR(slot);
    c_bound_direction_horizontally(slot);
    if (ENEMY_TYPE(slot) != 0x20)
        c_bound_direction_vertically(slot);
    if (ENEMY_FRAME_FLAGS != 0)
        return;
    c_reverse_obj_dir8(slot);
}
