#ifndef ENEMY_PROJECTILE_RUNTIME_H
#define ENEMY_PROJECTILE_RUNTIME_H

#include "enemy_runtime_private.h"

#ifdef __cplusplus
extern "C" {
#endif

void enrt_init_monster_shot(unsigned int slot);
void enrt_init_monster_shot_unknown54(unsigned int slot);
void enrt_init_boulder(unsigned int slot);
void enrt_init_boulder_set(unsigned int slot);
void enrt_destroy_monster_shot(unsigned int slot);
void enrt_destroy_counted_monster_shot(unsigned int slot);
void enrt_destroy_monster_bank4(unsigned int slot);
unsigned int enrt_shoot_fireball(unsigned int type, unsigned int source_slot);
unsigned int enrt_shoot_fireball_55(unsigned int source_slot);
void enrt_update_candle(void);
void enrt_update_boulder_set(unsigned int slot);
void enrt_draw_shot(unsigned int slot);
void enrt_bounce_shot(unsigned int slot);
void enrt_check_shot_link_collision(unsigned int slot);
void enrt_update_monster_shot(unsigned int slot);
void enrt_update_monster_arrow(unsigned int slot);
void enrt_update_arrow_or_boomerang(unsigned int slot);
unsigned char enrt_fireball_move_one_axis(unsigned char qspeed, unsigned char pos_frac, unsigned int slot);
void enrt_update_fireball(unsigned int slot);

#ifdef __cplusplus
}
#endif

/* --- ASM shim functions used by enemy_projectile_runtime.c --- */
extern void c_move_object(unsigned short slot);
extern void c_draw_arrow(unsigned int slot);
extern void draw_arrow_spark(unsigned int slot);   /* world/draw_dispatch.c */
extern void c_draw_sword_shot_or_magic_shot(unsigned int slot);

#endif
