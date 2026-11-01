#ifndef COMBO_RECIPES_H
#define COMBO_RECIPES_H

/* T-260: Remix ordered B+C item recipes (gate, charge, spawn).
 *
 * NES source: none for combinations. Building blocks Z_05 WieldArrow /
 * Z_01 WieldBomb (costs, one-object limits); error blip = Z_01.asm
 * PlayParryTune (Tune0Request $01). Drained C: arrow.c, bomb.c.
 * Coverage: single-item paths FULL; combinations NONE. Stance: GREENFIELD.
 *
 * Items are inventory/loadout slot numbers (cursor order), the same values
 * loadout_party_b/c return. A recipe charges nothing unless every check
 * passes; any failure requests the error blip and spawns nothing. */
#define COMBO_SLOT_BOMBS  1u
#define COMBO_SLOT_ARROW  2u
#define COMBO_ERROR_TUNE0 0x01u

typedef enum { COMBO_NONE = 0, COMBO_BOMB_ARROW, COMBO_ARROW_BURST } combo_kind_t;
typedef enum { COMBO_OK = 0, COMBO_NO_RECIPE, COMBO_NO_BOW, COMBO_NO_ARROW,
               COMBO_NO_BOMB, COMBO_BUSY } combo_result_t;

combo_kind_t   combo_recipe_lookup(unsigned char b_slot, unsigned char c_slot);
combo_result_t combo_recipe_try(unsigned char player, unsigned char b_slot,
                                unsigned char c_slot);

/* Effect owners: bomb arrow = arrow.c roomrom_arrow_fire_explosive; burst here. */
void combo_arrow_burst_place(unsigned char player);
unsigned char combo_burst_live(void);
/* combo_burst.c: emitted by the burst bomb's detonation; update once per
 * play update after the weapon objects; clear on death/reload. */
void combo_burst_emit(unsigned char x, unsigned char y);
void combo_burst_update(void);
void combo_burst_clear(void);
/* Called only from an enemy owner's admitted arrow collision path. */
void combo_burst_check_monster(unsigned int monster_slot);

#endif /* COMBO_RECIPES_H */
