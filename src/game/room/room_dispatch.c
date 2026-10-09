/* room_dispatch.c — native room subsystem dispatch (Phase 4).
 *
 * Drain MATCH (verified-by-use; in production via Debug.md gameplay).
 */

#include "room_dispatch.h"
#include <stdint.h>
#include "platform_abi.h"
#include "world_state.h"       /* CUR_ROOM_ID */
#include "progress_state.h"    /* CUR_LEVEL, SAVEFILE_PTR_LO/HI, SUBMODE_VALUE,
                                * MODE_VALUE */
#include "room_state.h"        /* ROOM_MAX_MONSTER_SLOT, ROOM_MONSTER_ALL_DEAD,
                                * ROOM_OBJ_TYPE, ROOM_MODE_TIMER */
#include "world_state.h"       /* TRANSFER_BUF_POS — included via combat below */
#include "combat_state.h"      /* LINK_DAMAGE_DISABLE_FLAG, LINK_ACTION_TIMER,
                                * LINK_STUN_TIMER */
#include "link_state.h"        /* LINK_HALT_FLAG */
#include "item_state.h"        /* ITEM_SFX_SECONDARY */
#include "core/core_dispatch.h" /* core_get_opposite_dir,
                                  * core_compare_hearts_to_containers */
#include "hud/hud_dispatch.h"   /* hud_world_change_rupees */
#include "world/level_info_install.h" /* level_info_apply_q2_patch */
#include "world/progress_dispatch.h" /* progress_update_world_curtain_effect,
                                      * progress_reset_room_tile_obj_info */
#include "combat/collision_dispatch.h" /* collision_get_collidable_tile_still */
#include "world/object_dispatch.h"   /* object_move_object (T-050) */
#include "world/draw_dispatch.h"     /* draw_object_not_mirrored */
#include "world/dyn_tile_dispatch.h" /* dyn_tile_change_tile_obj_tiles */
#include "dungeon/uw_render.h"       /* roomrom_uw_room_render_refresh_square */
#include "world/world_dispatch.h"    /* world_get_object_middle, shortcut xy */
#include "enemies/enemy_dispatch.h"  /* enemy_play_secret_found_tune */
#include "item_state.h"         /* LINK_PARTIAL_HEART, LINK_HEARTS, ITEM_SFX_PRIMARY,
                                 * SAVE_SLOT_INDEX */
#include "save_state.h"         /* CONTINUE_COUNT */
#include "enemy_state.h"        /* SAVE_SLOT_QUEST */

/* Genesis VDP native primitive — display enable/disable (Reg 1 bit 6).
 * Forward decl from src/sgdk_adapter/render_adapter.c. */
extern void render_display_enable(unsigned char on);

/* Genesis ROM bank-window cache — Genesis-native asset cache primitive,
 * NOT NES MMC1 emulation. Forward decl from
 * src/sgdk_adapter/render_adapter.c. */
extern void render_bank_window_load(unsigned char bank);

/* Asm-bound data tables — NOT shims (not c_/z01_/z07_ prefixed).
 * MenuPalettesTransferBuf is RW state shared across item-pickup,
 * file-select, and palette-cue paths. SaveSlotToPaletteRowOffset
 * is read-only. Native code reads/writes the same backing memory
 * the transpile-asm path uses, ensuring NATIVE_ROOM=on/off paths
 * stay coherent. */
extern unsigned char MenuPalettesTransferBuf[];
extern const unsigned char SaveSlotToPaletteRowOffset[];

#define NES_SRAM_BASE 0x6000u

unsigned char room_get_room_flags(void)
{
    /* drain at room_runtime.c:14-22. NES GetRoomFlags.
     * SRAM ROOM_FLAGS_PTR_LO/HI at $6BAF/$6BB0; deref + read at
     * CUR_ROOM_ID offset. Stashes ptr to SAVEFILE_PTR_LO/HI. */
    const unsigned char ptr_lo =
        nes_ram[NES_SRAM_BASE + NES_SRAM_ROOM_FLAGS_PTR_LO];
    const unsigned char ptr_hi =
        nes_ram[NES_SRAM_BASE + NES_SRAM_ROOM_FLAGS_PTR_HI];
    SAVEFILE_PTR_LO = ptr_lo;
    SAVEFILE_PTR_HI = ptr_hi;
    const unsigned short ptr =
        (unsigned short)(((unsigned short)ptr_hi << 8) | ptr_lo);
    return (unsigned char)nes_ram[ptr + CUR_ROOM_ID];
}

unsigned int room_split_room_id(void)
{
    /* drain at room_runtime.c:71-76. */
    const unsigned char room = (unsigned char)CUR_ROOM_ID;
    const unsigned char col = (unsigned char)(room & 0x0Fu);
    const unsigned char row = (unsigned char)(room >> 4);
    return ((unsigned int)row << 8) | (unsigned int)col;
}

unsigned char room_is_dark_room(unsigned int col)
{
    /* drain at room_runtime.c:78-82. */
    if (CUR_LEVEL == 0u) {
        return 0u;
    }
    return (unsigned char)(nes_ram[NES_SRAM_BASE + 0x0A7Eu + (col & 0xFFu)] & 0x80u);
}

void room_silence_sound(void)
{
    /* drain at room_runtime.c:120-123. */
    RAM(0x0604u) = 0x80u;  /* ROOM_SFX_MAIN */
    RAM(0x0603u) = 0x80u;  /* ROOM_SFX_AUX */
}

void room_check_has_living_monsters(void)
{
    /* drain at room_runtime.c:102-118. NES CheckHasLivingMonsters. */
    const unsigned char max_slot = (unsigned char)ROOM_MAX_MONSTER_SLOT;
    for (signed char i = (signed char)max_slot; i >= 0; i--) {
        const unsigned char obj = (unsigned char)ROOM_OBJ_TYPE((unsigned char)i);
        if (obj == 0u) {
            continue;
        }
        if (obj < 0x2Bu) {
            return;
        }
        if (obj < 0x2Eu) {
            continue;
        }
        if (obj < 0x49u) {
            return;
        }
    }
    LINK_DAMAGE_DISABLE_FLAG = 0u;
    ROOM_MONSTER_ALL_DEAD = (uint8_t)(ROOM_MONSTER_ALL_DEAD + 1u);
}

unsigned char room_end_game_mode(void)
{
    /* drain at room_mode_runtime.c:249-253. Plan-C drain (the inner
     * one). Outer roommd_end_game_mode12 is the cellar dance + level
     * fall — defer until cellar logic ports. */
    ROOM_MODE_TIMER = 0u;
    SUBMODE_VALUE = 0u;
    return 0u;
}

void room_hide_all_sprites(void)
{
    /* drain at room_runtime.c:273-276. */
    for (unsigned char i = 0u; i < 64u; ++i) {
        RAM(0x0200u + (unsigned short)((unsigned short)i * 4u)) = 0xF8u;
    }
}

unsigned char room_get_unique_room_id(void)
{
    /* drain at room_runtime.c:278-281. */
    const unsigned char room = (unsigned char)CUR_ROOM_ID;
    return (unsigned char)(nes_ram[NES_SRAM_BASE +
                                   NES_SRAM_ROOM_UNIQUE_ID_BASE + room] &
                           0x3Fu);
}

void room_clear_room_history(void)
{
    /* NES ClearRoomHistory (Z_07.asm:1387): [$0529] = 0, then RoomHistory
     * $621-$626; CurRoomHistoryIndex $620 is left alone (T-013: the drain
     * cleared $620 instead of $0529). */
    RAM(0x0529u) = 0u;
    for (signed char i = 5; i >= 0; --i) {
        RAM(NES_ROOM_HISTORY_BASE + (unsigned char)i) = 0u;
    }
}

/* NES source: reference/aldonunez/Z_07.asm:
 * RunCrossRoomTasksAndBeginUpdateMode @LoopHistory.
 * Drained C: NONE.
 * Coverage: PARTIAL (native room-entry history ownership).
 * Stance: EXTEND.
 *
 * CreateRoomObjects consults the prior six-room history before this runs;
 * the current room is inserted only after its object count is resolved. */
void room_record_history(unsigned char room_id)
{
    unsigned char i;
    unsigned char idx;
    for (i = 0u; i < 6u; ++i) {
        if ((unsigned char)ROOM_HISTORY(i) == room_id) return;
    }
    idx = (unsigned char)ROOM_HISTORY_IDX;
    if (idx >= 6u) idx = 0u;
    ROOM_HISTORY(idx) = room_id;
    idx = (unsigned char)(idx + 1u);
    ROOM_HISTORY_IDX = (idx < 6u) ? idx : 0u;
}

void room_reset_player_state(void)
{
    /* drain at room_runtime.c:289-292. */
    LINK_ACTION_TIMER = 0u;
    LINK_HALT_FLAG = 0u;
}

void room_mark_room_visited(void)
{
    /* drain at room_runtime.c:294-299. Re-uses the GetRoomFlags ptr
     * stash side-effect. */
    const unsigned char flags = room_get_room_flags();
    const unsigned short ptr =
        (unsigned short)(((unsigned short)(unsigned char)SAVEFILE_PTR_HI << 8) |
                         (unsigned char)SAVEFILE_PTR_LO);
    nes_ram[ptr + CUR_ROOM_ID] = (uint8_t)(flags | 0x20u);
}

void room_go_to_next_mode(void)
{
    /* drain at room_mode_runtime.c:255-258. */
    MODE_VALUE = (uint8_t)((unsigned char)MODE_VALUE + 1u);
    (void)room_end_game_mode();
}

void room_copy_column_to_tilebuf(void)
{
    /* drain at room_transfer_runtime.c:6-32. NES CopyColumnToTilebuf.
     * Reads PlayArea ($6530 + col*$16); writes 22 column tiles into
     * the transfer-buffer at TRANSFER_BUF_POS. Stashes src + dst
     * pointers in SAVEFILE_PTR_LO/HI for the next pass. */
    #define ROOM_PLAY_AREA_BASE 0x6530u
    #define ROOM_COL_STRIDE     0x16u

    SAVEFILE_PTR_LO = 0x1Au;
    SAVEFILE_PTR_HI = 0x65u;

    const unsigned char col =
        (unsigned char)((unsigned char)CUR_ROOM_FLAGS_PTR - 1u);
    const unsigned char buf = (unsigned char)TRANSFER_BUF_POS;

    RAM(0x0302u + buf) = 33u;             /* TRANSFER_BUF_BYTE(buf) */
    RAM(0x0303u + buf) = col;

    unsigned short src =
        (unsigned short)(ROOM_PLAY_AREA_BASE +
                         (unsigned short)col * ROOM_COL_STRIDE);

    RAM(0x0304u + buf) = 0x96u;
    RAM(0x031Bu + buf) = 0xFFu;

    unsigned char dst = buf;
    for (unsigned char i = 0u; i < 22u; ++i) {
        RAM(0x0305u + dst) = nes_ram[src + i];
        ++dst;
    }
    src = (unsigned short)(src + 22u);
    dst = (unsigned char)(dst + 3u);
    TRANSFER_BUF_POS = dst;

    SAVEFILE_PTR_LO = (uint8_t)(src & 0xFFu);
    SAVEFILE_PTR_HI = (uint8_t)((src >> 8) & 0xFFu);

    #undef ROOM_PLAY_AREA_BASE
    #undef ROOM_COL_STRIDE
}

/* Z_07.asm LevelSongIds[10] (line 2977). */
static const unsigned char k_level_song_ids[10] = {
    0x01u, 0x40u, 0x40u, 0x40u, 0x40u,
    0x40u, 0x40u, 0x40u, 0x40u, 0x20u
};

void room_go_to_next_mode_reset_grid_offset(void)
{
    /* drain at room_mode_runtime.c:267-270. */
    room_go_to_next_mode();
    RAM(0x0394u) = 0u;
}

void room_go_to_next_mode_play_level_song(void)
{
    /* drain at room_mode_runtime.c:260-265. */
    const unsigned char level = (unsigned char)CUR_LEVEL;
    /* drain reads LevelSongIds[level] unbounded; CUR_LEVEL is
     * constrained to 0..9 by gameplay — table sized 10. */
    ITEM_SFX_SECONDARY = k_level_song_ids[level];
    room_go_to_next_mode();
    RAM(0x0394u) = 0u;
}

void room_go_to_next_mode_from_play(void)
{
    /* drain at room_mode_runtime.c:306-315. */
    MODE_VALUE = (uint8_t)((unsigned char)MODE_VALUE + 1u);
    SUBMODE_VALUE = 0u;
    ROOM_MODE_TIMER = 0u;
    RAM(0x000Fu) = 0u;
    LINK_ACTION_TIMER = 0u;
    RAM(0x00C0u) = 0u;
    RAM(0x00D3u) = 0u;
    LINK_STUN_TIMER = 0u;
}

/* roomrt_reverse_directions[4] (room_runtime.c:10). */
static const unsigned char k_room_reverse_directions[4] = {
    0x08u, 0x04u, 0x02u, 0x01u
};

/* roomrt_player_screen_edge_bounds[4] (room_runtime.c:11). */
static const unsigned char k_room_player_screen_edge_bounds[4] = {
    0x3Du, 0xDDu, 0x00u, 0xF0u
};

void room_check_screen_edge(void)
{
    /* drain at room_runtime.c:301-322. */
    if ((unsigned char)ROOM_INPUT_DIR == 0u) {
        return;
    }
    const unsigned int dir_info =
        core_get_opposite_dir((unsigned int)(unsigned char)ROOM_INPUT_DIR);
    const unsigned char dir_idx = (unsigned char)(dir_info >> 8);
    const unsigned char single_dir =
        k_room_reverse_directions[dir_idx & 3u];
    const unsigned char coord =
        ((single_dir & 0x0Cu) == 0u) ?
            (unsigned char)LINK_X :
            (unsigned char)LINK_Y;

    if (coord != k_room_player_screen_edge_bounds[dir_idx & 3u]) {
        return;
    }

    LINK_DIR = single_dir;
    room_go_to_next_mode_from_play();
}

void room_turn_off_all_video(void)
{
    /* drain Z_07.asm:1739 TurnOffAllVideo. NES asm path:
     *   moveq #0,D0
     *   jsr _ppu_write_1     ; PPUMASK=0 + Genesis VDP Reg 1 = $8134
     *   move.b D0,($00FE,A4) ; PPU_MASK shadow = 0
     *
     * Native equivalent: maintain shadow + disable Genesis VDP display.
     * Per debate 007 synthesis: Sonnet flagged that pure shadow write
     * is insufficient — without VDP Reg 1 disable, mode transitions
     * display stale VRAM (tearing). Both writes required. */
    RAM(NES_PPU_MASK_SHADOW) = 0u;
    render_display_enable(0u);
}

void room_world_fill_hearts(void)
{
    /* drain at room_object_runtime.c:31-48. NES WorldFillHearts.
     * Heart-fill animation tick: advance partial-heart by +6/tick;
     * roll over to next heart when full; stop at hearts=containers. */
    if ((unsigned char)ROOM_HEART_FILL_STATE == 0u) {
        return;
    }
    ROOM_SFX_MAIN = 16u;
    if ((unsigned char)LINK_PARTIAL_HEART >= 0xF8u) {
        LINK_PARTIAL_HEART = 0u;
        /* CompareHeartsToContainers installs the container count in $00
         * before comparing it with the full-heart count. */
        unsigned char whole_hearts = core_compare_hearts_to_containers();
        if (whole_hearts == (unsigned char)RAM(0x0000u)) {
            LINK_PARTIAL_HEART = 0xFFu;
            RAM(0x052Eu) = 0u;             /* ROOM_SWORD_BLOCKED_FLAG */
            ROOM_HEART_FILL_STATE = 0u;
            RAM(0x00E0u) = 0u;             /* ROOM_PAUSED_FLAG */
            return;
        }
        LINK_HEARTS = (uint8_t)((unsigned char)LINK_HEARTS + 1u);
        return;
    }
    LINK_PARTIAL_HEART =
        (uint8_t)((unsigned char)LINK_PARTIAL_HEART + 6u);
}

void room_update_hearts_and_rupees(void)
{
    /* drain at room_mode_runtime.c:323-327. NES UpdateHeartsAndRupees:
     *   c_switch_bank(5);          // MMC1 PRG bank switch — Genesis no-op
     *   c_world_fill_hearts();
     *   c_world_change_rupees();
     *
     * Per debate 007 synthesis option A: drop MMC1 SwitchBank entirely
     * (Genesis flat M68K address space, no mapper). */
    room_world_fill_hearts();
    hud_world_change_rupees();
}

/* Q2 UW level-info replacement: data comes from the ROM-extracted
 * rooms_dungeons blob via level_info_apply_q2_patch (single owner,
 * src/game/world/level_info_install.c). */

void room_update_mode2_load(void)
{
    /* drain at room_mode_runtime.c:317-321 + room_load_runtime.c:299-325.
     * NES UpdateMode2_Load: turn_off_all_video; mode2_load_full;
     * go_to_next_mode. */
    room_turn_off_all_video();

    /* mode2_load_full inlined per debate 007 option D: bank load is
     * Genesis-native (render_bank_window_load), Q1 returns early on
     * quest=0, Q2 level=0 patches inline, Q2 level>0 STAGE-1 stub. */
    render_bank_window_load(6u);

    const unsigned char profile = (unsigned char)SAVE_SLOT_INDEX;
    const unsigned char quest = (unsigned char)SAVE_SLOT_QUEST(profile);
    if (quest == 0u) {
        /* Q1 (first quest) — no patches needed. */
    } else {
        const unsigned char level = (unsigned char)CUR_LEVEL;
        if (level == 0u) {
            /* Q2 overworld — Block-attr patches (ROM tables, single owner). */
            level_info_apply_q2_ow_patch();
        } else {
            /* Q2 underworld level — Z_06 UpdateMode2Load_Full patch of
             * LevelInfo from $6BA7 (Sizes[L]+1 bytes, NES-exact). */
            level_info_apply_q2_patch(level);
        }
    }

    room_go_to_next_mode();
}

void room_update_mode3_unfurl(void)
{
    /* drain at room_mode_runtime.c:295-304. NES UpdateMode3Unfurl:
     *   c_update_world_curtain_effect();
     *   if (CURTAIN_LEFT_COL != 0) return;
     *   c_set_mmc1_control(15);    // MMC1 ctrl reg — Genesis no-op
     *   if (ROOM_LINK_CELLAR_FLAG) go_to_next_mode_reset_grid_offset;
     *   else                       go_to_next_mode_play_level_song;
     *
     * Per debate 007 synthesis option A: drop MMC1 SetMMC1Control. */
    progress_update_world_curtain_effect();
    if ((unsigned char)CURTAIN_LEFT_COL != 0u) {
        return;
    }
    if ((unsigned char)ROOM_LINK_CELLAR_FLAG != 0u) {
        room_go_to_next_mode_reset_grid_offset();
    } else {
        room_go_to_next_mode_play_level_song();
    }
}

/* PatchAndCueLevelPalettesTransfer's patch half: Link's color for the
 * save slot (MenuPalettesTransferBuf+20 row 4/5/6) into the level
 * palettes ($6B92 = $3F11). T-171: the NES File Select fills each
 * profile's row with LinkColors[ring] (Z_02.asm:2436) and taking a ring
 * patches it (Z_01.asm:4673); the Genesis front end does not, so a game
 * loaded with a ring kept the green tunic (t121_ring1: NES $32, Genesis
 * $29). The row is LinkColors[InvRing] of the current profile. Also run
 * after every LevelInfo install (level_info_install.c): the Genesis load
 * paths that skip InitMode3 would otherwise keep the ROM's $29. */
void room_patch_level_palette_link_color(void)
{
    static const unsigned char k_link_colors[3] = { 0x29u, 0x32u, 0x16u }; /* LinkColors_CommonCode */
    const unsigned char slot = (unsigned char)SAVE_SLOT_INDEX;
    const unsigned char row_off = SaveSlotToPaletteRowOffset[slot & 3u];
    const unsigned char ring = nes_ram[0x0662u];
    if (ring < 3u) MenuPalettesTransferBuf[20u + row_off] = k_link_colors[ring];
    nes_ram[NES_SRAM_BASE + 0x0B92u] = MenuPalettesTransferBuf[20u + row_off];
}

void room_patch_and_cue_level_palettes_transfer(void)
{
    /* drain at room_mode_runtime.c:272-279. */
    room_patch_level_palette_link_color();
    ROOM_TRANSFER_BUF_SELECT = 24u;
    SUBMODE_VALUE = (uint8_t)((unsigned char)SUBMODE_VALUE + 1u);
}

/* Z_07.asm InitMode5Play (1460-1536): cue the sprite palette row 7
 * ($3F1C, NES sprite sub-palette 3) transfer for the room. UW: a special
 * boss (SpecialBossPaletteObjTypes, ObjType+1) takes its own row, else
 * the level's row 7 (selector $06, patched from LevelInfo by the drain).
 * OW: by the tile object in slot 11 (gravestone $20; Armos1 $20 or green
 * $22; rock brown $24 or green $22 by LevelBlockAttrsB bit 0; anything
 * else red Armos $7A). T-171: never cued, so a boss drew with the
 * level's row 7 (t171_boss_l7 Aquamentus). */
void room_init_mode5_play_palette_row7(void)
{
    static const unsigned char k_boss_types[9] = {
        0x3Du, 0x3Eu, 0x38u, 0x39u, 0x32u, 0x31u, 0x43u, 0x44u, 0x45u };
    static const unsigned char k_boss_selectors[9] = {
        0x08u, 0x36u, 0x0Au, 0x0Au, 0x0Au, 0x0Au, 0x7Cu, 0x7Cu, 0x7Cu };
    unsigned char x;
    if (nes_ram[0x0010u] != 0u) {                     /* CurLevel: UW */
        const unsigned char t = nes_ram[0x0350u];      /* ObjType+1 */
        signed char y;
        x = 0x06u;                                     /* @UseLevelPalette */
        for (y = 8; y >= 0; --y) {
            if (t == k_boss_types[(unsigned char)y]) { x = k_boss_selectors[(unsigned char)y]; break; }
        }
    } else {
        const unsigned char t = nes_ram[0x035Au];      /* ObjType+11 */
        x = 0x20u;
        if (t != 0x65u) {
            if (t != 0x66u && t != 0x62u) {
                x = 0x7Au;                             /* @UseRedArmosPalette */
            } else {
                if (t == 0x62u) x = 0x24u;
                if ((nes_ram[NES_SRAM_BASE + 0x08FEu + nes_ram[0x00EBu]] & 1u) == 0u)
                    x = 0x22u;                         /* LevelBlockAttrsB even */
            }
        }
    }
    ROOM_TRANSFER_BUF_SELECT = x;
}

void room_init_mode3_sub1(void)
{
    /* drain at room_mode_runtime.c:281-293. */
    unsigned char room_id;
    if ((unsigned char)CUR_LEVEL != 0u ||
        (unsigned char)ROOM_ID_ALT == 0xFFu) {
        room_id = nes_ram[NES_SRAM_BASE + 0x0BADu];
    } else {
        room_id = (unsigned char)ROOM_ID_ALT;
    }
    CUR_ROOM_ID = room_id;
    if (room_id == (unsigned char)ROOM_ID_ALT) {
        ROOM_ID_ALT = 0xFFu;
    }
    room_patch_and_cue_level_palettes_transfer();
}

void room_reset_inv_obj_state(void)
{
    /* drain at room_load_runtime.c:25-30. */
    RAM(0x0064u) = 0u;                    /* ROOM_INV_OBJ_ACTIVE */
    for (signed char i = 5; i >= 0; --i) {
        RAM(0x00B9u + (unsigned char)i) = 0u;  /* ROOM_INV_OBJ_STATE */
    }
}

/* roomrt_level_masks[8] — power-of-2 single-bit masks. Local bake
 * (mirrors progress_dispatch's k_level_masks). */
static const unsigned char k_room_level_masks[8] = {
    0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x20u, 0x40u, 0x80u
};

static unsigned char room_has_item_by_level(unsigned char base_offset)
{
    /* drain at room_runtime.c:24-37. */
    const unsigned char level = (unsigned char)CUR_LEVEL;
    if (level == 0u) {
        return 0u;
    }
    const unsigned char idx = (unsigned char)(level - 1u);
    unsigned char offset = base_offset;
    if (idx >= 8u) {
        offset = (unsigned char)(offset + 2u);
    }
    const unsigned char bit_idx = (unsigned char)(idx & 7u);
    return (unsigned char)(RAM(0x0657u + offset) &
                           k_room_level_masks[bit_idx]);
}

unsigned char room_has_compass(void)
{
    /* drain at room_runtime.c:39-41. */
    return room_has_item_by_level(16u);
}

unsigned char room_has_map(void)
{
    /* drain at room_runtime.c:43-45. */
    return room_has_item_by_level(17u);
}

void room_update_triforce_position_marker(void)
{
    /* drain at Z_07.asm:1821-1834. */
    if ((unsigned char)CUR_LEVEL == 0u) {
        return;
    }
    /* SwitchBank(5) — Genesis no-op per debate 007. */
    if (room_has_compass() == 0u) {
        return;
    }
    progress_update_position_marker(
        nes_ram[NES_SRAM_BASE + 0x0BAEu], 4u);
}

/* roomld_obj_room_bounds[10]: 5 OW bounds + 5 UW bounds.
 * NES room_load_runtime.c:8-11. */
static const unsigned char k_obj_room_bounds[10] = {
    0x11u, 0xE0u, 0x4Eu, 0xCDu, 0x89u,
    0x21u, 0xD0u, 0x5Eu, 0xBDu, 0x78u
};

void room_setup_obj_room_bounds(void)
{
    /* drain at room_load_runtime.c:58-67. */
    unsigned char base = 5u;
    if ((unsigned char)CUR_LEVEL == 0u) {
        base = 0u;
        RAM(0x0053u) = 0u;  /* ROOM_IN_DOORWAY_FLAG */
    }
    for (unsigned char i = 0u; i < 5u; ++i) {
        RAM(0x0346u + i) = k_obj_room_bounds[base + i];
    }
}

/* roomld_sprite0_descriptor[4] (room_load_runtime.c:6). */
static const unsigned char k_sprite0_descriptor[4] = {
    0x27u, 0x61u, 0x20u, 0x58u
};

void room_write_and_enable_sprite0(void)
{
    /* drain at room_load_runtime.c:13-18. */
    RAM(0x00E3u) = 1u;  /* ROOM_SPRITE0_ENABLED */
    for (signed char i = 3; i >= 0; --i) {
        RAM(0x0200u + (unsigned char)i) =
            k_sprite0_descriptor[(unsigned char)i];
    }
}

void room_put_link_behind_background(void)
{
    /* drain at room_load_runtime.c:20-23. */
    RAM(0x024Au) = (uint8_t)(RAM(0x024Au) | 0x20u);
    RAM(0x024Eu) = (uint8_t)(RAM(0x024Eu) | 0x20u);
}

/* roomld_palette_to_nt_attr[4] (room_load_runtime.c:7). */
static const unsigned char k_palette_to_nt_attr[4] = {
    0x00u, 0x55u, 0xAAu, 0xFFu
};

void room_fill_play_area_attrs(unsigned int room_id)
{
    /* drain at room_load_runtime.c:32-56. */
    const unsigned char outer_sel =
        (unsigned char)(nes_ram[NES_SRAM_BASE + 0x087Eu + room_id] & 0x03u);
    const unsigned char outer_attr = k_palette_to_nt_attr[outer_sel];
    for (unsigned char d3 = 0u; d3 < 48u; ++d3) {
        RAM(0x0530u + d3) = outer_attr;
    }
    const unsigned char inner_sel =
        (unsigned char)(nes_ram[NES_SRAM_BASE + 0x08FEu + room_id] & 0x03u);
    const unsigned char inner_attr = k_palette_to_nt_attr[inner_sel];
    for (unsigned char d3 = 9u; d3 < 0x27u; ++d3) {
        const unsigned char mod = (unsigned char)(d3 & 0x07u);
        if (mod == 0u || mod == 7u) {
            continue;
        }
        if (d3 >= 0x21u) {
            const unsigned char cur = (unsigned char)RAM(0x0530u + d3);
            RAM(0x0530u + d3) =
                (uint8_t)((inner_attr & 0x0Fu) | (cur & 0xF0u));
        } else {
            RAM(0x0530u + d3) = inner_attr;
        }
    }
}

void room_init_link_speed(void)
{
    /* drain at room_load_runtime.c:69-81. */
    unsigned char speed = 96u;
    if ((unsigned char)CUR_LEVEL != 0u) {
        RAM(0x03BCu) = speed;  /* ROOM_LINK_SPEED */
        return;
    }
    const unsigned char tile = (unsigned char)RAM(0x049Eu);
    if (tile == 0x74u || tile == 0x75u) {
        speed = 48u;
        if ((unsigned char)RAM(0x03BCu) != 48u) {
            RAM(0x03A8u) = 0u;  /* ROOM_LINK_SPEED_FRAC */
        }
    }
    RAM(0x03BCu) = speed;
}

void room_init_mode10(void)
{
    /* drain at room_object_runtime.c:7-15. */
    (void)collision_get_collidable_tile_still(0u);
    if ((unsigned char)RAM(0x049Eu) == 0x24u) {  /* ROOM_COLLIDABLE_TILE */
        RAM(0x0619u) = 0u;                       /* ROOM_TRIFORCE_HOLD_FLAG */
        RAM(0x0603u) = 8u;                       /* ROOM_SFX_AUX */
        RAM(0x0412u) =
            (uint8_t)((unsigned char)LINK_Y + 0x10u);  /* ROOM_PUSH_TIMER */
    }
    ROOM_MODE_TIMER = (uint8_t)((unsigned char)ROOM_MODE_TIMER + 1u);
}

void room_end_prepare_mode(void)
{
    /* drain at room_object_runtime.c:50-58. */
    SUBMODE_VALUE = 0u;
    ROOM_MODE_TIMER = 0u;
    RAM(0x000Fu) = 0u;     /* COMBAT_PART_INDEX (ZP_TMPF) */
    LINK_ACTION_TIMER = 0u;
    RAM(0x00C0u) = 0u;     /* MON_SHOVE_DIR(0) */
    RAM(0x00D3u) = 0u;     /* MON_SHOVE_TIMER(0) */
    LINK_STUN_TIMER = 0u;
}

void room_setup_tile_object_ow(void)
{
    /* drain at room_object_runtime.c:17-29. */
    unsigned char type;
    if ((unsigned char)CUR_ROOM_ID == 0x3Fu ||
        (unsigned char)CUR_ROOM_ID == 0x55u) {
        type = 97u;
    } else {
        RAM(0x007Bu) = (unsigned char)RAM(0x052Cu);  /* X_SCRATCH = TILE_OBJ_1 */
        RAM(0x008Fu) = (unsigned char)RAM(0x052Du);  /* Y_SCRATCH = TILE_OBJ_2 */
        type = (unsigned char)RAM(0x052Bu);          /* TILE_OBJ_0 */
    }
    RAM(0x035Au) = type;     /* ROOM_OBJECT_SLOT_TYPE */
    progress_reset_room_tile_obj_info();
    RAM(0x00B7u) = 0u;        /* ROOM_OBJECT_INIT_DONE */
}

/* --------------------------------------------------------------- */
/* Mode-helper leaves — drain at room_mode_runtime.c.              */
/* --------------------------------------------------------------- */

void room_inc_submode(void)
{
    SUBMODE_VALUE = (uint8_t)((unsigned char)SUBMODE_VALUE + 1u);
}

void room_inc_2_submodes(void)
{
    SUBMODE_VALUE = (uint8_t)((unsigned char)SUBMODE_VALUE + 2u);
}

void room_init_mode_a_sub_a_go_to_mode4(void)
{
    /* room_mode_runtime.c:17-21. roomld_reset_inv_obj_state forwarder. */
    room_reset_inv_obj_state();
    SUBMODE_VALUE = 0u;
    MODE_VALUE = 4u;
}

void room_init_mode4_go_to_sub0(void)
{
    /* room_mode_runtime.c:23-26. */
    SUBMODE_VALUE = 0u;
    RAM(0x056Eu) = 0u;  /* ROOM_SCROLL_STATE */
}

void room_update_mode11_death_sub6(void)
{
    /* room_mode_runtime.c:28-31. */
    RAM(0x00FFu) = (uint8_t)((unsigned char)RAM(0x00FFu) & 0xFEu);
    room_inc_submode();
}

void room_reset_vscroll_lo(void)
{
    /* room_mode_runtime.c:33-36. */
    RAM(0x00E2u) = 0u;
    room_inc_submode();
}

void room_select_transfer_buf(unsigned int val)
{
    /* room_mode_runtime.c:38-41. */
    ROOM_TRANSFER_BUF_SELECT = (uint8_t)val;
    room_inc_submode();
}

void room_select_transfer_buf_and_inc_state(unsigned int val)
{
    /* room_mode_runtime.c:43-46. */
    ROOM_TRANSFER_BUF_SELECT = (uint8_t)val;
    RAM(0x00E1u) =
        (uint8_t)((unsigned char)RAM(0x00E1u) + 1u);  /* ROOM_STATE_INDEX */
}

void room_set_fade_cycle_and_advance_submode(unsigned int val)
{
    /* room_mode_runtime.c:65-68. */
    RAM(0x051Cu) = (uint8_t)val;  /* WORLD_FADE_STEP */
    room_inc_submode();
}

void room_switch_to_nt1(void)
{
    /* room_mode_runtime.c:129-131. */
    RAM(0x005Fu) = 1u;  /* ROOM_NAMETABLE_SELECT */
}

void room_update_menu_common2(void)
{
    room_select_transfer_buf_and_inc_state(72u);
}

void room_update_menu_common3(void)
{
    room_select_transfer_buf_and_inc_state(74u);
}

void room_update_menu_common4(void)
{
    room_select_transfer_buf_and_inc_state(76u);
}

void room_update_menu5_ow(void)
{
    room_select_transfer_buf_and_inc_state(92u);
}

void room_init_mode9_transfer_attrs(void)
{
    room_select_transfer_buf(38u);
}

void room_update_mode11_death_set_timer_inc_submode(unsigned int val)
{
    /* room_mode_runtime.c:133-136. */
    CURTAIN_TIMER = (uint8_t)val;
    room_inc_submode();
}

void room_update_mode11_death_sub4(void)
{
    room_select_transfer_buf(98u);
}

void room_update_mode11_death_sub5(void)
{
    /* room_mode_runtime.c:142-145. */
    RAM(0x00E3u) = 0u;  /* ROOM_SPRITE0_ENABLED */
    room_select_transfer_buf(94u);
}

void room_update_mode11_death_sub9(void)
{
    /* room_mode_runtime.c:147-151. */
    ROOM_TRANSFER_BUF_SELECT = 44u;
    RAM(0x00E5u) = 15u;  /* ROOM_MENU_SCROLL_TIMER */
    room_update_mode11_death_set_timer_inc_submode(24u);
}

void room_start_filling_hearts(void)
{
    /* room_mode_runtime.c:157-160. */
    RAM(0x0064u) = 2u;  /* ROOM_INV_OBJ_ACTIVE */
    room_inc_submode();
}

void room_init_mode7_finish(void)
{
    /* room_mode_runtime.c:123-127. */
    CUR_ROOM_ID = (uint8_t)RAM(0x00ECu);  /* PREV_ROOM_ID */
    room_write_and_enable_sprite0();
    core_begin_update_mode();
}

/* room_object_runtime.c:60-62. NES DecSubmenuScroll. */
void room_dec_submenu_scroll(void)
{
    ROOM_MENU_SCROLL_POS = (unsigned char)(ROOM_MENU_SCROLL_POS - 1u);
}

/* room_transfer_runtime.c:34-60. NES CopyRowToTilebuf. */
void room_copy_row_to_tilebuf(void)
{
    const unsigned char row = ROOM_ROW_INDEX;

    unsigned short ptr = 0x6530u + row;
    SAVEFILE_PTR_LO = (unsigned char)(ptr & 0xFFu);
    SAVEFILE_PTR_HI = (unsigned char)((ptr >> 8) & 0xFFu);

    unsigned short vram = 0x20E0u;
    for (signed char r = (signed char)row; r >= 0; r--)
        vram += 0x20u;
    TRANSFER_BUF_BYTE(0) = (unsigned char)((vram >> 8) & 0xFFu);
    RAM(0x0303u) = (unsigned char)(vram & 0xFFu);

    RAM(0x0304u) = 32u;
    RAM(0x0325u) = 0xFFu;

    unsigned short s = 0x6530u + row;
    for (unsigned char i = 0u; i < 32u; i++) {
        RAM(0x0305u + i) = nes_ram[s];
        s += 0x16u;
    }

    TRANSFER_BUF_POS = 35u;
    SAVEFILE_PTR_LO = (unsigned char)(s & 0xFFu);
    SAVEFILE_PTR_HI = (unsigned char)((s >> 8) & 0xFFu);
}

/* room_transfer_runtime.c:62-76. NES Cycle9InDirection. */
unsigned int room_cycle9_in_direction(unsigned int d3_in)
{
    unsigned char d3 = (unsigned char)d3_in;
    const unsigned char dir = (unsigned char)(ROOM_CYCLE_DIR & 0x03u);
    if (dir == 0u)
        return d3;
    if (dir & 1u)
        d3++;
    else
        d3--;
    if (d3 == 0xFFu)
        d3 = 8u;
    else if (d3 == 9u)
        d3 = 0u;
    return d3;
}

/* room_transfer_runtime.c:78-90. NES CopyColumnOrRowToTilebuf. */
void room_copy_column_or_row_to_tilebuf(void)
{
    const unsigned char row = ROOM_ROW_INDEX;
    if (row < 0x16u) {
        if (row == ROOM_LAST_ROW_INDEX)
            return;
        ROOM_LAST_ROW_INDEX = row;
        room_copy_row_to_tilebuf();
        return;
    }
    if (CUR_ROOM_FLAGS_PTR == 0u || CUR_ROOM_FLAGS_PTR >= 0x21u)
        return;
    room_copy_column_to_tilebuf();
}

/* room_transfer_runtime.c:92-95. NES FetchTileMapAddr. */
void room_fetch_tile_map_addr(void)
{
    SAVEFILE_PTR_LO = 48u;
    SAVEFILE_PTR_HI = 101u;
}

/* room_transfer_runtime.c:97-108. NES CopyPlayAreaAttrsHalf. */
void room_copy_play_area_attrs_half(unsigned int ppu_hi,
                                    unsigned int ppu_lo,
                                    unsigned int end_off)
{
    unsigned char src = (unsigned char)end_off;
    TRANSFER_BUF_BYTE(0) = (unsigned char)ppu_hi;
    TRANSFER_BUF_BYTE(1) = (unsigned char)ppu_lo;
    TRANSFER_BUF_BYTE(2) = 24u;
    TRANSFER_BUF_BYTE(27) = 0xFFu;
    for (unsigned char dst = 24u; dst > 0u; dst--) {
        TRANSFER_BUF_BYTE((unsigned char)(2u + dst)) = ROOM_PALETTE_ATTR(src);
        src--;
    }
}

/* room_player_runtime.c:4-12. NES GetPlayerCoordsForDirection. */
void room_player_get_coords_for_direction(unsigned int dir)
{
    if ((unsigned char)dir & 0x03u) {
        WORLD_TMP0 = LINK_Y;
        WORLD_TMP1 = LINK_X;
    } else {
        WORLD_TMP0 = LINK_X;
        WORLD_TMP1 = LINK_Y;
    }
}

/* room_player_runtime.c:14-22. NES IsDistanceSafeToSpawn. */
unsigned int room_player_is_distance_safe_to_spawn(unsigned int slot)
{
    const unsigned char dx =
        core_abs((unsigned char)(LINK_X - OBJ_X((unsigned char)slot)));
    if (dx < 0x22u) {
        const unsigned char dy =
            core_abs((unsigned char)(LINK_Y - OBJ_Y((unsigned char)slot)));
        if (dy < 0x22u)
            return CARRY_SET;
    }
    return 0u;
}

/* room_player_runtime.c:24-26. NES SetMovingDirAndSwitchToPlayerSlot. */
void room_player_set_moving_dir_and_switch_to_player_slot(unsigned int dir)
{
    COMBAT_PART_INDEX = (unsigned char)dir;
}

/* room_player_runtime.c:28-43. NES LinkModifyDirInDoorway. */
void room_player_link_modify_dir_in_doorway(void)
{
    if (ROOM_IN_DOORWAY_FLAG == 0u)
        return;
    if (ROOM_INPUT_DIR == 0u)
        return;
    if (LINK_DIR & ROOM_INPUT_DIR) {
        ROOM_INPUT_DIR = LINK_DIR;
        return;
    }
    WORLD_TMP0 = (unsigned char)core_get_opposite_dir(LINK_DIR);
    if (WORLD_TMP0 & ROOM_INPUT_DIR) {
        ROOM_INPUT_DIR = WORLD_TMP0;
        return;
    }
    ROOM_INPUT_DIR = LINK_DIR;
}

/* room_runtime.c:47-59. NES CalcOpenDoorwayMask. */
void room_calc_open_doorway_mask(unsigned int attr, unsigned int dir_idx)
{
    unsigned char is_open;
    if (attr < 4u) {
        is_open = 1u;
    } else {
        const unsigned char flags = room_get_room_flags();
        is_open = (flags & k_room_level_masks[dir_idx & 7u]) ? 1u : 0u;
    }
    unsigned char mask = ROOM_DOOR_MASK_ACC;
    mask = (unsigned char)(((unsigned char)(mask << 1) | is_open) & 0x0Fu);
    ROOM_DOOR_MASK_ACC = mask;
}

/* room_runtime.c:61-69. NES AddDoorFlags. */
void room_add_door_flags(void)
{
    const unsigned char flags = room_get_room_flags();
    for (signed char d = 3; d >= 0; d--) {
        const unsigned char masked =
            (unsigned char)(flags & k_room_level_masks[(unsigned char)d]);
        if (masked)
            CUR_OPENED_DOORS = (unsigned char)(CUR_OPENED_DOORS | masked);
    }
}

/* room_runtime.c:84-89. NES SetDoorFlag. */
void room_set_door_flag(unsigned int dir_idx)
{
    unsigned char flags = room_get_room_flags();
    const unsigned short ptr =
        (unsigned short)(((unsigned short)SAVEFILE_PTR_HI << 8) | SAVEFILE_PTR_LO);
    flags = (unsigned char)(flags | k_room_level_masks[dir_idx & 7u]);
    nes_ram[ptr + CUR_ROOM_ID] = flags;
}

/* room_runtime.c:91-100. NES ResetDoorFlag. */
void room_reset_door_flag(unsigned int dir_idx)
{
    (void)room_get_room_flags();
    const unsigned short ptr =
        (unsigned short)(((unsigned short)SAVEFILE_PTR_HI << 8) | SAVEFILE_PTR_LO);
    const unsigned char mask =
        (unsigned char)(k_room_level_masks[dir_idx & 7u] ^ 0xFFu);
    const unsigned char flags = nes_ram[ptr + CUR_ROOM_ID];
    nes_ram[ptr + CUR_ROOM_ID] = (unsigned char)(flags & mask);
}

/* room_runtime.c:125-130. NES SetEnteringDoorway. */
void room_set_entering_doorway(void)
{
    const unsigned char scroll_dir = LINK_DIR;  /* ROOM_SCROLL_DIR == LINK_DIR */
    const unsigned char a = (unsigned char)((scroll_dir >> 1) & 0x05u);
    const unsigned char b = (unsigned char)((scroll_dir << 1) & 0x0Au);
    CUR_OPENED_DOORS = (unsigned char)(a | b);
}

/* room_runtime.c:132-155. NES SaveKillCountOW. */
void room_save_kill_count_ow(unsigned int slot)
{
    const unsigned char flags = room_get_room_flags();
    const unsigned char kill_count = (unsigned char)(flags & 7u);
    WORLD_TMP2 = kill_count;
    const unsigned short ptr =
        (unsigned short)(((unsigned short)SAVEFILE_PTR_HI << 8) | SAVEFILE_PTR_LO);
    const unsigned char cell =
        (unsigned char)(nes_ram[ptr + (unsigned char)slot] & 0xF8u);
    nes_ram[ptr + (unsigned char)slot] = cell;
    const unsigned char cur_count = ROOM_OW_CUR_KILL_TOTAL;
    const unsigned char max_count = ROOM_OW_KILL_COUNT;
    unsigned char new_kill;
    if (cur_count >= max_count) {
        new_kill = 7u;
    } else {
        new_kill = (unsigned char)((cur_count & 7u) + kill_count);
        if (new_kill >= 7u)
            new_kill = 7u;
    }
    nes_ram[ptr + (unsigned char)slot] = (unsigned char)(cell | new_kill);
}

/* NES source: Z_05.asm:SaveKillCountUW ($4036 source listing).
 * Drained C: room_save_kill_count_ow / room_get_room_flags; UW translated
 * body exists in src/zelda_translated/z_05.asm:SaveKillCountUW.
 * Coverage: PARTIAL (dungeon room departure, not SRAM serialization).
 * Stance: EXTEND.
 */
void room_save_kill_count_uw(void)
{
    unsigned char flags = (unsigned char)(room_get_room_flags() & 0x3Fu);
    unsigned short ptr = (unsigned short)(((unsigned short)SAVEFILE_PTR_HI << 8) |
                                         SAVEFILE_PTR_LO);
    unsigned char room = (unsigned char)CUR_ROOM_ID;
    unsigned char count = (unsigned char)RAM(0x034Eu);
    unsigned char killed = (unsigned char)RAM(0x034Fu);
    unsigned char type = (unsigned char)RAM(0x035Fu);
    unsigned char total;
    if (count == 0u || killed >= count ||
        (killed != 0u && type >= 0x32u && type != 0x3Au &&
         type != 0x3Bu && type < 0x49u)) {
        RAM(0x0560u + room) = 0x0Fu;
        flags |= 0xC0u;
    } else {
        total = (unsigned char)(killed + RAM(0x0560u + room));
        RAM(0x0560u + room) = total;
        if (total > 2u) total = 2u;
        flags |= (unsigned char)(total << 6);
    }
    nes_ram[ptr + room] = flags;
}

/* room_runtime.c:157-160. NES TriggerOpenDoor. */
void room_trigger_open_door(unsigned int val)
{
    ROOM_OPEN_DOOR_ARG = (unsigned char)val;
    ROOM_OPEN_DOOR_TIMER = 6u;
}

/* room_runtime.c:162-164. NES TouchDoorWall. */
void room_touch_door_wall(void)
{
    ROOM_TOUCH_BLOCK_FLAG = 0xFFu;
}

/* room_runtime.c:166. NES TouchDoorOpen — empty body. */
void room_touch_door_open(void) {}

/* room_runtime.c:168. NES WieldNothing — empty body. */
void room_wield_nothing(void) {}

/* room_runtime.c:170-172. NES MaskCurPpuMaskGrayscale. */
void room_mask_cur_ppu_mask_grayscale(void)
{
    CUR_INV_TILE = (unsigned char)(CUR_INV_TILE & 0xFEu);
}

/* room_runtime.c:174-176. NES BlockAtWall. */
void room_block_at_wall(void)
{
    room_touch_door_wall();
}

/* room_runtime.c:178-180. NES CheckSecretTriggerNone. */
unsigned int room_check_secret_trigger_none(void) { return 0u; }

/* room_runtime.c:182-185. NES TriggerShutters. */
unsigned int room_trigger_shutters(void)
{
    ROOM_SHUTTER_TRIGGERED = 1u;
    return CARRY_SET;
}

/* room_runtime.c:187-189. NES ReturnFalse. */
unsigned int room_return_false(void) { return 0u; }

/* room_runtime.c:191-195. NES CheckSecretTriggerAllDead. */
unsigned int room_check_secret_trigger_all_dead(void)
{
    if (ROOM_MONSTER_ALL_DEAD != 0u)
        return room_trigger_shutters();
    return 0u;
}

/* room_runtime.c:197-201. NES CheckSecretTriggerLastBoss. */
unsigned int room_check_secret_trigger_last_boss(void)
{
    if (ROOM_BOSS_SECRET_FLAG == 0u)
        return 0u;
    return room_trigger_shutters();
}

/* room_runtime.c:203-207. NES CheckSecretTriggerMoneyOrLife. */
unsigned int room_check_secret_trigger_money_or_life(void)
{
    if (ROOM_OBJ_TYPE(0) != 0u)
        return 0u;
    return room_trigger_shutters();
}

/* room_runtime.c:209-213. NES CheckSecretTriggerBlockDoor. */
unsigned int room_check_secret_trigger_block_door(void)
{
    if (ROOM_BLOCK_SECRET_FLAG == 0u)
        return 0u;
    return room_trigger_shutters();
}

/* NES CheckSecretTriggerBlockStairs (Z_05.asm). BlockPushComplete 1 =
 * pushed and not yet acted on: make it 2 and lay the stairs tile ($70) at
 * ($D0, $60) through tile object slot $B. */
unsigned int room_check_secret_trigger_block_stairs(void)
{
    const unsigned char pushed = (unsigned char)ROOM_BLOCK_SECRET_FLAG;
    if (pushed == 0u || (pushed & 1u) == 0u)
        return 0u;
    ROOM_BLOCK_SECRET_FLAG = (uint8_t)(pushed + 1u);
    RAM(0x0070u + 11u) = 0xD0u;                  /* ObjX+11 */
    RAM(0x0084u + 11u) = 0x60u;                  /* ObjY+11 */
    dyn_tile_change_tile_obj_tiles(0x70u, 11u);
    roomrom_uw_room_render_refresh_square(0xD0u >> 3, (unsigned char)((0x60u - 0x40u) >> 3));
    return CARRY_SET;
}

/* NES CheckUnderworldSecrets (Z_05.asm), UpdateMode5Play with CurLevel
 * != 0: count the room clear, run the room's secret trigger
 * (LevelBlockAttrsByteF & 7), and for "foes for item" activate the room
 * item once the trigger fires. */
void room_check_underworld_secrets(void)
{
    unsigned int met;
    unsigned char trigger;
    room_check_has_living_monsters();
    trigger = (unsigned char)((unsigned char)RAM(0x04CDu) & 0x07u);
    switch (trigger) {
    case 1u: case 7u: met = room_check_secret_trigger_all_dead(); break;
    case 2u:          met = room_check_secret_trigger_ringleader(); break;
    case 3u:          met = room_check_secret_trigger_last_boss(); break;
    case 4u:          met = room_check_secret_trigger_block_door(); break;
    case 5u:          met = room_check_secret_trigger_block_stairs(); break;
    case 6u:          met = room_check_secret_trigger_money_or_life(); break;
    default:          return;
    }
    if (!met || trigger != 7u)
        return;
    if ((unsigned char)RAM(0x00ACu + 19u) == 0u)  /* ObjState+19: item active */
        return;
    if (progress_get_room_flag_uw_item_state() != 0u)
        return;
    RAM(0x00ACu + 19u) = 0u;
    RAM(0x0602u) = 0x02u;                        /* Tune1Request: item appears */
}

/* room_runtime.c:215-230. NES CheckSecretTriggerRingleader. */
unsigned int room_check_secret_trigger_ringleader(void)
{
    const unsigned char first = ROOM_OBJ_TYPE(0);
    if (first != 0u && first < 0x53u)
        return 0u;
    for (signed char i = (signed char)ROOM_MAX_MONSTER_SLOT; i >= 0; i--) {
        const unsigned char slot = (unsigned char)i;
        const unsigned char obj = ROOM_OBJ_TYPE(slot);
        if (obj == 0u || obj >= 0x53u)
            continue;
        if (ROOM_OBJ_STUN_TIMER(slot) != 0u)
            continue;
        ROOM_OBJ_STUN_TIMER(slot) = 16u;
    }
    return CARRY_SET;
}

/* room_runtime.c:232-236. NES TouchDoorBombable. */
void room_touch_door_bombable(void)
{
    if (ROOM_TOUCH_DOOR_BITS & CUR_OPENED_DOORS)
        return;
    room_touch_door_wall();
}

/* room_runtime.c:238-241. NES BlockUntilTime. */
void room_block_until_time(void)
{
    if (CURTAIN_TIMER != 0u)
        room_block_at_wall();
}

/* room_runtime.c:243-251. NES TouchDoorFalse. */
unsigned int room_touch_door_false(void)
{
    const unsigned char timer = CURTAIN_TIMER;
    if (timer == 1u)
        return CARRY_SET;
    if (timer == 0u)
        CURTAIN_TIMER = 24u;
    room_touch_door_wall();
    return 0u;
}

/* room_runtime.c:253-269. NES TouchDoorShutter. */
void room_touch_door_shutter(void)
{
    if (ROOM_OPEN_DOOR_TIMER != 0u) {
        room_touch_door_wall();
        return;
    }
    const unsigned char door_bits =
        (unsigned char)(ROOM_TOUCH_DOOR_BITS & CUR_OPENED_DOORS);
    if (!door_bits) {
        room_touch_door_wall();
        return;
    }
    if (door_bits & ROOM_SHUTTER_TOUCH_MASK) {
        room_block_until_time();
        return;
    }
    ROOM_SHUTTER_TOUCH_MASK = (unsigned char)(ROOM_SHUTTER_TOUCH_MASK | ROOM_TOUCH_DOOR_BITS);
}

/* room_mode_runtime.c:48-56. NES CopyNextRowToTransferBuf.
 * Returns ROOM_ROW_INDEX | CARRY_SET if more rows remain. */
unsigned int room_copy_next_row_to_transfer_buf(void)
{
    room_copy_row_to_tilebuf();
    ROOM_ROW_INDEX = (unsigned char)(ROOM_ROW_INDEX + 1u);
    unsigned int result = ROOM_ROW_INDEX;
    if (ROOM_ROW_INDEX < 0x16u)
        result |= CARRY_SET;
    return result;
}

/* room_mode_runtime.c:58-63. NES CopyNextRowAdvanceSubmode. */
unsigned int room_copy_next_row_advance_submode(void)
{
    const unsigned int result = room_copy_next_row_to_transfer_buf();
    if (!(result & CARRY_SET))
        SUBMODE_VALUE = (unsigned char)(SUBMODE_VALUE + 1u);
    return result;
}

/* room_mode_runtime.c:70-78. NES UpdateMode7Scroll_Sub2. */
void room_update_mode7_scroll_sub2(void)
{
    SUBMODE_VALUE = (unsigned char)(SUBMODE_VALUE + 1u);
    unsigned char frame = (unsigned char)(FRAME_COUNTER + 1u);
    frame &= 0x03u;
    if (CUR_LEVEL == 0u)
        frame &= 0x01u;
    ROOM_SCROLL_FRAME = frame;
}

/* room_mode_runtime.c:80-87. NES UpdateMode7Scroll_Sub7. */
void room_update_mode7_scroll_sub7(void)
{
    SUBMODE_VALUE = 1u;
    ROOM_MODE_TIMER = 0u;
    ROOM_SCROLL_LOCK_FLAG = 0u;
    ROOM_LEVEL_INDEX = 0u;
    ROOM_SPRITE0_ENABLED = 0u;
    MODE_VALUE = 4u;
}

/* room_mode_runtime.c:89-100. NES UpdateMode7Scroll_Sub6. */
void room_update_mode7_scroll_sub6(void)
{
    if (CUR_LEVEL == 0u) {
        room_update_mode7_scroll_sub7();
        return;
    }
    if (room_is_dark_room(CUR_ROOM_ID) == 0u) {
        room_update_mode7_scroll_sub7();
        return;
    }
    ROOM_ROW_INDEX = 0u;
    SUBMODE_VALUE = (unsigned char)(SUBMODE_VALUE + 1u);
}

/* room_mode_runtime.c:102-105. NES CueTransferPlayAreaAttrsHalfAndAdvance. */
void room_cue_transfer_play_area_attrs_half_and_advance_submode(unsigned int ppu_hi,
                                                                unsigned int ppu_lo,
                                                                unsigned int end_off)
{
    room_copy_play_area_attrs_half(ppu_hi, ppu_lo, end_off);
    SUBMODE_VALUE = (unsigned char)(SUBMODE_VALUE + 1u);
}

/* room_mode_runtime.c:162-164. NES InitModeB_Sub1. */
void room_init_mode_b_sub1(void)
{
    room_select_transfer_buf(62u);
}

/* room_mode_runtime.c:166-172. NES UpdateMode12_EndLevel_Sub1. */
void room_update_mode12_end_level_sub1(void)
{
    if (CURTAIN_TIMER == 0u) {
        room_start_filling_hearts();
        return;
    }
    ROOM_TRANSFER_BUF_SELECT = (unsigned char)(((CURTAIN_TIMER & 7u) < 4u) ? 24u : 120u);
}

/* room_mode_runtime.c:174-177. NES InitMode3_Sub2. */
void room_init_mode3_sub2(void)
{
    room_fill_play_area_attrs(CUR_ROOM_ID);
    room_select_transfer_buf(24u);
}

/* room_mode_runtime.c:179-181. NES InitMode3_Sub3. */
void room_init_mode3_sub3(void)
{
    room_cue_transfer_play_area_attrs_half_and_advance_submode(35u, 0xD0u, 23u);
}

/* room_mode_runtime.c:183-185. NES InitMode3_Sub4. */
void room_init_mode3_sub4(void)
{
    room_cue_transfer_play_area_attrs_half_and_advance_submode(35u, 0xE8u, 47u);
}

/* room_mode_runtime.c:187-189. NES InitMode3_Sub5. */
void room_init_mode3_sub5(void)
{
    room_select_transfer_buf(14u);
}

/* room_mode_runtime.c:191-197. NES InitMode3_Sub6. */
void room_init_mode3_sub6(void)
{
    if (CUR_LEVEL != 0u && !room_has_map()) {
        room_inc_submode();
        return;
    }
    room_select_transfer_buf(68u);
}

/* room_mode_runtime.c:199-206. NES InitMode3_Sub7. */
void room_init_mode3_sub7(void)
{
    extern unsigned char LevelNumberTransferBuf[];
    if (ROOM_LEVEL_NUMBER_VALUE == 0u) {
        room_inc_submode();
        return;
    }
    LevelNumberTransferBuf[9] = ROOM_LEVEL_NUMBER_VALUE;
    room_select_transfer_buf(12u);
}

/* room_mode_runtime.c:208-214. NES InitModeA_Sub1. */
void room_init_mode_a_sub1(void)
{
    if (CUR_LEVEL != 0u) {
        room_inc_submode();
        return;
    }
    room_patch_and_cue_level_palettes_transfer();
}

/* room_mode_runtime.c:216-225. NES UpdateMode11_Death_SubC. */
void room_update_mode11_death_sub_c(void)
{
    if (MODE11_DEATH_TIMER != 0u) return;
    (void)room_end_game_mode();
    MODE_VALUE = 8u;
    DEATH_FRAME_COUNTER = 64u;
    const unsigned char slot = SAVE_SLOT_INDEX;
    const unsigned char continue_count = CONTINUE_COUNT(slot);
    if (continue_count != 0xFFu)
        CONTINUE_COUNT(slot) = (unsigned char)(continue_count + 1u);
}

/* room_mode_runtime.c:227-235. NES UpdateMode11_Death_Sub2. */
void room_update_mode11_death_sub2(void)
{
    const unsigned int result = room_copy_next_row_advance_submode();
    if (result & CARRY_SET)
        room_write_and_enable_sprite0();
    unsigned char val = TRANSFER_BUF_BYTE(0);
    val = (unsigned char)(val + 0x08u);
    TRANSFER_BUF_BYTE(0) = val;
}

/* room_mode_runtime.c:237-245. NES EndGameMode12. */
void room_end_game_mode12(void)
{
    const unsigned char result = room_end_game_mode();
    ROOM_LEVEL_INDEX = result;
    CUR_LEVEL = result;
    MODE_VALUE = 2u;
    ROOM_LINK_CELLAR_FLAG = 2u;
    ROOM_SFX_MAIN = 0x80u;
    CUR_INV_TILE = (unsigned char)(CUR_INV_TILE & 0xFEu);
}

/* --------------------------------------------------------------- */
/* T-050 OW tile objects (slot $B).                                 */
/*                                                                  */
/* NES source: Z_04.asm UpdateRockOrGravestone, RevealSecretTileObj, */
/*   UpdateRockWall, UpdateTree, RevealAndFlagSecretTileObj,        */
/*   CheckTileObjWeaponCollision, IsQuestSecretMismatch,            */
/*   DestroyMonster_Bank4; Z_07.asm InitTileObjOrItem.              */
/* Drained C: none (drain_coverage: no candidate); helpers drained: */
/*   MoveObject, DrawObjectNotMirrored, ChangeTileObjTiles,         */
/*   MarkRoomVisited, GetShortcutOrItemXYForRoom, GetObjectMiddle,  */
/*   DoObjectsCollide, PlaySecretFoundTune.                         */
/* Coverage: FULL for types $62-$67. Stance: GREENFIELD per asm.    */
/* --------------------------------------------------------------- */

#define TO_OBJ_X(s)          RAM(0x0070u + (s))
#define TO_OBJ_Y(s)          RAM(0x0084u + (s))
#define TO_OBJ_DIR(s)        RAM(0x0098u + (s))
#define TO_OBJ_STATE(s)      RAM(0x00ACu + (s))
#define TO_OBJ_TIMER(s)      RAM(0x0028u + (s))
#define TO_OBJ_TYPE(s)       RAM(0x034Fu + (s))
#define TO_OBJ_GRID(s)       RAM(0x0394u + (s))
#define TO_OBJ_META(s)       RAM(0x0405u + (s))
#define TO_OBJ_UNINIT(s)     RAM(0x0492u + (s))
#define TO_OBJ_ATTR(s)       RAM(0x04BFu + (s))
#define TO_OBJ_INV(s)        RAM(0x04F0u + (s))
#define TO_OBJ_SHOVE_DIR(s)  RAM(0x00C0u + (s))
#define TO_OBJ_SHOVE_DIST(s) RAM(0x00D3u + (s))
#define TO_TILE_OBJ_ROOM(s)  RAM(0x0412u + (s))   /* TileObjRoomId */
#define TO_OBJ_INPUT_DIR     RAM(0x03F8u)
#define TO_INV_BRACELET      RAM(0x0665u)
#define TO_LBA_BYTE_F        RAM(0x04CDu)         /* LevelBlockAttrsByteF */
#define TO_QUEST_NUMBERS(i)  RAM(0x062Du + (i))
#define TO_CUR_SAVE_SLOT     RAM(0x0016u)
#define TO_RETURN_TO_BANK4   RAM(0x00F7u)

static const unsigned char k_rock_push_dirs[2] = { 0x08u, 0x04u };
/* SecretQuestNumbers {0,0,1}; index 3 would read the next ROM byte, the
 * LDA-absolute opcode $AD that starts IsQuestSecretMismatch. */
static const unsigned char k_secret_quest_numbers[4] = { 0x00u, 0x00u, 0x01u, 0xADu };

/* InitTileObjOrItem: attr $81, then ResetObjMetastateAndTimer. */
void room_init_tile_obj_or_item(unsigned int slot)
{
    TO_OBJ_ATTR(slot) = 0x81u;
    TO_OBJ_TIMER(slot) = 0u;
    TO_OBJ_META(slot) = 0u;
}

/* IsQuestSecretMismatch: 1 when the room's secret belongs to the other
 * quest. */
static unsigned char tile_obj_quest_mismatch(void)
{
    unsigned char q = (unsigned char)((unsigned char)TO_LBA_BYTE_F >> 6);
    if (q == 0u) return 0u;
    return (k_secret_quest_numbers[q] ==
            (unsigned char)TO_QUEST_NUMBERS((unsigned char)TO_CUR_SAVE_SLOT))
        ? 0u : 1u;
}

/* DestroyMonster_Bank4 (Z_04.asm). */
static void tile_obj_destroy(unsigned int slot)
{
    TO_OBJ_TYPE(slot) = 0u;
    TO_OBJ_SHOVE_DIR(slot) = 0u;             /* SetShoveInfoWith0 */
    TO_OBJ_SHOVE_DIST(slot) = 0u;
    TO_OBJ_TIMER(slot) = 0u;
    TO_OBJ_STATE(slot) = 0u;
    TO_OBJ_INV(slot) = 0u;
    TO_OBJ_UNINIT(slot) = 0xFFu;
    TO_OBJ_META(slot) = 0x01u;
}

/* RevealSecretTileObj. */
static void tile_obj_reveal(unsigned char tile, unsigned int slot)
{
    TO_RETURN_TO_BANK4 = (uint8_t)((unsigned char)TO_RETURN_TO_BANK4 + 1u);
    dyn_tile_change_tile_obj_tiles(tile, slot);
    tile_obj_destroy(slot);
    enemy_play_secret_found_tune();
}

/* RevealAndFlagSecretTileObj: reveal, then room flags |= $80. */
static void tile_obj_reveal_and_flag(unsigned char tile, unsigned int slot)
{
    unsigned char flags;
    unsigned short ptr;
    tile_obj_reveal(tile, slot);
    flags = room_get_room_flags();
    ptr = (unsigned short)(((unsigned short)(unsigned char)SAVEFILE_PTR_HI << 8) |
                           (unsigned char)SAVEFILE_PTR_LO);
    nes_ram[ptr + (unsigned char)CUR_ROOM_ID] = (uint8_t)(flags | 0x80u);
}

/* CheckTileObjWeaponCollision: weapon middle -> [04]/[05], tile object
 * middle -> [02]/[03], threshold $10. */
static unsigned char tile_obj_weapon_collides(unsigned int slot, unsigned char weapon)
{
    RAM(0x0000u) = weapon;
    RAM(0x0004u) = (uint8_t)((unsigned char)TO_OBJ_X(weapon) + 8u);
    RAM(0x0005u) = (uint8_t)((unsigned char)TO_OBJ_Y(weapon) + 8u);
    world_get_object_middle(slot);
    return collision_do_objects_collide(0x10u);
}

/* UpdateRockOrGravestone ($62 rock, $65 gravestone, $66). */
void room_update_rock_or_gravestone(unsigned int slot)
{
    if ((unsigned char)TO_OBJ_STATE(slot) == 0u) {
        unsigned char y = 0u;
        unsigned char dist;
        unsigned char in;
        if ((unsigned char)TO_OBJ_TYPE(slot) != 0x65u &&
            (unsigned char)TO_INV_BRACELET == 0u) return;
        if (tile_obj_quest_mismatch()) return;
        /* Pushed only vertically: X must match Link's. */
        if ((unsigned char)TO_OBJ_X(0) != (unsigned char)TO_OBJ_X(slot)) return;
        dist = (unsigned char)((unsigned char)TO_OBJ_Y(0) + 3u -
                               (unsigned char)TO_OBJ_Y(slot));
        if (dist & 0x80u) {                 /* Link is above the object */
            y = 1u;
            dist = (unsigned char)(0u - dist);
        }
        if (dist >= 0x11u) return;
        in = (unsigned char)((unsigned char)TO_OBJ_INPUT_DIR & 0x0Cu);
        if (in == 0u) return;
        if (in != k_rock_push_dirs[y]) return;
        TO_OBJ_DIR(slot) = in;
        TO_OBJ_STATE(slot) = (uint8_t)((unsigned char)TO_OBJ_STATE(slot) + 1u);
        TO_TILE_OBJ_ROOM(slot) = (unsigned char)CUR_ROOM_ID;
        /* Gravestone / rock square -> gray floor; the object draws above. */
        TO_RETURN_TO_BANK4 = (uint8_t)((unsigned char)TO_RETURN_TO_BANK4 + 1u);
        dyn_tile_change_tile_obj_tiles(0x26u, slot);
        return;
    }

    /* State 1: move, draw one pixel above the object. */
    RAM(NES_OBJ_DIR) = (unsigned char)TO_OBJ_DIR(slot);
    object_move_object((unsigned short)slot);
    RAM(0x0000u) = (unsigned char)TO_OBJ_X(slot);      /* Anim_FetchObjPos... */
    RAM(0x0001u) = (uint8_t)((unsigned char)TO_OBJ_Y(slot) - 1u);   /* DEC $01 */
    RAM(0x000Fu) = 0u;
    draw_object_not_mirrored(0u, slot);
    {
        unsigned char g = (unsigned char)TO_OBJ_GRID(slot);
        unsigned char save_room;
        unsigned char tile;
        unsigned char type;
        unsigned int xy;
        if (g != 0x10u && g != 0xF0u) return;
        save_room = (unsigned char)CUR_ROOM_ID;
        CUR_ROOM_ID = (uint8_t)TO_TILE_OBJ_ROOM(slot);
        room_mark_room_visited();
        CUR_ROOM_ID = save_room;
        type = (unsigned char)TO_OBJ_TYPE(slot);
        tile = (type == 0x62u) ? 0xC8u : (type == 0x65u) ? 0xBCu : 0xC0u;
        TO_RETURN_TO_BANK4 = (uint8_t)((unsigned char)TO_RETURN_TO_BANK4 + 1u);
        dyn_tile_change_tile_obj_tiles(tile, slot);
        xy = world_get_shortcut_or_item_xy_for_room(
            (unsigned int)(unsigned char)TO_TILE_OBJ_ROOM(slot));
        TO_OBJ_X(slot) = (uint8_t)(xy >> 8);
        TO_OBJ_Y(slot) = (uint8_t)(xy & 0xFFu);
        tile_obj_reveal(0x70u, slot);
    }
}

/* NES source: Z_07.asm UpdateFluteSecret / CueTransferPondPaletteRow /
 * RevealPondStairs / AnimatePond (the flute's pond secret, Q1 room $42).
 * Drained C: core_init_flute_secret (InitFluteSecret) only.
 * Coverage: FULL. Stance: GREENFIELD per asm (T-171).
 * Every 8 frames the water palette row (BG row 3) steps through
 * PondCycleColors; at step $A most water tiles become walkable
 * (ObjectFirstUnwalkableTile $99); after step $B the stairs appear at
 * ($60, $90) and the secret is flagged. Leaving the room (InitMode7
 * Sub0) runs the cycle backwards. */
static const unsigned char k_water_palette_row[8] = {
    0x3Fu, 0x0Cu, 0x04u, 0x0Fu, 0x17u, 0x37u, 0x12u, 0xFFu
};
static const unsigned char k_pond_cycle_colors[12] = {
    0x12u, 0x11u, 0x22u, 0x21u, 0x31u, 0x32u, 0x33u, 0x35u,
    0x34u, 0x36u, 0x37u, 0x37u
};

void room_cue_pond_palette_row(unsigned char y)
{
    unsigned char i;
    for (i = 0u; i < 8u; ++i) RAM((unsigned short)(0x0302u + i)) = k_water_palette_row[i];
    RAM(0x0308u) = k_pond_cycle_colors[y];       /* DynTileBuf+6 */
    if (y == 0x0Au) RAM(0x034Au) = 0x99u;         /* ObjectFirstUnwalkableTile */
}

void room_update_flute_secret(unsigned int slot)
{
    const unsigned char y = (unsigned char)RAM(0x051Au);   /* SecretColorCycle */
    if (y >= 0x0Cu) return;
    if (((unsigned char)RAM(0x0015u) & 0x07u) != 0x04u) return;
    RAM(0x051Au) = (uint8_t)(y + 1u);
    if (y == 0x0Bu) {                                       /* RevealPondStairs */
        TO_OBJ_X(slot) = 0x60u;
        TO_OBJ_Y(slot) = 0x90u;
        tile_obj_reveal_and_flag(0x70u, slot);
        return;
    }
    room_cue_pond_palette_row(y);
}

/* AnimatePond: InitMode7_Sub0 while SecretColorCycle != 0. */
void room_animate_pond(void)
{
    if (((unsigned char)RAM(0x0015u) & 0x04u) == 0u) return;
    RAM(0x051Au) = (uint8_t)((unsigned char)RAM(0x051Au) - 1u);
    room_cue_pond_palette_row((unsigned char)RAM(0x051Au));
}

/* UpdateRockWall ($63, $67): a detonating bomb reveals a cave. */
void room_update_rock_wall(unsigned int slot)
{
    unsigned char w;
    if (tile_obj_quest_mismatch()) return;
    w = 0x10u;
    if ((unsigned char)TO_OBJ_STATE(w) != 0x13u) {
        w = 0x11u;
        if ((unsigned char)TO_OBJ_STATE(w) != 0x13u) return;
    }
    if (!tile_obj_weapon_collides(slot, w)) return;
    tile_obj_reveal_and_flag(0x24u, slot);
}

/* UpdateTree ($64): a standing fire about to go out reveals stairs. */
void room_update_tree(unsigned int slot)
{
    unsigned char w;
    if (tile_obj_quest_mismatch()) return;
    w = 0x10u;
    if ((unsigned char)TO_OBJ_STATE(w) != 0x22u) {
        w = 0x11u;
        if ((unsigned char)TO_OBJ_STATE(w) != 0x22u) return;
    }
    RAM(0x0000u) = w;                   /* NES @FoundFire: STY $00 (T-171) */
    if ((unsigned char)TO_OBJ_TIMER(w) >= 0x02u) return;
    if (!tile_obj_weapon_collides(slot, w)) return;
    tile_obj_reveal_and_flag(0x70u, slot);
}
