/* save_game.h — persistent NES-format saves (T-100).
 *
 * The NES SaveRAM block (nes_ram[$6000..$652F], see save_serializer.h) is
 * persisted byte-for-byte to cart SRAM logical $000..$52F. Options stay at
 * $800 (sram_abi.h). Slot indices are 0..2; bad indices return 0.
 */
#ifndef SAVE_GAME_H
#define SAVE_GAME_H

/* Power-on / title Start: cart -> NES save block, then the NES file A
 * validation and slot-info copy (UpdateMode0Demo_Sub1/Sub2). */
void save_game_boot(void);

/* IsSaveSlotActive[slot] from the slot info (valid after boot). */
unsigned char save_game_slot_active(unsigned char slot);

/* NES QuestNumbers[slot]: 0 = first quest, 1 = second quest. */
unsigned char save_game_slot_quest(unsigned char slot);

/* Continue: @ChoseSlot copy of file A into the live profile. Returns 1 if
 * the slot is active and was loaded, 0 otherwise (profile untouched). */
unsigned char save_game_load_slot(unsigned char slot);

/* Mode $0D save of the live profile into CurSaveSlot ($16), then commit
 * the whole block to cart SRAM. Returns 1 on success. */
unsigned char save_game_save_current(void);

/* T-099 File Select operations. Each commits the save block to cart.
 *
 * Register (NES UpdateModeERegister): name[8] in NES tile codes. An
 * inactive slot with a non-blank name becomes a new file: items zero
 * except HeartValues $22, HeartPartial $FF, MaxBombs 8; quest 2 when the
 * name starts with "ZELDA". Returns 1 if a file was created. */
unsigned char save_game_register(unsigned char slot, const unsigned char *name);

/* Elimination (NES DeleteSlot): FormatFileA + blank name in slot info. */
void save_game_erase(unsigned char slot);

/* COPY SAVE (custom File Select feature, not on the NES): copies file A of
 * src over dst, including name. Returns 0 if src is inactive or src==dst. */
unsigned char save_game_copy(unsigned char src, unsigned char dst);

/* Slot info for display: name[8] tiles, hearts value/partial, deaths. */
const volatile unsigned char *save_game_slot_name(unsigned char slot);
unsigned char save_game_slot_hearts(unsigned char slot);
unsigned char save_game_slot_heart_partial(unsigned char slot);
unsigned char save_game_slot_deaths(unsigned char slot);

#endif /* SAVE_GAME_H */
