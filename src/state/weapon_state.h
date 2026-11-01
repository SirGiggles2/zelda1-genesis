#ifndef WEAPON_STATE_H
#define WEAPON_STATE_H

#include "platform_abi.h"

/* T-367: per-player sword lifecycle/pose record.
 *
 * P1 owns lifecycle and pose here and publishes the legacy NES sword slot
 * for collision/render consumers. P2..P4 use this record directly. One
 * owner per player; no singleton publication state.
 */
#include "player_records.h"

/* T-367: single shared boomerang/food record. Slot $0F is a shared object,
 * not a per-player pool, matching the NES contract. */
typedef struct PlayerBoomerangState {
    unsigned char state;
    unsigned char dir;
    unsigned char grid;
    unsigned char qspeed;
    unsigned char owner;
    unsigned char x;
    unsigned char y;
    unsigned char count;
    unsigned char limit;
    unsigned char timer;
    unsigned char pos_frac; /* retained across spawn/state-only scene reset */
} PlayerBoomerangState;

/* Shared capacity-two bomb/fire lifecycle slice. Pose/animation still belong
 * to the declared legacy mover/render boundary until their migration. */
typedef struct SharedEffectState {
    unsigned char state;
    unsigned char timer;
} SharedEffectState;

extern PlayerSwordState g_player_sword[4];
extern PlayerBeamState g_player_beam[4];
extern PlayerBoomerangState g_player_boomerang;
extern SharedEffectState g_shared_effect[2];
/* Slot arguments are the temporary world ABI slots16/17, never player IDs. */
void shared_effect_set_state(unsigned char slot, unsigned char value);
void shared_effect_set_timer(unsigned char slot, unsigned char value);
unsigned char shared_effect_tick_timer(unsigned char slot);

void player_sword_reset(unsigned char player);
void player_sword_reset_all(void);
void player_beam_reset(unsigned char player);
void player_beam_reset_all(void);
/* ClearRam0300UpTo owns the full movement reset; scene init does not. */
void player_beam_clear_fractions(void);
void player_boomerang_reset(void);
/* Called once at the existing unpaused NMI timer phase. */
unsigned char player_boomerang_tick_timer(void);

#endif /* WEAPON_STATE_H */
