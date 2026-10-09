/* uw_person_dispatch.c — native underworld person subsystem dispatch
 * (Phase 4).
 *
 * Drain MATCH (verified-by-use). Pure C; calls native core_* +
 * progress_*. Drain provenance: src/oracle/cave/uw_person_runtime.c.
 */

#include "uw_person_dispatch.h"
#include <stdint.h>
#include "platform_abi.h"
#include "cave_state.h"        /* CAVE_DELAY_TIMER, CAVE_TEXT_SELECTOR,
                                * CAVE_TEXT_LINE_ADDR_LO, CAVE_ROOM_TYPE */
#include "object_state.h"      /* OBJ_STATE, OBJ_TILE_Y, OBJ_TYPE */
#include "link_state.h"        /* LINK_MOVING_DIR */
#include "item_state.h"        /* ITEM_SFX_PRIMARY */
#include "room_state.h"        /* ROOM_SHUTTER_TRIGGERED */
#include "combat_state.h"      /* MON_STATUS_FLAGS, LINK_HEARTS, LINK_PARTIAL_HEART */
#include "core/core_dispatch.h"      /* core_cue_transfer_buf_and_advance_state,
                                      * core_set_up_common_cave_objects,
                                      * core_play_character_sfx,
                                      * core_destroy_monster,
                                      * core_init_one_simple_object,
                                      * core_abs */
#include "world/progress_dispatch.h" /* progress_set_room_flag_uw_item_state,
                                      * progress_get_room_flag_uw_item_state */
#include "world/draw_dispatch.h"     /* draw_animate_item_object,
                                      * draw_object_mirrored,
                                      * draw_object_not_mirrored */
#include "world/sprite_dispatch.h"   /* sprite_anim_fetch_obj_pos */
#include "combat/link_collision_dispatch.h" /* link_collision_check_monster_collisions */
#include "room_state.h"             /* ROOM_OBJ_STUN_TIMER, ROOM_TRANSFER_BUF_SELECT */
#include "enemy_state.h"            /* ENEMY_STATUE_PERSON_FIREBALLS */
#include "progress_state.h"         /* FRAME_COUNTER, CUR_LEVEL */

/* Z_01.asm UnderworldPersonTextSelectorsB[8]. drain at
 * uw_person_runtime.c:35. */
static const unsigned char k_underworld_person_text_selectors_b[8] = {
    0x2Au, 0x38u, 0x3Au, 0x2Cu, 0x40u, 0x42u, 0x42u, 0x3Cu
};

/* Z_01.asm UnderworldPersonTextSelectorsA[8]. */
static const unsigned char k_underworld_person_text_selectors_a[8] = {
    0x28u, 0x26u, 0x2Eu, 0x30u, 0x32u, 0x3Eu, 0x3Eu, 0x34u
};

/* Z_01.asm TextboxLineAddrsLo[3]. */
static const unsigned char k_uw_textbox_line_addrs_lo[3] = {
    0xC4u, 0xE4u, 0xA4u
};

/* Z_01.asm UnderworldPersonTextSelectorsC[4], indexed by type - $4B for
 * all eight person types: entries 4..7 are the first bytes of
 * InitUnderworldPersonC that follow the table (bank 1 $8A84..$8A87,
 * A9 78 A0 80, read from the PRG0 ROM). */
static const unsigned char k_underworld_person_text_selectors_c[8] = {
    0x44u, 0x46u, 0x48u, 0x4Au, 0xA9u, 0x78u, 0xA0u, 0x80u
};

/* Z_01.asm RupeeStashXs[10] (1281). */
static const unsigned char k_rupee_stash_xs[10] = {
    0x78u, 0x70u, 0x80u, 0x60u, 0x70u, 0x80u, 0x90u, 0x70u, 0x80u, 0x78u
};

/* Z_01.asm RupeeStashYs[10] (1287). */
static const unsigned char k_rupee_stash_ys[10] = {
    0x70u, 0x80u, 0x80u, 0x90u, 0x90u, 0x90u, 0x90u, 0xA0u, 0xA0u, 0xB0u
};

/* Z_01.asm LifeOrMoneyItemXs[2] (687). */
static const unsigned char k_life_or_money_item_xs[2] = {
    0x58u, 0x98u
};

/* Z_01.asm LifeOrMoneyItemTypes[2] (691). */
static const unsigned char k_life_or_money_item_types[2] = {
    0x1Au, 0x18u
};

/* z07_reset_moving_dir is `LINK_MOVING_DIR = 0` — inline to avoid the
 * not-yet-native core ASM-bridge wrapper. NES ResetMovingDir is one
 * STA, the helper exists only for cross-bank dispatch. */
static inline void uw_reset_moving_dir(void)
{
    LINK_MOVING_DIR = 0u;
}

void uw_person_update_life_or_money_state_0(void)
{
    /* drain at uw_person_runtime.c:88-91. */
    CAVE_DELAY_TIMER = 10u;
    core_cue_transfer_buf_and_advance_state(118u);
}

void uw_person_check_person_blocking(void)
{
    /* drain at uw_person_runtime.c:116-122. */
    if ((unsigned char)OBJ_TILE_Y(0) >= 0x8Eu) {
        return;
    }
    if (((unsigned char)LINK_MOVING_DIR & 0x08u) == 0u) {
        return;
    }
    uw_reset_moving_dir();
}

void uw_person_flag_item_taken_and_advance_state(void)
{
    /* drain at uw_person_runtime.c:110-114. */
    progress_set_room_flag_uw_item_state();
    CAVE_DELAY_TIMER = 64u;
    core_cue_transfer_buf_and_advance_state(30u);
}

void uw_person_update_grumble1(void)
{
    /* drain at uw_person_runtime.c:124-131. */
    const unsigned char val = (unsigned char)OBJ_STATE(15);
    if ((val & 0x80u) == 0u) {
        return;
    }
    OBJ_STATE(0) = 64u;
    ITEM_SFX_PRIMARY = 4u;
    uw_person_flag_item_taken_and_advance_state();
}

void uw_person_init_underworld_person_b(unsigned int slot)
{
    /* drain at uw_person_runtime.c:34-43. */
    core_set_up_common_cave_objects(120u, slot, 0x80u);
    const unsigned char obj_type = (unsigned char)OBJ_TYPE(slot);
    const unsigned char idx = (unsigned char)(obj_type - 0x4Bu);
    CAVE_TEXT_SELECTOR = k_underworld_person_text_selectors_b[idx & 7u];
    core_play_character_sfx();
}

void uw_person_destroy_if_taken(unsigned int slot)
{
    /* drain at uw_person_runtime.c:93-101. */
    const unsigned char item_state = progress_get_room_flag_uw_item_state();
    if (item_state == 0u) {
        core_play_character_sfx();
        return;
    }
    OBJ_STATE(0) = 0u;
    core_destroy_monster(slot);
}

void uw_person_init_underworld_person_a(unsigned int slot)
{
    /* drain at uw_person_runtime.c:133-143. */
    core_set_up_common_cave_objects(120u, slot, 0x80u);
    const unsigned char idx =
        (unsigned char)((unsigned char)OBJ_TYPE(slot) - 0x4Bu);
    CAVE_TEXT_SELECTOR = k_underworld_person_text_selectors_a[idx & 7u];
    if ((unsigned char)CAVE_ROOM_TYPE == 0x4Fu) {
        uw_person_destroy_if_taken(slot);
        return;
    }
    core_play_character_sfx();
}

void uw_person_init_life_or_money_full(unsigned int slot)
{
    /* drain at uw_person_runtime.c:103-108. */
    core_set_up_common_cave_objects(120u, slot, 0x80u);
    CAVE_TEXT_SELECTOR = 54u;
    CAVE_TEXT_LINE_ADDR_LO = k_uw_textbox_line_addrs_lo[2];
    uw_person_destroy_if_taken(slot);
}

void uw_person_init_grumble_full(unsigned int slot)
{
    /* drain at uw_person_runtime.c:62-74. */
    core_set_up_common_cave_objects(120u, slot, 0x80u);
    CAVE_TEXT_SELECTOR = 36u;
    CAVE_TEXT_LINE_ADDR_LO = k_uw_textbox_line_addrs_lo[2];
    const unsigned char item_state = progress_get_room_flag_uw_item_state();
    if (item_state == 0u) {
        core_play_character_sfx();
        return;
    }
    OBJ_STATE(0) = 0u;
    CAVE_ROOM_TYPE = 0u;
}

void uw_person_init_underworld_person_c(unsigned int slot)
{
    /* drain at uw_person_runtime.c:45-60. */
    core_set_up_common_cave_objects(120u, slot, 0x80u);
    core_play_character_sfx();
    const unsigned char obj_type = (unsigned char)OBJ_TYPE(slot);
    const unsigned char idx = (unsigned char)(obj_type - 0x4Bu);
    CAVE_TEXT_SELECTOR = k_underworld_person_text_selectors_c[idx & 7u];
    if (obj_type != 0x4Bu) {
        return;
    }
    if ((unsigned char)RAM(0x0671) != 0xFFu) {
        return;
    }
    ROOM_SHUTTER_TRIGGERED = 1u;
    OBJ_STATE(0) = 0u;
    core_destroy_monster(slot);
}

void uw_person_init_rupee_stash_full(unsigned int slot)
{
    /* drain at uw_person_runtime.c:76-86. */
    CAVE_TMP1 = (uint8_t)MON_STATUS_FLAGS(slot);
    CAVE_TMP0 = 53u;
    for (unsigned char i = 10u; i >= 1u; --i) {
        core_init_one_simple_object(i);
        OBJ_TILE_X(i) = k_rupee_stash_xs[i - 1u];
        OBJ_TILE_Y(i) = k_rupee_stash_ys[i - 1u];
    }
}

void uw_person_update_complex_state_sense_link(void)
{
    /* drain at uw_person_runtime.c:145-163. */
    if ((unsigned char)CAVE_ROOM_TYPE != 0x4Fu) {
        return;
    }
    if ((unsigned char)OBJ_TILE_X(0) != 0x78u) {
        return;
    }
    const unsigned char ydiff =
        (unsigned char)((unsigned char)OBJ_TILE_Y(0) - 0x98u);
    if (core_abs((unsigned int)ydiff) >= 6u) {
        return;
    }
    if ((unsigned char)LINK_RUPEES < 100u) {
        return;
    }
    CAVE_DOOR_REPAIR_RUPEE_DELTA =
        (uint8_t)(100u + (unsigned char)CAVE_DOOR_REPAIR_RUPEE_DELTA);
    ROOM_SFX_MAIN = 8u;
    const unsigned char max_bombs =
        (unsigned char)((unsigned char)LINK_MAX_HEARTS + 4u);
    LINK_MAX_HEARTS = max_bombs;
    LINK_BOMB_COUNT = max_bombs;
    uw_person_flag_item_taken_and_advance_state();
}

void uw_person_update_life_or_money_state_2(void)
{
    /* drain at uw_person_runtime.c:165-198. */
    for (signed char i = 1; i >= 0; --i) {
        if ((unsigned char)OBJ_TILE_X(0) !=
            k_life_or_money_item_xs[(unsigned char)i]) {
            continue;
        }
        const unsigned char ydiff =
            (unsigned char)((unsigned char)OBJ_TILE_Y(0) - 0x98u);
        if (core_abs((unsigned int)ydiff) >= 6u) {
            continue;
        }
        if (i != 0) {
            if ((unsigned char)LINK_RUPEES < 50u) {
                return;
            }
            CAVE_DOOR_REPAIR_RUPEE_DELTA =
                (uint8_t)(50u + (unsigned char)CAVE_DOOR_REPAIR_RUPEE_DELTA);
        } else {
            const unsigned char hearts = (unsigned char)LINK_HEARTS;
            const unsigned char containers = (unsigned char)(hearts & 0xF0u);
            if (containers >= 0x30u) {
                const unsigned char new_cont =
                    (unsigned char)(containers - 0x10u);
                int partial = (int)(hearts & 0x0Fu) - 1;
                CAVE_TMP0 = new_cont;
                if (partial < 0) {
                    partial = 0;
                }
                LINK_HEARTS = (uint8_t)(new_cont | (unsigned char)partial);
            } else {
                LINK_HEARTS = containers;
                LINK_PARTIAL_HEART = 0u;
            }
        }
        ROOM_SFX_MAIN = 8u;
        ROOM_SHUTTER_TRIGGERED = 1u;
        uw_person_flag_item_taken_and_advance_state();
        return;
    }
}

void uw_person_draw_life_or_money_items(void)
{
    /* drain at uw_person_runtime.c:210-217. NES DrawLifeOrMoneyItems.
     * Loops i = 1 -> 0 ; sets ObjX[19]=LifeOrMoneyItemXs[i],
     * ObjY[19]=$98, AnimateItemObject(LifeOrMoneyItemTypes[i], 19). */
    for (signed char i = 1; i >= 0; --i) {
        RAM(0x0083u) = k_life_or_money_item_xs[(unsigned char)i];
        RAM(0x0097u) = 0x98u;
        draw_animate_item_object(
            k_life_or_money_item_types[(unsigned char)i], 19u);
    }
}

void uw_person_person_check_collisions(unsigned int slot)
{
    /* drain at uw_person_runtime.c:200-208. NES PersonCheckCollisions. */
    link_collision_check_monster_collisions(slot);
    const unsigned char killed = (unsigned char)ROOM_OBJ_STUN_TIMER(0);
    if (killed) {
        ENEMY_STATUE_PERSON_FIREBALLS = killed;
        ROOM_OBJ_STUN_TIMER(0) = 0u;
    }
}

void uw_person_person_draw_and_check_collisions(unsigned int slot)
{
    /* drain at uw_person_runtime.c:28-32. NES Person_DrawAndCheckCollisions. */
    uw_person_person_check_collisions(slot);
    sprite_anim_fetch_obj_pos(slot);
    draw_object_mirrored(0u, slot);
}

/* uwrt_complex_state_begin file-static helper.
 * drain at uw_person_runtime.c:20-26. */
static void uw_person_complex_state_begin(void)
{
    if ((unsigned char)CAVE_ROOM_TYPE == 0x4Fu) {
        ROOM_TRANSFER_BUF_SELECT = 108u;
    }
    CAVE_DELAY_TIMER = 10u;
    CAVE_PERSON_STATE =
        (uint8_t)((unsigned char)CAVE_PERSON_STATE + 1u);
}

/* Textbox state arm — fully native via cave's char-streamer port
 * (cave_dispatch.c:780+ post-D3). Forward decl avoids header cycle
 * between uw_person_dispatch.h and cave_dispatch.h. */
extern void cave_update_person_state_textbox(void);
static inline void uw_person_state_textbox(void)
{
    cave_update_person_state_textbox();
}

/* Link_EndMoveAndAnimate_Bank1 in UpdateGrumble3: Link is halted
 * (ObjState $40) and standing still, so the ladder check skips and
 * CheckWarps cannot fire (he would have warped on arrival); what remains
 * is AnimateLinkBase, which walks Link's legs while halted (T-057:
 * t057_grumble, ObjAnimCounter/ObjAnimFrame; the old stub pinned the
 * counter at 6 like Link_EndMoveAndDraw). */
extern void roomrom_combat_animate_link_base(void);  /* combat_runtime.c */
static void uw_person_link_end_move_and_animate(void)
{
    /* Link's sprite is drawn natively from these cells (main.c); an OAM
     * copy here drew him twice (t057_grumble t800 screen diff). */
    roomrom_combat_animate_link_base();          /* AnimateLinkBase */
}

void uw_person_update_grumble3(void)
{
    /* drain at uw_person_runtime.c:219-230. NES UpdateGrumble3. */
    uw_person_link_end_move_and_animate();
    if ((unsigned char)CAVE_DELAY_TIMER != 0u) {
        return;
    }
    OBJ_STATE(15) = 0u;
    RAM(0x065Du) = 0u;
    OBJ_STATE(0) = 0u;
    CAVE_ROOM_TYPE = 0u;
}

void uw_person_update_person_complex(unsigned int slot)
{
    /* drain at uw_person_runtime.c:232-249. NES UpdateUnderworldPerson_Complex. */
    const unsigned char state = (unsigned char)CAVE_PERSON_STATE;
    if (state != 4u || ((unsigned char)FRAME_COUNTER & 1u) == 0u) {
        uw_person_person_draw_and_check_collisions(slot);
        if ((unsigned char)CAVE_ROOM_TYPE == 0x4Fu) {
            RAM(0x0083u) = 120u;
            RAM(0x0097u) = 0x98u;
            draw_animate_item_object(24u, 19u);
        }
    }
    switch (state) {
        case 0u: uw_person_complex_state_begin(); break;
        case 1u: uw_person_state_textbox(); break;
        case 2u: uw_person_update_complex_state_sense_link(); break;
        case 3u: core_cue_transfer_blank_person_wares(); break;
        case 4u: core_uw_person_complex_state_delay_and_quit(); break;
        default: break;
    }
}

void uw_person_update_person_full(unsigned int slot)
{
    /* drain at uw_person_runtime.c:251-263. NES UpdateUnderworldPerson_Full. */
    const unsigned char level = (unsigned char)CUR_LEVEL;
    if (level < 3u || level == 5u || level == 7u) {
        uw_person_update_person_complex(slot);
        return;
    }
    uw_person_person_draw_and_check_collisions(slot);
    switch ((unsigned char)CAVE_PERSON_STATE) {
        case 0u: core_update_person_state_reset_char_offset(); break;
        case 1u: uw_person_state_textbox(); break;
        case 2u: /* z01_update_person_state_do_nothing — no-op */ break;
        default: break;
    }
}

void uw_person_update_grumble_full(unsigned int slot)
{
    /* drain at uw_person_runtime.c:265-278. NES UpdateGrumble_Full. */
    const unsigned char state = (unsigned char)CAVE_PERSON_STATE;
    if (state != 3u || ((unsigned char)FRAME_COUNTER & 1u) == 0u) {
        uw_person_person_check_collisions(slot);
        sprite_anim_fetch_obj_pos(slot);
        draw_object_not_mirrored(0u, slot);
    }
    switch (state) {
        case 0u: uw_person_state_textbox(); break;
        case 1u: uw_person_update_grumble1(); break;
        case 2u: core_cue_transfer_blank_person_wares(); break;
        case 3u: uw_person_update_grumble3(); break;
        default: break;
    }
}

void uw_person_update_life_or_money_full(unsigned int slot)
{
    /* drain at uw_person_runtime.c:280-293. NES UpdateUnderworldPersonLifeOrMoney_Full. */
    const unsigned char state = (unsigned char)CAVE_PERSON_STATE;
    if (state != 4u || ((unsigned char)FRAME_COUNTER & 1u) == 0u) {
        uw_person_person_draw_and_check_collisions(slot);
        uw_person_draw_life_or_money_items();
    }
    switch (state) {
        case 0u: uw_person_update_life_or_money_state_0(); break;
        case 1u: uw_person_state_textbox(); break;
        case 2u: uw_person_update_life_or_money_state_2(); break;
        case 3u: core_cue_transfer_blank_person_wares(); break;
        case 4u: core_uw_person_complex_state_delay_and_quit(); break;
        default: break;
    }
}
