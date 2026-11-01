#include "input_state.h"

PlayerInputState g_input_state[INPUT_STATE_PLAYERS];
static unsigned short s_game_held[INPUT_STATE_PLAYERS],s_game_pressed[INPUT_STATE_PLAYERS];

void input_state_clear(void)
{
    unsigned char i;
    for (i = 0; i < INPUT_STATE_PLAYERS; ++i) {
        g_input_state[i].held = 0u;
        g_input_state[i].pressed = 0u;
        g_input_state[i].released = 0u;
        s_game_held[i]=s_game_pressed[i]=0u;
    }
}

void input_state_set(unsigned char player, unsigned short held)
{
    unsigned short previous;
    if (player >= INPUT_STATE_PLAYERS) {
        return;
    }
    previous = g_input_state[player].held;
    g_input_state[player].held = held;
    g_input_state[player].pressed = (unsigned short)(held & (unsigned short)~previous);
    g_input_state[player].released = (unsigned short)((unsigned short)~held & previous);
    s_game_held[player]=held;
    s_game_pressed[player]=g_input_state[player].pressed;
}

unsigned short input_state_held(unsigned char player)
{
    return (player < INPUT_STATE_PLAYERS) ? g_input_state[player].held : 0u;
}

unsigned short input_state_pressed(unsigned char player)
{
    return (player < INPUT_STATE_PLAYERS) ? g_input_state[player].pressed : 0u;
}

unsigned short input_state_released(unsigned char player)
{
    return (player < INPUT_STATE_PLAYERS) ? g_input_state[player].released : 0u;
}
void input_state_filter_game(unsigned char player,unsigned short held,unsigned short pressed)
{
    if(player>=INPUT_STATE_PLAYERS)return;
    s_game_held[player]=held;s_game_pressed[player]=pressed;
}
unsigned short input_state_game_held(unsigned char player)
{
    return player<INPUT_STATE_PLAYERS?s_game_held[player]:0u;
}
unsigned short input_state_game_pressed(unsigned char player)
{
    return player<INPUT_STATE_PLAYERS?s_game_pressed[player]:0u;
}
