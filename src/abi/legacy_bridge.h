// src/abi/legacy_bridge.h
//
// Transitional bridge for z01_*/z07_* transpiled-bank symbols still called by
// owned C runtimes during the S1-S10 cutover. Deleted at S10 along with
// src/zelda_translated/ and src/gen/z_*.c.

#ifndef LEGACY_BRIDGE_H
#define LEGACY_BRIDGE_H

// z01 — bank 1 transpiled functions

extern unsigned char z01_abs(unsigned int val);
extern unsigned char z01_anim_set_sprite_desc_attrs(unsigned int val);
extern unsigned int  z01_animate_world_fading(void);
extern void          z01_begin_update_mode(void);
extern unsigned char z01_bound_by_room(unsigned int slot);
extern unsigned char z01_bound_by_room_with_a(unsigned char direction, unsigned int slot);
extern unsigned int  z01_calc_diagonal_speed_index(unsigned int mid_speed_idx);
extern void          z01_check_link_collision(unsigned int slot);
extern void          z01_check_monster_arrow_or_rod_collision(unsigned int monster_slot, unsigned int weapon_slot);
extern void          z01_check_monster_bomb_or_fire_collision(unsigned int monster_slot, unsigned int weapon_slot);
extern void          z01_check_monster_boomerang_or_food_collision(unsigned int monster_slot, unsigned int weapon_slot);
extern void          z01_check_monster_sword_collision(unsigned int monster_slot, unsigned int weapon_slot);
extern void          z01_check_monster_sword_shot_or_magic_shot_collision(unsigned int monster_slot, unsigned int weapon_slot);
extern unsigned char z01_compare_hearts_to_containers(void);
extern void          z01_copy_price_list_template(void);
extern void          z01_cue_transfer_blank_person_wares(void);
extern void          z01_cue_transfer_buf_and_advance_state(unsigned int val);
extern void          z01_deal_damage(unsigned int slot);
extern void          z01_destroy_object_wram(unsigned int val, unsigned int slot);
extern void          z01_destroy_whirlwind(unsigned int slot);
extern unsigned char z01_do_objects_collide_with_thresholds(void);
extern void          z01_fetch_file_a_address_set(void);
extern void          z01_format_decimal_byte(unsigned char val);
extern void          z01_get_directions_and_distances_to_target(unsigned char target_slot, unsigned int origin_slot);
extern void          z01_get_object_middle(unsigned int slot);
extern unsigned int  z01_get_opposite_dir(unsigned int dir);
extern unsigned char z01_get_room_flag_uw_item_state(void);
extern void          z01_inc_cave_state(void);
extern void          z01_init_one_simple_object(unsigned int slot);
extern void          z01_init_underworld_person_do_nothing(void);
extern void          z01_play_character_sfx(void);
extern void          z01_play_effect(unsigned int val);
extern void          z01_play_key_taken_tune(void);
extern void          z01_play_parry_sound_for_damage_type(void);
extern void          z01_play_parry_tune(void);
extern void          z01_play_sample(unsigned int val);
extern void          z01_post_credit(unsigned int val);
extern void          z01_post_debit(unsigned int amount);
extern unsigned char z01_reset_cur_sprite_index(void);
extern void          z01_reset_shove_info_and_inv_timer(unsigned int slot);
extern void          z01_set_item_value(unsigned int val, unsigned int slot3);
extern void          z01_set_room_flag_uw_item_state(void);
extern void          z01_set_up_common_cave_objects(unsigned int x, unsigned int slot, unsigned int y);
extern void          z01_set_up_whirlwind(unsigned int slot);
extern void          z01_silence_all_sound(void);
extern void          z01_take_5_rupees(void);
extern void          z01_take_hearts(void);
extern void          z01_take_hearts_no_sound(void);
extern void          z01_take_item(unsigned char item_type);
extern void          z01_take_one_rupee(void);
extern void          z01_take_power_triforce(void);
extern void          z01_unhalt_link(void);
extern void          z01_update_person_state_do_nothing(void);
extern void          z01_update_person_state_reset_char_offset(void);
extern void          z01_update_player_position_marker(void);
extern void          z01_uw_person_complex_state_delay_and_quit(void);

// z07 — bank 7 transpiled functions

extern void          z07_anim_advance_and_fetch(unsigned int val, unsigned int slot);
extern unsigned char z07_anim_fetch_obj_pos(unsigned int slot);
extern void          z07_anim_set_obj_hflip(unsigned int slot);
extern void          z07_animate_object_walking(unsigned int slot);
extern void          z07_destroy_monster(unsigned int slot);
extern unsigned char z07_end_game_mode(void);
extern unsigned int  z07_find_empty_monster_slot(void);
extern unsigned char z07_get_collidable_tile(unsigned int hotspot_offset, unsigned int slot);
extern unsigned char z07_get_collidable_tile_still(unsigned int slot);
extern unsigned char z07_get_colliding_tile_moving(unsigned int slot);
extern unsigned char z07_get_room_flags(void);
extern unsigned char z07_get_unique_room_id(void);
extern void          z07_hide_all_sprites(void);
extern void          z07_patch_and_cue_level_palettes_transfer(void);
extern void          z07_reset_obj_metastate(unsigned int slot);
extern void          z07_reset_obj_metastate_and_timer(unsigned int slot);
extern unsigned char z07_reset_obj_state(unsigned int slot);
extern unsigned char z07_reset_moving_dir(void);
extern void          z07_set_shove_info_with0(unsigned int val, unsigned int slot);
extern void          z07_set_type_and_clear_object(unsigned int type, unsigned int slot);
extern void          z07_update_dead_dummy(unsigned int slot);

#endif // LEGACY_BRIDGE_H
