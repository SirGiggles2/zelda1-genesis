#ifndef ENEMY_DODONGO_RUNTIME_H
#define ENEMY_DODONGO_RUNTIME_H

#include "c_runtime.h"

void enrt_dodongo_dec_bloated_timer(unsigned int slot);
void enrt_update_dodongo_state2_stunned(unsigned int slot);
void enrt_init_dodongo(unsigned int slot);
void enrt_update_dodongo_state1_bloated_sub_die(unsigned int slot);
void enrt_update_dodongo_bloated_sub_end(unsigned int slot);
void enrt_dodongo_check_collisions_standard_size(unsigned int slot);
void enrt_dodongo_check_collisions(unsigned int slot);
unsigned int enrt_dodongo_is_bomb_in_range(unsigned int limit_idx);
void enrt_dodongo_try_eat_bomb(unsigned int slot);
void enrt_dodongo_check_bomb_hit(unsigned int slot);
void enrt_dodongo_draw(unsigned int slot);

#endif
