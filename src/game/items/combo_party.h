#ifndef COMBO_PARTY_H
#define COMBO_PARTY_H

/* T-260 Remix combo glue. filter: once per update after item_ring_tick;
 * fire: at each player's item-use point (player context loaded, idle gate
 * passed); reset_all: pause / scene change. Original: filter is a no-op. */
void combo_party_filter(unsigned char player_count);
void combo_party_fire(unsigned char player);
unsigned char combo_party_pending_fire(unsigned char player);
void combo_party_reset_all(void);

#endif /* COMBO_PARTY_H */
