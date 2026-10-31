#ifndef CONTROLLER_RUNTIME_H
#define CONTROLLER_RUNTIME_H

#define CONTROLLER_STANDARD 0u
#define CONTROLLER_TEAMPLAYER 1u
#define CONTROLLER_EA4WAY 2u
#define CONTROLLER_DISCONNECTED 255u

/* SGDK logical joy index, not a physical port number. */
unsigned char controller_runtime_route(unsigned char player,
                                       unsigned char port1,
                                       unsigned char port2);
unsigned short controller_runtime_normalize(unsigned short raw,
                                             unsigned char swap_ab);
unsigned char controller_runtime_bare_start(unsigned short held,
                                             unsigned short pressed);
#endif
