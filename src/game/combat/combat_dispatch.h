/* combat_dispatch.h — native combat subsystem dispatch (Phase 4).
 *
 * Native rewrite of src/oracle/combat/combat_runtime.c. Damage +
 * monster-died bookkeeping. Both ROMs link.
 */

#ifndef COMBAT_DISPATCH_H
#define COMBAT_DISPATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Play parry tune unless damage type is sword-shot ($20) or fire ($08).
 * NES PlayParrySoundForDamageType. */
void combat_play_parry_sound_for_damage_type(void);

/* Bump room kill count + chain kill count, mark dead-dummy obj, clear
 * stun + shove + inv timer for the slot. NES HandleMonsterDied. */
void combat_handle_monster_died(unsigned int slot);

/* SFX_COMBAT = 2; subtract COMBAT_DAMAGE_AMOUNT from MON_HP(slot) — if
 * underflow or HP reaches 0, route to combat_handle_monster_died.
 * NES DealDamage. */
void combat_deal_damage(unsigned int slot);

#ifdef __cplusplus
}
#endif

#endif /* COMBAT_DISPATCH_H */
