/* SGDK joy.c: readTeamPlayer/readEa4WayPlay define the logical index ABI.
 * Keep polling/protocol setup here; native player routing owns no IO registers. */
#include <genesis.h>
#include "controller_abi.h"
#include "../game/input/controller_runtime.h"

_Static_assert(BUTTON_A == 0x0040u && BUTTON_B == 0x0010u,
               "Native normalization follows the pinned SGDK button ABI");
static unsigned char s_port_layout[2];

static void controller_noop_event(u16 joy, u16 changed, u16 state)
{
    (void)joy; (void)changed; (void)state;
}

void controller_party_init(void)
{
    unsigned char port;
    /* Pinned SGDK readTeamPlayer calls joyEventCB on change without the
     * null guard used by its other readers (joy.c:959). A real first press
     * otherwise jumps through NULL and traps. Use its public callback API;
     * preserve any event consumer installed by the host. */
    if ((JOY_getPortType(PORT_1) == PORT_TYPE_TEAMPLAYER ||
         JOY_getPortType(PORT_2) == PORT_TYPE_TEAMPLAYER) &&
        JOY_getEventHandler() == 0)
        JOY_setEventHandler(controller_noop_event);
    for (port = 0u; port < 2u; ++port) {
        const u8 type = JOY_getPortType(port);
        s_port_layout[port] = CONTROLLER_STANDARD;
        if (type == PORT_TYPE_EA4WAYPLAY) {
            s_port_layout[port] = CONTROLLER_EA4WAY;
            if (port == PORT_1) JOY_setSupport(PORT_1, JOY_SUPPORT_EA4WAYPLAY);
        } else if (type == PORT_TYPE_TEAMPLAYER) {
            s_port_layout[port] = CONTROLLER_TEAMPLAYER;
            JOY_setSupport(port, JOY_SUPPORT_TEAMPLAYER);
        } else if (port == PORT_2 && type == PORT_TYPE_PAD) {
            /* Original boot policy; Remix enables P2 extended controls
             * when its file profile enters gameplay. */
            JOY_setSupport(PORT_2, JOY_SUPPORT_3BTN);
        } else if (type == PORT_TYPE_UNKNOWN) {
            /* T-415: on real consoles some six-button pads fail JOY_reset's
             * one-shot ID read (SGDK issue #363: PORT_TYPE_UNKNOWN), which
             * leaves the port OFF and the whole pad dead. Poll it as a pad
             * anyway: read6Btn classifies 3/6-button per read and an empty
             * port's pull-ups read as no buttons. */
            JOY_setSupport(port, port == PORT_1 ? JOY_SUPPORT_6BTN : JOY_SUPPORT_3BTN);
        }
    }
}

void controller_party_profile(unsigned char remix)
{
    const u8 type = JOY_getPortType(PORT_2);
    if (type == PORT_TYPE_PAD || type == PORT_TYPE_UNKNOWN)
        JOY_setSupport(PORT_2, remix ? JOY_SUPPORT_6BTN : JOY_SUPPORT_3BTN);
}

unsigned short controller_read_party(unsigned char player)
{
    const unsigned char joy = controller_runtime_route(player, s_port_layout[0],
                                                       s_port_layout[1]);
    return joy == CONTROLLER_DISCONNECTED ? 0u : JOY_readJoypad(joy);
}
