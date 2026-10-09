#ifndef LINK_COLLISION_RUNTIME_H
#define LINK_COLLISION_RUNTIME_H

#include "combat_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void lcrt_harm_link(unsigned int monster_slot);
void lcrt_link_be_harmed(unsigned int monster_slot);
void lcrt_check_link_collision_preinit(unsigned int monster_slot);
void lcrt_check_link_collision(unsigned int monster_slot);
void lcrt_check_monster_collisions(unsigned int monster_slot);
void lcrt_begin_shove(unsigned int monster_slot);

#ifdef __cplusplus
}
#endif

/* --- External data arrays used by link_collision_runtime.c --- */
extern const unsigned char ObjTypeToDamagePoints[];

/* --- ASM shim / bank-forwarder functions used by link_collision_runtime.c --- */

#endif
