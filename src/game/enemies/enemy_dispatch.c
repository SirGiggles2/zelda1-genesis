/* enemy_dispatch.c — native enemy subsystem dispatch (Phase 4).
 *
 * Phase 4 first enemy batch: 5 trivial leaf helpers. Pure C, no
 * shims. Drain MATCH per finding 4_7n.
 */

#include "enemy_dispatch.h"
#include <stdint.h>            /* uint8_t */
#include "platform_abi.h"      /* RAM, OBJ, NES_OBJ_TYPE */
#include "enemy_state.h"       /* ENEMY_OAM_HIDE_*, ENEMY_SFX_*, ENEMY_NEXT_SHOT_SLOT, ENEMY_X/Y, ENEMY_DIR, ENEMY_RNG_A/B, ENEMY_FRAME_FLAGS, ENEMY_BLOCKED_FLAG, ENEMY_AI_STATE, ENEMY_TURN_TIMER, ENEMY_INVINCIBILITY, ENEMY_TYPE, ENEMY_CUR_SPRITE_ATTR_ROW, ENEMY_MOVE_TIMER */
#include "combat_state.h"      /* MON_SUBSTATE, COMBAT_PART_INDEX, OBJ_DIR */
#include "core/core_dispatch.h"  /* core_anim_set_sprite_desc_attrs */
#include "world/progress_dispatch.h"  /* progress_get_room_flag_uw_item_state */
#include "combat/combat_dispatch.h"   /* combat_deal_damage */

/* T5.2 plan v5b — DMC SFX dispatch from enemy tune writers. NES asm
 * audio driver consumed SampleRequest ($0600) bitmap and triggered the
 * indexed DMC sample. Our XGM-backed audio_sfx_play takes 1..7 = direct
 * sample index. NES bitmap → sample index per DMC_SAMPLE_LOOKUP
 * (src/nes_io.asm:2547):
 *   $01 (bit 0) → sample 1
 *   $02 (bit 1) → sample 2  (boss hit/death roar)
 *   $04 (bit 2) → sample 3  (secret revealed / door unlock)
 *   $08 (bit 3) → sample 4  (Link hurt)
 *   $10 (bit 4) → sample 5  (Aquamentus/Gleeok/Ganon roar)
 *   $20 (bit 5) → sample 6  (Dodongo/Gohma roar)
 *   $40 (bit 6) → sample 7  (Digdogger/Manhandla/Patra roar)
 */
extern void audio_sfx_play(unsigned char sfx);

unsigned int enemy_find_empty_monster_slot(void)
{
    /* drain at enemy_runtime.c:12-20. NES FindEmptyMonsterSlot.
     * Scan slots 11..1 (skip slot 0 = Link); first empty slot wins. */
    for (signed char i = 11; i >= 1; i--) {
        if (OBJ(NES_OBJ_TYPE, (unsigned char)i) == 0u) {
            ENEMY_NEXT_SHOT_SLOT = (unsigned char)i;
            return (unsigned int)(unsigned char)i;
        }
    }
    return 0u;
}

void enemy_hide_sprites_over_link(void)
{
    /* drain at enemy_common_runtime.c:4-7. */
    ENEMY_OAM_HIDE_0 = 0xF8u;
    ENEMY_OAM_HIDE_1 = 0xF8u;
}

void enemy_play_secret_found_tune(void)
{
    /* drain at enemy_common_runtime.c:9-11. */
    ENEMY_SFX_SECRET = 4u;
}

void enemy_play_boss_death_cry(void)
{
    /* drain at enemy_common_runtime.c:13-16. */
    ENEMY_SFX_BOSS_CRY = 2u;
    ENEMY_SFX_BOSS_CRY_FLAGS = 0x80u;
}

void enemy_gohma_play_parry_tune(void)
{
    /* drain at enemy_common_runtime.c:18-20. */
    ENEMY_SFX_PARRY = 1u;
}

/* NES Z_01.asm ReverseDirections (line 3003): $08 $04 $02 $01.
 * Used by walker_alt_dir_get_random_perpendicular. */
static const unsigned char k_reverse_directions[4] = {
    0x08u, 0x04u, 0x02u, 0x01u
};

unsigned int enemy_walker_alt_dir_get_opposite(void)
{
    /* drain at enemy_wanderer_runtime.c:14-19. NES WalkerAltDirGetOpposite. */
    const unsigned char dir = ENEMY_FRAME_FLAGS;
    if (dir & 0x0Au) {
        return (unsigned int)(dir >> 1);
    }
    return (unsigned int)((dir << 1) & 0xFFu);
}

void enemy_walker_alt_dir_end_loop(void)
{
    /* drain at enemy_wanderer_runtime.c:21-23. */
    ENEMY_BLOCKED_FLAG = 0u;
}

unsigned char enemy_walker_alt_dir_get_random_perpendicular(unsigned int slot)
{
    /* drain at enemy_wanderer_runtime.c:25-31. NES
     * WalkerAltDirGetRandomPerpendicular.
     *
     *   rnd = ENEMY_RNG_A(slot)
     *   dir = ENEMY_DIR(slot)
     *   idx = (rnd & $80) ? 0 : 1
     *   if (dir & $0C) idx += 2     ; vertical movement → use second pair
     *   return ReverseDirections[idx] */
    const unsigned char rnd = (unsigned char)ENEMY_RNG_A(slot);
    const unsigned char dir = (unsigned char)ENEMY_DIR(slot);
    unsigned int idx = (rnd & 0x80u) ? 0u : 1u;
    if (dir & 0x0Cu) {
        idx += 2u;
    }
    return k_reverse_directions[idx];
}

/* NES Z_04.asm TektiteStartingDirs (line 1832): $01 $02 $05 $0A. */
static const unsigned char k_tektite_starting_dirs[4] = {
    0x01u, 0x02u, 0x05u, 0x0Au
};

/* NES Z_04.asm GanonStartXs (line 10488): $30 $B0. */
static const unsigned char k_ganon_start_xs[2] = { 0x30u, 0xB0u };

void enemy_flyer_set_state_and_turns(unsigned int state, unsigned int slot)
{
    /* drain at enemy_boss_runtime.c:88-91. */
    ENEMY_AI_STATE(slot) = (uint8_t)state;
    ENEMY_TURN_TIMER(slot) = 6u;
}

void enemy_anim_set_sprite_desc_level_palette_row(void)
{
    /* drain at enemy_boss_runtime.c:98-100. NES — boss palette row 3. */
    (void)core_anim_set_sprite_desc_attrs(3u);
}

void enemy_init_aquamentus(unsigned int slot)
{
    /* drain at enemy_boss_runtime.c:102-107. */
    ENEMY_INVINCIBILITY(slot) = 0xE2u;
    ENEMY_SFX_BOSS_CRY = 16u;
    ENEMY_X(slot) = 0xB0u;
    ENEMY_Y(slot) = 0x80u;
}

void enemy_init_tektite(unsigned int slot)
{
    /* drain at enemy_boss_runtime.c:119-124. NES InitTektite. */
    const unsigned char rnd = (unsigned char)(ENEMY_RNG_B(slot) & 0x03u);
    const unsigned char dir = k_tektite_starting_dirs[rnd];
    ENEMY_DIR(slot) = dir;
    ENEMY_MOVE_TIMER(slot) = (uint8_t)(dir << 2);
}

void enemy_ganon_randomize_location(unsigned int slot)
{
    /* drain at enemy_boss_runtime.c:238-241. */
    ENEMY_Y(slot) = 0xA0u;
    ENEMY_X(slot) = k_ganon_start_xs[ENEMY_CUR_SPRITE_ATTR_ROW & 0x01u];
}

void enemy_jumper_point_boulder_downward(unsigned int slot)
{
    /* drain at enemy_boss_runtime.c:243-247. NES — only if type == $20. */
    if (ENEMY_TYPE(slot) != 0x20u) {
        return;
    }
    ENEMY_DIR(slot) = (uint8_t)((ENEMY_DIR(slot) & 0x03u) | 0x04u);
}

void enemy_set_dead_dummy_obj_type(unsigned int slot)
{
    /* drain at enemy_boss_runtime.c:361-363. */
    ENEMY_TYPE(slot) = 93u;
}

void enemy_play_boss_hit_cry_if_needed(unsigned int slot)
{
    /* drain at enemy_boss_runtime.c:374-377. */
    if (ENEMY_HIT_REACTION(slot) == 0x10u) {
        ENEMY_SFX_BOSS_CRY = 2u;
    }
}

void enemy_ganon_get_cur_cloud_bottom(unsigned int slot)
{
    /* drain at enemy_boss_runtime.c:379-381. */
    ENEMY_SCRATCH_Y =
        (uint8_t)((unsigned char)ENEMY_Y(slot) +
                  (unsigned char)ENEMY_BOUNCE_FLAGS(slot));
}

void enemy_ganon_activate_room_item(void)
{
    /* drain at enemy_boss_runtime.c:365-372. NES GanonActivateRoomItem. */
    if (ENEMY_LIFE(0) == 0u) {
        return;
    }
    if (progress_get_room_flag_uw_item_state() != 0u) {
        return;
    }
    ENEMY_LIFE(0) = 0u;
    ENEMY_SFX_SECRET = 2u;
}

void enemy_check_boss_hit_reaction(unsigned int slot)
{
    /* drain at enemy_boss_runtime.c:93-96. NES CheckBossHitReaction. */
    /* TODO Phase 4: native equivalent of z04_play_boss_death_cry_if_needed
     * (z04 bank logic not yet drained). */
    core_set_shove_info_with0(0u, slot);
}

void enemy_gohma_handle_weapon_collision(unsigned int monster_slot,
                                         unsigned int weapon_slot)
{
    /* drain at reference/aldonunez/Z_04.asm:6787-6832. NES
     * Gohma_HandleWeaponCollision.
     *
     * D2 = monster_slot, D3 = weapon_slot (asm convention).
     *
     * Behavior:
     *   1. If weapon is arrow ($12), set arrow life timer to 40 +
     *      arrow spark state to 4.
     *   2. If COMBAT_PART_INDEX is not 3 or 4 (eye parts) -> parry tune.
     *   3. If MON_SUBSTATE(monster_slot) != 3 (eye not open) -> parry tune.
     *   4. If OBJ_DIR(weapon_slot) != 8 (arrow not aimed up) -> parry tune.
     *   5. RAM($0601)=2; deal damage; play boss hit cry.
     *   6. Fall through to parry tune (NES asm has no rts before
     *      PlayParryTune label — both damage and parry tune fire when
     *      hit lands).
     */
    if (weapon_slot == 0x12u) {
        /* nes_ram[$00AC + weapon_slot] = 40 (arrow's lifetime/anim).
         * Use OBJ()-style addressing to mirror asm's lea+move.b. */
        OBJ(0x00ACu, weapon_slot) = 40u;
        OBJ(0x03D0u, weapon_slot) = 4u;
    }

    const unsigned char part = (unsigned char)COMBAT_PART_INDEX;
    if (part != 3u && part != 4u) {
        enemy_gohma_play_parry_tune();
        return;
    }
    if ((unsigned char)MON_SUBSTATE(monster_slot) != 3u) {
        enemy_gohma_play_parry_tune();
        return;
    }
    if ((unsigned char)OBJ_DIR(weapon_slot) != 8u) {
        enemy_gohma_play_parry_tune();
        return;
    }
    RAM(0x0601u) = 2u;
    combat_deal_damage(monster_slot);
    enemy_play_boss_hit_cry_if_needed(monster_slot);
    /* Fall-through: NES Gohma_HandleWeaponCollision_PlayParryTune.
     * Damage + parry tune both fire on successful hit. */
    enemy_gohma_play_parry_tune();
}
