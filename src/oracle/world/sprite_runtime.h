#ifndef SPRITE_RUNTIME_H
#define SPRITE_RUNTIME_H

#include "platform_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

void sprrt_cycle_cur_sprite_index(void);
unsigned char sprrt_cycle_sprite_index_in_a(unsigned char idx);
void sprrt_hide_object_sprites(void);
void sprrt_show_link_sprites_behind_horizontal_doors(void);
void sprrt_roll_over_anim_counter(unsigned int slot);
unsigned char sprrt_anim_fetch_obj_pos(unsigned int slot);
void sprrt_anim_set_obj_hflip(unsigned int slot);
void sprrt_anim_advance_and_fetch(unsigned int val, unsigned int slot);
void sprrt_animate_object_walking(unsigned int slot);

#ifdef __cplusplus
}
#endif

#endif
