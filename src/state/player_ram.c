#include "player_ram.h"

/* Byte guards avoid host/target long-width and endian assumptions. They use
 * separately budgeted capacity, never record padding or D/P mask bytes. */
static const unsigned char s_player_ram_guard[4] = { 0x50u, 0x52u, 0x41u, 0x4Du };

void player_ram_init_inert(void)
{
    unsigned char slot, i;
    g_cur_player = 0u;
    for (slot = 0u; slot < PLAYER_RAM_OBJECT_COUNT; ++slot) {
        ObjectPlayerRefs *ref = &G_OBJECT_PLAYER_REFS[slot];
        ref->capture_player = PLAYER_REF_NONE;
        ref->target_player = PLAYER_REF_NONE;
        ref->status_player = PLAYER_REF_NONE;
        ref->pickup_player = PLAYER_REF_NONE;
    }
    for (i = 0u; i < 4u; ++i) {
        nes_ram[GR_OFF + PLAYER_RAM_CANARY_OFF + i] = s_player_ram_guard[i];
        nes_ram[GR_OFF + PLAYER_RAM_END_CANARY_OFF + i] = s_player_ram_guard[i];
    }
}

unsigned char player_ram_canaries_ok(void)
{
    unsigned char i;
    for (i = 0u; i < 4u; ++i)
        if (nes_ram[GR_OFF + PLAYER_RAM_CANARY_OFF + i] != s_player_ram_guard[i]
            || nes_ram[GR_OFF + PLAYER_RAM_END_CANARY_OFF + i] != s_player_ram_guard[i])
            return 0u;
    return 1u;
}
