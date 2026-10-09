/* weapon_dispatch.h — native weapon subsystem dispatch (Phase 4).
 *
 * Native rewrite of src/oracle/items/weapon_runtime.c. Both ROMs link.
 *
 * RoomRom has its own roomrom_combat / boomerang / arrow / bomb /
 * sword for live B-item handling; this dispatch is the NES gameplay-
 * tier weapon-spawn path.
 */

#ifndef WEAPON_DISPATCH_H
#define WEAPON_DISPATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Place weapon at Link's position + offset based on facing dir.
 * NES PlaceWeapon. drain at weapon_runtime.c:19-28. */
void weapon_place_weapon(unsigned char offset, unsigned int slot);

/* LINK_ACTION_TIMER = 16; weapon_place_weapon(16, slot).
 * NES PlaceWeaponForPlayerState. */
void weapon_place_weapon_for_player_state(unsigned int slot);

/* OBJ_ANIM_TIMER(0) = 1; weapon_place_weapon_for_player_state.
 * NES PlaceWeaponForPlayerStateAndAnim. */
void weapon_place_weapon_for_player_state_and_anim(unsigned int slot);

/* OBJ_STATE(slot) = weapon_state; weapon_place_weapon_for_player_state_and_anim.
 * NES PlaceWeaponForPlayerStateAndAnimAndWeaponState. */
void weapon_place_weapon_for_player_state_and_anim_and_weapon_state(
    unsigned char weapon_state, unsigned int slot);

/* Wield bomb: choose A/B draw slot, decrement bomb count, place
 * weapon with state 17. NES WieldBomb. */
void weapon_wield_bomb(unsigned int slot);

/* Wield candle: choose A/B draw slot, set candle-lit flag, position
 * weapon, play sfx 4, place weapon. Returns 0. NES WieldCandle. */
unsigned int weapon_wield_candle(unsigned int slot);
/* NES WieldFlute (Z_07.asm:2449). */
void weapon_wield_flute(void);
/* NES WieldPotion (Z_05.asm:3011). */
void weapon_wield_potion(void);

#ifdef __cplusplus
}
#endif

#endif /* WEAPON_DISPATCH_H */
