/* weapon_dispatch.c — native weapon subsystem dispatch (Phase 4).
 *
 * Drain MATCH (verified-by-use). Pure C; calls native core_play_effect
 * (NATIVE_CORE chain) directly.
 */

#include "weapon_dispatch.h"
#include <stdint.h>
#include "platform_abi.h"
#include "world_state.h"       /* WORLD_TMP*, LINK_X/_Y, LINK_DIR, WEAPON_DRAW_SLOT_*, SFX_COMBAT, OBJ_QSPD_FRAC, OBJ_POS_FRAC, OBJ_GRID_OFFSET, OBJ_MOVE_TIMER, OBJ_ANIM_TIMER */
#include "combat_state.h"      /* LINK_ACTION_TIMER, OBJ_STATE, OBJ_DIR, OBJ_X, OBJ_Y */
#include "item_state.h"        /* LINK_BOMB_COUNT, LINK_CANDLE_LEVEL */
#include "core/core_dispatch.h"  /* core_play_effect */

/* Forward decl for static helper. */
static unsigned char weapon_choose_offset_for_direction_h(unsigned char dir);

/* Candle lit flag. Defined in world_state.h. */

/* CANDLE_LIT_FLAG = RAM(0x0513). */
#ifndef CANDLE_LIT_FLAG_REF
#define CANDLE_LIT_FLAG_REF  RAM(0x0513u)
#endif

static unsigned char weapon_choose_offset_for_direction_h(unsigned char dir)
{
    /* drain at weapon_runtime.c:5-17. */
    WORLD_TMP0 = 0u;
    const unsigned char d = (unsigned char)(dir & 0x03u);
    if (d == 0u) {
        return 0u;
    }
    if (d & 0x01u) {
        return (unsigned char)WORLD_TMP2;
    }
    return (unsigned char)WORLD_TMP3;
}

void weapon_place_weapon(unsigned char offset, unsigned int slot)
{
    /* drain at weapon_runtime.c:19-28. */
    WORLD_TMP2 = offset;
    WORLD_TMP3 = 0xF0u;
    const unsigned char player_dir = (unsigned char)LINK_DIR;
    OBJ_DIR(slot) = player_dir;
    OBJ_X(slot) =
        (uint8_t)((unsigned char)LINK_X +
                  weapon_choose_offset_for_direction_h(player_dir));
    OBJ_Y(slot) =
        (uint8_t)((unsigned char)LINK_Y +
                  weapon_choose_offset_for_direction_h(
                      (unsigned char)(player_dir >> 2)));
}

void weapon_place_weapon_for_player_state(unsigned int slot)
{
    LINK_ACTION_TIMER = 16u;
    weapon_place_weapon(16u, slot);
}

void weapon_place_weapon_for_player_state_and_anim(unsigned int slot)
{
    OBJ_ANIM_TIMER(0) = 1u;
    weapon_place_weapon_for_player_state(slot);
}

void weapon_place_weapon_for_player_state_and_anim_and_weapon_state(
    unsigned char weapon_state, unsigned int slot)
{
    OBJ_STATE(slot) = weapon_state;
    weapon_place_weapon_for_player_state_and_anim(slot);
}

void weapon_wield_bomb(unsigned int slot)
{
    /* drain at weapon_runtime.c:45-75. */
    (void)slot;
    if (LINK_BOMB_COUNT == 0u) {
        return;
    }
    unsigned int use_slot = WEAPON_DRAW_SLOT_A;
    {
        const unsigned char state16 = (unsigned char)OBJ_STATE(WEAPON_DRAW_SLOT_A);
        if (state16 != 0u && (state16 & 0xF0u) == 0x10u) {
            use_slot = WEAPON_DRAW_SLOT_B;
            const unsigned char state17 =
                (unsigned char)OBJ_STATE(WEAPON_DRAW_SLOT_B);
            if (state17 != 0u && (state17 & 0xF0u) == 0x10u) {
                return;
            }
        }
    }
    {
        const unsigned int other_slot = use_slot ^ 1u;
        const unsigned char other_state =
            (unsigned char)OBJ_STATE(other_slot);
        if (other_state != 0u && other_state < 0x13u) {
            return;
        }
    }
    LINK_BOMB_COUNT = (uint8_t)(LINK_BOMB_COUNT - 1u);
    SFX_COMBAT = 32u;
    OBJ_MOVE_TIMER(use_slot) = 0u;
    weapon_place_weapon_for_player_state_and_anim_and_weapon_state(17u, use_slot);
}

unsigned int weapon_wield_candle(unsigned int slot)
{
    /* drain at weapon_runtime.c:77-99. */
    (void)slot;
    unsigned int use_slot = WEAPON_DRAW_SLOT_A;
    if (OBJ_STATE(WEAPON_DRAW_SLOT_A) != 0u) {
        use_slot = WEAPON_DRAW_SLOT_B;
        if (OBJ_STATE(WEAPON_DRAW_SLOT_B) != 0u) {
            return 0u;
        }
    }
    if (LINK_CANDLE_LEVEL == 1u && CANDLE_LIT_FLAG_REF != 0u) {
        return 0u;
    }
    CANDLE_LIT_FLAG_REF = 1u;
    OBJ_GRID_OFFSET(use_slot) = 0u;
    OBJ_POS_FRAC(use_slot) = 0u;
    OBJ_QSPD_FRAC(use_slot) = 32u;
    OBJ_STATE(use_slot) = 33u;
    core_play_effect(4u);
    OBJ_ANIM_TIMER(use_slot) = 4u;
    weapon_place_weapon_for_player_state(use_slot);
    return 0u;
}

/* NES source: Z_07.asm WieldFlute (2449).
 * Drained C: none (the Genesis had a sound-only stub, T-171).
 * Coverage: FULL. Stance: GREENFIELD per asm.
 * Plays the flute tune, sets FluteTimer $98 (UpdateMode5Play waits for
 * it). UW: flags UsedFlute (Digdogger reads it). OW, mode 5 only: a room
 * in FluteRoomSecretsOW reveals its secret (flute secret object $5E: the
 * pond color cycle and stairs) in the quest that owns it, else the
 * whirlwind is summoned (SummonWhirlwind, Z_01.asm). */
extern void trap_summon_whirlwind(void);
static const unsigned char k_flute_room_secrets_ow[11] = {
    0x42u, 0x06u, 0x29u, 0x2Bu, 0x30u, 0x3Au, 0x3Cu, 0x58u,
    0x60u, 0x6Eu, 0x72u
};

void weapon_wield_flute(void)
{
    signed char y;
    unsigned char quest;
    RAM(0x0602u) = 0x10u;                       /* Tune1Request */
    RAM(0x003Cu) = 0x98u;                       /* FluteTimer */
    if (RAM(0x0010u) != 0u) {                   /* CurLevel: UW */
        if (RAM(0x051Bu) == 0u)                 /* UsedFlute */
            RAM(0x051Bu) = 1u;
        return;
    }
    if (RAM(0x0012u) != 0x05u) return;          /* GameMode */
    quest = RAM((unsigned short)(0x062Du + RAM(0x0016u)));   /* QuestNumbers */
    for (y = 10; y >= 0; --y)
        if (RAM(0x00EBu) == k_flute_room_secrets_ow[(unsigned char)y]) break;
    if (y < 0) {                                /* @FluteSecretNotFound */
        trap_summon_whirlwind();
        return;
    }
    /* Room $42 (index 0) is Q1's only flute secret and has none in Q2;
     * the others are Q2 secrets and summon the whirlwind in Q1. */
    if (y == 0) {
        if (quest != 0u) { trap_summon_whirlwind(); return; }
    } else if (quest == 0u) {
        trap_summon_whirlwind();
        return;
    }
    /* @RevealSecret: not while the color cycle runs or is done. */
    if (RAM(0x051Au) != 0u) return;             /* SecretColorCycle */
    /* Empty slot from 8 down to 0 (ObjType+1 + y), as the NES. */
    for (y = 8; y >= 0; --y) {
        if (RAM((unsigned short)(0x0350u + (unsigned char)y)) == 0u) {
            RAM((unsigned short)(0x0350u + (unsigned char)y)) = 0x5Eu;
            return;
        }
    }
}

/* NES source: Z_05.asm WieldPotion (3011).
 * Drained C: none (the Genesis mapped the potion slot to no B item, T-057).
 * Coverage: FULL. Stance: GREENFIELD per asm.
 * One potion drunk: hearts fill (World_FillHearts) while the game is
 * involuntarily paused (Paused 2); World_FillHearts clears both. */
void weapon_wield_potion(void)
{
    if (RAM(0x065Eu) == 0u) return;             /* Potion */
    RAM(0x065Eu) = (unsigned char)(RAM(0x065Eu) - 1u);
    RAM(0x0063u) = 1u;                          /* World_IsFillingHearts */
    RAM(0x00E0u) = 2u;                          /* Paused */
}
