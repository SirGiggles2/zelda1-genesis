/* NES source: unchanged button/action rules; Genesis SGDK joy.c xlt tables.
 * Drained C: existing host party pad selection and A/B policy.
 * Coverage: player routing/normalization, not input polling or game simulation.
 * Stance: EXTEND with explicit physical-adapter to native-player handoff. */
#include "controller_runtime.h"

unsigned char controller_runtime_route(unsigned char player,
                                       unsigned char port1,
                                       unsigned char port2)
{
    static const unsigned char first_tap[4] = { 0u, 2u, 3u, 4u };
    static const unsigned char second_tap[4] = { 0u, 1u, 5u, 6u };
    if (player >= 4u) return CONTROLLER_DISCONNECTED;
    /* A port-1 adapter owns the first four party positions. EA uses both
     * connectors and has the same SGDK joy index order as TeamPlayer 1. */
    if (port1 == CONTROLLER_TEAMPLAYER || port1 == CONTROLLER_EA4WAY)
        return first_tap[player];
    if (port2 == CONTROLLER_TEAMPLAYER)
        return second_tap[player];
    return player < 2u ? player : CONTROLLER_DISCONNECTED;
}

unsigned short controller_runtime_normalize(unsigned short raw,
                                             unsigned char swap_ab)
{
    if (!swap_ab) return raw;
    return (unsigned short)((raw & (unsigned short)~0x0050u) |
                            ((raw & 0x0010u) << 2) | ((raw & 0x0040u) >> 2));
}

/* T-251: the shared menu accepts one Start edge from any party pad.
 * Preserve the host's modifier/chord policy; directions do not block Start.
 * NES source: Z_05.asm:UpdateMenuActive; multiplayer plan P6.5.
 * Drained C: existing main.c handle_pause_start; Coverage: PARTIAL party
 * arbitration only; Stance: EXTEND the single existing pause owner. */
unsigned char controller_runtime_bare_start(unsigned short held,
                                             unsigned short pressed)
{
    return (unsigned char)((pressed & 0x0080u) != 0u &&
                           (held & 0x0F70u) == 0u);
}
