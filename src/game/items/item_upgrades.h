#ifndef ITEM_UPGRADES_H
#define ITEM_UPGRADES_H

#include "platform_abi.h"
#include "../../state/game_profile.h"

/* T-418. NES source: inventory tiers / HandleShotBlocked / WieldCandle.
 * Drained C: existing cave, fire and weapon-collision owners.
 * Coverage: PARTIAL (user-requested Remix extensions). Stance: EXTEND.
 * Named inventory fields remain the existing transitional inventory owner;
 * these predicates introduce no independent state or save representation. */
static inline unsigned char item_upgrade_boomerang(void)
{ return (unsigned char)(game_profile_is_remix() && F_ram_0675_N != 0u); }
static inline unsigned char item_upgrade_candle(void)
{ return (unsigned char)(game_profile_is_remix() && F_ram_065B_N == 2u); }
static inline unsigned char item_upgrade_wand(void)
{ return (unsigned char)(game_profile_is_remix() && F_NES_INV_BOOK_R != 0u); }
static inline unsigned char item_upgrade_wand_weapon(unsigned int slot)
{
    if (!item_upgrade_wand()) return 0u;
    return (unsigned char)((slot == 14u && (F_ram_00AC_X2(slot) & 0x80u)) ||
        (slot == 18u && F_ram_00AC_X2(slot) >= 0x32u && F_ram_00AC_X2(slot) < 0x40u));
}
static inline unsigned char item_upgrade_is_bubble(unsigned char type)
{ return (unsigned char)(type >= 0x2Bu && type <= 0x2Du); }

#endif
