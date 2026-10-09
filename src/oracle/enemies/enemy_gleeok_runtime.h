#ifndef ENEMY_GLEEOK_RUNTIME_H
#define ENEMY_GLEEOK_RUNTIME_H

#include "c_runtime.h"

void enrt_gleeok_dec_head_timer(void);
void enrt_gleeok_set_segment_x(unsigned int val, unsigned int slot);
void enrt_gleeok_contract_segment_x(unsigned int slot);
void enrt_gleeok_contract_segment_y(unsigned int slot);
void enrt_gleeok_contract_segment(unsigned int slot);
void enrt_gleeok_ignore_segment(void);
void enrt_gleeok_set_segment_y(unsigned int val3, unsigned int slot);
void enrt_init_gleeok_head(unsigned int slot);
void enrt_update_gleeok(unsigned int slot);
void enrt_gleeok_store_ref_seg_distance(unsigned int signed_ref_dist);
void enrt_gleeok_check_collisions(unsigned int slot);

#endif
