#include "room_load_runtime.h"
#include "enemy_state.h"

#define NES_SRAM_BASE 0x6000u

static const unsigned char roomld_sprite0_descriptor[] = { 0x27, 0x61, 0x20, 0x58 };
static const unsigned char roomld_palette_to_nt_attr[] = { 0x00, 0x55, 0xAA, 0xFF };
static const unsigned char roomld_obj_room_bounds[] = {
    0x11, 0xE0, 0x4E, 0xCD, 0x89,
    0x21, 0xD0, 0x5E, 0xBD, 0x78
};

void roomld_write_and_enable_sprite0(void) {
    signed char i;
    ROOM_SPRITE0_ENABLED = 1;
    for (i = 3; i >= 0; i--)
        ROOM_OAM_BYTE((unsigned char)i) = roomld_sprite0_descriptor[(unsigned char)i];
}

void roomld_put_link_behind_background(void) {
    ROOM_LINK_BG_ATTR_A |= 0x20;
    ROOM_LINK_BG_ATTR_B |= 0x20;
}

void roomld_reset_inv_obj_state(void) {
    signed char i;
    ROOM_INV_OBJ_ACTIVE = 0;
    for (i = 5; i >= 0; i--)
        ROOM_INV_OBJ_STATE((unsigned char)i) = 0;
}

void roomld_fill_play_area_attrs(unsigned int room_id) {
    unsigned char outer_sel = F_ram_687E_X6(room_id) & 0x03;
    unsigned char outer_attr = roomld_palette_to_nt_attr[outer_sel];
    unsigned char inner_sel;
    unsigned char inner_attr;
    unsigned char d3;

    for (d3 = 0; d3 < 48; d3++)
        ROOM_PALETTE_ATTR(d3) = outer_attr;

    inner_sel = F_ram_68FE_X5(room_id) & 0x03;
    inner_attr = roomld_palette_to_nt_attr[inner_sel];

    for (d3 = 9; d3 < 0x27; d3++) {
        unsigned char mod = d3 & 0x07;
        if (mod == 0 || mod == 7)
            continue;
        if (d3 >= 0x21) {
            unsigned char combined = (inner_attr & 0x0F) | (ROOM_PALETTE_ATTR(d3) & 0xF0);
            ROOM_PALETTE_ATTR(d3) = combined;
        } else {
            ROOM_PALETTE_ATTR(d3) = inner_attr;
        }
    }
}

void roomld_setup_obj_room_bounds(void) {
    unsigned char base = 5;
    unsigned char i;
    if (CUR_LEVEL == 0) {
        base = 0;
        ROOM_IN_DOORWAY_FLAG = 0;
    }
    for (i = 0; i < 5; i++)
        ROOM_BOUNDS(i) = roomld_obj_room_bounds[(unsigned char)(base + i)];
}

void roomld_init_link_speed(void) {
    WORLD_TMP0 = 96;
    if (CUR_LEVEL != 0) {
        ROOM_LINK_SPEED = WORLD_TMP0;
        return;
    }
    if (ROOM_COLLIDABLE_TILE == 0x74 || ROOM_COLLIDABLE_TILE == 0x75) {
        WORLD_TMP0 = 48;
        if (ROOM_LINK_SPEED != 48)
            ROOM_LINK_SPEED_FRAC = 0;
    }
    ROOM_LINK_SPEED = WORLD_TMP0;
}

/* ---- Plan F: drained from z_03 (CHR pattern-block transfer) ----------- */

#define PATTERN_BLOCK_INDEX 0x051D
/* CUR_LEVEL is the level VALUE (progress_state.h: RAM($0010)). A local
 * redefinition to the ADDRESS $0010 used to follow here, so the later
 * `level = CUR_LEVEL` reads in the Mode 2 loaders always got 16 (T-093). */

static void roomld_reset_pattern_block_index(void) {
    F_PATTERN_BLOCK_INDEX_R = 0;
}

static void roomld_fetch_pattern_block_addr_uw(void) {
    c_copy_bank_to_window(3);
    unsigned char idx = F_PATTERN_BLOCK_INDEX_R;
    idx <<= 1;
    F_ram_0000_R3 = PatternBlockSrcAddrsUW[idx];
    F_ram_0001_R2 = PatternBlockSrcAddrsUW[idx + 1];
}

static void roomld_fetch_pattern_block_info_ow(void) {
    c_copy_bank_to_window(3);
    unsigned char idx = F_PATTERN_BLOCK_INDEX_R;
    idx <<= 1;
    F_ram_0000_R3 = PatternBlockSrcAddrsOW[idx];
    F_ram_0002_R2 = PatternBlockSizesOW[idx];
    F_ram_0001_R2 = PatternBlockSrcAddrsOW[idx + 1];
    F_ram_0003_R3 = PatternBlockSizesOW[idx + 1];
}

static void roomld_fetch_pattern_block_addr_uw_special(void) {
    c_copy_bank_to_window(3);
    unsigned char idx = CUR_LEVEL;
    idx <<= 1;
    F_ram_0000_R3 = LevelPatternBlockSrcAddrs[idx];
    F_ram_0001_R2 = LevelPatternBlockSrcAddrs[idx + 1];
}

static void roomld_fetch_pattern_block_uw_boss(void) {
    c_copy_bank_to_window(3);
    unsigned char idx = CUR_LEVEL;
    idx <<= 1;
    F_ram_0000_R3 = BossPatternBlockSrcAddrs[idx];
    F_ram_0001_R2 = BossPatternBlockSrcAddrs[idx + 1];
}

static void roomld_fetch_pattern_block_size_uw(void) {
    unsigned char idx = F_PATTERN_BLOCK_INDEX_R;
    idx <<= 1;
    F_ram_0002_R2 = PatternBlockSizesUW[idx];
    F_ram_0003_R3 = PatternBlockSizesUW[idx + 1];
}

static void roomld_transfer_pattern_block_bank3(void) {
    unsigned char idx = F_PATTERN_BLOCK_INDEX_R;
    idx <<= 1;
    c_ppu_write_6(PatternBlockPpuAddrs[idx]);
    c_ppu_write_6(PatternBlockPpuAddrs[idx + 1]);

    for (;;) {
        unsigned char lo = F_ram_0000_R3;
        unsigned char hi = F_ram_0001_R2;
        unsigned short addr = ((unsigned short)hi << 8) | lo;
        unsigned char val = nes_ram[addr]; /* native_ram bulk:dyn_addr */
        c_ppu_write_7(val);

        lo = F_ram_0000_R3;
        lo++;
        F_ram_0000_R3 = lo;
        if (lo == 0) {
            hi = F_ram_0001_R2;
            hi++;
            F_ram_0001_R2 = hi;
        }

        unsigned char cnt_lo = F_ram_0003_R3;
        unsigned char cnt_hi = F_ram_0002_R2;
        if (cnt_lo == 0) {
            cnt_hi--;
            F_ram_0002_R2 = cnt_hi;
        }
        cnt_lo--;
        F_ram_0003_R3 = cnt_lo;

        if (F_ram_0002_R2 == 0 && F_ram_0003_R3 == 0)
            break;
    }
    F_PATTERN_BLOCK_INDEX_R++;
}

static void roomld_transfer_level_pattern_blocks_uw(void) {
    do {
        roomld_fetch_pattern_block_addr_uw();
        roomld_fetch_pattern_block_size_uw();
        roomld_transfer_pattern_block_bank3();
    } while (F_PATTERN_BLOCK_INDEX_R != 2);

    roomld_fetch_pattern_block_addr_uw_special();
    roomld_fetch_pattern_block_size_uw();
    roomld_transfer_pattern_block_bank3();

    roomld_fetch_pattern_block_uw_boss();
    roomld_fetch_pattern_block_size_uw();
    roomld_transfer_pattern_block_bank3();

    roomld_reset_pattern_block_index();
}

void roomld_transfer_level_pattern_blocks(void) {
    c_turn_off_all_video();
    c_ppu_read_2();
    roomld_reset_pattern_block_index();

    if (CUR_LEVEL != 0) {
        roomld_transfer_level_pattern_blocks_uw();
        return;
    }

    do {
        roomld_fetch_pattern_block_info_ow();
        roomld_transfer_pattern_block_bank3();
    } while (F_PATTERN_BLOCK_INDEX_R != 2);
}

/* ---- Plan F: drained from z_06 (level info / Q2 patches) -------------- */

static void roomld_copy_block_rom(const unsigned char *src) {
    for (;;) {
        unsigned short dest = ((unsigned short)F_ram_0003_R3 << 8) | F_ram_0002_R2;
        nes_ram[dest] = *src; /* native_ram bulk:dyn_addr */

        if (F_ram_0002_R2 == F_ram_0004_R2 && F_ram_0003_R3 == F_ram_0005_R2) {
            SUBMODE_VALUE++;
            return;
        }

        unsigned char lo = F_ram_0002_R2;
        lo++;
        F_ram_0002_R2 = lo;
        if (lo == 0)
            F_ram_0003_R3++;

        src++;
    }
}

static void roomld_fetch_level_block_dest_info(void) {
    F_ram_0002_R2 = 0x7E;
    F_ram_0003_R3 = 0x68;
    F_ram_0004_R2 = 0x7D;
    F_ram_0005_R2 = 0x6B;
}

static void roomld_fetch_level_info_dest_info(void) {
    F_ram_0002_R2 = 0x7E;
    F_ram_0003_R3 = 0x6B;
    F_ram_0004_R2 = 0x7D;
    F_ram_0005_R2 = 0x6C;
}

static void roomld_fetch_dest_addr_for_common_data_block(void) {
    F_ram_0002_R2 = 0xF0;
    F_ram_0003_R3 = 0x67;
    F_ram_0004_R2 = 0x7D;
    F_ram_0005_R2 = 0x68;
}

static void roomld_init_mode2_sub0(void) {
    unsigned char level = CUR_LEVEL;
    unsigned char idx = level;

    unsigned char profile = SAVE_SLOT_INDEX;
    unsigned char quest = SAVE_SLOT_QUEST(profile);

    const unsigned long *table = (quest != 0) ? LevelBlockAddrsQ2 : LevelBlockAddrsQ1;
    const unsigned char *src = (const unsigned char *)table[idx];
    roomld_fetch_level_block_dest_info();
    roomld_copy_block_rom(src);
}

static void roomld_init_mode2_sub1(void) {
    unsigned char level = CUR_LEVEL;
    const unsigned char *src = (const unsigned char *)LevelInfoAddrs[level];
    roomld_fetch_level_info_dest_info();
    roomld_copy_block_rom(src);
    SUBMODE_VALUE = 0;
    ROOM_MODE_TIMER++;
}

void roomld_init_mode2_submodes(void) {
    unsigned char submode = SUBMODE_VALUE;
    if (submode == 0)
        roomld_init_mode2_sub0();
    else
        roomld_init_mode2_sub1();
}

void roomld_copy_common_data_to_ram(void) {
    const unsigned char *src = (const unsigned char *)CommonDataBlockAddr_Bank6[0];
    roomld_fetch_dest_addr_for_common_data_block();
    roomld_copy_block_rom(src);
    SUBMODE_VALUE = 0;
}

static void roomld_patch_q2_rooms(void) {
    for (signed char i = 7; i >= 0; i--) {
        unsigned char off = LevelBlockAttrsBQ2ReplacementOffsets[i];
        unsigned char val = LevelBlockAttrsBQ2ReplacementValues[i];
        F_ram_68FE_X5(off) = val;
    }
    F_NES_SRAM_BASE_6A09_N = 123;
    F_NES_SRAM_BASE_6A3A_N = 123;
    F_NES_SRAM_BASE_6A72_N = 90;
    F_NES_SRAM_BASE_68BA_N = 114;
    F_NES_SRAM_BASE_68F2_N = 114;
    F_NES_SRAM_BASE_6B3A_N = 1;
    F_NES_SRAM_BASE_6B72_N = 0;
}

void roomld_update_mode2_load_full(void) {
    c_copy_bank_to_window(6);

    unsigned char profile = SAVE_SLOT_INDEX;
    unsigned char quest = SAVE_SLOT_QUEST(profile);
    if (quest == 0)
        return;

    unsigned char level = CUR_LEVEL;
    if (level == 0) {
        roomld_patch_q2_rooms();
        return;
    }

    unsigned char idx = level << 1;
    F_ram_0000_R3 = LevelInfoUWQ2ReplacementAddrs[idx - 2];
    F_ram_0001_R2 = LevelInfoUWQ2ReplacementAddrs[idx - 1];

    unsigned char count = LevelInfoUWQ2ReplacementSizes[level - 1];
    for (signed char i = (signed char)count; i >= 0; i--) {
        unsigned char ptr_lo = F_ram_0000_R3;
        unsigned char ptr_hi = F_ram_0001_R2;
        unsigned short src_addr = ((unsigned short)ptr_hi << 8) | ptr_lo;
        unsigned char val = nes_ram[src_addr + (unsigned char)i]; /* native_ram bulk:dyn_addr */
        F_NES_SRAM_BASE_6BA7_X2((unsigned char)i) = val;
    }
}
