#ifndef COMBO_RAM_H
#define COMBO_RAM_H

/* T-260: all Remix B+C combo state, in the native block (GR_COMBO_OFF).
 * Regular .bss has no room (it ends 14 B under the $FF7FFE heap wall). */
#include "../../abi/platform_abi.h"
#include "../../state/game_ram_reserve.h"
#include "../enemies/enemy_render.h"      /* enemy_render_entry_t */

#define COMBO_PLAYERS        4u
#define COMBO_BURST_N        8u
#define COMBO_BURST_SPRITES  16u          /* 8 arrows x at most 2 sprites */

typedef struct {
    unsigned char state, x, y, grid, frac, cnt;
} ComboBurstArrow;

typedef struct {
    /* combo_input.c */
    unsigned short mask_b, mask_c, mask_x;
    unsigned char pending[COMBO_PLAYERS], age[COMBO_PLAYERS];   /* pending: 1 B, 2 C */
    /* combo_party.c */
    unsigned char fire[COMBO_PLAYERS], party_room, party_level;
    /* arrow.c bomb arrow: 1 flying, 2 blast pending */
    unsigned char explode[COMBO_PLAYERS], blast_x[COMBO_PLAYERS], blast_y[COMBO_PLAYERS];
    unsigned char fire_room[COMBO_PLAYERS], fire_level[COMBO_PLAYERS];
    /* bomb.c: slot $10/$11 bomb emits the burst */
    unsigned char emit[2];
    /* combo_burst.c */
    unsigned char burst_room, burst_level, burst_pal, sprite_count;
    ComboBurstArrow burst[COMBO_BURST_N];
    enemy_render_entry_t sprites[COMBO_BURST_SPRITES];
    unsigned short tap[COMBO_PLAYERS]; /* ring taps owed until chord window ends */
    unsigned char blast_slot[COMBO_PLAYERS]; /* reserved shared slot, or zero */
    unsigned char borrowed_arrow; /* collision is a burst, not the real arrow */
} ComboRam;

_Static_assert(sizeof(ComboRam) <= GR_COMBO_SIZE, "ComboRam fits its native reserve");

#define COMBO_RAM ((ComboRam *)(void *)(nes_ram + GR_COMBO_OFF))

/* combo_burst.c: this update's burst sprites (NES tile/attrs/x/y, item
 * tiles; the emitter adds the item-atlas marker), after the weapons. */
const enemy_render_entry_t *combo_burst_sprites(unsigned char *count);

#endif /* COMBO_RAM_H */
