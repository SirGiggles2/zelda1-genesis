#ifndef LOADOUT_H
#define LOADOUT_H

#define LOADOUT_SLOT_NONE 0xFFu

/* Remix B slot is NES SelectedItemSlot ($0656); C has no NES cell. */
unsigned char loadout_c_slot(void);
void loadout_set_c_slot(unsigned char slot);
/* Letter and potion share the same on-screen equipment cell. */
unsigned char loadout_same_cell(unsigned char a, unsigned char b);

/* Native per-player selection; shared inventory ownership is checked by callers. */
void loadout_party_reset(unsigned char b, unsigned char c);
unsigned char loadout_party_b(unsigned char player);
unsigned char loadout_party_c(unsigned char player);
unsigned char loadout_party_set(unsigned char player, unsigned char b, unsigned char c);

/* Redux-only ring intent model. Caller owns one record and B/C pair per player.
 * Inputs are normalized UI intents, not SGDK/legacy addresses. World pause,
 * drawing, resource spending and persistent storage stay caller-owned.
 * Simultaneous B+C follows the existing B-first item precedence. */
#define LOADOUT_RING_HOLD_TICKS 12u
#define LOADOUT_RING_ANIM_TICKS 6u
enum { LOADOUT_RING_B=1u, LOADOUT_RING_C=2u, LOADOUT_RING_PREV=4u,
       LOADOUT_RING_NEXT=8u, LOADOUT_RING_CLOSE=16u };
enum { LOADOUT_RING_USE_B=1u, LOADOUT_RING_USE_C=2u, LOADOUT_RING_OPENED=4u,
       LOADOUT_RING_ASSIGNED_B=8u, LOADOUT_RING_ASSIGNED_C=16u,
       LOADOUT_RING_CLOSED=32u, LOADOUT_RING_REJECTED=64u };
enum { LOADOUT_RING_IDLE=0u, LOADOUT_RING_ARMED=1u, LOADOUT_RING_OPENING=2u,
       LOADOUT_RING_OPEN=3u, LOADOUT_RING_CLOSING=4u };
typedef struct { unsigned char phase,button,held_ticks,highlight,released,animation; } LoadoutRing;
typedef struct { unsigned short held,pressed; unsigned char events; } LoadoutRingInput;
void loadout_ring_reset(LoadoutRing *ring);
unsigned char loadout_ring_visible(const LoadoutRing *ring);
unsigned char loadout_ring_step(LoadoutRing *ring,unsigned char held,unsigned char pressed,
    unsigned short owned,unsigned char *b,unsigned char *c,unsigned char enabled);
/* Post-remap Genesis button masks (sgdk/inc/joy.h), kept free of SGDK IO. */
LoadoutRingInput loadout_ring_filter(LoadoutRing *ring,unsigned short held,unsigned short pressed,
    unsigned short owned,unsigned char *b,unsigned char *c,unsigned char enabled);

#endif /* LOADOUT_H */
