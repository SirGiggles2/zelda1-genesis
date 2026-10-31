#ifndef INPUT_STATE_H
#define INPUT_STATE_H

/* T-367: explicit per-player held/pressed/released input.
 *
 * Raw button values are the Genesis pad bits supplied by engine after its
 * profile remap/swap (BUTTON_* values). This module deliberately does not
 * include <genesis.h>; it stores opaque 16-bit masks and exposes edge state.
 *
 * Action timers (sword swing, item use) live in PlayerState/weapon records,
 * never in these fields.
 */
#define INPUT_STATE_PLAYERS 4u

typedef struct PlayerInputState {
    unsigned short held;
    unsigned short pressed;
    unsigned short released;
} PlayerInputState;

extern PlayerInputState g_input_state[INPUT_STATE_PLAYERS];

void input_state_clear(void);
void input_state_set(unsigned char player, unsigned short held);
unsigned short input_state_held(unsigned char player);
unsigned short input_state_pressed(unsigned char player);
unsigned short input_state_released(unsigned char player);
/* Gameplay copy; filtering never changes physical history or edge detection. */
void input_state_filter_game(unsigned char player,unsigned short held,unsigned short pressed);
unsigned short input_state_game_held(unsigned char player);
unsigned short input_state_game_pressed(unsigned char player);

#endif /* INPUT_STATE_H */
