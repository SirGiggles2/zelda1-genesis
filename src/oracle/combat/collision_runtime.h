#ifndef COLLISION_RUNTIME_H
#define COLLISION_RUNTIME_H

#include "combat_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void colrt_handle_monster_weapon_collision(unsigned int monster_slot, unsigned int weapon_slot);
void colrt_check_monster_weapon_collision(unsigned int monster_slot, unsigned int weapon_y_mid);
void colrt_check_monster_slender_weapon_collision2(unsigned int monster_slot);
void colrt_check_monster_slender_weapon_collision(unsigned int monster_slot, unsigned int damage_points);
void colrt_parry_or_shove(unsigned int monster_slot, unsigned int weapon_slot);
void colrt_check_monster_stabbing_collision(unsigned int monster_slot, unsigned int damage_points);
unsigned char colrt_do_objects_collide_with_thresholds(void);
unsigned char colrt_do_objects_collide(unsigned int threshold);
void colrt_check_monster_sword_collision(unsigned int monster_slot, unsigned int weapon_slot);
void colrt_check_monster_shot_collision(unsigned int monster_slot, unsigned int weapon_slot, unsigned int damage_points);
void colrt_check_monster_arrow_or_rod_collision(unsigned int monster_slot, unsigned int weapon_slot);
void colrt_check_monster_boomerang_or_food_collision(unsigned int monster_slot, unsigned int weapon_slot);
void colrt_check_monster_sword_shot_or_magic_shot_collision(unsigned int monster_slot, unsigned int weapon_slot);
void colrt_check_monster_bomb_or_fire_collision(unsigned int monster_slot, unsigned int weapon_slot);
unsigned char colrt_get_collidable_tile(unsigned int hotspot_offset, unsigned int slot);
unsigned char colrt_get_collidable_tile_still(unsigned int slot);
unsigned char colrt_get_colliding_tile_moving(unsigned int slot);

#ifdef __cplusplus
}
#endif

/* --- External data arrays used by collision_runtime.c --- */
extern const unsigned char PlayAreaColumnAddrs[];
extern const unsigned char WalkableTiles[];

/* --- ASM shim functions used by collision_runtime.c --- */
extern void c_call_gohma_handle_weapon_collision(unsigned int monster_slot, unsigned int weapon_slot);
extern void c_call_begin_shove(unsigned int monster_slot);
extern void c_call_handle_shot_blocked(unsigned int weapon_slot);

#endif
