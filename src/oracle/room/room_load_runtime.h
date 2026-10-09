#ifndef ROOM_LOAD_RUNTIME_H
#define ROOM_LOAD_RUNTIME_H

#include "room_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void roomld_write_and_enable_sprite0(void);
void roomld_put_link_behind_background(void);
void roomld_reset_inv_obj_state(void);
void roomld_fill_play_area_attrs(unsigned int room_id);
void roomld_setup_obj_room_bounds(void);
void roomld_init_link_speed(void);
void roomld_transfer_level_pattern_blocks(void);
void roomld_init_mode2_submodes(void);
void roomld_copy_common_data_to_ram(void);
void roomld_update_mode2_load_full(void);

#ifdef __cplusplus
}
#endif

/* --- External data arrays used by room_load_runtime.c --- */
extern const unsigned char LevelPatternBlockSrcAddrs[];
extern const unsigned char BossPatternBlockSrcAddrs[];
extern const unsigned char PatternBlockSrcAddrsUW[];
extern const unsigned char PatternBlockSrcAddrsOW[];
extern const unsigned char PatternBlockPpuAddrs[];
extern const unsigned char PatternBlockPpuAddrsExtra[];
extern const unsigned char PatternBlockSizesOW[];
extern const unsigned char PatternBlockSizesUW[];
extern const unsigned long LevelBlockAddrsQ1[];
extern const unsigned long LevelBlockAddrsQ2[];
extern const unsigned long LevelInfoAddrs[];
extern const unsigned long CommonDataBlockAddr_Bank6[];
extern const unsigned char LevelInfoUWQ2ReplacementAddrs[];
extern const unsigned char LevelInfoUWQ2ReplacementSizes[];
extern const unsigned char LevelBlockAttrsBQ2ReplacementOffsets[];
extern const unsigned char LevelBlockAttrsBQ2ReplacementValues[];

/* --- ASM shim functions used by room_load_runtime.c --- */
extern void c_copy_bank_to_window(unsigned int bank);
extern unsigned char c_ppu_read_2(void);
extern void c_ppu_write_6(unsigned int val);
extern void c_ppu_write_7(unsigned int val);
extern void c_turn_off_all_video(void);

#endif
