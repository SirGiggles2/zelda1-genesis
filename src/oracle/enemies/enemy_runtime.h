#ifndef ENEMY_RUNTIME_H
#define ENEMY_RUNTIME_H

#include "platform_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* enemy_runtime is owned C for promoted gameplay enemy logic.
 * Generated bank C may wrap these entry points, but new fixes live here.
 */
void enrt_update_bubble(unsigned int slot);
void enrt_update_keese(unsigned int slot);
void enrt_update_rope(unsigned int slot);
void enrt_update_standing_fire(unsigned int slot);
void enrt_update_zol(unsigned int slot);
void enrt_update_gel(unsigned int slot);
void enrt_update_zora(unsigned int slot);
void enrt_update_stalfos(unsigned int slot);
void enrt_update_darknut(unsigned int slot);
void enrt_update_ghini(unsigned int slot);
void enrt_update_moblin(unsigned int slot);
void enrt_update_lynel(unsigned int slot);
void enrt_update_gibdo(unsigned int slot);
void enrt_update_aquamentus(unsigned int slot);
void enrt_draw_ghini_and_check_collisions(unsigned int slot);
void enrt_update_candle(void);
void enrt_update_boulder_set(unsigned int slot);
void enrt_init_leever(unsigned int slot);
void enrt_init_walker(unsigned int slot);
void enrt_init_bubble(unsigned int slot);
void enrt_init_rope(unsigned int slot);
void enrt_init_darknut(unsigned int slot);
void enrt_init_slow_octorock_or_ghini(unsigned int slot);
void enrt_init_fast_octorock(unsigned int slot);
void enrt_init_gel(unsigned int slot);
void enrt_flyer_slow_down(unsigned int slot);
void enrt_flyer_speed_up(unsigned int slot);
void enrt_set_flying_state_1(unsigned int slot);
void enrt_gleeok_contract_segment_x(unsigned int slot);
void enrt_gleeok_contract_segment_y(unsigned int slot);
void enrt_gleeok_contract_segment(unsigned int slot);
void enrt_flyer_fairy_decide_state(unsigned int slot);
void enrt_check_boss_hit_reaction(unsigned int slot);
void enrt_anim_set_sprite_desc_level_palette_row(void);
void enrt_defer_bounce(unsigned int slot, unsigned int dir_idx);
void enrt_flyer_do_nothing(void);
void enrt_flyer_ghini_decide_state(unsigned int slot);
void enrt_flyer_gleeok_head_decide_state(unsigned int slot);
void enrt_flyer_moldorm_decide_state(unsigned int slot);
void enrt_flyer_patra_decide_state(unsigned int slot);
void enrt_flyer_compare_max_speed(unsigned char speed, unsigned int slot);
void enrt_gleeok_ignore_segment(void);
void enrt_flyer_keese_decide_state(unsigned int slot);
void enrt_flyer_peahat_decide_state(unsigned int slot);
void enrt_update_dodongo_state2_stunned(unsigned int slot);
void enrt_init_peahat(unsigned int slot);
void enrt_init_pond_fairy(unsigned int slot);
void enrt_init_dodongo(unsigned int slot);
void enrt_init_aquamentus(unsigned int slot);
void enrt_init_tektite(unsigned int slot);
void enrt_ganon_randomize_location(unsigned int slot);
void enrt_end_init_flyer(unsigned int slot);
void enrt_set_up_fairy_object(unsigned int slot);
void enrt_jumper_point_boulder_downward(unsigned int slot);
void enrt_flyer_delay(unsigned int slot);
void enrt_init_manhandla(unsigned int slot);
void enrt_update_manhandla(unsigned int slot);
void enrt_manhandla_set_all_segments_direction(unsigned int val);
void enrt_manhandla_check_collisions(unsigned int slot);
void enrt_manhandla_move(unsigned int slot);
void enrt_manhandla_draw(unsigned int slot);
void enrt_init_lamnola(unsigned int slot);
void enrt_update_lamnola(unsigned int slot);
void enrt_lamnola_update_head(unsigned int slot);
void enrt_lamnola_move(unsigned int slot);
void enrt_init_moldorm(unsigned int slot);
void enrt_update_moldorm(unsigned int slot);
void enrt_init_ganon(unsigned int slot);
void enrt_update_ganon(unsigned int slot);
void enrt_update_vire(unsigned int slot);
void enrt_update_vire_state(unsigned int slot);
void enrt_check_vire_collisions(unsigned int slot);
void enrt_draw_vire(unsigned int slot);
void enrt_update_statues(void);
void enrt_update_tektite_or_boulder(unsigned int slot);
void enrt_move_flyer(unsigned int slot);
void enrt_bound_flyer(unsigned int slot);
void enrt_reset_flyer_state(unsigned int slot);
void enrt_reset_push_timer(unsigned int slot);
void enrt_set_dead_dummy_obj_type(unsigned int slot);
void enrt_jumper_reset_vspeed_frac(unsigned int slot);
void enrt_gleeok_set_segment_y(unsigned int val3, unsigned int slot);
void enrt_init_gleeok_head(unsigned int slot);
void enrt_ganon_activate_room_item(void);
void enrt_init_monster_shot(unsigned int slot);
void enrt_init_boulder(unsigned int slot);
void enrt_init_boulder_set(unsigned int slot);
void enrt_play_boss_hit_cry_if_needed(unsigned int slot);
void enrt_flyer_set_flying_state(unsigned int val, unsigned int slot);
void enrt_destroy_monster_shot(unsigned int slot);
void enrt_destroy_counted_monster_shot(unsigned int slot);
void enrt_ganon_get_cur_cloud_bottom(unsigned int slot);
void enrt_ganon_get_cur_cloud_right(unsigned int slot);
void enrt_ganon_get_cur_cloud_left(unsigned int slot);
void enrt_ganon_get_cur_cloud_top(unsigned int slot);
void enrt_pols_voice_move_x(unsigned int slot);
void enrt_init_blue_keese(unsigned int slot);
void enrt_init_red_or_black_keese(unsigned int slot);
void enrt_destroy_monster_bank4(unsigned int slot);
unsigned int enrt_shoot_fireball(unsigned int type, unsigned int source_slot);
void enrt_wallmaster_prepare_to_draw(unsigned int slot);
unsigned int enrt_wallmaster_calc_start_position(unsigned int instr_offset,
                                                 unsigned int init_major_min,
                                                 unsigned int slot);
void enrt_wallmaster_put_sprite_behind_bg_if_needed(unsigned int sprite_byte_off);
void enrt_wallmaster_put_sprites_behind_bg_if_needed(void);
void enrt_gohma_set_sprite_attributes(unsigned int slot);
void enrt_init_gohma(unsigned int slot);
unsigned int enrt_shoot(void);
unsigned int enrt_extract_hit_point_value(unsigned int val);
unsigned char enrt_is_dark_room_bank4(unsigned int room_idx);
void enrt_init_digdogger1(unsigned int slot);
void enrt_init_digdogger2(unsigned int slot);
void enrt_update_digdogger(unsigned int slot);
void enrt_init_patra(unsigned int slot);
void enrt_update_patra_child(unsigned int slot);
void enrt_update_dodongo_state1_bloated_sub_die(unsigned int slot);
void enrt_hide_sprites_over_link(void);
void enrt_play_secret_found_tune(void);
void enrt_play_boss_death_cry(void);
void enrt_dodongo_dec_bloated_timer(unsigned int slot);
void enrt_gleeok_dec_head_timer(void);
void enrt_gleeok_set_segment_x(unsigned int val, unsigned int slot);
void enrt_gohma_play_parry_tune(void);
void enrt_flyer_set_state_and_turns(unsigned int state, unsigned int slot);
void enrt_update_dodongo_bloated_sub_end(unsigned int slot);
void enrt_play_boss_death_cry_if_needed(unsigned int slot);
unsigned int enrt_is_quest_secret_mismatch(void);
unsigned int enrt_pols_voice_get_colliding_tile(unsigned int slot);
unsigned int enrt_wizzrobe_get_base_collidable_tile(unsigned int slot);
unsigned int enrt_pols_voice_is_square_walkable(unsigned int slot);
unsigned int enrt_shoot_fireball_55(unsigned int source_slot);
void enrt_animate_and_draw_common_object(unsigned int val, unsigned int slot);
unsigned int enrt_walker_alt_dir_get_opposite(void);
void enrt_walker_alt_dir_end_loop(void);
unsigned char enrt_walker_alt_dir_get_random_perpendicular(unsigned int slot);
unsigned int enrt_find_empty_monster_slot(void);

/* Wanderer / Goriya family (drained from z_04.asm / z_07.asm). */
void enrt_update_common_wanderer(unsigned int turn_rate, unsigned int slot);
void enrt_wanderer_target_player(unsigned int slot);
void enrt_update_goriya(unsigned int slot);
void enrt_walker_set_input_dir_and_try_shooting_boomerang(unsigned int slot);

void enrt_update_block(unsigned int slot);
void enrt_draw_block(unsigned int slot);
void enrt_update_monster_shot(unsigned int slot);
void enrt_draw_shot(unsigned int slot);
void enrt_bounce_shot(unsigned int slot);
void enrt_check_shot_link_collision(unsigned int slot);
void enrt_update_fireball(unsigned int slot);

/* Dodongo family (drained from z_04.asm). */
void enrt_dodongo_check_collisions(unsigned int slot);
void enrt_dodongo_check_collisions_standard_size(unsigned int slot);
void enrt_dodongo_check_bomb_hit(unsigned int slot);
void enrt_dodongo_try_eat_bomb(unsigned int slot);
unsigned int enrt_dodongo_is_bomb_in_range(unsigned int limit_idx);
void enrt_dodongo_draw(unsigned int slot);

#ifdef __cplusplus
}
#endif

#endif /* ENEMY_RUNTIME_H */
