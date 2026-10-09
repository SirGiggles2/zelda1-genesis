#ifndef ENEMY_MANHANDLA_RUNTIME_H
#define ENEMY_MANHANDLA_RUNTIME_H

#include "c_runtime.h"

void enrt_manhandla_set_all_segments_direction(unsigned int val);
void enrt_manhandla_check_collisions(unsigned int slot);
void enrt_manhandla_move(unsigned int slot);
void enrt_manhandla_draw(unsigned int slot);

#endif
