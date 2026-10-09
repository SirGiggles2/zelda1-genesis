/* combat_dispatch.c — native combat subsystem dispatch (Phase 4).
 *
 * Drain MATCH (verified-by-use). Pure C; calls native core_*.
 * NES sources: PlayParrySoundForDamageType, HandleMonsterDied, DealDamage.
 * Drain provenance: src/oracle/combat/combat_runtime.c.
 */

#include "combat_dispatch.h"
#include <stdint.h>
#include "platform_abi.h"
#include "combat_state.h"   /* COMBAT_DAMAGE_TYPE/_AMOUNT, ROOM_KILL_COUNT,
                             * ROOM_CHAIN_KILL_COUNT, ROOM_CHAIN_KILL_BONUS,
                             * SFX_COMBAT, MON_HP, MON_STUN_TIMER */
#include "core/core_dispatch.h"  /* core_play_parry_tune, core_update_dead_dummy,
                                  * core_reset_shove_info_and_inv_timer */

/* Plan v5b T5.2 — enemy hit / death SFX. SFX IDs registered in
 * data/audio/MANIFEST.json: 1=swordsword, 2=arrowboom, 3=bombdoor,
 * 4=linkhurt, 5=enemyhit, 6=enemykill, 7=itempickup. */
extern void audio_sfx_play(unsigned char sfx);

void combat_play_parry_sound_for_damage_type(void)
{
    /* drain at combat_runtime.c:4-10. */
    const unsigned char dtype = (unsigned char)COMBAT_DAMAGE_TYPE;
    if (dtype == 0x20u || dtype == 0x08u) {
        return;
    }
    core_play_parry_tune();
}

void combat_handle_monster_died(unsigned int slot)
{
    /* drain at combat_runtime.c:12-23. */
    ROOM_KILL_COUNT = (uint8_t)((unsigned char)ROOM_KILL_COUNT + 1u);
    if ((unsigned char)ROOM_CHAIN_KILL_COUNT < 0x0Au) {
        ROOM_CHAIN_KILL_COUNT =
            (uint8_t)((unsigned char)ROOM_CHAIN_KILL_COUNT + 1u);
        if ((unsigned char)ROOM_CHAIN_KILL_COUNT == 0x0Au &&
            (unsigned char)COMBAT_DAMAGE_TYPE == 0x08u) {
            ROOM_CHAIN_KILL_BONUS =
                (uint8_t)((unsigned char)ROOM_CHAIN_KILL_BONUS + 1u);
        }
    }
    core_update_dead_dummy(slot);
    MON_STUN_TIMER(slot) = 0u;
    core_reset_shove_info_and_inv_timer(slot);
}

void combat_deal_damage(unsigned int slot)
{
    /* drain at combat_runtime.c:25-40. */
    SFX_COMBAT = 2u;
    const unsigned char damage = (unsigned char)COMBAT_DAMAGE_AMOUNT;
    const unsigned char hp = (unsigned char)MON_HP(slot);
    if (hp < damage) {
        combat_handle_monster_died(slot);
        return;
    }
    const unsigned char new_hp = (unsigned char)(hp - damage);
    MON_HP(slot) = new_hp;
    if (new_hp == 0u) {
        combat_handle_monster_died(slot);
    }
}
