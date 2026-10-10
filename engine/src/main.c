#include "../../src/game/room/room_dispatch.h"
#include "../../src/game/world/startup_triangle.h"
#include "../../src/game/world/circle_transition.h"
#include "../../src/game/items/weapon_dispatch.h"  /* weapon_wield_flute */
#include <genesis.h>
#include "engine_runtime.h"
#include "../../src/game/world/render/ow_render.h"  /* Phase 12.2 promoted */
#include "../../src/game/world/render/cave_palette.h"  /* Tier 0 #42 cave palette */
#include "../../src/game/world/render/cave_fade.h"     /* Tier 1 cave entry/exit fade */
#include "../../src/game/debug/state_dump.h"            /* A+B+C+Start freeze + dump */
#include "../../src/game/dungeon/uw_render.h"        /* Phase 12.2 promoted */
#include "../../src/game/hud/hud_runtime.h"  /* Phase 12.2 promoted */
#include "../../src/game/world/render/sprite_render.h"
#include "../../src/game/world/draw_dispatch.h"  /* T-092 HUD A/B boxes */
#include "../../src/game/world/render/sprite_slots.h"  /* Phase 8 W0c HUD slot 10 */
#include "../../src/game/combat/combat_runtime.h"  /* Phase 12.2 promoted */
#include "../../src/game/items/boomerang.h"      /* Phase 12.2 promoted */
#include "../../src/game/items/arrow.h"          /* Phase 12.2 promoted */
#include "../../src/game/items/bomb.h"           /* Phase 12.2 promoted */
#include "../../src/game/world/scene_load.h"  /* Phase 12.2 promoted */
#include "../../src/game/world/palette_tick_runtime.h"  /* Phase 12.2 promoted */
#include "cave_dispatch.h"  /* debate 006 D2: native cave gamemode entry */
#include "../../src/game/cave/cave_entrance.h"  /* Tier 0 cave-entrance detect */
#include "../../src/game/combat/collision_dispatch.h"  /* Tier 0 tile-under-Link lookup */
#include "../../src/game/dungeon/door_state.h"  /* Phase 12.2 promoted */
#include "../../src/game/dungeon/walk_model.h"  /* Phase 12.2 promoted */
#include "render_abi.h"
#include "../../src/state/rng_state.h"  /* Phase 7 NMI fix: per-frame rng_next() */
#include "engine_state.h"  /* Task 5.4: warp-outcome apply boundary */
#include "../../src/game/world/ow_scroll.h"
#include "../../src/game/world/transition.h"  /* Task 5.4 warp coord (Phase 12.2 promoted) */
#include "../../src/game/dungeon/cellar_mode.h"
#include "../../src/game/dungeon/cellar_meta.h"      /* Phase 12.2 promoted */
#include "../../src/game/world/pushblock.h"  /* Task 5.7 (Phase 12.2 promoted) */
#include "../../src/game/dungeon/dark_meta.h"        /* Phase 12.2 promoted */
#include "../../src/game/dungeon/item_room_meta.h"   /* Phase 12.2 promoted */
#include "../../src/game/items/candle_fire.h"   /* Task 5.8.1 candle fire (Phase 12.2 promoted) */
#include "../../src/state/pause_state.h"  /* Task 6.10.1: Paused flag (Phase 12.2 promoted) */
#include "../../src/game/inventory/inventory_render.h"  /* P6.2: pause subscreen */
#include "../../src/game/combat/link_damage.h"   /* Task 6.11.1 (Phase 12.2 promoted) */
#include "../../src/state/inventory.h"                   /* Task 6.10.10: rupee tick */
#include "../../src/state/nes_ram_sync.h"                /* Plan v5a Tier-1: $FA/$FB/$66F/$670/$008C */
#include "../../src/game/audio/audio_dispatch.h"         /* Plan v5b Tier-5 T5.5: gamemode+scene music dispatcher */
#include "../../src/abi/audio_abi.h"                     /* Phase 8 W6/W7: audio_sfx_play */
#include "../../src/game/world/transfer_buf_drain.h"     /* Plan v5b: TRANSFER_BUF -> CRAM bridge (unblocks Mode 11 palette cycle) */
#include "../../src/game/world/mode_endlevel.h"             /* T-013: GameMode $12 */
#include "../../src/game/world/mode_continue_question.h"    /* T-013: GameMode 8 */
#include "../../src/game/world/mode_save.h"                 /* T-013: GameMode $0D */
#include "vram_layout.h"                                /* ROOMROM_BG_TILE_BASE */
#include "../../src/game/world/progress_dispatch.h"      /* Tier 2: triforce fanfare driver */
#include "../../src/game/world/world_dispatch.h"     /* world_animate_world_fading */
#include "../../src/game/world/trap_dispatch.h"     /* CheckPassiveTileObjects on OW collision */
#include "../../src/game/dungeon/uw_dark.h"
#include "../../src/game/dungeon/link_doorway.h"  /* T-131 */
#include "../../src/game/world/link_ladder.h"     /* T-056 */
#include "../../src/game/world/object_dispatch.h"  /* NES MoveObject slot */
#include "../../src/game/enemies/bosses/boss_framework.h"  /* CreateRoomObjects */
#include "../../src/game/audio/audio_requests.h"         /* T-127: NES sound request cells */              /* T-111: dark rooms by palette */
#include "../../src/game/cave/uw_person_dispatch.h"    /* T-120: CheckPersonBlocking */
#include "probes/metadata_probe.h"     /* Task 5.4: Gate D in-ROM probe */
#include "atlas/level_chr_swap.h"        /* PR-4a: scene-bank DMA state machine */
#include "player_state.h"                 /* Phase 6 Task 6.1: typed players[] */
#include "enemy_loop.h"                   /* Phase 7 Task 7.2 step 2 (WT-5) */
#include "enemy_loop_probe.h"             /* Phase 7 Task 7.2 step 2 probe */
#include "warp_routes_probe.h"            /* Phase E (2026-05-24) warp dispatch probe */
#include "dungeon_roundtrip_probe.h"      /* Phase F (2026-05-25) dungeon round-trip */
#include "options_probe.h"                /* Phase 9 Task 9.1 in-ROM tests */
#include "options_persistence_probe.h"    /* Phase 9 Task 9.2 SRAM tests */
#include "options_persistence.h"          /* Phase 9 Task 9.4 load-or-default */
#include "options_runtime.h"              /* Phase 9: snapshot live options around probes */
#include "options_consumer.h"             /* Phase 9 Task 9.4 game-start hook */
#include "options_state.h"                /* Phase 9: saved runtime option ids */
#include "options_consumer_probe.h"       /* Phase 9 Task 9.4 consumer tests */
#include "hud_format_probe.h"             /* Phase 9 Task 9.5 HUD format tests */
#include "save_serializer_probe.h"        /* Phase 9 Task 9.7 save serializer tests */
#include "../../src/game/world/mode_dispatch.h"  /* Phase 9.7 gameplay-mode dispatcher */
#include "../../src/game/world/mode_wingame.h"
#include "../../src/game/world/level_info_install.h"  /* substrate: install $687E..$6C7D LBA + LevelInfo */
#include "../../src/game/enemies/enemy_render.h"      /* Phase 7: NES OAM -> Genesis SAT bridge */
#include "../../src/game/world/mode_death.h"         /* T-097: GameMode $11 */
/* Phase 7: ROOM_BOUNDS setup. Forward-declare to avoid oracle types
 * header pulling conflicting u8/s32 definitions. */
extern void roomld_setup_obj_room_bounds(void);

/* Boots to overworld room 0x77.
 *
 * Modes (toggled by X):
 *   WALK       D-pad moves Link
 *   TELEPORT   D-pad jumps room (16x8 grid: room_id = (row<<4)|col)
 *
 * Walk style (toggled by Y):
 *   NES        Z1-faithful: single-axis only, grid-locked turns,
 *              QSpeed=$60 -> 1.5 px/frame avg, instant stop on release.
 *              Source: reference/aldonunez/Z_05.asm Link_HandleInput +
 *              Z_07.asm Walker_Move / MoveObject.
 *   ALTTP      8-direction (real diagonal), 8.8 sub-pixel position,
 *              cardinal vel=24, diagonal vel=16 (sqrt(2) compensation).
 *              Source: github.com/snesrev/zelda3 src/player.c
 *              Link_HandleVelocity + Link_MovePosition + kSpeedMod.
 *
 * Buttons (always):
 *   X         toggle WALK <-> TELEPORT
 *   Y         toggle NES <-> ALTTP walk style
 *   A         swing sword
 *   B         use selected B-item (boomerang/arrow/bomb/candle/rod)
 *   Z         cycle B-item slot forward
 *   C         map variant toggle (original <-> redux), per-scene
 *   START     scene toggle (overworld <-> dungeon)
 *   Z+START   (dungeon scene) toggle quest 1 <-> 2
 *
 *   MODE button is reserved (Genesis 6-button hardware mode select)
 *   and intentionally unbound. */

/* SCENE_CAVE (debate 006 D2 follow-up): native cave gamemode harness.
 * Toggle from SCENE_OW with C+START. While SCENE_CAVE is active the
 * main loop calls cave_tick per VBlank â€” currently a stub, so the
 * scene visually inherits OW (no dedicated cave render until Phase 4
 * native object_draw lands). C+START again exits back to SCENE_OW
 * and calls cave_exit. */
typedef enum { SCENE_OW = 0, SCENE_UW = 1, SCENE_CAVE = 2 } scene_t;
typedef enum { MODE_WALK = 0, MODE_TELEPORT = 1 } mode_t;
typedef enum { MOVE_STYLE_NES = 0, MOVE_STYLE_ALTTP = 1 } move_style_t;

/* NES Z1 movement direction (matches Z_05.asm Link_ModifyDir bit layout
 * conceptually: only one axis at a time, no diagonal). */
typedef enum {
    LINK_DIR_NONE  = 0,
    LINK_DIR_DOWN  = 1,
    LINK_DIR_UP    = 2,
    LINK_DIR_LEFT  = 3,
    LINK_DIR_RIGHT = 4
} link_dir_t;

/* TEST DEFAULTS: boot into overworld room $77 (NES start screen,
 * Level 1 cave entrance just south). Press MODE for OW<->UW toggle,
 * C+START for cave enter, etc. â€” see button map in comment block
 * around line 50. Prior boot was SCENE_UW $73 (L1 entrance); switched
 * 2026-05-15 so debug-enter shows the real game starting screen. */
static scene_t       s_scene       = SCENE_OW;
static mode_t        s_mode        = MODE_WALK;
static move_style_t  s_move_style  = MOVE_STYLE_NES;
static u8 s_room_id = 0x77;
/* Plan v5a T3.1 â€” gameplay-active flag. Set true at end of
 * roomrom_debug_enter; gates the a4_probe GameMode-sentinel restore
 * (RAM($0012)==$CD -> Mode 5) so future Modes 3/4 Unfurl/Enter don't
 * get force-snapped to Play mid-transition. */
static u8 s_in_gameplay = 0u;
/* Phase 6 Task 6.1: Link position/facing now lives in `players[0]`.
 * Boot defaults are seeded in `init_player_state()` below before any
 * scene/render code runs. */
/* T-092: keys are NES InvKeys ($66E), loaded from the save (debug
 * sessions get theirs from debug_unlock_all_items). */
#define s_link_keys (*(unsigned char *)&nes_ram[0x066Eu])

/* S7 B-item slot (cycle with Z, fire with B). Order roughly matches
 * Z1 inventory grid: boomerang -> bombs -> arrow -> candle -> rod. */
typedef enum {
    B_ITEM_NONE      = 0,
    B_ITEM_BOOMERANG = 1,
    B_ITEM_ARROW     = 2,
    B_ITEM_BOMB      = 3,
    B_ITEM_CANDLE    = 4,
    B_ITEM_ROD       = 5,
    B_ITEM_FLUTE     = 6,
    B_ITEM_FOOD      = 7,
    B_ITEM_POTION    = 8,
    B_ITEM_COUNT     = 9
} b_item_t;
static b_item_t s_b_item = B_ITEM_BOOMERANG;

/* T-090: set only by the title A+B+C / X+Y+Z debug chords
 * (src/platform/game_main.c); gates every gameplay debug input and seed. */
extern unsigned char g_debug_session;

/* Phase 8 W0 (2026-05-20): inventory subscreen A-press setter. NES cursor
 * slot 0..8 (per SubmenuCursorXs Z_05.asm:7909) maps to s_b_item enum:
 *   cursor 0 boomerang -> B_ITEM_BOOMERANG
 *   cursor 1 bombs     -> B_ITEM_BOMB
 *   cursor 2 arrow     -> B_ITEM_ARROW
 *   cursor 3 bow       -> B_ITEM_NONE (WieldNothing in NES)
 *   cursor 4 candle    -> B_ITEM_CANDLE
 *   cursor 5 recorder  -> B_ITEM_FLUTE
 *   cursor 6 food      -> B_ITEM_FOOD
 *   cursor 7 potion    -> B_ITEM_POTION (WieldPotion, T-057)
 *   cursor 8 wand      -> B_ITEM_ROD */
static const unsigned char k_inv_cursor_to_b_item[9] = {
    B_ITEM_BOOMERANG, B_ITEM_BOMB, B_ITEM_ARROW, B_ITEM_NONE,
    B_ITEM_CANDLE, B_ITEM_FLUTE, B_ITEM_FOOD, B_ITEM_POTION, B_ITEM_ROD
};

void roomrom_set_b_item_from_inv_cursor(unsigned char cursor_slot)
{
    if (cursor_slot >= 9u) return;
    s_b_item = (b_item_t)k_inv_cursor_to_b_item[cursor_slot];
}

/* Phase 8 W0c (2026-05-20): per-frame HUD B-item icon update.
 * Uses SGDK VDP_setSpriteFull cached path (VBlank-safe DMA).
 * SGDK adds +128 to (x, y) internally, pass NES pixel coords directly. */
static unsigned long s_hud_b_key;
static unsigned char s_hud_b_key_valid;

static void roomrom_hud_b_item_update(void)
{
    /* T-092: NES DrawStatusBarItemsAndEnsureItemSelected (Z_07.asm). B box
     * at ($7C,$1F), A box (sword) at ($94,$1F); DrawItemBySlot tile/attr,
     * narrow item tiles get X+4 (Anim_WriteSpecificItemSprites). Lockstep
     * captures: no sword -> no A sprite; sword 1/2/3 -> $20/$00, $20/$01,
     * $48/$02 at X $98; wooden boomerang -> $36/$00 at X $80. */
    unsigned char slot, attr, tile;
    s16 bx = (s16)-32, ax = (s16)-32;
    unsigned short battr = 0u, aattr = 0u;
    const s16 hud_y = (s16)(0x1Fu - 7u);   /* -8 top-crop + 1 user nudge = -7 */
    const unsigned char has_b = hud_status_bar_b_item(&slot);
    {
        /* T-172: the two sprites only change with the item, its value, the
         * sword, the 8-frame flash phase or the pause edge; recomputing
         * them every frame cost busy rooms ~100 instructions. */
        const unsigned long key = ((unsigned long)(has_b ? slot : 0xFFu) << 24) |
            ((unsigned long)(has_b ? nes_ram[0x0657u + slot] : 0u) << 16) |
            ((unsigned long)nes_ram[0x0657u] << 8) |
            (unsigned long)(((nes_ram[0x0015u] >> 3) & 1u) |
                            (roomrom_pause_is_active() ? 2u : 0u));
        if (s_hud_b_key_valid && key == s_hud_b_key) return;
        s_hud_b_key = key;
        s_hud_b_key_valid = 1u;
    }
    if (has_b) {
        tile = draw_item_icon(slot, nes_ram[0x0657u + slot], &attr);
        battr = enemy_render_item_sat(tile, attr);
        bx = (s16)(0x7Cu + ((tile == 0xF3u || (tile >= 0x20u && tile < 0x62u)) ? 4u : 0u));
    }
    if (nes_ram[0x0657u] != 0u) {
        tile = draw_item_icon(0u, nes_ram[0x0657u], &attr);
        aattr = enemy_render_item_sat(tile, attr);
        ax = (s16)(0x94u + ((tile == 0xF3u || (tile >= 0x20u && tile < 0x62u)) ? 4u : 0u));
    }
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_HUD_B_ITEM, bx, (bx < 0) ? (s16)-32 : hud_y,
        RENDER_SPRITE_SIZE(1, 2), battr, ROOMROM_SPRITE_SLOT_HUD_B_ITEM_R);
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_HUD_B_ITEM_R, ax, (ax < 0) ? (s16)-32 : hud_y,
        RENDER_SPRITE_SIZE(1, 2), aattr, ROOMROM_SPRITE_SLOT_HUD_PLAYER);
    /* Phase 8 W0c safeguard: the pause subscreen writes the SAT directly,
     * so the gameplay sprite cache is stale when it opens or closes.
     * Invalidate on that edge only (T-131: invalidating every frame made
     * every cached SAT write go through, a busy-room lag cost). */
    {
        static unsigned char s_prev_paused = 0xFFu;
        unsigned char paused = roomrom_pause_is_active() ? 1u : 0u;
        if (paused != s_prev_paused) {
            s_prev_paused = paused;
            roomrom_sprites_invalidate_cache();
        }
    }
}
static link_dir_t  s_link_dir  = LINK_DIR_NONE;  /* NES ObjDir: last active axis */
static unsigned char s_doorway_dir = UW_WALK_DOOR_NONE; /* active UW doorway */
static signed char s_link_grid_offset = 0;       /* NES ObjGridOffset: -8..8 */
static unsigned char s_link_go_straight = 0u;    /* NES Link_GoStraight ($57) */
static u8          s_link_pos_frac   = 0u;       /* NES single-axis sub-pixel */
static u8          s_link_subx       = 0u;       /* ALTTP per-axis sub-pixel X */
static u8          s_link_suby       = 0u;       /* ALTTP per-axis sub-pixel Y */

/* Task 5.4: NES `UndergroundExitType` analogue. Slice 1 wires this static
 * into the warp coordinator's rule-1 precondition but never writes it â€”
 * the writer is the deferred UW->OW exit slice. Until then the rule
 * collapses to "grid_offset == 0", which slice-1 acknowledges in the
 * spec rather than pretending it enforces both halves. */
static u8          s_underground_exit_type = 0u;

/* Phase B (2026-05-24) â€” master quest selector. NES Z1 stores per-save-slot
 * quest at SaveFileAQuestNumber{0,1,2} ($651B-$651D) and live at
 * QuestNumbers ($62D). Genesis mirror: single byte, 1 = Q1 (default), 2 = Q2.
 * Coordinator reads via roomrom_main_current_quest(); save-slot loader +
 * future quest-selector menu write via roomrom_main_set_quest(). */
static u8          s_current_quest = 1u;

/* Task 5.8/5.9 perf: per-room cache of expensive lookups. Refreshed
 * in load_room on every room change. Per-tick reads are O(1) instead
 * of full master-table linear scan (uw_dark_rooms 261 rows +
 * uw_item_rooms 969 rows would otherwise burn ~80% of frame budget). */
static u8          s_cur_room_is_dark   = 0u;
/* T-111: NES fades that hold the game: InitMode7 Sub6 (fade to black
 * before scrolling into a dark room) and InitMode4 Sub3 (fade to light
 * after entering a light room from an unlit dark one). */
enum { UW_FADE_NONE = 0, UW_FADE_BEFORE_SCROLL, UW_FADE_AFTER_SCROLL };
static u8          s_uw_fade_phase      = UW_FADE_NONE;
static u8          s_cur_room_is_cellar = 0u;
static u8          s_cur_room_has_item  = 0u;
static struct uw_item_room_meta s_cur_room_item_meta;

/* Task 5.5: per-touch latch for the state-mirror diff harness. */
static unsigned char s_last_touch_dir       = 0xFFu;
static unsigned char s_last_touch_result    = 0xFFu;
static unsigned char s_last_touch_keys_pre  = 0u;
static unsigned char s_last_touch_keys_post = 0u;
static unsigned char s_last_touch_door_type = 0xFFu;
static unsigned char s_uw_shutter_trigger_count = 0u;

/* S6.6 transition state machine.
 * BG_A is a 64x64 tile staging plane split into four 32x32 screen slots.
 * The fixed HUD is drawn on Window, so BG_A can scroll as one plane in both
 * axes without a per-row vertical split. */
#define SCROLL_TOTAL_FRAMES_SMOOTH 32u   /* Genesis-native: 8 px H / 5.5 px V per frame. */
#define SCROLL_TOTAL_FRAMES_CLASSIC 64u  /* Original debug cadence: 4 px H / 2.75 px V. */
#define ROOMROM_SLOT_TILES 32u
#define ROOMROM_SLOT_PIXELS ((short)(ROOMROM_SLOT_TILES * 8u))
#define ROOMROM_PLANE_PIXELS ((short)(ROOMROM_PLANE_ROWS * 8u))
/* PR-2c: 64x64 single-surface layout. BG_A holds the scroll surface;
 * BG_B mirrors BG_A so transparent pixels cannot reveal a distinct
 * staging room underneath. BG_A H scroll still uses two 32-col slots. */
#define ROOMROM_VERTICAL_STRIDE_TILES ROOMROM_ROOM_ROWS
#define ROOMROM_PLAYFIELD_TOP_PX ((short)(ROOMROM_ROOM_FIRST_ROW * 8u))

typedef enum {
    SCROLL_NONE    = 0,
    SCROLL_H_RIGHT = 1,   /* Link walked right; new room slides in from right */
    SCROLL_H_LEFT  = 2,   /* Link walked left;  new room slides in from left */
    SCROLL_V_DOWN  = 3,   /* Link walked down;  new room slides in from bottom */
    SCROLL_V_UP    = 4    /* Link walked up;    new room slides in from top */
} scroll_state_t;

static scroll_state_t s_scroll_state    = SCROLL_NONE;
static u8             s_ow_edge = 0u;
/* T-131: UW CheckScreenEdge / false-wall exit direction (NES bit). */
static u8             s_uw_edge = 0u;
/* T-132: level entry from the OW (NES modes $10 / 2 / 3, then 4). */
enum { LVL_NONE = 0, LVL_STAIRS, LVL_CURTAIN, LVL_EXIT_LOAD, LVL_STEP_OUT,
       LVL_CAVE_EXIT, LVL_PLAY_INIT, LVL_MODE3_INIT, LVL_MODE2_INIT };
static u8             s_lvl_phase = LVL_NONE;
static u8             s_lvl_target_y = 0u;
static u8             s_lvl_step = 0u;
static u8             s_lvl_timer = 0u;
static u8             s_lvl_enter_only = 0u;
static u8             s_lvl_init = 0u;       /* InitMode10 frame pending */
static u8             s_lvl_load_pending = 0u;  /* T-171: mode 2 entered, load next tick */
static u8             s_curtain_song_pending = 0u; /* start after final plane transfer */
static u8             s_lvl_display_pending = 0u; /* T-171: display on at the curtain */
static u8             s_lvl_exiting = 0u;    /* curtain leads to StepOutside */
static u8             s_lvl_entrance_tile = 0u; /* UndergroundEntranceTile */
static u16            s_curtain[22][32];     /* play-area words behind it */
static unsigned char begin_level_exit(void);
static void step_out_start_pos(void);
static void end_prepare_mode(void);
static void begin_cave_exit(void);
/* T-135: NES UndergroundEntranceTile of the cave Link is in ($24 = the
 * black opening: method 1-a step out; else the stairs). */
static u8            s_cave_entrance_tile = 0x24u;
static short         s_tick_start_link_y = 0;   /* players[0].y at tick start */
/* T-171: Link's move/animate cells at tick start, for the cave exit
 * (NES CheckCaveEdge leaves before UpdatePlayer moves or animates). */
static signed char   s_tick_start_grid;
static u8            s_tick_start_frac, s_tick_start_anim, s_tick_start_frame;
static rr_warp_outcome_t s_lvl_out;
static u8             s_scroll_frame    = 0u;     /* counts up during scroll */
static u8             s_scroll_total_frames = SCROLL_TOTAL_FRAMES_SMOOTH;
static u8             s_active_slot_x   = 0u;     /* 0 = cols 0..31, 1 = cols 32..63 */
static u8             s_active_row_base = 0u;     /* room base row in the 64-row plane */
static u8             s_transition_target = 0u;
static u8             s_transition_row_base = 0u;
/* PR-2c: vertical transitions stage the incoming room in unused rows of
 * the same 64x64 nametable. BG_B mirrors BG_A rather than staging a
 * separate room, avoiding color-0 transparency leaks. */
static u8             s_active_plane = 0u;        /* 0 = BG_A, 1 = BG_B */
static short          s_active_scroll_x = 0;
static short          s_active_scroll_y = 0;
static short          s_scroll_start_x = 0;
static short          s_scroll_start_y = 0;
static short          s_scroll_target_x = 0;
static short          s_scroll_target_y = 0;
static long           s_scroll_cur_x_8_8 = 0;
static long           s_scroll_cur_y_8_8 = 0;
static long           s_scroll_step_x_8_8 = 0;
static long           s_scroll_step_y_8_8 = 0;
/* Pre-scroll Link screen position (where he was when edge was crossed). */
static short          s_scroll_start_link_x = 0;
static short          s_scroll_start_link_y = 0;
/* Post-scroll Link screen position (entry pos in the new room). */
static short          s_transition_link_x = 0;
static short          s_transition_link_y = 0;

/* Increments every frame; used by Phase 2.6.5 palette tick. */
static u16 s_frame_counter = 0u;
static u16 s_joy_prev = 0u;

/* Tier 0 (plan v6) cave return-state: save OW room+pos on cave entry
 * so cave exit returns Link to the entrance tile. */
static u8  s_cave_return_room = 0x77u;
static u8  s_cave_return_face = LINK_FACE_DOWN;
static u8  s_cave_return_x    = 120u;
static u8  s_cave_return_y    = 133u;

/* Map room metatile col 0..15 into one 32x32 staging slot. */
static u8 plane_col_for_slot(u8 src_col, u8 slot_x)
{
    return (u8)(src_col + (slot_x ? 16u : 0u));
}

static short scroll_x_offset_for_slot(u8 slot)
{
    return slot ? (short)-ROOMROM_SLOT_PIXELS : 0;
}

static u8 ow_nes_scroll_enabled(void)
{
    return s_scene == SCENE_OW && s_move_style == MOVE_STYLE_NES;
}

/* T-131: NES modes 6/7/4 (ow_scroll.c) run the OW and UW room scrolls. */
static u8 nes_scroll_enabled(void)
{
    return (s_scene == SCENE_OW || s_scene == SCENE_UW) &&
           s_move_style == MOVE_STYLE_NES;
}

static u8 transition_scroll_total_frames(void)
{
    return (options_consumer_get_room_scroll() == OPTIONS_SCROLL_CLASSIC)
        ? SCROLL_TOTAL_FRAMES_CLASSIC
        : SCROLL_TOTAL_FRAMES_SMOOTH;
}

static u8 row_base_add(u8 base, short delta)
{
    short rows = (short)((short)base + delta);
    while (rows < 0) rows = (short)(rows + (short)ROOMROM_PLANE_ROWS);
    while (rows >= (short)ROOMROM_PLANE_ROWS) {
        rows = (short)(rows - (short)ROOMROM_PLANE_ROWS);
    }
    return (u8)rows;
}

static short scroll_y_for_row_base(u8 row_base)
{
    short y = (short)((unsigned short)row_base * 8u);
    if (y >= (short)(ROOMROM_PLANE_PIXELS / 2)) {
        y = (short)(y - ROOMROM_PLANE_PIXELS);
    }
    return y;
}

static short scroll_from_8_8(long value)
{
    if (value < 0)
    {
        return (short)-((short)((-value) >> 8));
    }
    return (short)(value >> 8);
}

static void scroll_init_fixed_point_steps(void)
{
    long den = (long)s_scroll_total_frames;
    if (den <= 0)
    {
        den = 1;
    }
    s_scroll_cur_x_8_8 = ((long)s_scroll_start_x) << 8;
    s_scroll_cur_y_8_8 = ((long)s_scroll_start_y) << 8;
    s_scroll_step_x_8_8 = ((((long)s_scroll_target_x - (long)s_scroll_start_x) << 8) / den);
    s_scroll_step_y_8_8 = ((((long)s_scroll_target_y - (long)s_scroll_start_y) << 8) / den);
}

static void scroll_advance_fixed_point(short *h_scroll, short *v_scroll)
{
    s_scroll_cur_x_8_8 += s_scroll_step_x_8_8;
    s_scroll_cur_y_8_8 += s_scroll_step_y_8_8;

    *h_scroll = scroll_from_8_8(s_scroll_cur_x_8_8);
    *v_scroll = scroll_from_8_8(s_scroll_cur_y_8_8);

    if (s_scroll_frame >= (u8)(s_scroll_total_frames - 1u))
    {
        *h_scroll = s_scroll_target_x;
        *v_scroll = s_scroll_target_y;
    }
}

/* T-135: while set, the hardware scroll stays parked on blank plane rows
 * (a room loads unseen behind a black playfield). */
static u8 s_scroll_hold = 0u;
#define PARK_VSCROLL 256   /* plane rows 32..59 on screen */

static void set_plane_scroll(u8 plane, short h_scroll, short v_scroll)
{
    if (s_scroll_hold) return;
    if (plane)
    {
        VDP_setHorizontalScroll(BG_B, h_scroll);
        VDP_setVerticalScroll(BG_B, v_scroll);
    }
    else
    {
        VDP_setHorizontalScroll(BG_A, h_scroll);
        VDP_setVerticalScroll(BG_A, v_scroll);
    }
}

static void clear_tile_rect_on_plane(u8 plane, u16 x, u16 y, u16 w, u16 h)
{
    u16 row;
    /* T-125: one VDP address per row (was per cell; ~30k instructions of
     * the dungeon-entry load). */
    for (row = 0u; row < h; row++)
        render_plane_fill_row(plane, x, (u16)(y + row), w, 0u);
}

static void clear_room_scroll_gutters_on_plane(u8 plane)
{
    const u16 full_width = (u16)(ROOMROM_SLOT_TILES * 2u);
    const u16 bottom_row =
        (u16)(ROOMROM_ROOM_FIRST_ROW + ROOMROM_ROOM_ROWS);
    const u16 bottom_rows = (u16)(ROOMROM_PLANE_ROWS - bottom_row);

    /* T-172: whole rows (full_width = the plane's 64 cells): VDP fill. */
    render_plane_clear_full_rows(plane ? 1u : 0u, 0u, (u16)ROOMROM_ROOM_FIRST_ROW,
                                 full_width);
    render_plane_clear_full_rows(plane ? 1u : 0u, bottom_row, bottom_rows, full_width);
}

static void clear_hud_underlay_for_slot(u8 row_base, u8 slot_x)
{
    u16 rows_left = (u16)ROOMROM_HUD_ROWS;
    u16 row = row_base;
    const u16 col = slot_x ? ROOMROM_SLOT_TILES : 0u;

    /* Window tile color 0 is transparent, so the shared scroll surface under
     * the HUD must be black at the active row base. Fixed rows 0..6 are not
     * always safe: after an upward scroll they are live bottom-room rows. */
    while (rows_left != 0u) {
        u16 chunk = (u16)(ROOMROM_PLANE_ROWS - row);
        if (chunk > rows_left) chunk = rows_left;
        VDP_clearTileMapRect(BG_A, col, row, ROOMROM_SLOT_TILES, chunk);
        rows_left = (u16)(rows_left - chunk);
        row = 0u;
    }
}

static void clear_hud_underlay_for_row_base(u8 row_base)
{
    clear_hud_underlay_for_slot(row_base, s_active_slot_x);
}

static void set_bg_scroll(short h_scroll, short v_scroll)
{
    /* Transition smoothness probe (stack page, masked by the lockstep
     * diff, like the T-125 cells $01FE/$01FF): VDP V counter when this
     * tick last wrote the plane scroll. */
    nes_ram[0x01FDu] = (u8)(*(volatile u16 *)0xC00008u >> 8);
    set_plane_scroll(s_active_plane, h_scroll, v_scroll);
    set_plane_scroll((u8)(s_active_plane ^ 1u), h_scroll, v_scroll);
}

/* Room transitions: the plane scroll goes out in the next VBlank, with the
 * sprite table this tick queued (VDP_updateSprites DMA_QUEUE). Written at
 * once, the frame showed this tick's camera with the previous tick's
 * sprites: Link jumped 8 px against the room on every vertical row step
 * (tools/lockstep/transition_smooth.py, t013_route OW-V "world" +-8).
 * Both planes get the same values (set_bg_scroll). */
/* T-168: plane B shares plane A's cells, so in the playfield it adds
 * nothing; but the HUD is the window plane, whose black pixels are
 * transparent, and plane B shows through them. During a vertical scroll
 * the camera passes plane rows that hold room tiles, so the HUD lost its
 * black background (t168_trans_ow / t168_trans_uw composed frames, up to
 * 12k of 14k HUD pixels). A vertical scroll leaves the room's other
 * 32-column slot unused: it is cleared at the scroll start
 * (v_scroll_blank_other_slot) and plane B looks at it (all blank) until
 * the next horizontal scroll or pause, which set B back to A. */
static u8 s_b_blank_col = 0xFFu;   /* slot column base plane B shows, or none */

static void set_bg_scroll_with_sprites(short h_scroll, short v_scroll)
{
    const u8 v = (u8)(s_scroll_state == SCROLL_V_DOWN || s_scroll_state == SCROLL_V_UP);
    if (s_scroll_hold) return;
    nes_ram[0x01FDu] = 0xFFu;   /* committed in VBlank */
    VDP_setHorizontalScrollVSync(BG_A, h_scroll);
    VDP_setVerticalScrollVSync(BG_A, v_scroll);
    if (v && s_b_blank_col != 0xFFu) {
        VDP_setHorizontalScrollVSync(BG_B, (s16)-(s16)((u16)s_b_blank_col << 3));
        VDP_setVerticalScrollVSync(BG_B, 0);
    } else {
        VDP_setHorizontalScrollVSync(BG_B, h_scroll);
        VDP_setVerticalScrollVSync(BG_B, v_scroll);
    }
}

/* NES PutLinkBehindBackground (UpdateMode10Stairs): stamp the plane cells
 * around Link's standing position high priority so the low-priority Link
 * sprite shows only over colour 0 (the black entrance). Plane cell of NES
 * pixel (X, Y): the Genesis frame is the NES frame minus its top 8 lines;
 * screen x shows plane pixel x - hscroll, line l shows plane line
 * l + vscroll (the plane is 64x64 and scrolled; T-132: the old
 * (Y >> 3) + 7 assumed an unscrolled 32-row plane, so no cell was marked). */
static void mark_behind_bg_at(u8 x, u8 y)
{
    unsigned short px = (unsigned short)((x - s_active_scroll_x) & 511);
    unsigned short py = (unsigned short)((y - 8 + s_active_scroll_y) & 511);
    cave_fade_mark_arch_hi_prio((unsigned char)(px >> 3), (unsigned char)(py >> 3));
}

static u8 s_step_out_restamp = 0u;
static u8 s_arch_restore_ticks = 0u;   /* ticks until the stamp lifts */

static void mark_link_behind_bg(void)
{
    mark_behind_bg_at((u8)players[0].x, (u8)players[0].y);
}

/* Forward-decl of s_link_frame which is defined below at line ~529 with
 * other Link movement statics. Cave fade descend handler ticks it to
 * give Link a visible walk-cycle while sinking into the entrance. */
extern u8 s_link_frame;

/* T-125: the NES movement step asks for Link's walk pose; it is drawn
 * once per frame, after AnimateLinkBase (NES SetUpWalkingSprites). */
static unsigned char s_link_draw_pending = 0u;

/* T-134: set while the cave-load playfield is black (Link hidden). */
static unsigned char s_cave_load_blank = 0u;

static void draw_link_pending(void)
{
    if (!s_link_draw_pending) return;
    s_link_draw_pending = 0u;
    /* CheckSubroom installs the return coordinates after drawing Link.
     * Keep that cellar frame; InitModeA hides him on the next tick. */
    if (nes_ram[0x0012u]==0xAu && !nes_ram[0x0011u] && !nes_ram[0x0013u]) return;
    if (s_cave_load_blank) return;
    if (nes_ram[0x04F0u] != 0u)
        roomrom_sprites_set_link_hurt_pose(players[0].x, players[0].y,
            players[0].face, s_link_frame, nes_ram[0x04F0u]);
    else
        roomrom_sprites_set_link_pose(players[0].x, players[0].y,
                                      players[0].face, s_link_frame);
}

/* T-012: the NES runs its NMI frame work (timers, Random, FrameCounter)
 * once per frame of a load; Genesis loads in fewer frames. Enemy AI reads
 * Random and the timers, so after a fast load the missing NES frames' work
 * runs at once (no wait): t012_route room $67 octoroks turned the other
 * way and hit Link 60 ticks early, FrameCounter 20 behind the NES. */
static void nes_frame_timers_and_random(void);
/* The NES NMI handler's per-frame work the Genesis mirrors: INC
 * FrameCounter (Z_07.asm:519), the sound engine consuming the request
 * cells written during the previous frame (T-127), timers and Random. */
static void nes_frame_nmi(void)
{
    nes_ram[0x0015u] = (unsigned char)(nes_ram[0x0015u] + 1u);
    audio_requests_consume();
    nes_frame_timers_and_random();
}
static u16 s_nes_load_base = 0u;   /* s_frame_counter at the load's start */
/* T-171: NES load timelines on the video-frame clock. Frame t counts
 * hardware frames (vtimer) since the mode's submode-0 frame, so a Genesis
 * load that overruns frames itself stays on the NES schedule. Per frame:
 * the NES NMIs run so far (an NMI is lost while the NMI handler is still
 * busy: no FrameCounter, timers, Random or audio) and GameMode / Submode /
 * IsUpdatingMode. While a timeline runs it owns the per-frame NMI work;
 * its owner waits for it to end before the next step (cave swap,
 * curtain). NES captures (tools/nesemu, T-171 entrance traces). */
typedef struct {
    u8 len;            /* frames 0..len-1 */
    const u8 *nmis;    /* NMIs run by the end of frame t (frame 0's ran) */
    const u8 *mode;    /* 0 = leave GameMode as it is */
    const u8 *sub;
    const u8 *upd;
    void (*on_frame)(u8 t);   /* other cells the NES changes mid-load, or 0 */
} nes_load_timeline_t;
/* InitModeB (OW cave): submodes 1-3, the 22 row copies of submode 4,
 * submodes 5-7, InitMode_WalkCave (submode 8) on frame 31. LayoutCave
 * overruns: the NMIs of frames 5 and 6 are lost (OW $77: FC $44 on frames
 * 4-6, submode 8 with FC $5D on frame 31). Mode $0B or $0C as set. */
static const u8 k_cave_enter_nmis[32] = {
    0, 1, 2, 3, 4, 4, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
    14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29 };
static const u8 k_cave_enter_sub[32] = {
    0, 1, 2, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 6, 7, 8 };
static const u8 k_zero32[32] = { 0 };
/* IsSprite0CheckActive: InitModeSubroom_Sub0 enables the sprite-0 split
 * in the OW (WriteAndEnableSprite0; on from frame 1), submode 6
 * (InitModeAOrB_TransferBottomHalfAttrs) disables it (off from frame 30).
 * The NES reads no input while it is on (IsrNmi @CheckInput). */
static void cave_enter_frame(u8 t)
{
    nes_ram[0x00E3u] = (u8)(t >= 1u && t <= 29u);
}
static const nes_load_timeline_t k_cave_enter_tl = {
    32u, k_cave_enter_nmis, 0, k_cave_enter_sub, k_zero32, cave_enter_frame };
/* Level entry from OW stairs: mode 2 (TurnOffAllVideo; submode 0 copies
 * the level's pattern blocks and common code with the NMI handler busy for
 * frames 2-17), mode 3 submodes 1-8, init done (mode 3 updating) on frame
 * 31; frame 32 is the first UpdateMode3Unfurl, the curtain's first step.
 * Same for levels 1-9 (all Q1 entrances but the L7 lake). */
static const u8 k_level_enter_nmis[33] = {
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 12, 12, 12, 13 };
static const u8 k_level_enter_mode[33] = {
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 };
static const u8 k_level_enter_sub[33] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 8, 8, 0, 0 };
static const u8 k_level_enter_upd[33] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1 };
/* The OW room and Link's stairs position until InitMode3 replaces them:
 * RoomId = LevelInfo_StartRoomId from frame 21 (submode 2), Link at ($78,
 * LevelInfo_StartY) from frame 31 (submode 8). */
static u8 s_lvl_enter_room = 0u, s_lvl_enter_x = 0u, s_lvl_enter_y = 0u;
static u8 s_lvl_dest_room = 0xFFu;   /* level exit: the OW room (else $6BAD) */
static u8 s_lvl_exit_objdir = 0u;    /* level exit: ObjDir at the edge */
static u8 s_lvl_exit_src526 = 0xFFu; /* level exit: CaveSourceRoomId at the edge */
static void level_enter_frame(u8 t)
{
    nes_ram[0x00EBu] = (t >= 21u)
        ? ((s_lvl_dest_room != 0xFFu) ? s_lvl_dest_room : nes_ram[0x6BADu])
        : s_lvl_enter_room;
    if (t >= 31u) {             /* InitMode3_Sub8, after LayOutRoom (it
                                 * overruns frames 29-30; on the NES frame
                                 * 31 with mode 3 updating): curtain columns */
        nes_ram[0x007Cu] = 0x10u;
        nes_ram[0x007Du] = 0x11u;
    }
    if (t < 31u) {
        nes_ram[0x0070u] = s_lvl_enter_x;
        nes_ram[0x0084u] = s_lvl_enter_y;
    } else {
        nes_ram[0x0070u] = 0x78u;
        nes_ram[0x0084u] = nes_ram[0x6BA6u];
    }
}
static const nes_load_timeline_t k_level_enter_tl = {
    33u, k_level_enter_nmis, k_level_enter_mode, k_level_enter_sub, k_level_enter_upd,
    level_enter_frame };
/* Level exit through the start room's open side (mode 6 edge frame = 0):
 * InitMode6 (frame 0) and its update (1), InitMode7 submodes 0-1 (2-3),
 * then mode 2 from frame 4 as the level entry (k_level_enter_tl frames
 * 0-29, 31, 32: the OW room's layout needs one submode-8 frame less).
 * t270-305 of tools/emu/lockstep_entrance.py 0x37 (L1 out to OW $37). */
static const u8 k_level_exit_nmis[36] = {
    0, 1, 2, 3,
    4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 16, 16, 17 };
static const u8 k_level_exit_mode[36] = {
    6, 6, 7, 7,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 };
static const u8 k_level_exit_sub[36] = {
    0, 0, 0, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 8, 0, 0 };
static const u8 k_level_exit_upd[36] = {
    0, 1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1 };
static void level_exit_frame(u8 t)
{
    u8 te;
    if (t < 4u) return;                       /* still in the level */
    te = (u8)(t - 4u);
    if (te >= 30u) ++te;                      /* no entry frame 30 */
    level_enter_frame(te);
    /* InitMode3_Sub8 places Link facing up at the start spot. */
    nes_ram[0x0098u] = (te >= 31u) ? 0x08u : s_lvl_exit_objdir;
    /* InitMode3_Sub1: CaveSourceRoomId used (unchanged until then; $FF
     * when the level was not entered from the OW, the room then comes
     * from StartRoomId), then invalid. */
    nes_ram[0x0526u] = (te >= 21u) ? 0xFFu : s_lvl_exit_src526;
}
static const nes_load_timeline_t k_level_exit_tl = {
    36u, k_level_exit_nmis, k_level_exit_mode, k_level_exit_sub, k_level_exit_upd,
    level_exit_frame };
#define LEVEL_EXIT_MODE2_FRAME 4u   /* EndGameMode12: CurLevel 0, mode 2 */
#define LEVEL_EXIT_LOAD_FRAME  5u   /* InitMode2's first frame (entry 1) */
static u32 s_lvl_exit_vt0 = 0u;
static u8 s_lvl_exit_tl = 0u;       /* 1: mode 6 exit on k_level_exit_tl */
static u8 s_lvl_exit_mode2 = 0u;
/* Cave exit, mode $0A (InitModeA, OW): submode 0 (InitModeSubroom_Sub0:
 * DrawSpritesBetweenRooms, WriteAndEnableSprite0) on frame 1, submodes
 * 1-3, LayoutRoom (submode 4) overruns: the NMIs of frames 6 and 7 are
 * lost, the 22 row copies of submode 5, submodes 6-A, mode 4 on frame 34
 * (InitModeA_SubA_GoToMode4). t134_cave_exit OW $77, NES FC $06 on the
 * mode $0A frame, $26 on mode 4's (tools/emu/lockstep_frames.py). */
static const u8 k_cave_leave_nmis[35] = {
    0, 1, 2, 3, 4, 5, 5, 5, 6, 7, 8, 9, 10, 11, 12, 13,
    14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 32 };
static const u8 k_cave_leave_mode[35] = {
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 4 };
static const u8 k_cave_leave_sub[35] = {
    0, 1, 2, 3, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 7, 8,
    9, 10, 0 };
static const u8 k_cave_leave_upd[35] = { 0 };
/* IsSprite0CheckActive: on from submode 0 (frame 1), off from frame 31
 * (submode 7, InitModeAOrB_TransferBottomHalfAttrs). */
static void cave_leave_frame(u8 t)
{
    nes_ram[0x00E3u] = (u8)(t >= 1u && t <= 30u);
}
static const nes_load_timeline_t k_cave_leave_tl = {
    35u, k_cave_leave_nmis, k_cave_leave_mode, k_cave_leave_sub, k_cave_leave_upd,
    cave_leave_frame };
static const nes_load_timeline_t *s_load_tl = 0;
static u32 s_load_vt0 = 0u;
static u8 s_load_nmis = 0u;
static void nes_frame_nmi(void);
static void nes_load_clock_start(const nes_load_timeline_t *tl)
{
    s_load_tl = tl;
    s_load_vt0 = vtimer;
    s_load_nmis = 0u;
}
static u8 nes_load_clock_busy(void) { return (u8)(s_load_tl != 0); }
static u8 nes_load_clock_frame(void)
{
    u32 t = vtimer - s_load_vt0;
    return (u8)((t >= s_load_tl->len) ? s_load_tl->len - 1u : t);
}
/* Frame t's state (no NMIs): also for the end of a tick whose own work
 * (a Genesis load) rewrote these cells. */
static void nes_load_clock_reapply(void)
{
    const nes_load_timeline_t *tl = s_load_tl;
    u8 t;
    if (tl == 0) return;
    t = nes_load_clock_frame();
    if (tl->mode) nes_ram[0x0012u] = tl->mode[t];
    nes_ram[0x0013u] = tl->sub[t];
    nes_ram[0x0011u] = tl->upd[t];
    if (tl->on_frame) tl->on_frame(t);
}
/* Per tick, in place of the NMI work: catch up to frame t's NMI count and
 * state. Returns 0 when no timeline runs (the caller does the NMI work). */
static u8 nes_load_clock(void)
{
    const nes_load_timeline_t *tl = s_load_tl;
    u8 t;
    if (tl == 0) return 0u;
    t = nes_load_clock_frame();
    while (s_load_nmis < tl->nmis[t]) { nes_frame_nmi(); ++s_load_nmis; }
    nes_load_clock_reapply();
    if (t == (u8)(tl->len - 1u)) s_load_tl = 0;
    return 1u;
}
static void nes_frames_catch_up(unsigned char nes_frames)
{
    const u16 ran = (u16)(s_frame_counter - s_nes_load_base);
    u16 n;
    if (ran >= nes_frames) return;
    for (n = (u16)(nes_frames - ran); n != 0u; --n) {
        nes_ram[0x0015u] = (unsigned char)(nes_ram[0x0015u] + 1u);
        nes_frame_timers_and_random();
    }
}
/* FrameCounter steps from the mode $0B submode 0 frame (stairs end) to
 * InitMode_WalkCave's first frame: submodes 0-3 (4), 22 row copies,
 * 5-7 (3) (Z_05.asm InitModeB; t012_route NES fc $A8 -> $C5). */
#define NES_CAVE_ENTER_FRAMES 29u
/* From mode 2's first frame (stairs end) to the mode 3 curtain: mode 2
 * (4) + InitMode3 submodes (8) (t012_route NES fc $48 -> $54). */
#define NES_LEVEL_LOAD_FRAMES 12u
/* T-140: FrameCounter steps from the level-exit edge tick (mode 6 set by
 * CheckScreenEdge) to the mode 3 curtain: modes 6, 7, 2 and the InitMode3
 * submodes (t132_uw_exit NES fc $78 -> $88 over 34 frames). */
#define NES_LEVEL_EXIT_FC_STEPS 16u
/* T-013: from the GameMode $12 Sub4 frame (EndGameMode12; FrameCounter
 * already advanced for that frame) to the mode 3 curtain: mode 2 and the
 * InitMode3 submodes (t013_route NES curtain starts at fc $5A; the Sub4
 * frame runs with $4E). */
#define NES_MODE12_EXIT_FC_STEPS 12u
static u8 s_lvl_exit_fc = 0u;      /* FrameCounter on the exit edge tick */
static u8 s_lvl_exit_dir = 0u;     /* door exit: ObjDir, 0 = not a door exit */
static u8 s_lvl_exit_next = 0u;    /* door exit: CalculateNextRoom result */
static u8 s_lvl_exit_steps = NES_LEVEL_EXIT_FC_STEPS;

/* MD Remix presentation only: native entry/exit owners call this once the
 * destination is fully loaded. The iris freezes that settled scene. */
static void circle_open_here(void)
{
    if (!circle_transition_waiting()) return;
    roomrom_hud_set_counts_hidden(0u);
    roomrom_hud_draw(roomrom_uw_room_render_get_map(),s_room_id,s_scene == SCENE_UW);
    roomrom_hud_b_item_update();
    roomrom_sprites_set_link_pose(players[0].x,players[0].y,players[0].face,0u);
    VDP_updateSprites(80u,DMA_QUEUE);
    circle_transition_open((short)(players[0].x + 8),(short)(players[0].y + 1),
                            s_active_scroll_x,s_active_scroll_y);
}

/* Tier 1 cave-fade callbacks. cave_fade.c owns sequencing + cave_init/
 * cave_exit + plane fill; this side owns Link reposition + scene flip
 * + HUD underlay reset (which need engine-local statics). */

/* Per NES Z_05.asm:2308 UpdateMode10Stairs: Link Y += 1 every 4 frames.
 * cave_fade.c calls this 16 times across the descend (step_idx 0..15). */
static void cave_fade_descend_step_handler(unsigned char step_idx)
{
    (void)step_idx;
    players[0].y = (short)(players[0].y + 1);
    /* Mirror Link X+Y to nes_ram ObjX[0]/$0070 + ObjY[0]/$0084 so the byte-diff
     * (and any collision/sprite reader) tracks the real Link position during the
     * descent â€” the gated player->nes_ram sync is suppressed while cave_fade is
     * active, otherwise nes_ram $70 holds the stale pre-descent (teleport-spawn)
     * X while players[0].x is already the entrance column. */
    nes_ram[0x0070u] = (unsigned char)players[0].x;
    nes_ram[0x0084u] = (unsigned char)players[0].y;

    /* Walk pose is driven by cave_fade_anim_tick_handler on the NES 6-frame
     * cadence (ObjAnimCounter), NOT toggled here per 4-frame step. */

    /* NES also sets sprite priority bit $20 on Link upper-half sprites
     * so the entrance arch tile covers them ("Link sinks into hole"
     * effect). Genesis SAT priority bit is high â€” defer this polish:
     * direct VDP SAT munge needs sprite_render.c hook. For now, Link
     * walks down without arch overlay. */
}

static void cave_fade_swap_entry_handler(cave_id_t cid)
{
    (void)cid;
    s_cave_load_blank = 0u;
    s_scene = SCENE_CAVE;
    /* T-190: InitModeB's cave nametable has a fixed origin. The native
     * cave renderer fills columns0..31, rows7..28; OW scroll ownership
     * must publish that same origin before text/attribute transfers.
     * NES source: Z_05.asm:InitModeB_Sub5_FillTileAttrsAndTransferTopHalf.
     * Drained C: cave_fade.c + existing host camera publication.
     * Coverage: PARTIAL (camera retained the previous OW slot).
     * Stance: EXTEND camera handoff; preserve cave layout/palette/logic.
     * Commit scroll with the queued cave rows at VBlank, not midway
     * through the old room's blank frame. */
    s_active_slot_x = 0u;
    s_active_row_base = 0u;
    s_active_scroll_x = 0;
    s_active_scroll_y = 0;
    s_b_blank_col = 0xFFu;
    clear_hud_underlay_for_row_base(0u);
    set_bg_scroll_with_sprites(0, 0);
    /* NES Z1 cave-entry Link spawn (Z_01.asm:2965 InitModeB_EnterCave_Bank5):
     * ObjX=$70 (112), ObjY=$DD (221), facing up. Byte-verified vs NES via
     * cave_transition_diff (2026-05-30): Gen was (120,192)=($78,$C0); NES
     * cave-bottom ObjY=$DD before the 48-px emerge walk-up. */
    players[0].x    = 0x70u;   /* 112 */
    players[0].y    = 0xDDu;   /* 221 */
    players[0].face = LINK_FACE_UP;
    nes_ram[0x0098u] = 0x08u;  /* ObjDir up, on this frame as the NES (T-171) */
    /* T-012: this is InitMode_WalkCave's first frame (submode 8). */
    nes_frames_catch_up(NES_CAVE_ENTER_FRAMES);
    nes_ram[0x0013u] = 8u;
    /* InitModeB_EnterCave: InitMode_EnterRoom (ResetPlayerState, clears
     * ObjInputDir), and UndergroundExitType = 1 for the way out (T-171). */
    room_reset_player_state();
    nes_ram[0x03F8u] = 0u;
    nes_ram[0x005Au] = 1u;
    s_link_grid_offset = 0x30;
    s_link_pos_frac = 0u;
    /* Mirror spawn into nes_ram ObjX/ObjY[0] so the byte-diff sees $DD at the
     * swap frame (cave_init wrote $D0 here as its cave-spawn). The LINK_EMERGE
     * phase then walks ObjY[0] $DD -> $D5 each frame, below. */
    nes_ram[0x0070u] = 0x70u;
    nes_ram[0x0084u] = 0xDDu;
    nes_ram[0x0394u] = 0x30u;   /* ObjGridOffset budget */
    nes_ram[0x03A8u] = 0x00u;   /* ObjPosFrac */
}

/* NES cave-ENTRY emerge step (InitMode_WalkCave). cave_fade.c runs the
 * MoveObject math and hands us the new ObjY + ObjGridOffset + ObjPosFrac;
 * we write players[0].y and mirror the three nes_ram cells the byte-diff
 * tracks. Runs while cave_fade is active, so the gated cave-play path does
 * not fight Link's position. */
static void cave_fade_emerge_step_handler(unsigned char obj_y,
                                          unsigned char grid,
                                          unsigned char posfrac)
{
    players[0].y      = (short)obj_y;
    nes_ram[0x0084u]  = obj_y;
    nes_ram[0x0394u]  = grid;
    nes_ram[0x03A8u]  = posfrac;
    /* The end-of-tick Link publish copies these back to $394/$3A8. */
    s_link_grid_offset = (signed char)grid;
    s_link_pos_frac    = posfrac;
    nes_ram[0x0013u]  = 8u;   /* InitMode_WalkCave */
    nes_ram[0x03F8u]  = nes_ram[0x0098u];   /* ObjInputDir = ObjDir */
    /* Walk pose driven by cave_fade_anim_tick_handler (6-frame cadence). */
}

/* NES cave-exit Mode 10 mirror: Link walks UP, Y -= 1 every 4 frames. */
static void cave_fade_ascend_step_handler(unsigned char step_idx)
{
    (void)step_idx;
    players[0].y = (short)(players[0].y - 1);
    nes_ram[0x0084u] = (unsigned char)players[0].y;
}

/* NES walk-anim cadence (Z_07.asm:5045 AnimateObjectWalking): cave_fade.c runs
 * the 6-frame ObjAnimCounter down-count + frame toggle and hands them here
 * every frame. Drive the visible Link pose + mirror nes_ram ObjAnimCounter
 * ($3D0) / ObjAnimFrame ($3E4) so the byte-diff (Tier A) + sprite cadence
 * (Tier B) match NES. */
static void cave_fade_anim_tick_handler(unsigned char counter,
                                        unsigned char frame)
{
    s_link_frame      = frame;
    nes_ram[0x03D0u]  = counter;
    nes_ram[0x03E4u]  = frame;
}

/* T-012: InitMode_WalkCave's settle frame begins the cave update. */
static void cave_fade_walk_done_handler(void)
{
    nes_ram[0x0013u] = 0u;
    nes_ram[0x0011u] = 1u;   /* RunCrossRoomTasksAndBeginUpdateMode */
    circle_open_here();
}

static void cave_fade_swap_exit_handler(void)
{
    rr_warp_outcome_t out = {0};
    /* Use the same handoff as other scene exits: restore room data,
     * collision, CHR, enemies and the underground-exit latch together. */
    out.dest_scene = SCENE_OW;
    out.dest_room_id = s_cave_return_room;
    out.dest_link_x = s_cave_return_x;
    out.dest_link_y = (u8)(s_cave_return_y + 16u);
    out.dest_link_face = LINK_FACE_DOWN;
    out.dest_redux_flag = roomrom_main_current_redux_flag();
    roomrom_main_apply_warp_outcome(&out);
}

/* T-134: NES mode $0B blanks the playfield (HUD stays) while it lays out
 * the cave; the curtain blank is the same black play area (byte-checked
 * vs NES in T-132). */
static void playfield_blank(void);
static void cave_fade_load_blank_handler(unsigned char stage)
{
    if (stage == 0u) {
        /* The native stairs walk has finished. Close around the entrance
         * before the shared loader replaces the scene. */
        circle_transition_close((short)(players[0].x + 8),(short)(players[0].y + 1),
                                 s_active_scroll_x,s_active_scroll_y);
        /* Sprite changes reach VRAM at the next VBlank, one frame after
         * the plane writes below: hide Link a tick earlier so both show
         * on the same frame. NES enters mode $0B (cave) here and stays
         * in it while in the cave (T-011: TakeItem lifts the item only
         * outside mode 5). */
        nes_ram[0x0012u] = nes_ram[0x005Bu];  /* Mode B or shortcut Mode C */
        nes_ram[0x0013u] = 0u;
        nes_ram[0x0011u] = 0u;                /* EndGameMode: init mode */
        s_nes_load_base = s_frame_counter;
        nes_load_clock_start(&k_cave_enter_tl);
        s_cave_load_blank = 1u;
        roomrom_sprites_set_link_pose((short)-32, (short)-32,
                                      players[0].face, 0u);
    } else {
        playfield_blank();
        nes_ram[0x0013u] = 1u;   /* InitModeB load submodes (NES 1..7) */
    }
}

static const cave_fade_callbacks_t k_cave_fade_callbacks = {
    cave_fade_descend_step_handler,
    cave_fade_swap_entry_handler,
    cave_fade_ascend_step_handler,
    cave_fade_swap_exit_handler,
    cave_fade_emerge_step_handler,
    cave_fade_anim_tick_handler,
    cave_fade_load_blank_handler,
    cave_fade_walk_done_handler
};

static void anchor_active_slot(void)
{
    set_bg_scroll(s_active_scroll_x, s_active_scroll_y);
}

static void set_room_render_target_plane(u8 plane)
{
    if (s_scene == SCENE_UW) {
        roomrom_uw_room_render_set_target_plane(plane);
    } else {
        roomrom_ow_room_render_set_target_plane(plane);
    }
}

/* Render a room into the specified horizontal slot and vertical row base. */
static void render_room_into_slot(u8 room_id, u8 slot_x, u8 row_base)
{
    u8 c;
    if (s_scene == SCENE_UW) {
        if (roomrom_uw_room_render_fill_room_prepared(room_id,
                plane_col_for_slot(0u, slot_x), row_base))
            return;
        for (c = 0; c < 16; c++) {
            roomrom_uw_room_render_fill_one_col_at(room_id, c,
                plane_col_for_slot(c, slot_x), row_base);
        }
    } else {
        if (roomrom_ow_room_render_fill_room_prepared(room_id,
                plane_col_for_slot(0u, slot_x), row_base))
            return;
        for (c = 0; c < 16; c++) {
            roomrom_ow_room_render_fill_one_col_at(room_id, c,
                plane_col_for_slot(c, slot_x), row_base);
        }
    }
}

u8                 s_link_frame = 0u;      /* non-static: forward-declared at top of file for cave_fade descend handler */
static u8          s_link_anim_tick = 0u;
#define LINK_ANIM_PERIOD 8u
#define LINK_GRID_SIZE   8
#define LINK_QSPEED      0x60u   /* NES Z_05.asm InitLinkSpeed: $60 = 1.5 px/frame avg */

static unsigned char link_nes_grid_at_limit(void)
{
    return (s_link_grid_offset == LINK_GRID_SIZE ||
            s_link_grid_offset == -LINK_GRID_SIZE) ? 1u : 0u;
}

/* T-123: NES InitLinkSpeed. Link's q-speed is $60 except on the OW
 * stair / slow tiles $74 / $75 (ObjCollidedTile $49E) where it is $30;
 * switching to $30 resets the position fraction. */
/* Link's ObjQSpeedFrac lives in the NES cell: a private copy written back
 * every tick undid InitMode_EnterRoom's $60 after leaving a slow tile
 * (T-171: t111_dark_candle t1108). */
#define s_link_qspeed nes_ram[0x03BCu]

static void link_nes_init_speed(void)
{
    u8 q = LINK_QSPEED;
    if (nes_ram[0x0010u] == 0u) {
        u8 t = nes_ram[0x049Eu];
        if (t == 0x74u || t == 0x75u) {
            q = 0x30u;
            if (s_link_qspeed != 0x30u) s_link_pos_frac = 0u;
        }
    }
    s_link_qspeed = q;
}

static unsigned char link_nes_add_qspeed(void)
{
    unsigned short sum = (unsigned short)s_link_pos_frac + s_link_qspeed;
    s_link_pos_frac = (u8)(sum & 0xFFu);
    if (link_nes_grid_at_limit()) return 0u;
    if (sum >= 0x100u) {
        s_link_grid_offset++;
        return 1u;
    }
    return 0u;
}

static unsigned char link_nes_sub_qspeed(void)
{
    int diff = (int)s_link_pos_frac - (int)s_link_qspeed;
    s_link_pos_frac = (u8)(diff & 0xFF);
    if (link_nes_grid_at_limit()) return 0u;
    if (diff < 0) {
        s_link_grid_offset--;
        return 1u;
    }
    return 0u;
}

/* NES CheckWarps -> HandleWarpOW (Z_05.asm:7210-7318): on a mode 5 frame
 * that passes the gate (UndergroundExitType 0, grid 0, X on a $10 column,
 * $08 in room $22, Y low nibble $D) the OW stores Link's tile in
 * UndergroundEntranceTile; @CheckWarps restores ObjCollidedTile after
 * (T-171: t111_dark_candle t32, t054 t668 NES $65 set on the
 * InitMode5Play frame after a scroll). */
static void record_warp_tile_ow(void)
{
    const u8 lx = (u8)players[0].x;
    u8 coll;
    if (s_scene != SCENE_OW || nes_ram[0x005Au] != 0u ||
        nes_ram[0x0394u] != 0u || ((u8)players[0].y & 0x0Fu) != 0x0Du)
        return;
    if (nes_ram[0x0522u] != 0u) return;   /* teleporting: no EndMoveAndAnimate */
    if ((s_room_id == 0x22u) ? (lx & 0x07u) != 0u : (lx & 0x0Fu) != 0u)
        return;
    coll = nes_ram[0x049Eu];
    nes_ram[0x0065u] = collision_get_collidable_tile_still(0u);
    nes_ram[0x049Eu] = coll;
}

static void link_nes_finish_grid_cell(void)
{
    /* Link_EndMoveAndAnimate truncates any nonzero multiple of 8, not just
     * +-8: the cellar walk starts at $E4 and is cut at $E8 (T-171
     * ls_l1_cellar f106). */
    if (s_link_grid_offset != 0 && ((u8)s_link_grid_offset & 7u) == 0u) {
        s_link_grid_offset = 0;
        /* @TruncGridOffset: a whole tile stepped in mode 5 clears
         * UndergroundExitType (T-171). */
        if (nes_ram[0x0012u] == 0x05u) nes_ram[0x005Au] = 0u;
    }
}

static link_dir_t link_nes_opposite(link_dir_t d)
{
    switch (d) {
    case LINK_DIR_LEFT:  return LINK_DIR_RIGHT;
    case LINK_DIR_RIGHT: return LINK_DIR_LEFT;
    case LINK_DIR_UP:    return LINK_DIR_DOWN;
    case LINK_DIR_DOWN:  return LINK_DIR_UP;
    default:             return LINK_DIR_NONE;
    }
}

/* NES source: Z_05.asm:Link_ModifyDirOnGridLine (grid offset != 0).
 * Same direction: keep going. Opposite: turn now. Perpendicular: keep
 * going if Link_GoStraight or |offset| >= 4 (half a cell); otherwise,
 * if Link is still short of the next grid point in his facing direction,
 * reverse toward the one he left and mirror the offset (-1 -> 7,
 * 3 -> -5). Lockstep newgame f189: NES at X $37 (offset -1, facing
 * left) + Up steps right to $38 then up; Genesis previously walked on
 * to $30. Sign: left/up decrement the offset, right/down increment. */
static void link_nes_modify_dir_on_grid_line(link_dir_t input_dir)
{
    signed char off = s_link_grid_offset;
    unsigned char mag = (unsigned char)(off < 0 ? -off : off);
    unsigned char neg_facing;

    if (input_dir == s_link_dir) { link_nes_init_speed(); return; }   /* T-123 */
    if (input_dir == link_nes_opposite(s_link_dir)) {
        s_link_dir = input_dir;
        return;
    }
    if (s_link_go_straight) { link_nes_init_speed(); return; }       /* T-123 */
    if (mag >= 4u) return;
    neg_facing = (s_link_dir == LINK_DIR_LEFT || s_link_dir == LINK_DIR_UP) ? 1u : 0u;
    if (neg_facing ? (off >= 0) : (off < 0)) return;
    s_link_dir = link_nes_opposite(s_link_dir);
    s_link_grid_offset = (signed char)((off < 0) ? (8 + off) : (-8 + off));
}


/* T-122: NES direction bits (R 1, L 2, D 4, U 8) <-> link_dir_t. */
static unsigned char link_nes_bit_of(link_dir_t d)
{
    switch (d) {
    case LINK_DIR_RIGHT: return 0x01u;
    case LINK_DIR_LEFT:  return 0x02u;
    case LINK_DIR_DOWN:  return 0x04u;
    case LINK_DIR_UP:    return 0x08u;
    default:             return 0u;
    }
}

static link_dir_t link_dir_of_nes_bit(unsigned char b)
{
    if (b & 0x08u) return LINK_DIR_UP;
    if (b & 0x04u) return LINK_DIR_DOWN;
    if (b & 0x02u) return LINK_DIR_LEFT;
    if (b & 0x01u) return LINK_DIR_RIGHT;
    return LINK_DIR_NONE;
}

/* Z_01.asm GetOppositeDir's single direction: the lowest set bit wins
 * (right, left, down, up). */
static link_dir_t link_dir_of_lowest_bit(unsigned char b)
{
    if (b & 0x01u) return LINK_DIR_RIGHT;
    if (b & 0x02u) return LINK_DIR_LEFT;
    if (b & 0x04u) return LINK_DIR_DOWN;
    if (b & 0x08u) return LINK_DIR_UP;
    return LINK_DIR_NONE;
}

/* T-056: NES code outside UpdatePlayer moved or turned Link in the NES
 * cells (UpdateDock): take position, grid offset and facing from them. */
void roomrom_main_link_sync_from_nes(void)
{
    const link_dir_t d = link_dir_of_lowest_bit(nes_ram[0x0098u]);
    players[0].x = (short)nes_ram[0x0070u];
    players[0].y = (short)nes_ram[0x0084u];
    s_link_grid_offset = (signed char)nes_ram[0x0394u];
    if (d != LINK_DIR_NONE) {
        s_link_dir = d;
        players[0].face = (d == LINK_DIR_LEFT)  ? LINK_FACE_LEFT :
                          (d == LINK_DIR_RIGHT) ? LINK_FACE_RIGHT :
                          (d == LINK_DIR_UP)    ? LINK_FACE_UP : LINK_FACE_DOWN;
    }
}

/* T-056: Link_EndMoveAndAnimate called by an object (UpdateDock's
 * Link_EndMoveAndAnimate_Bank4) while UpdatePlayer returned (Link halted):
 * ladder setup and CheckWarps in mode 5, AnimateLinkBase, and Link drawn
 * at his new position after the objects (draw_link_pending). */
void roomrom_main_link_end_move_from_object(void)
{
    roomrom_main_link_sync_from_nes();
    if (nes_ram[0x0522u] != 0u) return;
    if (nes_ram[0x0012u] == 0x05u) {
        link_ladder_end_move();
        record_warp_tile_ow();   /* @CheckWarps keeps ObjCollidedTile */
    }
    roomrom_combat_animate_link_base();
    s_link_frame = (u8)((nes_ram[0x03E4u] & 1u) ^
        ((players[0].face == LINK_FACE_LEFT ||
          players[0].face == LINK_FACE_RIGHT) ? 1u : 0u));
    s_link_draw_pending = 1u;
}

static unsigned char link_walkable_at(short x, short y, link_dir_t dir);

/* NES Z_05.asm Link_ModifyDirAtGridPoint (grid offset 0). Input bits are
 * scanned right, left, down, up; the last input and last walkable input
 * win. One input: face it. No walkable input: only ObjInputDir changes
 * (Link keeps his facing, *moving gets the input). One walkable: take it
 * (Link_GoStraight). Two walkable: the UW doorway-centre cases keep ObjDir;
 * otherwise turn to the perpendicular axis once and remember it in
 * Link_GoStraightWhenDiagInput ($56), keeping ObjDir after that. */
static void link_nes_modify_dir_at_grid_point(unsigned char in, link_dir_t *moving)
{
    static const unsigned char k_rev_dirs[4] = { 0x08u, 0x04u, 0x02u, 0x01u };
    static const unsigned char k_axis_masks[4] = { 0x0Cu, 0x0Cu, 0x03u, 0x03u };
    unsigned char count = 0u, walk = 0u, last_in = 0u, last_walk = 0u;
    unsigned char objdir = link_nes_bit_of(s_link_dir);
    unsigned char a, x = 0u, y;
    s_link_go_straight = 0u;
    for (y = 4u; y-- > 0u;) {
        unsigned char b = (unsigned char)(in & k_rev_dirs[y]);
        if (!b) continue;
        last_in = b;
        ++count;
        /* NES GetCollidingTileMoving for Link: keeps ObjCollidedTile
         * ($49E) for InitLinkSpeed (T-123). */
        nes_ram[NES_OBJ_DIR] = b;
        (void)collision_get_colliding_tile_moving(0u);
        if (link_walkable_at(players[0].x, players[0].y, link_dir_of_nes_bit(b))) {
            last_walk = b;
            ++walk;
        }
    }
    if (count == 0u) { *moving = LINK_DIR_NONE; return; }
    if (count == 1u) {
        a = last_in;
    } else if (walk == 0u) {
        /* SetLinkInputDir only. */
        nes_ram[0x03F8u] = last_in;
        *moving = link_dir_of_nes_bit(last_in);
        return;
    } else {
        s_link_go_straight = 1u;
        a = last_walk;
        if (walk >= 2u) {
            unsigned char lx = (unsigned char)players[0].x, ly = (unsigned char)players[0].y;
            if (nes_ram[0x0010u] != 0u && (lx == 0x20u || lx == 0xD0u) &&
                ly == 0x85u && (objdir & 0x04u)) {
                a = objdir;                             /* @SetLinkDirToObjDir */
            } else {
                unsigned char perpendicular = 1u;
                a = objdir;
                x = nes_ram[0x0056u];
                if (x != 0u) {
                    if (nes_ram[0x0010u] == 0u || lx != 0x78u || ly != 0x5Du ||
                        (a & 0x03u) == 0u)
                        perpendicular = 0u;
                }
                if (perpendicular) {
                    unsigned char ridx = (objdir & 0x08u) ? 0u : (objdir & 0x04u) ? 1u :
                                         (objdir & 0x02u) ? 2u : 3u;
                    x = (unsigned char)(x + 1u);
                    a = (unsigned char)(in ^ (in & k_axis_masks[ridx]));
                }
            }
        }
    }
    /* @SetLinkDirAndSpeed / SetObjDirAndInputDir. */
    nes_ram[0x0056u] = x;
    nes_ram[0x03F8u] = a;
    s_link_dir = link_dir_of_nes_bit(a);
    *moving = s_link_dir;
    link_nes_init_speed();                       /* T-123 */
}

/* MoveObject for Link without the mode-5 grid truncation
 * (UpdateMode4and6EnterLeave walks to ObjGridOffset 0 / +-8). */
static void link_nes_move_object_raw(link_dir_t dir)
{
    unsigned char q;
    for (q = 0u; q < 4u; q++) {
        switch (dir) {
            case LINK_DIR_RIGHT:
                if (link_nes_add_qspeed()) players[0].x++;
                break;
            case LINK_DIR_DOWN:
                if (link_nes_add_qspeed()) players[0].y++;
                break;
            case LINK_DIR_LEFT:
                if (link_nes_sub_qspeed()) players[0].x--;
                break;
            case LINK_DIR_UP:
                if (link_nes_sub_qspeed()) players[0].y--;
                break;
            default:
                return;
        }
    }
}

static void link_nes_move_object(link_dir_t dir)
{
    link_nes_move_object_raw(dir);
    link_nes_finish_grid_cell();
}

static void init_video(void)
{
    VDP_setScreenWidth256();
    /* PR-2c: use a 64x64 plane without SGDK's default 64x64 VRAM table
     * layout. Let SGDK refresh its internal 64x64 stride cache, then
     * immediately override the table addresses before anything renders.
     * BG_A and BG_B intentionally share $C000; BG_B mirrors BG_A during
     * scrolls instead of leaking a different room through color 0.
     * T-168: setupVram FALSE. TRUE also re-laid SGDK's tables and reloaded
     * its default font (unused in gameplay), unpacking it into a heap
     * buffer that ran into the NES RAM mirror at $FF8000 (see
     * heap_wall_nes_mirror, game_main.c). The stride cache is set
     * either way; the addresses are set just below. */
    VDP_setPlaneSize(64, 64, FALSE);
    VDP_setBGAAddress(0xC000u);
    VDP_setBGBAddress(0xC000u);
    VDP_setWindowAddress(0xE000u);
    VDP_setHScrollTableAddress(0xF000u);
    VDP_setSpriteListAddress(0xF400u);
    render_mode_set_v64();
    /* V2.4j (2026-05-26): NES Z1 gameplay HUD at TOP. V2.4g misread NES
     * layout (NES inventory subscreen has bottom-strip, but gameplay HUD
     * is top). Revert to top. Subscreen-context bottom HUD is separate
     * per-pause-state toggle (deferred). */
    VDP_setWindowOnTop(ROOMROM_HUD_ROWS);
    /* Independent H/V scroll per plane; both planes receive the same values. */
    VDP_setScrollingMode(HSCROLL_PLANE, VSCROLL_PLANE);
    VDP_setHorizontalScroll(BG_A, 0);
    VDP_setVerticalScroll(BG_A, 0);
    VDP_setHorizontalScroll(BG_B, 0);
    VDP_setVerticalScroll(BG_B, 0);
}

/* NES source: Z_05.asm:InitMode_EnterRoom; Z_01.asm:TryTakeRoomItem.
 * Drained C: enemy_loop.c:enemy_loop_room_init (native room entry).
 * Coverage: PARTIAL (manifest-backed room properties/items).
 * Stance: EXTEND. Refresh on both direct load and scrolling entry. */
static void refresh_room_metadata(u8 room_id)
{
    /* NES InitMode_EnterRoom / CreateRoomObjects replaces room-item
     * ownership at entry. The native slot19 draw cache is separate from
     * the legacy fixed sprite below; clear both before the new scene's
     * first sweep (T-210: collected Triforce survived on the overworld). */
    enemy_render_weapon_reset(0x13u);
    if (s_scene == SCENE_UW) {
        u8 lvl = roomrom_uw_room_render_get_level();
        u8 q = roomrom_uw_room_render_get_quest();
        s_cur_room_is_dark   = uw_dark_is_dark_room(room_id);   /* IsDarkRoom */
        s_cur_room_is_cellar = roomrom_uw_room_is_cellar(lvl, q, room_id);
        s_cur_room_has_item  = roomrom_uw_item_for_room(
                                   lvl, q, room_id, &s_cur_room_item_meta);
        /* The room item is drawn each frame by the NES item writers
         * (play_update_objects, T-130); the old fixed sprite stays hidden. */
        roomrom_sprites_clear_room_item();
    } else {
        s_cur_room_is_dark = 0u;
        s_cur_room_is_cellar = 0u;
        s_cur_room_has_item = 0u;
        roomrom_sprites_clear_room_item();
    }
}

/* Cave exit: the status bar stayed up through the cave (NES keeps it);
 * load_room leaves it as it is. */
static u8 s_warp_keep_hud;

static void load_room(u8 room_id)
{
    transfer_buf_attr_shadow_reset();   /* T-097: attribute writes end */
    cave_fade_forget_arch();   /* T-011: the plane is redrawn below */
    /* Object bounds ($346-$34A): NES InitMode_EnterRoom installs them,
     * which every scene load reaches through enemy_loop_room_init /
     * _reenter (at once, or at mode 4 after a deferred load). T-171:
     * installing them here put UW bounds in RAM through a level entry's
     * whole load and curtain (NES capture: OW bounds until mode 4). */
    /* PR-2c: BG_A/B share one table, so these clears are intentionally
     * idempotent when issued through either plane handle. */
    /* T-172: a UW room with a blob layout overwrites every cell of this
     * rectangle below (render_room_into_slot); the clear is for the rest. */
    if (s_scene != SCENE_UW || !roomrom_uw_room_render_has_layout(room_id))
        clear_tile_rect_on_plane(0u, 0u, ROOMROM_ROOM_FIRST_ROW,
                                 ROOMROM_ROOM_COLS, ROOMROM_ROOM_ROWS);
    clear_room_scroll_gutters_on_plane(0u);
    s_doorway_dir = UW_WALK_DOOR_NONE;
    s_active_slot_x = 0u;
    s_active_plane = 0u;
    /* Canonical steady-state room placement. Vertical transitions may stage
     * at other row bases, then re-anchor the committed room here. */
    s_active_row_base = 0u;
    s_active_scroll_x = 0;
    s_active_scroll_y = 0;
    /* Reset both planes' scroll registers. Subsequent renders go to BG_A
     * via the default target_plane=0 setter. */
    set_plane_scroll(0u, 0, 0);
    set_plane_scroll(1u, 0, 0);
    set_room_render_target_plane(s_active_plane);
    /* HUD is on Window; BG_A only carries staged room playfields. */
    if (s_scene == SCENE_UW) {
        roomrom_uw_room_render_load_palette(room_id);
        roomrom_hud_draw(roomrom_uw_room_render_get_map(), room_id, 1u);
    } else {
        roomrom_ow_room_render_load_palette(room_id);
        if (!s_warp_keep_hud)
            roomrom_hud_draw(roomrom_ow_room_render_get_map(), room_id, 0u);
        /* Task 5.4: bracket the OW slot paint so the raw-tile cache
         * captures every column and ends marked stable. The warp
         * coordinator's rule-5 entrance-tile check gates on this. */
        roomrom_ow_room_render_begin_full_fill();
    }
    render_room_into_slot(room_id, s_active_slot_x, s_active_row_base);
    if (s_scene == SCENE_OW) {
        roomrom_ow_room_render_mark_stable();
        /* T0.1: publish raw-tile cache into NES PlayAreaTiles so the
         * collision drain (collision_get_collidable_tile_still) sees
         * real tile IDs. Unblocks HandleWarpOW cave/stairs entry. */
        roomrom_ow_room_render_publish_play_area_tiles();
    }
    clear_hud_underlay_for_row_base(s_active_row_base);
    anchor_active_slot();
    roomrom_sprites_load_palette();   /* PAL1 - reload after BG palette write */
    /* Phase 2.6.5: reset toggle table on room load (empty at Phase 2). */
    roomrom_palette_tick_init((const unsigned char *)0);
    refresh_room_metadata(room_id);
    /* Ph5.3: init door state after room render (needs filled plane + attr cache). */
    if (s_scene == SCENE_UW) {
        u8 lvl = roomrom_uw_room_render_get_level();
        u8 q   = roomrom_uw_room_render_get_quest();
        uw_door_state_room_init(lvl, q, room_id);
        roomrom_pushblock_room_load(lvl, q, room_id);
        /* T-111: a dark room shows the last fade-to-black palette row
         * (cycle $43) until a candle brightens it; CandleState resets on
         * every room entry (InitMode4_GoToSub0). */
        nes_ram[0x051Fu] = 0u;
        /* T-171: InitMode3 (level entry, continue, StartRoomId reload)
         * transfers the LevelInfo palettes (Sub2, selector $18) and nothing
         * darkens them: the NES shows a dark start room lit (t171_boss_l9
         * room $42, NES rows 2-3 $00 $10 $30 at t316). Darkening is the
         * scroll fade's (InitMode7 Sub5/6). */
        if (s_cur_room_is_dark && nes_ram[0x0012u] != 0x03u)
            uw_dark_apply_cycle_row(0x43u);
    }
    /* NES Z_01.asm:3967 UsedCandle clears on room transition â€” blue candle
     * regains its 1-shot per new room. Red candle ignores the flag. */
    roomrom_candle_fire_room_reset();
}

/* Phase 8: request the per-level boss CHR bank when a boss object actually
 * spawned into the room. NES Z1 loads the boss pattern block (z_03.asm:91)
 * on boss-room entry; boss + enemy share the SCENE_OBJ VRAM slot (boss
 * rooms hold no regular enemies), so gating on a spawned boss ObjType is
 * safe and needs no hardcoded boss-room table. Boss ObjTypes per the Z_07
 * InitObject jump table: Dodongo/Gohma $31-$34, Digdogger/Lamnola/
 * Manhandla/Aquamentus/Ganon $38-$3E, Moldorm/Gleeok/GleeokHead/Patra
 * $41-$48 (excludes $35 RupeeStash / $36 Grumble / $37 Zelda). */
static void request_boss_chr_if_boss_room(void)
{
    unsigned char s;
    unsigned char lv;
    if (s_scene != SCENE_UW) return;
    lv = roomrom_uw_room_render_get_level();
    if (lv == 0u || lv > 9u) lv = 1u;
    for (s = 1u; s <= 11u; ++s) {
        unsigned char t = nes_ram[0x034Fu + s];
        /* Lanmolas ($3A/$3B) draw from the level enemy bank ($9E+, NES
         * OAM in Q1/Q2 L9 rooms): the boss bank shares that VRAM slot on
         * the Genesis and replaced their segments with boss art. */
        if ((t >= 0x31u && t <= 0x34u) || t == 0x37u ||
            (t >= 0x38u && t <= 0x3Eu && t != 0x3Au && t != 0x3Bu) ||
            (t >= 0x41u && t <= 0x48u)) {
            level_chr_boss_request(
                (roomrom_scene_id_t)(ROOMROM_SCENE_UW_L1 + (lv - 1u)));
            return;
        }
    }
    /* Normal doorway exits stay in the same level. Restore the enemy
     * bank after a boss room; its old READY flag no longer means resident. */
    level_chr_swap_request(
        (roomrom_scene_id_t)(ROOMROM_SCENE_UW_L1 + (lv - 1u)));
}

/* Phase 1: pick the live-NES item-atlas variant for the current scene+map.
 * 0 = orig (vanilla Z1), 1 = redux. Read by roomrom_sprites_set_redux +
 * roomrom_combat_set_redux at boot and after every C-button toggle. */
static unsigned char current_redux_flag(void)
{
    if (s_scene == SCENE_UW)
        return (unsigned char)(roomrom_uw_room_render_get_map() != 0u);
    return (unsigned char)(roomrom_ow_room_render_get_map() != 0u);
}

/* Task 5.4: closed scene-switch reset contract. Called by the warp
 * coordinator's LOAD step through roomrom_main_apply_warp_outcome().
 * Adding a field requires a spec amendment. Fields preserved across the
 * switch (s_link_keys, s_b_item, s_frame_counter, s_joy_prev) are NOT
 * reset here; players[0].face is overwritten by the apply outcome, not
 * reset. */
/* Forward decl: upload_scene_chr() is defined below the apply-outcome
 * function for historical layout reasons. */
static void upload_scene_chr(void);
static unsigned char link_walkable_at(short x, short y, link_dir_t dir);

static void roomrom_state_reset_for_scene_switch(void)
{
    s_doorway_dir = UW_WALK_DOOR_NONE;
    s_link_grid_offset = 0;
    s_link_pos_frac = 0u;
    s_link_subx = 0u;
    s_link_suby = 0u;
    s_link_dir = LINK_DIR_NONE;

    s_scroll_state = SCROLL_NONE;
    s_scroll_frame = 0u;
    s_scroll_total_frames = SCROLL_TOTAL_FRAMES_SMOOTH;
    s_active_slot_x = 0u;
    s_active_plane = 0u;
    s_active_row_base = 0u;
    s_transition_target = 0u;
    s_transition_row_base = 0u;
    s_active_scroll_x = 0;
    s_active_scroll_y = 0;
    s_scroll_start_x = 0;
    s_scroll_start_y = 0;
    s_scroll_target_x = 0;
    s_scroll_target_y = 0;
    s_scroll_cur_x_8_8 = 0;
    s_scroll_cur_y_8_8 = 0;
    s_scroll_step_x_8_8 = 0;
    s_scroll_step_y_8_8 = 0;
    s_scroll_start_link_x = 0;
    s_scroll_start_link_y = 0;
    s_transition_link_x = 0;
    s_transition_link_y = 0;

    s_link_anim_tick = 0u;
    s_link_frame = 0u;

    /* Combat: full re-init clears sword cooldown / projectile state.
     * Scene bias re-applied in roomrom_main_apply_warp_outcome step 9. */
    roomrom_combat_init();
}

/* Task 5.4: atomic warp outcome applier. Coordinator's LOAD step calls
 * this once. Order matches the spec's Handoff section. */
/* T-171: set by the level loads that end in the mode 4 walk-in (stairs
 * entry, InitMode3 Sub8). NES runs InitMode_EnterRoom (objects, room
 * history) in mode 4 only; the walk-in calls enemy_loop_room_reenter. */
static u8 s_warp_defer_enter_room;
/* Cave exit: the OW tile sets never left VRAM (a cave draws with the OW
 * BG, HUD and item tiles; only the UW, cellar and ending renderers write
 * those ranges), so the load skips their re-upload. */
static u8 s_warp_keep_chr;

/* InitMode5Play -> RunCrossRoomTasksAndBeginUpdateMode_PlayModesNoCellar
 * runs CreateRoomObjects again, now in mode 5 (Z_07.asm:1559): only then
 * does the OW room $5F heart container get its slot $13 position (the
 * mode-4 call deactivates it). T-056 t056_ladder_ow: never drawn. */
static void init_mode5_play_create_room_objects(void)
{
    boss_framework_room_init(s_room_id);
}

void roomrom_main_apply_warp_outcome(const rr_warp_outcome_t *out)
{
    if (out == 0) {
        return;
    }

    /* Step 1. */
    roomrom_state_reset_for_scene_switch();

    /* Steps 2-6: direct state writes. */
    s_scene = (scene_t)out->dest_scene;
    if (s_scene == SCENE_UW) {
        roomrom_uw_room_render_set_level(out->dest_level);
        /* Quest must match level_info_install_uw's normalization below
         * (dest_quest 0 -> first quest 1). The UW room blob + find_blob_entry
         * are indexed under quest 1 (quest 0 table is empty); leaving
         * s_uw_quest=0 made find_blob_entry return -1 for every warped UW
         * room -> fill_one_col_at drew no tiles -> black playfield. */
        roomrom_uw_room_render_set_quest(
            (out->dest_quest == 0u) ? 1u : out->dest_quest);
        /* One quest identity for rendering, room data and return routes. */
        roomrom_main_set_quest(roomrom_uw_room_render_get_quest());
        nes_ram[0x0010u] = out->dest_level;
        /* Plan v5 D4: install LevelBlockAttrs + LevelInfo into NES SRAM
         * BEFORE enemy_loop_room_init reads LBA_C/D + FoeCounts. Without
         * this LBA_C returns 0 â†’ spawn skipped. Mirrors the regular
         * scene-transition load_room path (main.c:1593-1598). */
        level_info_install_uw(out->dest_level,
                              (out->dest_quest == 0u) ? 1u : out->dest_quest);
    } else {
        nes_ram[0x0010u] = 0u;
        level_info_install_ow();
    }
    s_room_id = out->dest_room_id;
    players[0].x = out->dest_link_x;
    players[0].y = out->dest_link_y;
    players[0].face = (link_face_t)out->dest_link_face;

    /* Step 7: CHR upload through the scene-load coordinator.
     * Plan v6-C: dispatch UW to correct scene_id per dest_level so
     * L2..L9 get their own CHR bank (was hardcoded to L1 â†’ garbage
     * tiles for higher levels). Enum 4..12 = L1..L9 contiguous per
     * roomrom_scene_vram_contracts.h. */
    if (!s_warp_keep_chr) upload_scene_chr();
    {
        roomrom_scene_id_t scene_id = ROOMROM_SCENE_OVERWORLD;
        if (s_scene == SCENE_UW) {
            unsigned char lv = out->dest_level;
            if (lv == 0u || lv > 9u) lv = 1u;  /* guard: clamp to L1 */
            scene_id = (roomrom_scene_id_t)(ROOMROM_SCENE_UW_L1 + (lv - 1u));
        }
        if (s_warp_keep_chr) level_chr_swap_request(scene_id);  /* no-op if resident */
        else roomrom_scene_load(scene_id, out->dest_redux_flag);
    }
    s_warp_keep_chr = 0u;
    roomrom_combat_set_redux(out->dest_redux_flag);

    /* Step 8: existing room-load path (palette + plane + door state). */
    load_room(s_room_id);
    s_warp_keep_hud = 0u;

    /* Step 9: combat scene bias single-call (reset already did combat_init). */
    roomrom_combat_set_uw(s_scene == SCENE_UW);

    /* Phase 7 Task 7.2 step 2: clear enemy slots + (Task 7.7) dispatch
     * per-room ObjList init. Stub returns NULL until 7.7 lands the
     * template_id table; force-spawn hook fires from probe Lua. */
    /* T-140: a level exit places Link at his OW step-out spot before the
     * room's objects (InitMode_EnterRoom method 1). */
    if (s_lvl_exiting && s_scene == SCENE_OW && !s_warp_defer_enter_room)
        step_out_start_pos();           /* deferred: at the mode 4 init */
    if (!s_warp_defer_enter_room) {
        enemy_loop_room_init(s_room_id, (unsigned char)s_scene,
                    s_scene == SCENE_UW ? roomrom_uw_room_render_get_level() : 0u,
                    s_scene == SCENE_UW ? roomrom_uw_room_render_get_quest() : 0u);
        request_boss_chr_if_boss_room();
    }
    s_warp_defer_enter_room = 0u;
    /* Cave exit is still Mode $0A here. NES resumes OW music only when
     * StepOutside finishes; audio_dispatch_tick owns that edge. */
    if (s_lvl_phase != LVL_CAVE_EXIT && s_lvl_phase != LVL_MODE3_INIT)
        audio_music_play((s_scene == SCENE_UW) ? 0x40 : 0x01);

    /* Phase C (2026-05-24) â€” UET state per NES dispatch (Z_01.asm:2990,
     * Z_05.asm:6717+7493, Z_07.asm:3200).
     *
     *   dest UW   â†’ UET = 2 (dungeon level marker; NES EndGameMode12)
     *   dest CAVE â†’ UET = 1 (cave/cellar marker; NES InitModeB_EnterCave)
     *   dest OW   â†’ UET = 1 (just exited underground; blocks re-trigger
     *                        on entrance tile until first grid-aligned
     *                        OW step clears it via the warp coordinator
     *                        per Z_07.asm:3200). */
    if (s_scene == SCENE_UW) {
        s_underground_exit_type = 2u;
    } else if (s_scene == SCENE_CAVE) {
        s_underground_exit_type = 1u;
    } else {
        s_underground_exit_type = 1u;
    }
}

/* Task 5.4: read-side accessors for the coordinator. Each is a one-line
 * trampoline so the coordinator never dereferences main.c statics. */
unsigned char roomrom_main_current_redux_flag(void)
{
    return current_redux_flag();
}

unsigned char roomrom_main_current_scene(void)
{
    return (unsigned char)s_scene;
}

/* T-167: the pause menu draws in the plane slot the room does not use
 * (inventory_render.c) and the room stays in the plane, as the NES keeps
 * it in its name table. Closing restores the room's scroll and the CRAM
 * the menu replaced; nothing is redrawn (load_room drew the overworld
 * room over a cave: s_room_id is the OW room there). */
static u16 s_pause_cram[64];
static render_sprite_entry_t s_pause_sat[80];

unsigned char roomrom_main_menu_col_base(void)
{
    /* The half the room is shown from (its H scroll), not s_active_slot_x:
     * the cave fill always uses columns 0-31. */
    const u16 room_col = (u16)((-(s16)s_active_scroll_x) >> 3) & 63u;
    return (unsigned char)((room_col & 32u) ^ 32u);
}

unsigned char roomrom_main_menu_room_row_base(void)
{
    return (unsigned char)(((unsigned short)s_active_scroll_y >> 3) & 63u);
}

void roomrom_main_set_hscroll(short h)
{
    VDP_setHorizontalScroll(BG_A, h);
    VDP_setHorizontalScroll(BG_B, h);
}

static void pause_cram_save(void)
{
    s_b_blank_col = 0xFFu;      /* the menu draws in the other slot */
    /* Menu DMA uses the same native SAT cache. Preserve its gameplay
     * links as well as the picture, then restore them at the handoff. */
    memcpy(s_pause_sat, g_render_sat_cache, sizeof(s_pause_sat));
    render_cram_read(s_pause_cram, 64u);        /* intended colors */
}

static void pause_restore_room(void)
{
    memcpy(g_render_sat_cache, s_pause_sat, sizeof(s_pause_sat));
    VDP_updateSprites(80u, DMA_QUEUE);
    render_cram_subrange_upload(0u, s_pause_cram, 64u);
    render_menu_restore_deferred(s_active_scroll_x, s_active_scroll_y, ROOMROM_HUD_ROWS);
}

/* Plane cell showing NES name-table cell (col, nt_row) of the current
 * room: the Genesis frame is the NES frame without its top 8 lines and the
 * room plane is scrolled (same mapping as curtain_addr and
 * mark_link_behind_bg). T-166: UW person text used plane row nt_row + 7
 * with no scroll and landed rows below / columns left of the NES text. */
/* T-172: the curtain buffer (22 x 32 words) as row staging for queued
 * VBlank DMA while no curtain is shown; it also holds the mode-3 UW
 * precompute, which is dropped. 0 = not available. */
unsigned short *roomrom_main_row_stage(void)
{
    if (s_lvl_phase == LVL_CURTAIN) return (unsigned short *)0;
    roomrom_uw_room_render_prepare_drop();
    return &s_curtain[0][0];
}

void roomrom_main_nt_cell_to_plane(unsigned char col, unsigned char nt_row,
                                   unsigned short *pc, unsigned short *pr)
{
    *pc = (unsigned short)((((unsigned short)col << 3) - s_active_scroll_x) & 511) >> 3;
    *pr = (unsigned short)((((unsigned short)nt_row << 3) - 8 + s_active_scroll_y) & 511) >> 3;
}

unsigned char roomrom_main_current_room_id(void)
{
    return s_room_id;
}

short roomrom_main_current_link_x(void)
{
    return players[0].x;
}

short roomrom_main_current_link_y(void)
{
    return players[0].y;
}

/* NES Zelda rescue repositions Link. Keep the typed owner and NES mirror
 * coherent before any later gameplay consumer reads the object cells. */
void roomrom_main_set_link_story_pose(unsigned char x, unsigned char y,
                                     unsigned char face)
{
    players[0].x = (short)x;
    players[0].y = (short)y;
    players[0].face = (link_face_t)face;
    nes_ram[0x0070u] = x;
    nes_ram[0x0084u] = y;
    nes_ram[0x0098u] = (face == ROOMROM_MAIN_LINK_FACE_LEFT) ? 0x02u :
                       (face == ROOMROM_MAIN_LINK_FACE_RIGHT) ? 0x01u :
                       (face == ROOMROM_MAIN_LINK_FACE_UP) ? 0x08u : 0x04u;
    s_link_grid_offset = 0;
    s_link_dir = LINK_DIR_NONE;
}

signed char roomrom_main_current_link_grid_offset(void)
{
    return s_link_grid_offset;
}

unsigned char roomrom_main_current_link_face(void)
{
    return (unsigned char)players[0].face;
}

unsigned char roomrom_main_underground_exit_type(void)
{
    return s_underground_exit_type;
}

void roomrom_main_set_underground_exit_type(unsigned char uet)
{
    s_underground_exit_type = uet;
}

unsigned char roomrom_main_current_quest(void)
{
    return s_current_quest;
}

void roomrom_main_set_quest(unsigned char quest)
{
    if (quest == 1u || quest == 2u) {
        s_current_quest = quest;
    }
}

/* Task 5.7: input dir + mode accessors for push-block state machine.
 * Forward declared because input_mask_from_buttons() is defined below
 * the warp-coordinator accessor block. */
static unsigned char input_mask_from_buttons(u16 input);

unsigned char roomrom_main_current_input_dir(void)
{
    return input_mask_from_buttons(s_joy_prev);
}

unsigned char roomrom_main_current_mode(void)
{
    return (s_mode == MODE_TELEPORT) ? ROOMROM_MAIN_MODE_TELEPORT
                                      : ROOMROM_MAIN_MODE_WALK;
}

/* Task 5.4: warp-state probes exposed through engine_runtime.h. */
unsigned char roomrom_debug_warp_is_active(void)
{
    return roomrom_world_transition_is_active();
}

unsigned char roomrom_debug_warp_unsupported_count(void)
{
    return roomrom_world_transition_unsupported_selector_count();
}

static unsigned char roomrom_debug_probe_flag(unsigned char flag)
{
    volatile unsigned char *ctrl =
        (volatile unsigned char *)ROOMROM_DEBUG_PROBE_CONTROL_BASE;
    if (ctrl[0] != ROOMROM_DEBUG_PROBE_ARM0 ||
        ctrl[1] != ROOMROM_DEBUG_PROBE_ARM1) {
        return 0u;
    }
    return (ctrl[ROOMROM_DEBUG_PROBE_FLAGS_OFF] & flag) ? 1u : 0u;
}

/* Task 5.4: state mirror for passive Lua probes. Called once per tick;
 * publishes the gate-B field set into a fixed 36-byte RAM block at
 * ROOMROM_DEBUG_STATE_MIRROR_BASE.
 *
 * Perf split (2026-05-09): always-on minimum (12 B) covers FPS / scene /
 * room / link xy / face â€” what every probe needs to lock in. Heavy work
 * (offsets 12..119 + the every-6f persistence/cache blocks) is gated on
 * the shared probe control at $FF73F8..$FF73FA. Default gameplay path
 * = 12 volatile writes; armed probes get the full 120 B + secondary blocks.
 * Restores VBlank budget
 * lost to ~30 getter calls and 108 extra volatile writes per frame. */
void roomrom_debug_publish_state_mirror(void)
{
    volatile unsigned char *p =
        (volatile unsigned char *)ROOMROM_DEBUG_STATE_MIRROR_BASE;
    const rr_warp_save_state_t *save;
    unsigned char uw_level;
    unsigned char uw_quest;

    /* 2026-05-15 perf fix: gate the entire state mirror behind probe
     * arm. PC histogram showed publish_state_mirror at 5.31% of frame
     * samples on default gameplay despite probe being un-armed â€”
     * function call setup + volatile semantics + accessor reads add up.
     * Default play skips entirely. Probes write arm magic before
     * reading the mirror. */
    {
        volatile unsigned char *ctrl =
            (volatile unsigned char *)ROOMROM_DEBUG_PROBE_CONTROL_BASE;
        if (ctrl[0] != ROOMROM_DEBUG_PROBE_ARM0 ||
            ctrl[1] != ROOMROM_DEBUG_PROBE_ARM1) {
            return;
        }
    }

    /* Always-on minimum (when armed). FPS / scene-toggle / link-trace
     * probes only need these 12 bytes; cost is one cache-line worth of
     * volatile writes. */
    p[0]  = 0x57u;                                /* 'W' */
    p[1]  = 0x50u;                                /* 'P' */
    p[2]  = (unsigned char)(s_frame_counter >> 8);
    p[3]  = (unsigned char)(s_frame_counter);
    p[4]  = (unsigned char)s_scene;
    p[5]  = s_room_id;
    p[6]  = (unsigned char)(((unsigned short)players[0].x) >> 8);
    p[7]  = (unsigned char)((unsigned short)players[0].x);
    p[8]  = (unsigned char)(((unsigned short)players[0].y) >> 8);
    p[9]  = (unsigned char)((unsigned short)players[0].y);
    p[10] = (unsigned char)players[0].face;
    p[11] = (unsigned char)s_link_dir;

    if (!roomrom_debug_probe_flag(ROOMROM_DEBUG_PROBE_HEAVY_MIRROR)) {
        return;
    }

    save = roomrom_world_transition_save_state();
    uw_level = (s_scene == SCENE_UW)
        ? roomrom_uw_room_render_get_level() : 0u;
    uw_quest = (s_scene == SCENE_UW)
        ? roomrom_uw_room_render_get_quest() : 0u;

    p[12] = (unsigned char)s_link_grid_offset;
    p[13] = s_doorway_dir;
    p[14] = roomrom_world_transition_is_active();
    p[15] = roomrom_world_transition_unsupported_selector_count();
    p[16] = uw_level;
    p[17] = uw_quest;
    p[18] = roomrom_ow_room_render_is_stable();
    p[19] = s_link_pos_frac;
    p[20] = s_underground_exit_type;
    /* Tile under Link's foot â€” raw NES BG tile id from the OW raw-tile
     * cache. NES GetCollidableTileStill samples at foot center =
     * (ObjX, ObjY + $0B); link_walkable_at uses the same offset. */
    if (s_scene == SCENE_OW && roomrom_ow_room_render_is_stable()) {
        short foot_y = (short)(players[0].y + 0x0B);
        if (foot_y >= ROOMROM_PLAYFIELD_TOP_PX) {
            unsigned char fc = (unsigned char)((players[0].x >> 3) & 0x1Fu);
            unsigned char fr = (unsigned char)(((foot_y - ROOMROM_PLAYFIELD_TOP_PX) >> 3) & 0x1Fu);
            p[21] = roomrom_ow_room_render_raw_tile_at(fc, fr);
        } else {
            p[21] = 0u;
        }
    } else {
        p[21] = 0u;
    }

    p[22] = save->version;
    p[23] = save->source_room_id;
    p[24] = save->source_underground_entrance_tile;
    p[25] = save->source_underground_entrance_tile_raw;
    p[26] = (unsigned char)(((unsigned short)save->source_link_x) >> 8);
    p[27] = (unsigned char)((unsigned short)save->source_link_x);
    p[28] = (unsigned char)(((unsigned short)save->source_link_y) >> 8);
    p[29] = (unsigned char)((unsigned short)save->source_link_y);
    p[30] = save->source_link_face;
    p[31] = save->dest_level;
    p[32] = save->dest_quest;
    p[33] = save->dest_room_id;
    p[34] = save->dest_link_face;
    /* Task 5.4 walkability diagnostic: metatile col/row + walkable
     * lookup for the metatile under Link. OW only; UW writes zeros. */
    if (s_scene == SCENE_OW && players[0].y >= ROOMROM_PLAYFIELD_TOP_PX) {
        unsigned char mc = (unsigned char)((players[0].x >> 4) & 0x0Fu);
        short fy = (short)(players[0].y + 0x0B - ROOMROM_PLAYFIELD_TOP_PX);
        unsigned char mr = (fy < 0) ? 0u :
                           (unsigned char)((fy >> 4) & 0x0Fu);
        if (mr > 10u) mr = 10u;
        p[35] = roomrom_ow_room_render_walkable_at(mc, mr);
        p[37] = mc;
        p[38] = mr;
    } else {
        p[35] = 0u;
        p[37] = 0u;
        p[38] = 0u;
    }
    /* Probe link_walkable_at for the UP direction so the user can see
     * whether collision allows stepping onto a tile to the north
     * (entrance approach is north-facing). Slice-1 only OW path. */
    if (s_scene == SCENE_OW) {
        p[36] = link_walkable_at(players[0].x, players[0].y, LINK_DIR_UP);
    } else {
        p[36] = 0u;
    }
    p[39] = 0u;                                    /* reserved */

    /* Task 5.5 extension: UW door state at offsets 40..71. */
    if (s_scene == SCENE_UW) {
        p[40] = uw_door_state_get_type(DOOR_DIR_E);
        p[41] = uw_door_state_get_type(DOOR_DIR_W);
        p[42] = uw_door_state_get_type(DOOR_DIR_S);
        p[43] = uw_door_state_get_type(DOOR_DIR_N);
        p[44] = uw_door_state_get_opened();
        p[45] = uw_door_state_false_timer();
        p[46] = uw_door_state_has_shutters();
    } else {
        p[40] = 0u; p[41] = 0u; p[42] = 0u; p[43] = 0u;
        p[44] = 0u; p[45] = 0u; p[46] = 0u;
    }
    p[47] = s_uw_shutter_trigger_count;
    p[48] = s_link_keys;
    p[49] = s_last_touch_keys_pre;
    p[50] = s_last_touch_keys_post;
    p[51] = s_last_touch_dir;
    p[52] = s_last_touch_result;
    p[53] = s_last_touch_door_type;
    {
        unsigned char i;
        for (i = 54u; i < 72u; i++) p[i] = 0u;  /* reserved */
    }

    /* Task 5.6 extension: cellar state at offsets 72..79. */
    p[72] = (s_scene == SCENE_UW) ? s_cur_room_is_cellar : 0u;
    p[73] = roomrom_world_transition_cellar_entry_count();
    p[74] = roomrom_world_transition_cellar_exit_count();
    p[75] = 0u;  /* pending exit reflected via room+save state already */
    p[76] = 0u; p[77] = 0u; p[78] = 0u; p[79] = 0u;  /* reserved */

    /* Task 5.7 extension: push-block state at offsets 80..95. */
    p[80] = roomrom_pushblock_state_for_room(s_room_id);
    p[81] = roomrom_pushblock_active_dir();
    p[82] = roomrom_pushblock_active_timer();
    p[83] = roomrom_pushblock_active_offset();
    p[84] = roomrom_pushblock_active_block_col();
    p[85] = roomrom_pushblock_active_block_row();
    p[86] = roomrom_pushblock_complete_count();
    p[87] = roomrom_pushblock_room_all_dead();
    p[88] = (unsigned char)roomrom_pushblock_active_state();
    p[89] = 0u; p[90] = 0u; p[91] = 0u;  /* reserved */
    p[92] = 0u; p[93] = 0u; p[94] = 0u; p[95] = 0u;  /* reserved */

    /* Task 5.8 extension: dark-room state at offsets 96..103. */
    if (s_scene == SCENE_UW) {
        p[96] = roomrom_uw_room_is_dark(uw_level, uw_quest, s_room_id);
        p[97] = roomrom_uw_room_lit(s_room_id);
    } else {
        p[96] = 0u;
        p[97] = 0u;
    }
    p[98] = roomrom_uw_dark_candle_used_count();
    p[99] = 0u; p[100] = 0u; p[101] = 0u; p[102] = 0u; p[103] = 0u;

    /* Task 5.9 extension: inventory + item pickup at offsets 104..119. */
    p[104] = s_link_keys;                                /* duplicate of 48 */
    p[105] = roomrom_uw_item_inv_compass();
    p[106] = roomrom_uw_item_inv_map();
    p[107] = roomrom_uw_item_inv_triforce();
    if (s_scene == SCENE_UW) {
        struct uw_item_room_meta m;
        if (roomrom_uw_item_for_room(uw_level, uw_quest, s_room_id, &m)) {
            p[108] = m.item_id;
        } else {
            p[108] = 0u;
        }
    } else {
        p[108] = 0u;
    }
    p[109] = roomrom_uw_item_taken(s_room_id);
    p[110] = 0u;  /* visited count â€” slice-1 deferral */
    p[111] = roomrom_uw_triforce_pickup_active();
    /* PR-4a CHR-TRANSIENT-SCENE state surface (probe-readable). */
    {
        unsigned short rc = level_chr_swap_request_count();
        unsigned long  bd = level_chr_swap_total_bytes_dma();
        p[112] = (unsigned char)level_chr_swap_state();
        p[113] = (unsigned char)level_chr_swap_active_scene();
        p[114] = (unsigned char)(rc >> 8);
        p[115] = (unsigned char)(rc);
        p[116] = (unsigned char)(bd >> 24);
        p[117] = (unsigned char)(bd >> 16);
        p[118] = (unsigned char)(bd >> 8);
        p[119] = (unsigned char)(bd);
    }

    /* Perf: heavy persistence + cache publishes (~2400 byte volatile
     * writes total) throttled to every 6 frames (10 Hz). Probes still
     * see fresh data within ~100ms; eliminates ~85% of frame budget
     * burned on debug RAM writes. The 120-byte $FF7200 mirror above
     * stays per-frame so the live state surface is immediate. */
    if ((s_frame_counter % 6u) == 0u) {
        roomrom_ow_room_render_publish_cache();    /* 708 B (Task 5.4) */
        roomrom_debug_publish_uw_persist();        /* 256 B (Task 5.5) */
        roomrom_pushblock_publish_persist();       /* 256 B (Task 5.7) */
        roomrom_uw_dark_publish_persist();         /* 256 B (Task 5.8) */
        roomrom_uw_item_publish_persist();         /* 256 B (Task 5.9) */
        roomrom_uw_room_render_publish_walkable(); /* 708 B (Task 5.5) */
    }
}

/* Task 5.5: copy active-level persistence row to probe block. */
void roomrom_debug_publish_uw_persist(void)
{
    unsigned char *dst = (unsigned char *)ROOMROM_DEBUG_UW_PERSIST_BASE;
    uw_door_state_copy_persist_for_active_level(dst,
        (unsigned short)ROOMROM_DEBUG_UW_PERSIST_BYTES);
}

static void upload_scene_chr(void)
{
    if (s_scene == SCENE_UW) {
        roomrom_uw_room_render_upload_chr();
    } else {
        roomrom_ow_room_render_upload_chr();
    }
    roomrom_hud_upload_chr();
}

static unsigned char input_mask_from_buttons(u16 input)
{
    unsigned char mask = 0u;
    if (input & BUTTON_RIGHT) mask |= 0x01u;
    if (input & BUTTON_LEFT)  mask |= 0x02u;
    if (input & BUTTON_DOWN)  mask |= 0x04u;
    if (input & BUTTON_UP)    mask |= 0x08u;
    return mask;
}

static link_dir_t link_face_dir(void)
{
    switch (players[0].face) {
        case LINK_FACE_RIGHT: return LINK_DIR_RIGHT;
        case LINK_FACE_LEFT:  return LINK_DIR_LEFT;
        case LINK_FACE_UP:    return LINK_DIR_UP;
        case LINK_FACE_DOWN:  return LINK_DIR_DOWN;
        default:              return LINK_DIR_DOWN;
    }
}

static link_dir_t doorway_search_dir(link_dir_t dir)
{
    return (dir == LINK_DIR_NONE) ? link_face_dir() : dir;
}

/* Task 5.5: latch wrapper around uw_door_state_touch so the state mirror
 * captures pre/post key counts + result + door type for the diff harness. */
static unsigned char link_door_touch_latched(unsigned char dir,
                                             unsigned char *keys)
{
    unsigned char result;
    s_last_touch_dir       = dir;
    s_last_touch_keys_pre  = *keys;
    s_last_touch_door_type = uw_door_state_get_type(dir);
    result = uw_door_state_touch(dir, keys);
    s_last_touch_keys_post = *keys;
    s_last_touch_result    = result;
    return result;
}

static void uw_doorway_adjust_velocity(s8 *vx, s8 *vy)
{
    unsigned char door_dir;
    link_dir_t dir = LINK_DIR_NONE;

    if (*vx > 0) dir = LINK_DIR_RIGHT;
    else if (*vx < 0) dir = LINK_DIR_LEFT;
    else if (*vy > 0) dir = LINK_DIR_DOWN;
    else if (*vy < 0) dir = LINK_DIR_UP;

    if (s_scene != SCENE_UW ||
        !uw_walk_find_doorway(s_doorway_dir,
                              (unsigned char)doorway_search_dir(dir),
                              players[0].x, players[0].y, &door_dir)) {
        s_doorway_dir = UW_WALK_DOOR_NONE;
        return;
    }

    /* Task 5.5 fix: axis-match guard (see uw_doorway_adjust_nes_dir). */
    if (!uw_walk_door_axis_matches(door_dir, (unsigned char)dir)) {
        return;
    }

    uw_walk_snap_to_doorway_axis(door_dir, &players[0].x, &players[0].y);
    s_doorway_dir = door_dir;
    if (door_dir == UW_WALK_DOOR_E || door_dir == UW_WALK_DOOR_W) {
        *vy = 0;
    } else {
        *vx = 0;
    }
}

/* S5 + S5.5 collision: returns 1 if Link's movement probe at the given
 * pixel position lands on a walkable tile in the current room.
 *
 * UW uses the NES GetCollidingTileMoving/GetCollidableTile sampler against
 * the live 8px PlayAreaTiles-equivalent cache. Doorway-axis bypass still
 * runs first so open NES door transitions keep their later Ph5.3 behavior.
 *
 * OW keeps the existing direction-dependent metatile probe. */
static unsigned char link_walkable_at(short x, short y, link_dir_t dir)
{
    /* T-128: NES Walker_CheckTileCollision for Link: in a doorway the tile
     * test is skipped; otherwise GetCollidingTileMoving (drained: hotspots,
     * the second column for vertical moves, OW WalkableTiles, room $1F)
     * on PlayAreaTiles, walkable below ObjectFirstUnwalkableTile ($34A).
     * Replaces the Genesis walk-cache probe, which let Link step partly onto
     * UW blocks. */
    unsigned char sx = nes_ram[0x0070u], sy = nes_ram[0x0084u];
    unsigned char sdir = nes_ram[NES_OBJ_DIR];
    unsigned char tile;
    nes_ram[0x0070u] = (unsigned char)x;
    nes_ram[0x0084u] = (unsigned char)y;
    nes_ram[NES_OBJ_DIR] = link_nes_bit_of(dir);
    tile = collision_get_colliding_tile_moving(0u);
    nes_ram[0x0070u] = sx;
    nes_ram[0x0084u] = sy;
    nes_ram[NES_OBJ_DIR] = sdir;
    return (tile < nes_ram[0x034Au]) ? 1u : 0u;
}

/* S4: edge-triggered room transition. Both OW and UW use the same 16x8 grid
 * (room_id = (row<<4)|col). When Link's position crosses a playfield edge:
 *  - if the adjacent grid cell exists, load it and snap Link to the opposite
 *    edge (preserving the perpendicular coordinate);
 *  - else, clamp at the edge (no transition).
 * Resets sub-pixel/grid/anim state on transition so movement starts clean
 * in the new room. */
static unsigned char shortcut_cave_exit_if_stair(void)
{
    unsigned char dest;
    unsigned char exit_y;
    if (s_scene != SCENE_CAVE || nes_ram[0x0012u] != 0x0Cu) return 0u;
    /* CheckSubroom runs before MoveObject. Checking after movement loses
     * the grid-zero test and can walk Link a pixel past the stair. */
    dest = cave_shortcut_destination(s_cave_return_room,
                (u8)players[0].x, (u8)players[0].y,
                (u8)s_link_grid_offset);
    if (dest == 0xFFu) return 0u;
    /* GoToModeAFromCellar marks the destination visited before Mode A.
     * InitMode_EnterRoom method 1 then reads its installed OW attrs. */
    exit_y = (u8)(((nes_ram[0x6AFEu + dest] & 7u) << 4) + 0x4Du);
    s_room_id = dest;
    nes_ram[0x00EBu] = dest;
    room_mark_room_visited();
    s_cave_return_room = dest;
    s_cave_return_x = (u8)(nes_ram[0x687Eu + dest] & 0xF0u);
    s_cave_return_y = (u8)(exit_y - 16u);
    begin_cave_exit();
    return 1u;
}

static void edge_load_or_clamp(void)
{
    /* NES CheckCaveEdge -> CheckScreenEdge: walking south to Y=$DD
     * leaves the cave. Caves must not use the overworld room-grid scroll
     * or the dungeon's Y=208 clamp. Test direction to avoid exiting at
     * the initial cave-bottom spawn while Link faces into the room. */
    if (s_scene == SCENE_CAVE) {
        /* NES CheckCaveEdge runs in UpdatePlayer before the move: the
         * exit starts the tick after Link reaches $DD (t011_exit_idle
         * tick 806, was 805). */
        if (s_tick_start_link_y >= 0xDD && players[0].y >= 0xDD &&
            (s_joy_prev & BUTTON_DOWN)) {
            /* NES leaves before moving: take back this tick's step,
             * movement and animation alike (T-171: t134_cave_exit
             * t443 grid/frac/ObjAnimCounter kept the extra step). */
            players[0].y = s_tick_start_link_y;
            nes_ram[0x0084u] = (unsigned char)players[0].y;
            s_link_grid_offset = s_tick_start_grid;
            s_link_pos_frac = s_tick_start_frac;
            nes_ram[0x0394u] = (u8)s_tick_start_grid;
            nes_ram[0x03A8u] = s_tick_start_frac;
            nes_ram[0x03D0u] = s_tick_start_anim;
            nes_ram[0x03E4u] = s_tick_start_frame;
            begin_cave_exit();   /* T-135: NES mode $0A */
        }
        return;
    }
    if (nes_ram[0x0012u] == 9u || nes_ram[0x0012u] == 0xAu) return;
    u8 col = s_room_id & 0x0Fu;
    u8 row = (u8)(s_room_id >> 4);
    scroll_state_t want = SCROLL_NONE;

    /* Whirlwind teleport (Z_01.asm UpdateWhirlwind_Full): the whirlwind
     * reached the right edge carrying Link and GoToNextModeFromPlay set
     * mode 6. InitMode7_Sub0 makes WhirlwindPrevRoomId ($EA) the room
     * scrolled from, so the next room is its right neighbour, the
     * level's entrance room (T-171 t171_flute_whirlwind). */
    if (s_scene == SCENE_OW && ow_nes_scroll_enabled() &&
        s_scroll_state == SCROLL_NONE && nes_ram[0x0012u] == 0x06u &&
        nes_ram[0x0011u] == 0u && nes_ram[0x0522u] != 0u) {
        col = (u8)(nes_ram[0x00EAu] & 0x0Fu);
        row = (u8)(nes_ram[0x00EAu] >> 4);
        s_ow_edge = 1u;
    }

    /* Don't re-trigger while a scroll is already running. */
    if (s_scroll_state != SCROLL_NONE) return;

    /* Caves are single-screen: clamp Link to the cave playfield, NEVER
     * scroll/transition (cave exit is the stairs tile, handled separately).
     * South floor = $D5 (213) â€” the NES InitMode_WalkCave emerge rest Y,
     * byte-verified vs Z1 cave $6A â€” not the dungeon south edge $D0 (208),
     * which was pulling the emerged Link 5 px too high. */
    if (s_scene == SCENE_CAVE) {
        if (players[0].x < UW_WALK_EDGE_WEST_X)  players[0].x = UW_WALK_EDGE_WEST_X;
        if (players[0].x > UW_WALK_EDGE_EAST_X)  players[0].x = UW_WALK_EDGE_EAST_X;
        if (players[0].y < UW_WALK_EDGE_NORTH_Y) players[0].y = UW_WALK_EDGE_NORTH_Y;
        /* Floor clamp $D5 â€” but NOT during the cave_fade emerge: LINK_EMERGE
         * walks players[0].y UP from the $DD spawn to the $D5 floor, and the
         * spawn ($DD=221) is BELOW $D5 (213), so clamping here teleports the
         * sprite straight to the floor on emerge frame 0 (the RAM ObjY still
         * animates via the emerge handler -> Tier-A ObjY passed, but the
         * on-screen Link never walked up). Let cave_fade own Y until it
         * releases; clamp only applies to normal in-cave gameplay. */
        if (!cave_fade_is_active() && players[0].y > 0xD5) players[0].y = 0xD5;
        return;
    }

    /* Capture pre-edge position before clamping/snapping. */
    short pre_x = players[0].x;
    short pre_y = players[0].y;

    if (ow_nes_scroll_enabled()) {
        want = (scroll_state_t)s_ow_edge;
        s_ow_edge = 0u;
        if (want == SCROLL_H_LEFT) { --col; players[0].x = 0xF0; }
        if (want == SCROLL_H_RIGHT) { ++col; players[0].x = 0; }
        if (want == SCROLL_V_UP) { --row; players[0].y = 0xDD; }
        if (want == SCROLL_V_DOWN) { ++row; players[0].y = 0x3D; }
    } else if (s_scene == SCENE_UW && s_move_style == MOVE_STYLE_NES) {
        /* T-131: NES CheckScreenEdge / open false or bombable wall. */
        want = s_uw_edge == 0x01u ? SCROLL_H_RIGHT :
               s_uw_edge == 0x02u ? SCROLL_H_LEFT :
               s_uw_edge == 0x04u ? SCROLL_V_DOWN :
               s_uw_edge == 0x08u ? SCROLL_V_UP : SCROLL_NONE;
        s_uw_edge = 0u;
        /* NES CalculateNextRoomForDoor: NextRoomId >= $80 -> EndGameMode12. */
        if (want != SCROLL_NONE) {
            unsigned char next = (unsigned char)(s_room_id +
                (want == SCROLL_H_RIGHT ? 1 : want == SCROLL_H_LEFT ? -1 :
                 want == SCROLL_V_DOWN ? 16 : -16));
            if ((next & 0x80u) && begin_level_exit()) {
                /* InitMode7_Sub1 on the way out: PrevOpenedDoors,
                 * CurOpenedDoors = entering side, DEC PrevRow,
                 * CalculateNextRoom ($80+ = invalid) -> EndGameMode12:
                 * UndergroundExitType 2 (T-171: t132_uw_exit t911). */
                s_lvl_exit_dir = (u8)(want == SCROLL_H_RIGHT ? 0x01u :
                                      want == SCROLL_H_LEFT  ? 0x02u :
                                      want == SCROLL_V_DOWN  ? 0x04u : 0x08u);
                s_lvl_exit_next = next;              /* applied in mode 7 */
                return;
            }
        }
        if (want == SCROLL_H_LEFT)  { --col; players[0].x = 0xF0; }
        if (want == SCROLL_H_RIGHT) { ++col; players[0].x = 0x00; }
        if (want == SCROLL_V_UP)    { --row; players[0].y = 0xDD; }
        if (want == SCROLL_V_DOWN)  { ++row; players[0].y = 0x3D; }
    } else {
        if (players[0].x < UW_WALK_EDGE_WEST_X) {
            if (col > 0u && (s_scene != SCENE_UW ||
                    link_door_touch_latched(UW_WALK_DOOR_W, &s_link_keys))) {
                col--;
                if (s_scene == SCENE_UW) {
                    uw_walk_arrival_position(UW_WALK_DOOR_W, &players[0].x, &players[0].y);
                    s_doorway_dir = UW_WALK_DOOR_W;
                } else {
                    players[0].x = UW_WALK_EDGE_EAST_X;
                }
                want = SCROLL_H_LEFT;
            } else { players[0].x = UW_WALK_EDGE_WEST_X; }
        } else if (players[0].x > UW_WALK_EDGE_EAST_X) {
            if (col < 15u && (s_scene != SCENE_UW ||
                    link_door_touch_latched(UW_WALK_DOOR_E, &s_link_keys))) {
                col++;
                if (s_scene == SCENE_UW) {
                    uw_walk_arrival_position(UW_WALK_DOOR_E, &players[0].x, &players[0].y);
                    s_doorway_dir = UW_WALK_DOOR_E;
                } else {
                    players[0].x = UW_WALK_EDGE_WEST_X;
                }
                want = SCROLL_H_RIGHT;
            } else { players[0].x = UW_WALK_EDGE_EAST_X; }
        }

        if (players[0].y < UW_WALK_EDGE_NORTH_Y) {
            if (row > 0u && (s_scene != SCENE_UW ||
                    link_door_touch_latched(UW_WALK_DOOR_N, &s_link_keys))) {
                row--;
                if (s_scene == SCENE_UW) {
                    uw_walk_arrival_position(UW_WALK_DOOR_N, &players[0].x, &players[0].y);
                    s_doorway_dir = UW_WALK_DOOR_N;
                } else {
                    players[0].y = UW_WALK_EDGE_SOUTH_Y;
                }
                want = SCROLL_V_UP;
            } else { players[0].y = UW_WALK_EDGE_NORTH_Y; }
        } else if (players[0].y > UW_WALK_EDGE_SOUTH_Y) {
            if (row < 7u && (s_scene != SCENE_UW ||
                    link_door_touch_latched(UW_WALK_DOOR_S, &s_link_keys))) {
                row++;
                if (s_scene == SCENE_UW) {
                    uw_walk_arrival_position(UW_WALK_DOOR_S, &players[0].x, &players[0].y);
                    s_doorway_dir = UW_WALK_DOOR_S;
                } else {
                    players[0].y = UW_WALK_EDGE_NORTH_Y;
                }
                want = SCROLL_V_DOWN;
            } else { players[0].y = UW_WALK_EDGE_SOUTH_Y; }
        }

    }

    if (want != SCROLL_NONE) {
        /* NES source: reference/aldonunez/Z_05.asm:SaveKillCountOW and
         * SaveKillCountUW. Drained C: src/oracle/room/room_runtime.c:
         * roomrt_save_kill_count_ow; native UW counterpart lives in
         * room_dispatch.c. Coverage: PARTIAL (native edge departure).
         * Stance: EXTEND. Save before enemy_loop_room_init clears the
         * source room's $034F kill total during scroll completion. */
        /* T-013: the NES-style scroll saves in InitMode6 (ow_scroll.c),
         * the frame after the edge tick. */
        if (!nes_scroll_enabled()) {
            if (s_scene == SCENE_UW) room_save_kill_count_uw();
            else if (s_scene == SCENE_OW) room_save_kill_count_ow(s_room_id);
        }
        s_transition_target = (u8)((row << 4) | col);
        /* NES CalculateNextRoomForDoor (Z_05.asm:7483): in the OW,
         * CheckMazes may send Link back into the same room (Lost Woods
         * $61, Lost Hills $1B) until the direction sequence is walked. */
        if (s_scene == SCENE_OW) {
            const u8 next_saved = nes_ram[0x00ECu];
            nes_ram[0x00EBu] = s_room_id;
            nes_ram[0x00ECu] = s_transition_target;
            nes_ram[0x0098u] = (u8)(want == SCROLL_H_RIGHT ? 0x01u :
                                    want == SCROLL_H_LEFT  ? 0x02u :
                                    want == SCROLL_V_DOWN  ? 0x04u : 0x08u);
            world_check_mazes();
            s_transition_target = nes_ram[0x00ECu];
            /* NextRoomId is set at InitMode7_Sub1 (ow_scroll.c), as on
             * the NES; the edge only needs the maze result (T-171). */
            nes_ram[0x00ECu] = next_saved;
        }
        s_transition_link_x = players[0].x;
        s_transition_link_y = players[0].y;
        /* Pre-edge screen pos clamped to playfield bounds, used as scroll
         * start. For H_RIGHT pre_x is just past 240 (clamp to 240); for
         * H_LEFT pre_x is just past 0 (clamp to 0). Y is unchanged. */
        if (pre_x < UW_WALK_EDGE_WEST_X)  pre_x = UW_WALK_EDGE_WEST_X;
        if (pre_x > UW_WALK_EDGE_EAST_X)  pre_x = UW_WALK_EDGE_EAST_X;
        if (!nes_scroll_enabled()) {
            if (pre_y < UW_WALK_EDGE_NORTH_Y) pre_y = UW_WALK_EDGE_NORTH_Y;
            if (pre_y > UW_WALK_EDGE_SOUTH_Y) pre_y = UW_WALK_EDGE_SOUTH_Y;
        }
        s_scroll_start_link_x = pre_x;
        s_scroll_start_link_y = pre_y;
        s_scroll_state = want;
        s_scroll_frame = 0u;
        s_scroll_total_frames = transition_scroll_total_frames();
        /* Reset sub-pixel/grid so motion starts clean post-transition. */
        if (!nes_scroll_enabled()) s_link_pos_frac = 0u;
        s_link_subx        = 0u;
        s_link_suby        = 0u;
        /* The UW mode 6 walk-out sets ObjGridOffset itself (T-131). */
        if (!(nes_scroll_enabled() && s_scene == SCENE_UW)) s_link_grid_offset = 0;
        s_link_anim_tick   = 0u;

        s_scroll_start_x = s_active_scroll_x;
        s_scroll_start_y = s_active_scroll_y;
        s_scroll_target_x = s_active_scroll_x;
        s_scroll_target_y = s_active_scroll_y;
        s_transition_row_base = s_active_row_base;
        /* Load new room's palette at scroll start â€” UW palettes vary per
         * room and the BG_A tile attributes baked into the rendered new
         * room reference whatever's in PAL0..PAL2 at render time. Old
         * room briefly shows in new palette during scroll; acceptable
         * trade vs new room wrong throughout. */
        if (s_scene == SCENE_UW) {
            u8 next_dark = uw_dark_is_dark_room(s_transition_target);
            roomrom_uw_room_render_load_palette(s_transition_target);
            /* T-111: the reload must not change the current room's BG
             * rows 2-3: dark unlit -> fade row $43, else the bright row $40.
             * InitMode7_Sub5: going into a dark room from a light or
             * candle-lit room fades to black first (cycle $40). */
            uw_dark_apply_cycle_row((s_cur_room_is_dark && nes_ram[0x051Fu] == 0u)
                                    ? 0x43u : 0x40u);
            nes_ram[0x051Cu] = 0u;
            /* NES mode 7 (ow_scroll.c) fades at InitMode7 Sub5/6. */
            if (!nes_scroll_enabled() &&
                next_dark && (!s_cur_room_is_dark || nes_ram[0x051Fu] != 0u)) {
                nes_ram[0x051Fu] = 0u;
                nes_ram[0x051Cu] = 0x40u;
                s_uw_fade_phase = UW_FADE_BEFORE_SCROLL;
            }
        }
        else if (!ow_nes_scroll_enabled())
            roomrom_ow_room_render_load_palette(s_transition_target);
        /* Original OW palette handoff follows the completed staged fill. */
        roomrom_sprites_load_palette();
        /* Task 5.4: bracket the OW staging-slot paint so the raw-tile
         * cache captures the incoming room. mark_stable runs when the
         * scroll completes (above), not here, so the warp coordinator
         * does not fire mid-scroll while Link is still in the old room. */
        if (s_scene == SCENE_OW) {
            roomrom_ow_room_render_begin_full_fill();
        }
        if (want == SCROLL_H_RIGHT || want == SCROLL_H_LEFT) {
            u8 target_slot_x = (u8)(s_active_slot_x ^ 1u);
            /* NES status-bar black stays fixed through room scrolling.
             * After pause + vertical scroll this offscreen slot can still
             * contain saved room rows behind the transparent HUD (T-210).
             * Clear only its HUD zone before the horizontal camera sees it. */
            clear_hud_underlay_for_slot(s_active_row_base, target_slot_x);
            s_b_blank_col = 0xFFu;       /* the other slot takes the room */
            /* H scroll within the active plane: render incoming into the
             * OTHER slot (cols 0..31 vs 32..63) and slide that plane. */
            set_room_render_target_plane(s_active_plane);
            if (!nes_scroll_enabled())
                render_room_into_slot(s_transition_target, target_slot_x, s_active_row_base);
            s_scroll_target_x = nes_scroll_enabled()
                ? (short)(s_active_scroll_x + (want == SCROLL_H_RIGHT ? -256 : 256))
                : scroll_x_offset_for_slot(target_slot_x);
        } else {
            /* PR-2c V scroll: render incoming into adjacent rows of the
             * same 64-row plane. The room renderers wrap rows at 64, so
             * row bases near the end of the plane do not spill into VDP
             * tables. */
            set_room_render_target_plane(s_active_plane);
            /* T-168: blank view for plane B behind the HUD (see
             * set_bg_scroll_with_sprites): rows 0-27 of the other slot. */
            {
                const u8 base = roomrom_main_menu_col_base();
                u16 r;
                for (r = 0u; r < 28u; ++r) render_plane_fill_row(0u, base, r, 32u, 0u);
                s_b_blank_col = base;
            }
            /* Active plane target: scrolls out. V_DOWN -> active scrolls
             * down (v_scroll +176 = view shifts down = plane content moves
             * up on screen, i.e. active room exits via top). V_UP mirrors. */
            if (want == SCROLL_V_DOWN) {
                s_transition_row_base = row_base_add(s_active_row_base,
                    (short)ROOMROM_VERTICAL_STRIDE_TILES);
                s_scroll_target_y = (short)(s_active_scroll_y +
                    (short)(ROOMROM_VERTICAL_STRIDE_TILES * 8));
            } else { /* SCROLL_V_UP */
                s_transition_row_base = row_base_add(s_active_row_base,
                    (short)-((short)ROOMROM_VERTICAL_STRIDE_TILES));
                s_scroll_target_y = (short)(s_active_scroll_y -
                    (short)(ROOMROM_VERTICAL_STRIDE_TILES * 8));
            }
            if (!nes_scroll_enabled())
                render_room_into_slot(s_transition_target, s_active_slot_x, s_transition_row_base);
            if (s_scene == SCENE_UW) {
                roomrom_uw_room_render_set_live_door_priority(
                    s_active_slot_x, s_active_row_base, 0u);
                /* NES-scroll mode stages the destination rows column by
                 * column in mode 7, after this: they hold nothing to fix
                 * yet, and the uncached read is 704 VRAM reads (T-171:
                 * t131_uw_ndoor room exit lagged 2 frames). */
                if (!nes_scroll_enabled())
                    roomrom_uw_room_render_set_live_door_priority(
                        s_active_slot_x, s_transition_row_base, 0u);
            }
            /* DELIBERATELY DO NOT CALL clear_hud_underlay_for_row_base
             * during v-scroll staging. The transition room render writes
             * to plane rows that include the active row_base's HUD
             * underlay zone (e.g. V_UP target_row_base=42 stages the new
             * room at plane rows 49..63 + 0..6 wrapping). Clearing
             * rows 0..6 here would erase the transition room's top 7
             * tile rows, producing a black bar mid-scroll between
             * source and destination rooms. The scroll-completion
             * underlay clear at line ~1539 handles final-state opacity
             * after view stabilizes. */
        }
        if (nes_scroll_enabled()) {
            players[0].x = pre_x;
            players[0].y = pre_y;
            nes_ram[0x0394u] = (u8)s_link_grid_offset;
            ow_scroll_begin((u8)want, s_transition_target);
        } else scroll_init_fixed_point_steps();
    }
}

/* T-056: UpdateDock's GoToNextModeFromPlay (raft at Y $3D) sets mode 6 in
 * the object phase. Start the OW scroll toward ObjDir in that same tick,
 * as a walked screen edge starts it in UpdatePlayer's tick (CheckScreenEdge
 * -> GoToNextModeFromPlay); the next tick is InitMode6. */
void roomrom_main_ow_scroll_from_object(void)
{
    const u8 d = nes_ram[0x0098u];
    if (s_scene != SCENE_OW || !ow_nes_scroll_enabled() ||
        s_scroll_state != SCROLL_NONE)
        return;
    s_ow_edge = (d & 0x01u) ? 1u : (d & 0x02u) ? 2u :
                (d & 0x04u) ? 3u : (d & 0x08u) ? 4u : 0u;
    edge_load_or_clamp();
}

/* T-171: UpdateWhirlwind_Full's GoToNextModeFromPlay starts the teleport
 * scroll in the same tick, as CheckScreenEdge does for a walk. */
void roomrom_main_begin_whirlwind_scroll(void)
{
    edge_load_or_clamp();
}

/* Make the scroll's target room current: camera anchor, room id, palette,
 * HUD, doors, room metadata and objects (scroll end; the UW NES path runs it
 * at mode 4 InitMode_EnterRoom, T-131). */
static void scroll_finalize_room(void)
{
    u8 was_v_scroll = (u8)(s_scroll_state == SCROLL_V_DOWN ||
                           s_scroll_state == SCROLL_V_UP);
    if (s_scroll_state == SCROLL_H_RIGHT ||
        s_scroll_state == SCROLL_H_LEFT) {
        s_active_slot_x ^= 1u;
        s_active_scroll_x = s_scroll_target_x;
        s_active_scroll_y = s_scroll_target_y;
    } else {
        s_active_row_base = s_transition_row_base;
        s_active_scroll_x = s_scroll_target_x;
        s_active_scroll_y = s_scroll_target_y;
        s_active_plane = 0u;
        set_room_render_target_plane(s_active_plane);
    }
    s_room_id = s_transition_target;
    players[0].x  = s_transition_link_x;
    players[0].y  = s_transition_link_y;
    if (ow_nes_scroll_enabled()) {
        s_link_grid_offset = 0;
        s_link_pos_frac = 0u;
        s_link_subx = s_link_suby = 0u;
        nes_ram[0x70u] = (u8)players[0].x;
        nes_ram[0x84u] = (u8)players[0].y;
    }
    if (s_scene == SCENE_UW) {
        u8 prev_dark = s_cur_room_is_dark;
        roomrom_uw_room_render_load_palette(s_room_id);
        /* T-111: entering a dark room keeps it dark (row $43);
         * a light room entered from an unlit dark room fades to
         * light (InitMode4 Sub2/Sub3, cycle $C0). */
        if (uw_dark_is_dark_room(s_room_id)) {
            uw_dark_apply_cycle_row(0x43u);
        } else if (!nes_scroll_enabled() && prev_dark && nes_ram[0x051Fu] == 0u) {
            uw_dark_apply_cycle_row(0x43u);
            nes_ram[0x051Cu] = 0xC0u;
            s_uw_fade_phase = UW_FADE_AFTER_SCROLL;
        }
        nes_ram[0x051Fu] = 0u;
        roomrom_hud_draw(roomrom_uw_room_render_get_map(), s_room_id, 1u);
        if (was_v_scroll) {
            roomrom_uw_room_render_set_live_door_priority(
                s_active_slot_x, s_active_row_base, 1u);
        }
        /* T-119: scroll entry â€” the doorway Link came through
         * starts opened (SetEnteringDoorwayAsCurOpenedDoors). */
        uw_door_state_set_entering(nes_ram[0x0098u]);
        uw_door_state_room_init(roomrom_uw_room_render_get_level(),
                                roomrom_uw_room_render_get_quest(),
                                s_room_id);
    } else {
        roomrom_ow_room_render_load_palette(s_room_id);
        roomrom_hud_draw(roomrom_ow_room_render_get_map(), s_room_id, 0u);
        /* Task 5.4: scroll-staging populated the raw-tile
         * cache during edge_load_or_clamp. Cache was keyed
         * by src col so it now reflects the new active
         * room. Mark it stable so the warp coordinator's
         * rule-5 check can fire. */
        roomrom_ow_room_render_mark_stable();
        /* T0.1: republish PlayAreaTiles after scroll so the
         * collision drain sees the new active room's tiles. */
        roomrom_ow_room_render_publish_play_area_tiles();
    }
    roomrom_sprites_load_palette();
    refresh_room_metadata(s_room_id);
    clear_hud_underlay_for_row_base(s_active_row_base);
    if (was_v_scroll) {
        set_room_render_target_plane(s_active_plane);
        s_active_scroll_y = scroll_y_for_row_base(s_active_row_base);
        set_bg_scroll(s_active_scroll_x, s_active_scroll_y);
    } else {
        if (nes_scroll_enabled())
            s_active_scroll_x = scroll_x_offset_for_slot(s_active_slot_x);
        anchor_active_slot();
    }
    /* Substrate fix 2026-05-15 â€” spawn fresh enemies on room
     * scroll. NES Z1 fires AssignObjSpawnPositions on every
     * room enter (mode 4); we mirror that here so adjacent
     * OW/UW rooms populate enemy slots when Link scrolls in.
     * Without this, ObjType[1..count] stays zero across
     * room transitions and the world appears empty.
     * T-171: a scroll is always a real InitMode_EnterRoom, also into the
     * same room id (Lost Hills $1B / Lost Woods $61 loop back): NES clears
     * $300-$51F and re-places objects; the same-room guard skipped that
     * (t111_dark_candle t1322: ObjInputDir kept, Link animated). */
    enemy_loop_room_reenter(s_room_id, (unsigned char)s_scene,
    s_scene == SCENE_UW ? roomrom_uw_room_render_get_level() : 0u,
    s_scene == SCENE_UW ? roomrom_uw_room_render_get_quest() : 0u);
    request_boss_chr_if_boss_room();
    /* InitMode_EnterRoom -> UpdatePlayerPositionMarker: the map dot moves
     * to the new room here (t054_uw_block42_nes: NES f994; the Genesis
     * play path moved it 3 frames later). */
    roomrom_hud_refresh_marker(nes_ram[0x00EBu],
                               (unsigned char)(s_scene == SCENE_UW),
                               nes_ram[0x0015u]);
}

/* T-132: OW -> dungeon level entry, NES order.
 * NES source: Z_05.asm InitMode10 / UpdateMode10Stairs_Full (tile $24:
 * stairs sound, Link Y+1 every 4th frame to Y+$10, drawn behind the BG),
 * Z_06/Z_07 mode 2 (load, screen off), InitMode3_Sub8 (Link at X $78,
 * Y LevelInfo_StartY, facing up; curtain columns $10/$11), Z_01.asm
 * UpdateWorldCurtainEffect (two columns every 5 frames from the centre),
 * GoToNextModePlayLevelSong (mode 4 submode 0: InitMode_EnterRoom walk-in).
 * Coverage: FULL for the entry path; Genesis loads during one frame of
 * mode 2 (NES blanks ~28 frames: faster load). */
/* Plane cell of NES play-area tile (col 0..31, row 0..21): see
 * mark_link_behind_bg for the NES pixel -> plane mapping. */
static u16 curtain_addr(u8 col, u8 row, u16 *pc, u16 *pr)
{
    *pc = (u16)((((u16)col << 3) - s_active_scroll_x) & 511) >> 3;
    *pr = (u16)((0x40u + ((u16)row << 3) - 8 + s_active_scroll_y) & 511) >> 3;
    return (u16)(0xC000u + ((*pr * 64u + *pc) << 1));
}

static void curtain_hide(void)
{
    u8 row;
    u16 pc, pr;
    roomrom_uw_room_render_prepare_drop();       /* s_curtain is reused */
    /* T-125: one run per plane row (two when the 32 screen columns wrap
     * the 64-cell plane row); was a VDP read + write address per cell. */
    for (row = 0u; row < 22u; ++row) {
        u16 first;
        (void)curtain_addr(0u, row, &pc, &pr);
        first = (u16)(64u - pc);
        if (first > 32u) first = 32u;
        render_vram_read_run((u16)(0xC000u + ((pr * 64u + pc) << 1)),
                             &s_curtain[row][0], first);
        if (!startup_triangle_preserve_scene() && !circle_transition_waiting())
            render_plane_fill_row(0u, pc, pr, first, 0u);
        if (first < 32u) {
            render_vram_read_run((u16)(0xC000u + ((pr * 64u) << 1)),
                                 &s_curtain[row][first], (u16)(32u - first));
            if (!startup_triangle_preserve_scene() && !circle_transition_waiting())
                render_plane_fill_row(0u, 0u, pr, (u16)(32u - first), 0u);
        }
    }
}

/* T-134: the curtain's black play area without saving what was there. */
static void playfield_blank(void)
{
    u8 row;
    u16 pc, pr;
    for (row = 0u; row < 22u; ++row) {
        u16 first;
        (void)curtain_addr(0u, row, &pc, &pr);
        first = (u16)(64u - pc);
        if (first > 32u) first = 32u;
        render_plane_fill_row(0u, pc, pr, first, 0u);
        if (first < 32u)
            render_plane_fill_row(0u, 0u, pr, (u16)(32u - first), 0u);
    }
}

static void curtain_reveal(u8 col)
{
    u8 row;
    u16 pc, pr;
    /* NES UpdateWorldCurtainEffect queues the column in a transfer record:
     * it appears at the next NMI (render_plane_defer: next VBlank). */
    render_plane_defer(1u);
    for (row = 0u; row < 22u; ++row) {
        (void)curtain_addr(col, row, &pc, &pr);
        render_set_plane_a_word(pc, pr, s_curtain[row][col]);
    }
    render_plane_defer(0u);
}

/* Mode 3 status bar before the curtain: static part only. */
static void level_hud_static_only(void)
{
    s_hud_b_key_valid = 0u;                     /* B/A sprites hidden below */
    roomrom_hud_set_counts_hidden(1u);
    roomrom_sprites_hide_hud_marker(0u);
    roomrom_sprites_hide_hud_marker(1u);
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_HUD_B_ITEM, -32, -32,
        RENDER_SPRITE_SIZE(1, 2), 0u, ROOMROM_SPRITE_SLOT_HUD_B_ITEM_R);
    VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_HUD_B_ITEM_R, -32, -32,
        RENDER_SPRITE_SIZE(1, 2), 0u, ROOMROM_SPRITE_SLOT_HUD_PLAYER);
}

/* NES ReadInputs runs in the NMI every frame, in every mode: ButtonsDown
 * ($FA) / ButtonsPressed ($F8) follow the pad through transitions, and a
 * button held across one is not a new press afterwards. ObjInputDir ($3F8)
 * is only set by mode 5 (T-013: $FA kept the last Up through the level
 * exit, t013_route t8465). */
/* T-013: controller 2 -> ButtonsPressed+1 / ButtonsDown+1 ($F9/$FB), read
 * with pad 1 every tick (NES ReadInputs). No A/B swap option on pad 2. */
static u16 s_joy2_prev = 0u;
static void nes_pad2_read(void)
{
    u16 joy = JOY_readJoypad(JOY_2);
    u16 pressed = joy & ~s_joy2_prev;
    s_joy2_prev = joy;
    nes_ram_sync_input2(joy, pressed);
}

static void nes_pad_read_between_modes(void)
{
    u16 joy = JOY_readJoypad(JOY_1);
    nes_pad2_read();
    u16 pressed = joy & ~s_joy_prev;
    u8 input_dir = nes_ram[0x03F8u];
    s_joy_prev = joy;
    if (options_consumer_get_ab_swap()) {
        u16 ab = (u16)(BUTTON_A | BUTTON_B);
        u16 j = (u16)(joy & ab), p = (u16)(pressed & ab);
        joy = (u16)((joy & ~ab) | ((j & BUTTON_A) ? BUTTON_B : 0u) | ((j & BUTTON_B) ? BUTTON_A : 0u));
        pressed = (u16)((pressed & ~ab) | ((p & BUTTON_A) ? BUTTON_B : 0u) | ((p & BUTTON_B) ? BUTTON_A : 0u));
    }
    nes_ram_sync_input(joy, pressed);
    nes_ram[0x03F8u] = input_dir;
}

/* NES InitMode2 submode 0 (Z_07.asm:1290): ClearRoomHistory and clear
 * LevelKillCounts ($560-$5DF) before the level loads (T-013: Genesis kept
 * the previous level's room history, t013_route t2186/t8465). */
static void init_mode2_sub0(void)
{
    u8 i;
    room_clear_room_history();
    for (i = 0u; i < 0x80u; ++i) nes_ram[0x0560u + i] = 0u;
}

/* T-013: GameMode $12 (src/game/world/mode_endlevel.c) presentation and
 * exit. NES InitMode12 hides the object sprites and draws Link lifting
 * the triforce (SetUpAndDrawLinkLiftingItem: item at ObjX, ObjY - $10);
 * UpdateWorldCurtainEffect copies blank tile-map columns in from both
 * sides; EndGameMode12 starts the mode 2 load back to the overworld. */
static void mode12_draw_lift(void)
{
    u8 saved_cur = nes_ram[0x0340u];
    nes_ram[0x0070u + 19u] = nes_ram[0x0070u];                     /* ObjX+19 */
    nes_ram[0x0084u + 19u] = (u8)(nes_ram[0x0084u] - 0x10u);       /* ObjY+19 */
    nes_ram[0x0340u] = 0x13u;
    enemy_render_weapon_reset(0x13u);
    draw_animate_item_object(nes_ram[0x0505u], 0x13u);
    nes_ram[0x0340u] = saved_cur;
    /* T-219: the item writer fills slot19's native draw cache. Publish
     * it before Link, as normal play does; otherwise the old ground
     * sprite remains in SAT throughout the end-level fanfare. */
    enemy_render_native_sweep();
    roomrom_sprites_set_link_lift(players[0].x, players[0].y,
                                  (unsigned char)(nes_ram[0x0052u] != 0u));
}

/* T-097: InitMode11 HideAllSprites: objects, weapons and the room item go
 * (the status bar items and Link are redrawn by the mode). */
void roomrom_mode11_hide_sprites(void)
{
    u8 s;
    enemy_render_reset_oam();
    enemy_render_native_sweep();
    for (s = ROOMROM_SPRITE_SLOT_SWORD; s <= ROOMROM_SPRITE_SLOT_MAGIC_SHOT; ++s)
        VDP_setSpritePosition(s, -32, -32);
    /* The map's Link dot is an object sprite too (UpdatePlayerPosition
     * Marker is not called again); the status bar shows the hearts the
     * death left (FormatStatusBarText). */
    VDP_setSpritePosition(ROOMROM_SPRITE_SLOT_HUD_PLAYER, -32, -32);
    inventory_sync_from_native();
    roomrom_hud_refresh_dynamic();
    VDP_updateSprites(80u, DMA_QUEUE);
}

/* Mode 11 sprites from the NES cells: Link (ObjDir, ObjAnimFrame, the
 * ObjInvincibilityTimer flash) until the spark replaces his two OAM slots
 * (Sprites+72..79, tiles $62/$64, attrs 1 / $41), then nothing. */
static void roomrom_draw_link_from_nes(void)
{
        const u8 d = nes_ram[0x0098u];
        const link_face_t face = (d & 0x01u) ? LINK_FACE_RIGHT :
                                 (d & 0x02u) ? LINK_FACE_LEFT :
                                 (d & 0x08u) ? LINK_FACE_UP : LINK_FACE_DOWN;
        const u8 frame = (u8)((nes_ram[0x03E4u] & 1u) ^
            ((face == LINK_FACE_LEFT || face == LINK_FACE_RIGHT) ? 1u : 0u));
        players[0].face = face;
        if (nes_ram[0x04F0u] != 0u)
            roomrom_sprites_set_link_hurt_pose((short)nes_ram[0x0070u], (short)nes_ram[0x0084u],
                                               face, frame, nes_ram[0x04F0u]);
        else
            roomrom_sprites_set_link_pose((short)nes_ram[0x0070u], (short)nes_ram[0x0084u],
                                          face, frame);
}

static void roomrom_mode11_draw(void)
{
    const u8 spark = mode11_spark_state();
    if (spark == 0u) {
        roomrom_draw_link_from_nes();
    } else if (spark == 1u) {
        const u8 y = nes_ram[0x0248u], x = nes_ram[0x024Bu];
        VDPSprite *s0 = &vdpSpriteCache[ROOMROM_SPRITE_SLOT_LINK];
        VDPSprite *s1 = &vdpSpriteCache[ROOMROM_SPRITE_SLOT_LINK_R];
        s0->y = s1->y = (s16)(y + ROOMROM_PLAY_SPRITE_DY + 0x80);
        s0->x = (s16)(x + 0x80);
        s1->x = (s16)(nes_ram[0x024Fu] + 0x80);
        s0->size = s1->size = SPRITE_SIZE(1, 2);
        s0->attribut = enemy_render_spark_sat(nes_ram[0x0249u], nes_ram[0x024Au]);
        s1->attribut = enemy_render_spark_sat(nes_ram[0x024Du], nes_ram[0x024Eu]);
    } else {
        VDP_setSpritePosition(ROOMROM_SPRITE_SLOT_LINK, -32, -32);
        VDP_setSpritePosition(ROOMROM_SPRITE_SLOT_LINK_R, -32, -32);
    }
    VDP_updateSprites(80u, DMA_QUEUE);
}

void roomrom_mode12_begin(void)
{
    enemy_render_reset_oam();                  /* HideObjectSprites */
    mode12_draw_lift();
    VDP_updateSprites(80u, DMA_QUEUE);
}

void roomrom_mode12_draw(void)
{
    enemy_render_reset_oam();
    roomrom_hud_refresh_dynamic();             /* hearts fill on screen */
    mode12_draw_lift();
    VDP_updateSprites(80u, DMA_QUEUE);
}

void roomrom_mode12_blank_column(unsigned char col)
{
    u8 row;
    u16 pc, pr;
    for (row = 0u; row < 22u; ++row) {
        (void)curtain_addr(col, row, &pc, &pr);
        render_set_plane_a_word(pc, pr, 0u);
    }
}

void roomrom_mode12_exit(void)
{
    if (!roomrom_world_transition_level_exit(&s_lvl_out)) return;
    s_lvl_exit_fc = nes_ram[0x0015u];
    s_lvl_exit_steps = NES_MODE12_EXIT_FC_STEPS;
    s_lvl_exiting = 1u;
    s_lvl_exit_tl = 0u;
    s_lvl_phase = LVL_EXIT_LOAD;
}

/* T-013 P2.6: GameMode 8 (src/game/world/mode_continue_question.c) and
 * $0D screen. NES TurnOffVideoAndClearArtifacts hides every sprite and
 * fills both name tables with blank tiles, status bar included. Genesis:
 * HUD window off, plane rows 0-31 blank, scroll 0 (so NES name-table row r
 * is plane row r - 1: the Genesis frame is the NES frame without its top 8
 * lines), every SAT entry parked off screen (links kept). */
extern const unsigned short bg_sparse_tile_lut[256][4];

void roomrom_mode8_blank(void)
{
    u16 row;
    u8 i;
    /* TurnOffAllVideo: with the display off the plane fill runs at full
     * VRAM speed (with it on, the fill spilled into the next frame: a lag
     * frame that put the tick's row a tick late, t013_save t133/t254). The
     * display comes back on in the same tick (SGDK's VBlank wait needs
     * it); the screen is blank by then. */
    render_display_enable(0u);
    VDP_setWindowOnTop(0u);
    for (row = 0u; row < 32u; ++row) render_plane_fill_row(0u, 0u, row, 64u, 0u);
    render_display_enable(1u);
    s_scroll_hold = 0u;
    VDP_setHorizontalScrollVSync(BG_A, 0);
    VDP_setVerticalScrollVSync(BG_A, 0);
    VDP_setHorizontalScrollVSync(BG_B, 0);
    VDP_setVerticalScrollVSync(BG_B, 0);
    for (i = 0u; i < 80u; ++i) vdpSpriteCache[i].y = 0;
    VDP_updateSprites(80u, DMA_QUEUE);
    enemy_render_reset_oam();                    /* HideAllSprites: caches too */
    enemy_render_weapon_reset_all();
    /* HideAllSprites proper: NES OAM shadow Y = $F8. The boss rooms that
     * draw from the shadow (enemy_render_needs_oam_sweep) otherwise drew
     * Mode 8's CONTINUE heart ($200: Y $4F, $F3, X $40) after a continue. */
    room_hide_all_sprites();
}

void roomrom_mode8_show(void)
{
    render_display_enable(1u);
}

/* NES name-table text (screen rows) with BG palette row subpal. */
void roomrom_mode8_text(unsigned char nes_row, unsigned char nes_col,
                        const unsigned char *tiles, unsigned char n,
                        unsigned char subpal)
{
    u8 i;
    for (i = 0u; i < n; ++i) {
        u16 s = bg_sparse_tile_lut[tiles[i]][subpal & 3u];
        render_set_plane_a_word((u16)(nes_col + i), (u16)(nes_row - 1u),
                                s == 0xFFFFu ? 0u : (u16)(ROOMROM_BG_TILE_BASE + s));
    }
}

/* Sprites+0..3 ($200-$203) as the Genesis sprite pair in Link's slots
 * (hidden in mode 8). NES 8x16: an odd tile id takes pattern table $1000
 * (the background tiles), top (id & $FE) over bottom (id | 1); the BG tile
 * copy for sprite palette row (attr & 3) under RENDER_PAL1 (NES sprite
 * palettes). NES sprite Y + 1 is the first line; Genesis line = NES - 8. */
void roomrom_mode8_cursor(void)
{
    u8 y = nes_ram[0x0200u], t = nes_ram[0x0201u], a = nes_ram[0x0202u];
    u8 x = nes_ram[0x0203u];
    u8 sp = (u8)(a & 3u);
    u16 top = bg_sparse_tile_lut[t & 0xFEu][sp];
    u16 bot = bg_sparse_tile_lut[t | 1u][sp];
    VDPSprite *s0 = &vdpSpriteCache[ROOMROM_SPRITE_SLOT_LINK];
    VDPSprite *s1 = &vdpSpriteCache[ROOMROM_SPRITE_SLOT_LINK_R];
    s0->y = (s16)(y + 1 - 8 + 0x80);
    s1->y = (s16)(y + 1 - 8 + 8 + 0x80);
    s0->x = s1->x = (s16)(x + 0x80);
    s0->size = s1->size = SPRITE_SIZE(1, 1);
    /* The planes are low priority: a priority-0 sprite shows over them as
     * the NES front sprite does (the mode 8 cursor, attr 2). */
    s0->attribut = RENDER_TILE_ATTR_FULL(RENDER_PAL1, 0,
                                         (a & 0x80u) ? 1 : 0, (a & 0x40u) ? 1 : 0,
                                         top == 0xFFFFu ? 0u : ROOMROM_BG_TILE_BASE + top);
    s1->attribut = RENDER_TILE_ATTR_FULL(RENDER_PAL1, 0,
                                         (a & 0x80u) ? 1 : 0, (a & 0x40u) ? 1 : 0,
                                         bot == 0xFFFFu ? 0u : ROOMROM_BG_TILE_BASE + bot);
    VDP_updateSprites(80u, DMA_QUEUE);
}

/* T-013 P2.6: mode 8 CONTINUE -> GameMode 3 (Z_07.asm InitMode3 Sub0-8):
 * ClearRoomHistory; Sub1 room = the level's StartRoomId in a dungeon, else
 * CaveSourceRoomId when valid, else StartRoomId (room_init_mode3_sub1);
 * Sub2-7 palettes, attributes, status bar, map, "LEVEL-X"; Sub8
 * LayOutRoom, curtain columns, Link at X $78, Y LevelInfo_StartY facing
 * up. Then UpdateMode3Unfurl (the Genesis curtain, LVL_CURTAIN) and, with
 * UndergroundExitType 0 (InitMode8), GoToNextModePlayLevelSong: mode 4
 * walk-in. One submode a tick as on the NES (t013_continue NES t214-t222);
 * the Genesis room load runs at Sub8 (the NES LayOutRoom tick). */
static void begin_mode8_continue(void)
{
    s_lvl_exiting = 0u;
    s_lvl_phase = LVL_MODE3_INIT;
}

/* T-171: GameMode 2 not started by a Genesis load path (a staged level
 * warp: CurLevel set, GameMode 2, IsUpdatingMode 0). NES ticks: InitMode2
 * Sub0 (ClearRoomHistory, LevelKillCounts, level block), Sub1 (level
 * info, IsUpdatingMode 1), UpdateMode2Load (Q2 patches, GoToNextMode);
 * t171_warp_l2 NES FC $5E/$5F/$60, mode 3 Sub0 at $61. The room itself
 * loads at mode 3 Sub8 (mode3_init_tick). */
static void mode2_init_tick(void)
{
    const u8 lvl = nes_ram[0x0010u];
    const u8 quest = roomrom_main_current_quest();
    if (nes_ram[0x0011u] != 0u) {                /* UpdateMode2Load */
        level_info_mode2_step(2u, lvl, quest);
        nes_ram[0x0012u] = 0x03u;
        nes_ram[0x0011u] = 0u;
        nes_ram[0x0013u] = 0u;
        s_lvl_phase = LVL_NONE;                  /* mode 3 hook next tick */
        return;
    }
    if (nes_ram[0x0013u] == 0u) {                /* InitMode2 Sub0 */
        init_mode2_sub0();
        level_info_mode2_step(0u, lvl, quest);
        nes_ram[0x0013u] = 1u;
        return;
    }
    level_info_mode2_step(1u, lvl, quest);       /* Sub1 */
    nes_ram[0x0013u] = 0u;
    nes_ram[0x0011u] = 1u;
}

static void mode3_init_tick(void)
{
    rr_warp_outcome_t out = {0};
    const u8 sub = nes_ram[0x0013u];
    switch (sub) {
    case 0u:                                     /* InitMode3_Sub0 */
        nes_ram[0x0017u] = 1u;
        nes_ram[0x0013u] = 1u;
        roomrom_mode8_blank();                   /* TurnOffVideoAndClearArtifacts */
        transfer_buf_nt_cleared();
        room_clear_room_history();
        return;
    case 1u:                                     /* RoomId, palette cue */
        room_init_mode3_sub1();
        return;
    case 2u: nes_ram[0x0014u] = 0x18u; break;    /* FillPlayAreaAttrs + palettes */
    case 3u: case 4u: break;                     /* play-area attributes */
    case 5u: nes_ram[0x0014u] = 0x0Eu; break;    /* status bar statics */
    case 6u: {                                   /* status bar map (HasMap) */
        const u8 lvl = nes_ram[0x0010u];
        const u8 lz = (u8)(lvl - 1u);
        if (lvl == 0u ||
            (nes_ram[(lz >= 8u) ? 0x066Au : 0x0668u] & (u8)(1u << (lz & 7u))))
            nes_ram[0x0014u] = 0x44u;
        break;
    }
    case 7u:                                     /* "LEVEL-X" */
        if (nes_ram[0x6BB1u] != 0u) nes_ram[0x0014u] = 0x0Cu;   /* LevelNumber */
        break;
    default: break;
    }
    if (sub < 8u) {
        /* T-172: precompute the OW room's columns (Genesis buffers only)
         * on the Sub2-7 ticks the NES spends on attributes / status bar,
         * so Sub8's LayOutRoom fits NES time (t013_continue t223: 8
         * frames vs NES 3). */
        /* T-172: the layout summary and one column on Sub2 (it and three
         * columns overran Sub2, t171_flute_pond t30), then three a tick
         * (four overran Sub7 into the VBlank, t013_continue). */
        if (sub >= 2u && nes_ram[0x0010u] == 0u)
            roomrom_ow_room_render_prepare(nes_ram[0x00EBu], sub == 2u ? 1u : 3u);
        /* UW: the room's tile words into the idle curtain buffer
         * (t171_patra_sword t300: Sub8 6 frames vs NES 4). */
        else if (sub >= 2u && nes_ram[0x0010u] != 0u)
            roomrom_uw_room_render_prepare(nes_ram[0x00EBu], &s_curtain[0][0], 3u);
        nes_ram[0x0013u] = (u8)(sub + 1u);
        return;
    }
    /* InitMode3_Sub8: LayOutRoom, curtain columns, Link, BeginUpdateMode. */
    out.dest_scene = (nes_ram[0x0010u] != 0u) ? SCENE_UW : SCENE_OW;
    out.dest_level = nes_ram[0x0010u];
    out.dest_quest = roomrom_main_current_quest();
    out.dest_room_id = nes_ram[0x00EBu];
    out.dest_link_x = 0x78;
    out.dest_link_y = (short)nes_ram[0x6BA6u];   /* LevelInfo_StartY */
    out.dest_link_face = LINK_FACE_UP;
    out.dest_redux_flag = current_redux_flag();
    render_display_enable(0u);
    VDP_setWindowOnTop(ROOMROM_HUD_ROWS);
    roomrom_hud_preset_counts_hidden();          /* level_hud_static_only below */
    s_warp_defer_enter_room = 1u;               /* objects at mode 4 */
    roomrom_main_apply_warp_outcome(&out);
    s_underground_exit_type = 0u;
    players[0].x = 0x78;
    players[0].y = (short)nes_ram[0x6BA6u];
    players[0].face = LINK_FACE_UP;
    s_link_dir = LINK_DIR_UP;
    s_link_grid_offset = 0;
    nes_ram[0x0070u] = 0x78u;
    nes_ram[0x0084u] = nes_ram[0x6BA6u];
    nes_ram[0x0098u] = 0x08u;
    nes_ram[0x0394u] = 0u;
    nes_ram[0x007Cu] = 0x10u;
    nes_ram[0x007Du] = 0x11u;
    nes_ram[0x0017u] = 0u;
    curtain_hide();
    level_hud_static_only();
    roomrom_sprites_set_link_pose((short)-32, (short)-32, players[0].face, 0u);
    enemy_render_reset_oam();
    enemy_render_native_sweep();
    VDP_updateSprites(80u, DMA_QUEUE);
    startup_triangle_prepare(s_active_scroll_x, s_active_scroll_y);
    render_display_enable(1u);
    nes_ram[0x0013u] = 0u;                       /* BeginUpdateMode */
    nes_ram[0x0011u] = (u8)(nes_ram[0x0011u] + 1u);
    s_lvl_step = 0u;
    s_lvl_timer = 0u;
    nes_ram[0x007Cu] = 0x10u;   /* InitMode3_Sub8: curtain columns (T-171) */
    nes_ram[0x007Du] = 0x11u;
    s_lvl_phase = LVL_CURTAIN;
}

/* T-132 exit: NES EndGameMode12 when NextRoomId >= $80 (start room S
 * edge). Returns 1 when the level exit started. */
static unsigned char begin_level_exit(void)
{
    if (!roomrom_world_transition_level_exit(&s_lvl_out)) return 0u;
    circle_transition_close((short)(players[0].x + 8),(short)(players[0].y + 1),
                             s_active_scroll_x,s_active_scroll_y);
    room_save_kill_count_uw();                  /* InitMode6 SaveKillCount */
    nes_ram[0x0604u] = 0x80u;                   /* Tune0Request: silence */
    /* T-140: the edge tick ends in mode 6 (CheckScreenEdge ->
     * GoToNextModeFromPlay), as on the NES; modes 6/7/2 run next. */
    s_lvl_exit_fc = nes_ram[0x0015u];
    s_lvl_exit_steps = NES_LEVEL_EXIT_FC_STEPS;
    s_lvl_exiting = 1u;
    s_lvl_phase = LVL_EXIT_LOAD;
    /* T-171: the NES exit's frames (k_level_exit_tl from this edge frame). */
    s_lvl_enter_room = nes_ram[0x00EBu];
    s_lvl_enter_x = nes_ram[0x0070u];
    s_lvl_enter_y = nes_ram[0x0084u];
    s_lvl_dest_room = s_lvl_out.dest_room_id;
    s_lvl_exit_objdir = nes_ram[0x0098u];
    s_lvl_exit_src526 = nes_ram[0x0526u];
    s_lvl_exit_vt0 = vtimer;
    s_lvl_exit_tl = 1u;
    s_lvl_exit_mode2 = 0u;
    nes_load_clock_start(&k_level_exit_tl);
    return 1u;
}

/* T-135: NES cave exit, mode $0A (t134_cave_exit, trigger f540 = T):
 * T+1 Link hidden (DrawSpritesBetweenRooms), T+2 playfield black, T+6/T+7
 * whole screen off (LayoutRoom), the OW room shown at once, then mode 4
 * InitMode_EnterRoom method 1 (StepOutside) with Link a frame later. */
/* Frames since the trigger (mode $0A submode 0 frame), the video-frame
 * clock of k_cave_leave_tl, so the 3-frame Genesis load overrun does not
 * move what follows. Plane writes show on the tick's frame, sprite writes
 * one frame later. */
#define CAVE_EXIT_HIDE_FRAME    2u   /* Link gone with the blank (f542) */
#define CAVE_EXIT_BLANK_FRAME   3u   /* playfield black on NES submode 2 */
#define CAVE_EXIT_DARK_FRAME    6u   /* whole screen off: frames 6-7 lose */
#define CAVE_EXIT_LOAD_FRAME    6u   /* their NMI (LayoutRoom overruns), so */
#define CAVE_EXIT_LIGHT_FRAME   8u   /* no status bar either; HUD back on 8.
                                      * The load fills both dark frames. */
#define CAVE_EXIT_REVEAL_FRAME 31u   /* submode 7 ends the sprite-0 blank:
                                      * the room shows from frame 32 */
#define CAVE_EXIT_MODE4_FRAME  34u   /* InitModeA_SubA_GoToMode4 */
static u32 s_cave_exit_vt0 = 0u;
static u8 s_cave_exit_enter_room = 0u;   /* InitMode_EnterRoom due at mode 4 */

/* NES RAM effects of a Genesis load held back to the NES frame that makes
 * them. The Genesis lays out the next room in one go at the frame the NES
 * does LayoutRoom (display off), but the NES changes the object RAM only
 * later, in InitMode_EnterRoom (cave exit: frame 35; until then the cave
 * person / fire slots stay in RAM). Begin: copy NES RAM; hold: the cells
 * the load changed go back to their old values, the new ones are kept
 * here; release: the new values land. */
static u32 s_ram_defer[0x800 / 4];       /* NES RAM, longs */
static u8 s_ram_defer_mask[0x800 / 4];   /* bit k: byte k of that long */
static u8 s_ram_defer_held = 0u;
static void ram_defer_begin(void)
{
    const u32 *r = (const u32 *)(const void *)nes_ram;
    u16 i;
    for (i = 0u; i < 0x800u / 4u; ++i) s_ram_defer[i] = r[i];
}
static void ram_defer_hold(void)
{
    u32 *r = (u32 *)(void *)nes_ram;
    u16 i;
    for (i = 0u; i < 0x800u / 4u; ++i) {
        const u32 now = r[i], old = s_ram_defer[i];
        u8 m = 0u;
        if (now != old) {
            const u32 diff = now ^ old;
            if (diff & 0xFF000000ul) m |= 1u;
            if (diff & 0x00FF0000ul) m |= 2u;
            if (diff & 0x0000FF00ul) m |= 4u;
            if (diff & 0x000000FFul) m |= 8u;
            r[i] = old;
            s_ram_defer[i] = now;
        }
        s_ram_defer_mask[i] = m;
    }
    s_ram_defer_held = 1u;
}
static void ram_defer_release(void)
{
    u8 *b = (u8 *)(void *)nes_ram;
    const u8 *n = (const u8 *)(const void *)s_ram_defer;
    u16 i;
    if (!s_ram_defer_held) return;
    s_ram_defer_held = 0u;
    for (i = 0u; i < 0x800u / 4u; ++i) {
        const u8 m = s_ram_defer_mask[i];
        u8 k;
        if (m == 0u) continue;
        for (k = 0u; k < 4u; ++k)                 /* bit 1 << k: byte k */
            if (m & (u8)(1u << k)) b[i * 4u + k] = n[i * 4u + k];
    }
}

/* Black playfield by parking the scroll on cleared plane rows 32..59: the
 * next room renders into rows 0..28 unseen (no display-off, HUD stays).
 * The rows are cleared on the two ticks before, while off screen (VRAM
 * writes stall during active display), so the park is one scroll write. */
static void playfield_park_clear(u16 first_row)
{
    u16 row;
    for (row = first_row; row < (u16)(first_row + 14u); ++row)
        render_plane_fill_row(0u, 0u, row, 32u, 0u);
}

/* Scroll change applied at the next VBlank (a tick's direct write lands
 * mid-frame and tears). */
static void set_bg_scroll_vsync(short h_scroll, short v_scroll)
{
    VDP_setHorizontalScrollVSync(BG_A, h_scroll);
    VDP_setVerticalScrollVSync(BG_A, v_scroll);
    VDP_setHorizontalScrollVSync(BG_B, h_scroll);
    VDP_setVerticalScrollVSync(BG_B, v_scroll);
}

static void playfield_park_black(void)
{
    set_bg_scroll_vsync(0, PARK_VSCROLL);
    s_scroll_hold = 1u;
}
static void begin_cave_exit(void)
{
    circle_transition_close((short)(players[0].x + 8),(short)(players[0].y + 1),
                             s_active_scroll_x,s_active_scroll_y);
    s_cave_load_blank = 1u;                 /* no Link redraw from here */
    nes_ram[0x0012u] = 0x0Au;
    nes_ram[0x0013u] = 0u;
    nes_ram[0x0011u] = 0u;                  /* EndPrepareMode */
    s_nes_load_base = s_frame_counter;
    nes_load_clock_start(&k_cave_leave_tl);
    s_cave_exit_vt0 = vtimer;
    s_lvl_step = 0u;                        /* last frame handled */
    s_lvl_phase = LVL_CAVE_EXIT;
}

static void cave_exit_frame(u8 f)
{
    if (f == CAVE_EXIT_DARK_FRAME) render_display_enable(0u);
    if (f == CAVE_EXIT_LIGHT_FRAME) render_display_enable(1u);
    if (f < CAVE_EXIT_BLANK_FRAME)           /* parking rows, off screen */
        playfield_park_clear(f == 1u ? 32u : 46u);
    if (f == 1u) {
        /* InitModeSubroom_Sub0 -> DrawSpritesBetweenRooms: the object
         * sprites go on NES f541; DrawLinkBetweenRooms animates Link in
         * the OW (Link_EndMoveAndAnimateBetweenRooms, $3D0). */
        enemy_render_reset_oam();
        enemy_render_native_sweep();
        VDP_updateSprites(80u, DMA_QUEUE);
        if (nes_ram[0x0010u] == 0u) roomrom_combat_animate_link_base();
    }
    if (f == CAVE_EXIT_HIDE_FRAME) {         /* Link goes with the blank */
        roomrom_sprites_set_link_pose((short)-32, (short)-32, players[0].face, 0u);
        VDP_updateSprites(80u, DMA_QUEUE);
        playfield_park_black();              /* shows on the blank frame */
        return;
    }
    if (f == CAVE_EXIT_LOAD_FRAME) {
        rr_warp_outcome_t out = {0};
        ram_defer_begin();
        out.dest_scene = SCENE_OW;
        out.dest_room_id = s_cave_return_room;
        out.dest_link_x = s_cave_return_x;
        out.dest_link_y = (u8)(s_cave_return_y + 16u);
        out.dest_link_face = LINK_FACE_DOWN;
        out.dest_redux_flag = roomrom_main_current_redux_flag();
        s_warp_keep_chr = 1u;
        s_warp_keep_hud = 1u;
        s_warp_defer_enter_room = 1u;        /* objects at mode 4 */
        s_cave_exit_enter_room = 1u;
        roomrom_main_apply_warp_outcome(&out);
        roomrom_sprites_set_link_pose((short)-32, (short)-32, players[0].face, 0u);
        enemy_render_reset_oam();
        enemy_render_native_sweep();
        VDP_updateSprites(80u, DMA_QUEUE);
        roomrom_ow_room_render_prepare_drop();
        /* The cave teardown right before the hold, so its RAM (person /
         * fire slots) never shows at a frame boundary; the hold before
         * the next frame's NES state (cave_exit_tick), which it must not
         * take back. */
        cave_exit();
        ram_defer_hold();                    /* lands with InitMode4 */
        return;
    }
    if (f == CAVE_EXIT_REVEAL_FRAME) {
        /* The OW room at once on the next frame (scroll at VBlank). Link's
         * sprite is still where DrawLinkBetweenRooms put it on frame 1 (the
         * cave spot, ObjX/ObjY held until InitMode4): it shows again with
         * the playfield, at the bottom edge. */
        roomrom_sprites_set_link_pose((short)nes_ram[0x0070u], (short)nes_ram[0x0084u],
                                      LINK_FACE_DOWN, (u8)(nes_ram[0x03E4u] & 1u));
        VDP_updateSprites(80u, DMA_QUEUE);
        /* PutLinkBehindBackground (UndergroundExitType set in a cave). */
        mark_behind_bg_at(nes_ram[0x0070u], nes_ram[0x0084u]);
        s_scroll_hold = 0u;
        set_bg_scroll_vsync(s_active_scroll_x, s_active_scroll_y);
        return;
    }
    if (f == CAVE_EXIT_MODE4_FRAME) {
        /* Mode 4 (k_cave_leave_tl's last frame); InitMode_EnterRoom method
         * 1, StepOutside (T-132), runs on the next frame. */
        s_cave_load_blank = 0u;
        nes_ram[0x0012u] = 0x04u;
        nes_ram[0x0013u] = 0u;
        s_lvl_entrance_tile = s_cave_entrance_tile;
        s_lvl_init = 1u;
        s_lvl_phase = LVL_STEP_OUT;
    }
}

/* Frame steps up to the current video frame, re-read after each step: a
 * step that runs into the next frame (the load) is followed in the same
 * tick by that frame's NES state (k_cave_leave_tl) and step. */
static void cave_exit_tick(void)
{
    for (;;) {
        u32 t = vtimer - s_cave_exit_vt0;
        if (t > CAVE_EXIT_MODE4_FRAME) t = CAVE_EXIT_MODE4_FRAME;
        if (s_lvl_step >= (u8)t || s_lvl_phase != LVL_CAVE_EXIT) return;
        (void)nes_load_clock();
        cave_exit_frame(++s_lvl_step);
        /* OW room columns (RAM only, 16 by frame 4) after the frame's NES
         * state, which must land before the frame ends. */
        if (s_lvl_step < CAVE_EXIT_LOAD_FRAME)
            roomrom_ow_room_render_prepare(s_cave_return_room, 4u);
    }
}

/* NES EndPrepareMode (Z_05.asm:3181), run when UpdatePlayer's stairs /
 * cave-entrance check sets GameMode $10: submode, IsUpdatingMode, [0F],
 * Link's ObjState, shove and invincibility timer cleared (T-145:
 * t111_dark_candle kept Link's invincibility $06 into the level). */
static void end_prepare_mode(void)
{
    nes_ram[0x0013u] = 0u;                     /* GameSubmode */
    nes_ram[0x0011u] = 0u;                     /* IsUpdatingMode */
    nes_ram[0x000Fu] = 0u;
    nes_ram[0x00ACu] = 0u;                     /* ObjState (Link) */
    nes_ram[0x00C0u] = 0u;                     /* ObjShoveDir */
    nes_ram[0x00D3u] = 0u;                     /* ObjShoveDistance */
    nes_ram[0x04F0u] = 0u;                     /* ObjInvincibilityTimer */
}

void roomrom_main_begin_level_entry(const rr_warp_outcome_t *out)
{
    unsigned char tile;
    s_lvl_out = *out;
    /* Z_05.asm @LoadLevel: CaveSourceRoomId = the OW room of the entrance
     * (a Continue in the OW after the level starts there). */
    if (s_scene == SCENE_OW) nes_ram[0x0526u] = s_room_id;
    /* @LoadLevel: CurLevel from the cave index, TargetMode 2 (load a level),
     * when the stairs start (T-171: t054 t787 NES $10 = 1, $5B = 2). */
    if (s_scene == SCENE_OW) {
        nes_ram[0x0010u] = out->dest_level;
        nes_ram[0x005Bu] = 0x02u;
    }
    nes_ram[0x0070u] = (unsigned char)players[0].x;
    nes_ram[0x0084u] = (unsigned char)players[0].y;
    tile = collision_get_collidable_tile_still(0u);
    s_lvl_entrance_tile = tile;
    s_lvl_exiting = 0u;
    nes_ram[0x0012u] = 0x10u;
    end_prepare_mode();
    s_lvl_target_y = (unsigned char)players[0].y;
    if (tile == 0x24u) {
        s_lvl_target_y = (unsigned char)(players[0].y + 0x10);
    }
    s_lvl_phase = LVL_STAIRS;
    s_lvl_init = 1u;
}

/* InitMode_EnterRoom method 1 (Z_05.asm:1570): X from LevelBlockAttrsA,
 * Y row from LevelBlockAttrsF of the OW room; 1-a (entered through a $24
 * opening): start $10 lower; facing down. NES sets this before
 * @PlaceObjects, so AssignObjSpawnPositions' IsSafeToSpawn sees it (T-140:
 * t132_uw_exit spawn spots, SpawnCycle NES 0 / Genesis 2). */
static void step_out_start_pos(void)
{
    unsigned char room = s_room_id;
    unsigned char ty = (unsigned char)(((nes_ram[0x6AFEu + room] & 7u) << 4) + 0x4Du);
    players[0].x = (short)(nes_ram[0x687Eu + room] & 0xF0u);
    s_lvl_target_y = ty;
    players[0].y = (short)(s_lvl_entrance_tile == 0x24u ? ty + 0x10 : ty);
    if (s_lvl_entrance_tile == 0x24u)
        nes_ram[0x0412u] = ty;                 /* StairsTargetY (method 1-a) */
    players[0].face = LINK_FACE_DOWN;
    s_link_dir = LINK_DIR_DOWN;
    s_link_grid_offset = 0;
    nes_ram[0x0098u] = 0x04u;
    nes_ram[0x0070u] = (unsigned char)players[0].x;
    nes_ram[0x0084u] = (unsigned char)players[0].y;
    nes_ram[0x0394u] = 0u;
}

static void level_entry_draw_link(void)
{
    s_link_frame = (u8)((nes_ram[0x03E4u] & 1u) ^
        ((players[0].face == LINK_FACE_LEFT ||
          players[0].face == LINK_FACE_RIGHT) ? 1u : 0u));
    roomrom_sprites_set_link_pose(players[0].x, players[0].y,
                                  players[0].face, s_link_frame);
}

/* T-187 same-level host publication: no scene reset or LevelInfo reinstall. */
void cellar_host_layout(unsigned char cellar)
{
    unsigned short colors[64];
    render_cram_read(colors,64u);
    s_room_id=nes_ram[0x00EBu];
    if (cellar) {
        s_active_slot_x=0u; s_active_plane=0u; s_active_row_base=0u;
        s_active_scroll_x=0; s_active_scroll_y=0;
        set_plane_scroll(0u,0,0); set_plane_scroll(1u,0,0);
        roomrom_cellar_render();
        refresh_room_metadata(s_room_id);
        clear_hud_underlay_for_row_base(0u);
    } else load_room(s_room_id);
    render_cram_subrange_upload(8u,colors+8,8u); /* retain active fade */
}

/* Cellar return (mode $0A, UW): LayoutRoom_SubmodeTask overruns on the
 * NES. Frame 0 = the submode-4 layout frame (its NMI ran): the NMIs of
 * frames 1-3 are lost, submode 5 shows from frame 3, the next NMI is
 * frame 4's (ls_l1_cellar, L1 $7F -> $22: FC $80 on frames 242-245,
 * submode 5 on 245, FC $81 and CurRow 1 on 246). */
static const u8 k_cellar_return_nmis[5] = { 0, 0, 0, 0, 1 };
static const u8 k_cellar_return_sub[5]  = { 4, 4, 4, 5, 5 };
static const nes_load_timeline_t k_cellar_return_tl = {
    5u, k_cellar_return_nmis, 0, k_cellar_return_sub, k_zero32, 0 };

/* The destination room's plane words, a metatile column a tick, on the
 * submode-3 fade ticks (T-172 precompute in the idle curtain buffer), so
 * the layout fits the NES's frames. */
void cellar_host_prepare(void)
{
    roomrom_uw_room_render_prepare(nes_ram[0x00EBu], &s_curtain[0][0], 1u);
}

void cellar_host_return_layout(void)
{
    nes_load_clock_start(&k_cellar_return_tl);
    nes_ram[0x00E9u] = 0u;
    cellar_host_layout(0u);
    nes_load_clock_reapply();
}

unsigned char cellar_host_clock_busy(void) { return nes_load_clock_busy(); }

void cellar_host_enter(void)
{
    room_reset_player_state();
    enemy_loop_room_reenter(s_room_id,(unsigned char)s_scene,
        roomrom_uw_room_render_get_level(),roomrom_uw_room_render_get_quest());
    room_reset_inv_obj_state();
    nes_ram[0x03D0u]=4u; /* native init, before first movement */
    nes_ram[0x03A8u]=0u; s_link_pos_frac=0u;
    request_boss_chr_if_boss_room();
}

void cellar_host_walk(void)
{
    roomrom_main_link_sync_from_nes();
    /* Walker_Move in mode 9: [0F] = ObjDir (down), [0E] = 0, then
     * Walker_CheckTileCollision (ObjCollidedTile at each grid point). */
    nes_ram[0x000Eu]=0u; nes_ram[0x000Fu]=0x04u;
    (void)link_walker_check_tile_collision(s_link_grid_offset);
    if (nes_ram[0x000Fu]!=0u) link_nes_move_object(LINK_DIR_DOWN);
    nes_ram[0x0070u]=(u8)players[0].x;
    nes_ram[0x0084u]=(u8)players[0].y;
    nes_ram[0x0394u]=(u8)s_link_grid_offset;
    nes_ram[0x03A8u]=s_link_pos_frac;
    /* Link_EndMoveAndAnimate @TruncGridOffset also runs in mode 9. */
    if ((nes_ram[0x0394u]&7u)==0u) {
        nes_ram[0x0394u]=0u; s_link_grid_offset=0;
    }
    roomrom_combat_end_move_and_animate();
}

/* NES source: Z_05 DrawLinkBetweenRooms/InitMode9; Z_07
 * DrawSpritesBetweenRooms/Link_EndMoveAndAnimateBetweenRooms.
 * Drained C: cellar_mode_tick and existing Link renderer.
 * Coverage: FULL cellar transition visibility; movement remains native.
 * Stance: EXTEND, publish only Link draws requested by the native mode. */
void cellar_host_draw(unsigned char link_draw)
{
    roomrom_main_link_sync_from_nes();
    enemy_render_reset_oam(); enemy_render_native_sweep();
    roomrom_hud_draw(roomrom_uw_room_render_get_map(),s_room_id,1u);
    if (link_draw) {
        if (link_draw==2u) roomrom_combat_end_move_and_animate();
        level_entry_draw_link();
        if (link_draw==2u) {
            g_render_sat_cache[ROOMROM_SPRITE_SLOT_LINK].attribut &= 0x7FFFu;
            g_render_sat_cache[ROOMROM_SPRITE_SLOT_LINK_R].attribut &= 0x7FFFu;
        }
    } else {
        VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_LINK,-32,-32,
            RENDER_SPRITE_SIZE(1,2),0u,ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_LINK));
        VDP_setSpriteFull(ROOMROM_SPRITE_SLOT_LINK_R,-32,-32,
            RENDER_SPRITE_SIZE(1,2),0u,ROOMROM_SPRITE_NEXT(ROOMROM_SPRITE_SLOT_LINK_R));
    }
    VDP_updateSprites(80u,DMA_QUEUE);
}

static void level_entry_tick(void)
{
    /* The new scene's sprite banks load while the screen is still dark. */
    level_chr_swap_tick();
    roomrom_scene_uw_sprite_base_tick();
    if (s_lvl_phase == LVL_CAVE_EXIT) {
        cave_exit_tick();
        return;
    }
    if (s_lvl_phase == LVL_MODE2_INIT) {         /* T-171 staged level load */
        mode2_init_tick();
        return;
    }
    if (s_lvl_phase == LVL_MODE3_INIT) {         /* T-013 mode 8 CONTINUE */
        mode3_init_tick();
        return;
    }
    if (s_lvl_phase == LVL_PLAY_INIT) {
        /* T-012: InitMode5Play takes a frame of its own (Link drawn, no
         * UpdatePlayer); t012_route NES t807 Link still, Genesis moved. */
        nes_ram[0x0011u] = 1u;                    /* IsUpdatingMode */
        room_init_mode5_play_palette_row7();
        init_mode5_play_create_room_objects();
        level_entry_draw_link();
        VDP_updateSprites(ROOMROM_SPRITE_SLOT_ENEMY_FIRST, DMA_QUEUE);
        s_lvl_phase = LVL_NONE;
        return;
    }
    if (s_lvl_phase == LVL_EXIT_LOAD) {
        const u32 te = vtimer - s_lvl_exit_vt0;
        if (s_lvl_exit_tl && te < LEVEL_EXIT_MODE2_FRAME) return;  /* modes 6/7 */
        if (s_lvl_exit_tl && !s_lvl_exit_mode2) {
            /* InitMode7_Sub1 -> EndGameMode12 (the clock sets mode 2). */
            s_lvl_exit_mode2 = 1u;
            /* CalculateNextRoomForDoor: [$04E4] = the start room's unique
             * ID (before CurLevel goes to 0); EndGameMode12 zeroes [$E7]. */
            nes_ram[0x04E4u] = room_get_unique_room_id();
            nes_ram[0x00E7u] = 0u;
            nes_ram[0x0010u] = 0u;              /* CurLevel: the OW */
            if (s_lvl_exit_dir) {
                const u8 d = s_lvl_exit_dir;
                s_lvl_exit_dir = 0u;
                nes_ram[0x0521u] = nes_ram[0x00EEu];
                nes_ram[0x00EEu] = (u8)(((d >> 1) & 0x05u) | ((d << 1) & 0x0Au));
                nes_ram[0x00EDu] = (u8)(nes_ram[0x00EDu] - 1u);
                nes_ram[0x00ECu] = s_lvl_exit_next;
            }
            nes_ram[0x005Au] = 0x02u;
            if (te < LEVEL_EXIT_LOAD_FRAME) return;
        }
        if (s_lvl_exit_dir) {
            /* Door exit, NES InitMode7_Sub1 then EndGameMode12 (the edge
             * tick was mode 6): PrevOpenedDoors, CurOpenedDoors = entering
             * side, DEC PrevRow, NextRoomId ($80+ = invalid),
             * UndergroundExitType 2 (T-171: t132_uw_exit t911). */
            const u8 d = s_lvl_exit_dir;
            s_lvl_exit_dir = 0u;
            nes_ram[0x0521u] = nes_ram[0x00EEu];
            nes_ram[0x00EEu] = (u8)(((d >> 1) & 0x05u) | ((d << 1) & 0x0Au));
            nes_ram[0x00EDu] = (u8)(nes_ram[0x00EDu] - 1u);
            nes_ram[0x00ECu] = s_lvl_exit_next;
        }
        /* EndGameMode12 (door exit and the triforce exit alike):
         * UndergroundExitType 2 (T-171: t132 t911, t013_route t8465). */
        nes_ram[0x005Au] = 0x02u;
        /* Mode 2 (display off) + mode 3 submodes: back to the OW room. */
        nes_ram[0x0012u] = 0x02u;
        nes_ram[0x0013u] = 0u;
        init_mode2_sub0();
        render_display_enable(0u);
        if (s_lvl_exit_tl) {
            /* InitMode_EnterRoom (objects, bounds) runs at mode 4 in the
             * OW, as for the cave exit (StepOutside init). */
            s_warp_defer_enter_room = 1u;
            s_cave_exit_enter_room = 1u;
        }
        roomrom_main_apply_warp_outcome(&s_lvl_out);
        /* InitMode3_Sub1: the OW room came from CaveSourceRoomId, which
         * then goes back to invalid (k_level_exit_tl does it on the NES
         * frame). */
        if (!s_lvl_exit_tl) nes_ram[0x0526u] = 0xFFu;
        s_underground_exit_type = 2u;           /* EndGameMode12 */
        roomrom_sprites_set_link_pose((short)-32, (short)-32, players[0].face, 0u);
        curtain_hide();
        level_hud_static_only();
        enemy_render_reset_oam();
        enemy_render_native_sweep();
        VDP_updateSprites(80u, DMA_QUEUE);
        render_display_enable(1u);
        s_lvl_step = 0u;
        s_lvl_timer = 0u;
        s_lvl_phase = LVL_CURTAIN;
        if (s_lvl_exit_tl) {
            /* Modes 2 / 3 init on k_level_exit_tl, display off until its
             * end, as the level entry; room objects at mode 4. */
            s_lvl_exit_tl = 0u;
            render_display_enable(0u);
            s_lvl_display_pending = 1u;
            players[0].x = 0x78;
            players[0].y = (short)nes_ram[0x6BA6u];     /* OW LevelInfo_StartY */
            nes_load_clock_reapply();
            return;
        }
        nes_ram[0x0012u] = 0x03u;
        nes_ram[0x0013u] = 0u;
        /* T-140: run the NES frame work of modes 6/7/2/3-init the fast
         * Genesis exit skipped, up to the NES curtain-start FrameCounter. */
        while ((u8)(nes_ram[0x0015u] - s_lvl_exit_fc) < s_lvl_exit_steps) {
            nes_ram[0x0015u] = (unsigned char)(nes_ram[0x0015u] + 1u);
            nes_frame_timers_and_random();
        }
        nes_ram[0x007Cu] = 0x10u;   /* InitMode3_Sub8: curtain columns (T-171) */
        nes_ram[0x007Du] = 0x11u;
        return;
    }
    if (s_lvl_phase == LVL_STEP_OUT) {
        unsigned char cnt;
        if (s_lvl_init) {
            s_lvl_init = 0u;
            ram_defer_release();        /* InitMode_EnterRoom's RAM */
            nes_ram[0x0011u] = 1u;      /* InitMode4 done: mode 4 updating */
            step_out_start_pos();
            if (s_cave_exit_enter_room) {
                /* InitMode_EnterRoom: ClearRam0300UpTo and the OW room's
                 * objects (Link placed first, method 1), on the NES's mode 4
                 * init frame. */
                s_cave_exit_enter_room = 0u;
                enemy_loop_room_reenter(s_room_id, (unsigned char)s_scene, 0u, 0u);
                request_boss_chr_if_boss_room();
                if (s_lvl_entrance_tile == 0x24u)   /* after the $300 clear */
                    nes_ram[0x0412u] = s_lvl_target_y;
            }
            if (s_lvl_entrance_tile == 0x24u) {
                nes_ram[0x0603u] |= 0x08u;
                /* The plane priority goes with the sprite: Link's new spot
                 * shows next frame (SAT at VBlank), so the reveal-spot
                 * stamp moves then too (s_step_out_restamp). */
                s_step_out_restamp = 1u;
            }
            roomrom_hud_b_item_update();          /* DrawSpritesBetweenRooms */
            level_entry_draw_link();
            nes_ram[0x0011u] = 1u;
            VDP_updateSprites(ROOMROM_SPRITE_SLOT_ENEMY_FIRST, DMA_QUEUE);
            circle_open_here();
            return;                     /* InitMode4 has its own frame */
        }
        /* StepOutside: beside stairs (method 1-b) the first update ends
         * the mode (GoToNextModePlayLevelSong). */
        if (s_lvl_entrance_tile != 0x24u) goto stepped_out;
        /* StepOutside: AnimateAndDrawLinkBehindBackground, Y-1 every 4th
         * frame to StairsTargetY. */
        if (s_step_out_restamp) {
            s_step_out_restamp = 0u;
            cave_fade_restore_arch();
            mark_link_behind_bg();
        }
        cnt = nes_ram[0x03D0u];
        if (cnt <= 1u) {
            nes_ram[0x03D0u] = 6u;
            nes_ram[0x03E4u] = (unsigned char)(nes_ram[0x03E4u] ^ 1u);
        } else {
            nes_ram[0x03D0u] = (unsigned char)(cnt - 1u);
        }
        /* AnimateAndDrawLinkBehindBackground, then DEC ObjY: the sprite
         * shows the Y from before this frame's step. */
        level_entry_draw_link();
        VDP_updateSprites(ROOMROM_SPRITE_SLOT_ENEMY_FIRST, DMA_QUEUE);
        if ((nes_ram[0x0015u] & 0x03u) == 0u) {
            players[0].y = (short)(players[0].y - 1);
            nes_ram[0x0084u] = (unsigned char)players[0].y;
        }
        if ((unsigned char)players[0].y != s_lvl_target_y) return;
stepped_out:
        /* Link walks in front of the ground again, but not yet: the next
         * frame's InitMode5Play draws him behind it once more
         * (DrawLinkBetweenRooms while UndergroundExitType is set) and the
         * NES shows a frame's sprites one frame later, so his first
         * frame in front is the third from here (tick hook below). */
        s_arch_restore_ticks = 3u;
        /* GoToNextModePlayLevelSong: play resumes. */
        nes_ram[0x0012u] = 0x05u;
        nes_ram[0x0013u] = 0u;
        nes_ram[0x0011u] = 0u;                    /* InitMode5Play next */
        roomrom_hud_set_counts_hidden(0u);
        s_lvl_phase = LVL_PLAY_INIT;
        return;
    }
    if (s_lvl_phase == LVL_STAIRS) {
        unsigned char cnt;
        /* InitMode10 takes a frame of its own (no move, no animation). */
        if (s_lvl_init) {
            s_lvl_init = 0u;
            /* InitMode10 (Z_05.asm:1400) starts with GetCollidableTileStill:
             * Link's ObjCollidedTile is the standing tile (T-171). */
            (void)collision_get_collidable_tile_still(0u);
            if (s_lvl_target_y != (unsigned char)players[0].y) {
                nes_ram[0x0603u] |= 0x08u;         /* stairs effect */
                nes_ram[0x0412u] = s_lvl_target_y; /* StairsTargetY (T-171) */
            }
            level_entry_draw_link();
            /* T-185: publish the entry pose on InitMode10's frame; otherwise
             * the old OW +2 pose remains on SAT for two frames. Native
             * InitMode10 begins updating; behind-BG starts on the following
             * UpdateMode10Stairs_Full, not during this initialization. */
            VDP_updateSprites(ROOMROM_SPRITE_SLOT_ENEMY_FIRST, DMA_QUEUE);
            if (s_lvl_target_y != (unsigned char)players[0].y)
                mark_link_behind_bg();
            nes_ram[0x0011u] = 1u;
            return;
        }
        if (s_lvl_load_pending) {
            /* T-171: InitMode2 from the NES's frame-1 NMI on; the busy
             * frames of this load show the NES's own (lost-NMI) state. */
            s_lvl_load_pending = 0u;
            init_mode2_sub0();
            render_display_enable(0u);          /* TurnOffAllVideo */
            s_warp_defer_enter_room = 1u;       /* objects at mode 4 */
            roomrom_main_apply_warp_outcome(&s_lvl_out);
            /* InitMode3_Sub8 (the RAM mirror follows k_level_enter_tl). */
            players[0].x = 0x78;
            players[0].y = (short)nes_ram[0x6BA6u];     /* LevelInfo_StartY */
            players[0].face = LINK_FACE_UP;
            s_link_dir = LINK_DIR_UP;
            s_link_grid_offset = 0;
            nes_ram[0x0098u] = 0x08u;
            nes_ram[0x0394u] = 0u;
            curtain_hide();
            /* Mode 3 status bar: static part only (no counts, hearts, map
             * dot or B/A items; t131_uw_doors f1420-1490). */
            level_hud_static_only();
            roomrom_sprites_set_link_pose((short)-32, (short)-32, players[0].face, 0u);
            enemy_render_reset_oam();
            enemy_render_native_sweep();
            VDP_updateSprites(80u, DMA_QUEUE);
            /* T-012 / T-171: NES modes 2 + 3 init take 12 FrameCounter
             * steps over 31 frames with the display off; k_level_enter_tl
             * keeps the Genesis on that schedule and the curtain (display
             * on) waits for it. */
            s_lvl_display_pending = 1u;
            nes_load_clock_reapply();
            s_lvl_step = 0u;
            s_lvl_timer = 0u;
            s_lvl_phase = LVL_CURTAIN;
            return;
        }
        /* AnimateObjectWalking: 6-frame ObjAnimCounter cadence. */
        cnt = nes_ram[0x03D0u];
        if (cnt <= 1u) {
            nes_ram[0x03D0u] = 6u;
            nes_ram[0x03E4u] = (unsigned char)(nes_ram[0x03E4u] ^ 1u);
        } else {
            nes_ram[0x03D0u] = (unsigned char)(cnt - 1u);
        }
        if ((unsigned char)players[0].y != s_lvl_target_y &&
            (nes_ram[0x0015u] & 0x03u) == 0u) {
            players[0].y = (short)(players[0].y + 1);
            nes_ram[0x0084u] = (unsigned char)players[0].y;
        }
        level_entry_draw_link();
        VDP_updateSprites(ROOMROM_SPRITE_SLOT_ENEMY_FIRST, DMA_QUEUE);
        if ((unsigned char)players[0].y != s_lvl_target_y) return;
        /* T-171: mode 2's first frame (k_level_enter_tl frame 0): the
         * stairs end here; InitMode2 runs from the next NMI. */
        circle_transition_close((short)(players[0].x + 8),(short)(players[0].y + 1),
                                 s_active_scroll_x,s_active_scroll_y);
        nes_ram[0x0012u] = 0x02u;
        nes_ram[0x0013u] = 0u;
        nes_ram[0x0011u] = 0u;
        s_lvl_enter_room = nes_ram[0x00EBu];
        s_lvl_enter_x = nes_ram[0x0070u];
        s_lvl_enter_y = nes_ram[0x0084u];
        s_lvl_dest_room = 0xFFu;
        nes_load_clock_start(&k_level_enter_tl);
        s_lvl_load_pending = 1u;
        return;
    }
    /* T-171: the NES is still in modes 2 / 3 init (k_level_enter_tl);
     * its display comes on with the first UpdateMode3Unfurl (frame 32). */
    if (nes_load_clock_busy()) return;
    if (s_lvl_display_pending) {
        s_lvl_display_pending = 0u;
        render_display_enable(1u);
    }
    /* Mode 3 UpdateWorldCurtainEffect: columns $10-k and $11+k (1-based)
     * when Link's ObjTimer has run out, then a 5-frame delay. */
    if (circle_transition_waiting()) {
        s_lvl_step = 16u;
        nes_ram[0x007Cu] = 0u;
        nes_ram[0x007Du] = 0x21u;
        if (!s_lvl_exiting) circle_open_here();
    } else if (startup_triangle_active()) {
        level_entry_draw_link();
        g_render_sat_cache[ROOMROM_SPRITE_SLOT_LINK].attribut &= 0x7FFFu;
        g_render_sat_cache[ROOMROM_SPRITE_SLOT_LINK_R].attribut &= 0x7FFFu;
        VDP_updateSprites(ROOMROM_SPRITE_SLOT_ENEMY_FIRST, DMA_QUEUE);
        if (!startup_triangle_tick()) return;
        /* Restore cached Link priority with the mask's final VBlank. */
        g_render_sat_cache[ROOMROM_SPRITE_SLOT_LINK].attribut |= 0x8000u;
        g_render_sat_cache[ROOMROM_SPRITE_SLOT_LINK_R].attribut |= 0x8000u;
        VDP_updateSprites(ROOMROM_SPRITE_SLOT_ENEMY_FIRST, DMA_QUEUE);
        s_lvl_step = 16u;
        nes_ram[0x007Cu] = 0u;
        nes_ram[0x007Du] = 0x21u;
    } else {
    if (s_lvl_timer != 0u) {
        s_lvl_timer = (unsigned char)(s_lvl_timer - 1u);
        return;
    }
    curtain_reveal((unsigned char)(16u + s_lvl_step));
    curtain_reveal((unsigned char)(15u - s_lvl_step));
    s_lvl_timer = 4u;
    /* NES UpdateMode3Unfurl keeps the delay in Link's ObjTimer ($28 = 5,
     * run down by the NMI timers: t013_continue NES t224-t228). */
    nes_ram[0x0028u] = 5u;
    ++s_lvl_step;
    /* NES UpdateWorldCurtainEffect moves ObjX+12 / ObjX+13 inward-out:
     * $10/$11 -> $00/$21 over the 16 steps (T-013). */
    nes_ram[0x007Cu] = (u8)(0x10u - s_lvl_step);
    nes_ram[0x007Du] = (u8)(0x11u + s_lvl_step);
    if (s_lvl_step < 16u) return;
    }
    if (s_lvl_exiting) {
        /* Mode 4 in the OW: InitMode_EnterRoom method 1, StepOutside. */
        s_lvl_exiting = 0u;
        nes_ram[0x0012u] = 0x04u;
        nes_ram[0x0013u] = 0u;
        nes_ram[0x0011u] = 0u;              /* a new mode starts in init */
        s_lvl_init = 1u;
        s_lvl_phase = LVL_STEP_OUT;
        return;
    }
    /* GoToNextModePlayLevelSong: mode 4 submode 0, the NES walk-in. */
    s_lvl_phase = LVL_NONE;
    s_lvl_enter_only = 1u;
    /* T-241: drained roommd_go_to_next_mode_play_level_song, matching
     * Z_07 UpdateMode3Unfurl. Publish SongRequest on the last column
     * pair; waiting for play_finish would start it after mode 4. */
    room_go_to_next_mode_play_level_song();
    /* XGM_startPlay can span a VBlank. Keep it after the final columns'
     * transfer rather than letting the audio ISR delay that transfer. */
    s_curtain_song_pending = nes_ram[0x0600u];
    nes_ram[0x0600u] = 0u;
    ow_scroll_begin_enter(0x08u);
    s_scroll_start_x = s_active_scroll_x;
    s_scroll_start_y = s_active_scroll_y;
    s_scroll_state = SCROLL_V_UP;
    s_scroll_frame = 0u;
}

void roomrom_debug_enter(void)
{
    unsigned char saved_options[OPTIONS_STATE_SIZE];
    unsigned int saved_options_len;

    /* T-226: this entry runs again after Save/ending returns to File Select.
     * NES Z_02 UpdateMode1Menu_Sub1 selects the OW before loading the chosen
     * profile; InitMode3 uses its StartRoomId when CaveSourceRoomId is $FF.
     * C static initializers only handled the first boot, leaving the old
     * dungeon scene/room (and transition state) alive on the second entry.
     * Reuse the native scene-switch reset before installing the OW data;
     * quest and the selected save remain owned by the caller/serializer. */
    roomrom_state_reset_for_scene_switch();
    s_scene = SCENE_OW;
    s_mode = MODE_WALK;
    s_lvl_phase = LVL_NONE;
    s_lvl_enter_only = s_lvl_init = s_lvl_exiting = 0u;
    s_cave_load_blank = 0u;
    s_b_blank_col = 0xFFu;
    s_hud_b_key_valid = 0u;

    /* Phase 9 Task 9.4 â€” load options from SRAM (or defaults) and apply
     * game-start option-driven seeds (start hearts, bomb cap) BEFORE
     * any inventory reader runs. The static `g_inventory` initializer
     * already seeds the NES vanilla profile; this layer overwrites
     * heart_values + max_bombs per the active OptionsState. */
    options_persistence_load_or_default();
    options_consumer_apply_inventory_at_start();
    saved_options_len =
        options_runtime_serialize(saved_options, OPTIONS_STATE_SIZE);
    /* T-172: pause menu cells built during this load, not on Start. */
    inventory_menu_cells_build();

    /* Phase 6 Task 6.1: seed `players[0]` with NES Z1 boot defaults
     * before anything reads it. engine always boots in 1-player mode;
     * Phase 13 will populate `players[1..3]` after lobby selection.
     *
     * NES Z1 vanilla start position in room $77 is (X=$78, Y=$8D),
     * south of (and facing) the start-cave entrance. Spawning AT the
     * entrance tile ($24 at col 8/9 row 3) auto-fires cave_entrance_check
     * on frame 0 and traps Link in the cave with no OW input available.
     * Tier-0 cave-entry test path should be exercised explicitly via
     * walking onto the tile, not by booting onto it. */
    players[0].x    = 0x78;
    players[0].y    = 0x8D;
    players[0].face = LINK_FACE_UP;

    /* Phase 7 root-cause fix #4 2026-05-16 â€” seed NES Random[$18..$24]
     * at boot. NES Z_07.asm @ScrambleRandom is bit 1 EOR + ROR-chain.
     * Cold-start zero array preserves zero through scramble forever
     * (b0=$00&$02=0, b1=$00&$02=0, carry=0, ROR 0s = 0s). NES Z1 hides
     * this by relying on stale prior-game RAM at boot; on Genesis with
     * cold zero, RNG never starts. Seed with non-zero pattern so the
     * scramble-chain has bits to propagate. */
    rng_seed(0xACE1u);

    /* Phase 7 root-cause fix #5 2026-05-16 â€” clear the a4_probe debug
     * sentinel at NES $0012 (= GameMode). game_main.c:79 stamps
     * RAM(0x0012) = $CD as a sentinel after A4-register verification;
     * the probe never cleans it up. roomrom_debug_enter never wrote
     * GameMode either, so $0012 stayed at $CD and mode_dispatch_update
     * (src/game/world/mode_dispatch.c:42) fell through to default ->
     * mode_stub() every frame -> no Mode-5 Play body fired -> Link
     * input + transition logic never ran.
     *
     * The debug entry starts Mode 5; the File Select entry below starts
     * Mode 3 so it can unfurl before play. */
    /* T-241: publish load mode throughout a real File Select startup;
     * do not advertise play while its graphics/profile are still loading.
     * The debug chord retains its direct-play entry. */
    nes_ram[0x0012u] = g_debug_session ? 0x05u : 0x03u;
    nes_ram[0x0013u] = 0x00u;
    /* Play is running (InitMode5Play done): IsUpdatingMode 1, as on the
     * NES from the first play frame (tick-0 residue $0011 NES 01 GEN 00; a
     * staged save then ran InitModeD, save_roundtrip t130). */
    nes_ram[0x0011u] = g_debug_session ? 0x01u : 0u;
    /* CaveSourceRoomId $FF: the NES menu (Z_02.asm:2577) sets it so mode 3
     * puts Link at StartRoomId (tick-0 residue NES FF GEN 00; a Continue
     * then loaded room $00, t013_continue t214). */
    nes_ram[0x0526u] = 0xFFu;

    s_joy_prev = 0u;
    render_cram_defer(1u);              /* T-168: palettes in VBlank */
    init_video();
    /* PR-4a: init scene-bank state machine BEFORE first scene_load so the
     * first scene_load enqueues into a clean state. */
    level_chr_swap_init();
    upload_scene_chr();
    /* PR-4b regression fix: persistent sprite CHR (common 1025..1262 +
     * Link walk 1263..1294 + attack 1295..1310) was orphaned when PR-4b
     * split upload_chr; without it Link tile 1263 stays zero and Link is
     * invisible. SCENE_OBJ slot 1069..1204 lives inside common range â€”
     * scene_load enqueues a DMA that overwrites that window via
     * level_chr_swap_tick on subsequent frames (last-writer wins). Link
     * tiles 1263+ are outside SCENE_OBJ and survive. */
    roomrom_sprites_upload_chr();
    {
        u32 blank[8] = {0,0,0,0,0,0,0,0};
        VDP_loadTileData(blank, 0, 1, CPU);
    }
    /* P5: scene-load coordinator handles variant selection + sprite CHR
     * upload.  combat redux is not a CHR-load concern, kept separate. */
    roomrom_scene_load(
        (s_scene == SCENE_UW) ? ROOMROM_SCENE_UW_L1 : ROOMROM_SCENE_OVERWORLD,
        current_redux_flag());
    roomrom_combat_set_redux(current_redux_flag());
    /* Substrate fix 2026-05-15 â€” install LevelBlockAttrs + LevelInfo
     * into NES SRAM at $687E..$6C7D BEFORE load_room +
     * enemy_loop_room_init read them. Without this, LBA_C/D + FoeCounts
     * are zero and no enemies spawn anywhere. CurLevel ($0010) drives
     * UW vs OW dispatch in enemy_room_load_objects + cave/dungeon code,
     * so seed it here too (0=OW, 1=UW L1). */
    if (s_scene == SCENE_UW) {
        nes_ram[0x0010u] = 1u;  /* CurLevel = 1 (UW L1) */
        level_info_install_uw(1u, s_current_quest);  /* X+Y+Z boot -> quest 2 */
    } else {
        nes_ram[0x0010u] = 0u;  /* CurLevel = 0 (OW) */
        level_info_install_ow();
        s_room_id = nes_ram[0x6BADu];  /* installed OW StartRoomId */
    }
    /* 2026-05-17 â€” level_info_install_* RESTORED. Prior "CRASH FIX"
     * removal was overcautious: A4=$FF8000 is in SGDK heap free-pool
     * (BSS ends $FF1C90, MEMORY_HIGH=$FFF600, boot allocs ~10KB low).
     * Mirror writes $FF867E..$FF8C7D land in unallocated heap until
     * heap grows past $FF8000. Empirical test follows. If heap collision
     * surfaces, cap MEMORY_HIGH to $FF8000 (only fix needed). */
    load_room(s_room_id);                  /* loads BG pal + sprite PAL1 */
    roomrom_sprites_spawn_link(players[0].x, players[0].y);
    roomrom_combat_init();                 /* S7: clear sword sprite slot */
    roomrom_combat_set_uw(s_scene == SCENE_UW);  /* sword Y bias for UW */
    roomrom_boomerang_init();              /* S7 v6: clear boomerang slot */
    roomrom_arrow_init();                  /* S7 v7: clear arrow slot */
    roomrom_bomb_init();                   /* S7 v8: clear bomb + explosion slots */
    roomrom_link_damage_init();            /* Task 6.11.1: clear invincibility timer */
    roomrom_world_transition_init();       /* Task 5.4: warp coordinator */
    roomrom_pushblock_init();              /* Task 5.7: push-block state machine */
    roomrom_candle_fire_init();            /* Task 5.8.1: candle fire slot 8 */
    enemy_render_reset_oam();              /* Phase 7: clear NES OAM mirror */
    enemy_loop_room_init(s_room_id, (unsigned char)s_scene,
                s_scene == SCENE_UW ? roomrom_uw_room_render_get_level() : 0u,
                s_scene == SCENE_UW ? roomrom_uw_room_render_get_quest() : 0u);  /* Phase 7 Task 7.2 */
    const unsigned char run_selftests =
        roomrom_debug_probe_flag(ROOMROM_DEBUG_PROBE_SELFTEST);
    if (run_selftests) {
        roomrom_probe_metadata_run();      /* Task 5.4 Gate D: in-ROM probe */
    }

    /* Cave diagnostics must not enter/exit a cave during gameplay boot:
     * those calls overwrite object slots, text state and Link's halt state.
     * Exercise cave entry/exit through the explicit harness instead. */
    if (enemy_loop_probe_is_armed()) {
        enemy_loop_probe_run();            /* Heavy 11-slot in-ROM stress probe. */
    }
    /* Phase E (2026-05-24) â€” Warp routes static dispatch probe. Pure
     * functional probe over rooms_overworld[] (loaded by level_info_install_ow
     * at line 1670). Publishes 128-byte result block at $FF7800 for
     * tools/build/probes/probe_warp_routes.lua to byte-diff vs
     * tools/parity/warp_routes_expected.json. */
    if (run_selftests) {
    warp_routes_probe_run();
    /* Phase F (2026-05-25) â€” Dungeon round-trip synthetic verifier.
     * Runs after Phase E so level_info_install_uw mid-sweep doesn't
     * corrupt the OW LBA state Phase E reads. Probe restores the
     * master quest selector on exit; LBA tables left at L9Q2 (last
     * iteration). main.c's regular load_room() will reinstall the
     * right level when the game enters gameplay. */
    dungeon_roundtrip_probe_run();
    options_probe_run();                   /* Phase 9 Task 9.1 â€” pure CPU-side. */
    options_persistence_probe_run();       /* Phase 9 Task 9.2 â€” SRAM I/O. */
    options_consumer_probe_run();          /* Phase 9 Task 9.4 â€” consumer wiring. */
    hud_format_probe_run();                /* Phase 9 Task 9.5 â€” HUD format contract. */
    save_serializer_probe_run();           /* Phase 9 Task 9.7 â€” save serializer round-trip. */
    }
    if (saved_options_len == OPTIONS_STATE_SIZE) {
        (void)options_runtime_apply(saved_options, OPTIONS_STATE_SIZE);
    }

    /* Plan v5a T3.1 â€” debug_enter complete, gameplay loop owns mode now. */
    s_in_gameplay = 1u;

    /* Plan v5 â€” seed ITEM_SWORD_LEVEL=1 (wood sword) so the damage
     * lookup k_sword_damage_points[level-1] returns $10 (16 dmg per
     * stab) instead of 0. Without this the sword sync above writes
     * OBJ_STATE(13)=2 each swing but combat_deal_damage rolls 0 dmg,
     * leaving every enemy unkillable. engine has no inventory UI
     * yet so the level is seeded directly into the NES cell. */
    if (g_debug_session) nes_ram_seed_sword_level(1u);   /* T-092: debug only */

    /* Plan v5b â€” seed nes_ram[$066F/$0670] hearts from g_inventory ONCE
     * here. Per-frame sync flipped to pull-direction so combat's
     * be_harmed writes to RAM($066F) survive; without this seed the
     * first frame would pull a 0 back into g_inventory and the HUD
     * would show empty hearts until the next combat write. */
    nes_ram_seed_inventory_hearts();

    /* Plan v5b T5.5 â€” force dispatcher to re-fire on the next tick;
     * debug_enter may have changed scene/room without going through
     * audio_dispatch_tick. */
    audio_dispatch_reset();
}

unsigned char roomrom_debug_get_scene(void)
{
    return (unsigned char)s_scene;
}

/* 2026-05-22 â€” expose scroll-state for enemy_render to skip drawing
 * enemy sprites during room transitions. Without this, scroll-completion
 * fires enemy_loop_room_init which respawns enemies at NES spawn-list
 * positions â†’ user sees enemies "fly" from old to new positions in one
 * frame. NES hides Link via sprite priority during scroll; we hide all
 * enemies via this gate. */
unsigned char roomrom_is_scrolling(void)
{
    return (unsigned char)(s_scroll_state != SCROLL_NONE);
}

unsigned char roomrom_debug_get_room_id(void)
{
    return s_room_id;
}

short roomrom_debug_get_link_x(void)
{
    return players[0].x;
}

short roomrom_debug_get_link_y(void)
{
    return players[0].y;
}

/* T-102: NES UpdateMode5Play after UpdatePlayer: chase target, weapons,
 * objects and the object-loop tail (Z_07.asm:1855-1990). Returns 1
 * when the frame ends early (cave entry started). */
/* Task 6.10.2: NES Z_07.asm:472 gates per-frame gameplay update on
 * `Paused != 0`. Mirror that here â€” projectile/combat ticks freeze
 * while paused (voluntary or involuntary). Cave + scroll handling
 * already returned above; only the in-room update path is gated.
 *
 * Tier 1: also gate on !cave_fade_is_active so cave-entry/exit
 * animations don't get bombed by combat updates or by
 * cave-entrance re-detection on the very tile that triggered
 * the fade. */
/* NES Z_07.asm CheckLiftItem / SetUpAndDrawLinkLiftingItem /
 * EndLinkLiftingItem (T-011). TakeItem outside mode 5 (caves $0B, UW
 * cellars) sets ItemLiftTimer $506 = $80 and ItemTypeToLift $505; every
 * play tick after the objects Link is halted (ObjState $40) and drawn
 * lifting: left half tile $78, right half $78 flipped (two hands) or $08
 * flipped for a half-width item (one hand), the item at slot $13 16 px
 * above him, left-aligned (LeftAlignHalfWidthObj $504). NES OAM t011
 * f504: #18 $78/$00 x$78, #19 $08/$40 x$80, sword $20 at Y-$10. */
static const unsigned char k_level_song_ids[10] = {
    0x01u, 0x40u, 0x40u, 0x40u, 0x40u, 0x40u, 0x40u, 0x40u, 0x40u, 0x20u
};

static void check_lift_item(void)
{
    const unsigned char item = nes_ram[0x0505u];
    unsigned char saved_cur;
    if (item == 0u) return;
    nes_ram[0x0506u] = (unsigned char)(nes_ram[0x0506u] - 1u);
    if (nes_ram[0x0506u] == 0u) {
        /* EndLinkLiftingItem. */
        nes_ram[0x00ACu] = 0u;
        nes_ram[0x0505u] = 0u;
        if (nes_ram[0x0010u] != 0u && nes_ram[0x0010u] < 10u)
            nes_ram[0x0600u] = k_level_song_ids[nes_ram[0x0010u]];
        return;
    }
    nes_ram[0x00ACu] = 0x40u;                         /* halted */
    nes_ram[0x0070u + 19u] = nes_ram[0x0070u];        /* ObjX+19 */
    nes_ram[0x0084u + 19u] = (unsigned char)(nes_ram[0x0084u] - 0x10u);
    saved_cur = nes_ram[0x0340u];
    nes_ram[0x0340u] = 0x13u;
    nes_ram[0x0504u] = (unsigned char)(nes_ram[0x0504u] + 1u);
    enemy_render_weapon_reset(0x13u);
    draw_animate_item_object(item, 0x13u);
    nes_ram[0x0504u] = (unsigned char)(nes_ram[0x0504u] - 1u);
    nes_ram[0x0340u] = saved_cur;
    /* ProcessedNarrowObj ($52): Anim_WriteItemSprites zeroes it, then
     * counts a half-width item (one-hand lift). */
    roomrom_sprites_set_link_lift(players[0].x, players[0].y,
                                  (unsigned char)(nes_ram[0x0052u] != 0u));
}

static unsigned char play_update_objects(void)
{
    roomrom_combat_update(players[0].x, players[0].y, players[0].face);
    roomrom_boomerang_update(players[0].x, players[0].y);
    roomrom_arrow_update();
    roomrom_bomb_update();
    roomrom_candle_fire_update();
    roomrom_link_damage_tick((unsigned char)s_frame_counter);
    /* Tier 2: drive Power Triforce fanfare. core_take_power_triforce
     * (item_dispatch.c:48) sets POWER_TRIFORCE_FANFARE_FLAG +
     * CURTAIN_TIMER on pickup; this check ticks the curtain +
     * palette ash-replace + ITEM_SFX_SECONDARY each frame until
     * the timer expires. Without this call the fanfare flag
     * never clears and triforce pickup has no visible/audible
     * effect. */
    progress_check_power_triforce_fanfare();
    /* Phase 7 substrate fix 2026-05-15 â€” clear NES OAM mirror
     * + reset RollingSpriteIndex AT FRAME START. NES Z1 NMI
     * resets RollingSpriteIndex per frame; without that
     * reset, sprite writes accumulate across frames + the
     * cumulative OAM eats SAT slot budget after 1-2 frames.
     * Each enemy then renders as the LEFT half only (right
     * halves bumped beyond SAT slot 79 by stale records).
     * Clear-before-draw mirrors the NES NMI sentinel pass. */
    enemy_render_reset_oam();
    /* T-056: the ladder CheckLadder drew during UpdatePlayer (the cache
     * clear above would have dropped it). */
    link_ladder_draw();
    /* Plan v5c T6.5 â€” mini-map position marker flash + room
     * change refresh. Per NES Z_01.asm:4095-4146 the marker
     * flashes every 16 frames keyed on FrameCounter ($0015). */
    /* T-132: no map dot during the curtain (mode 3); mode 4 draws it. */
    if (s_lvl_phase == LVL_NONE || s_lvl_phase == LVL_STEP_OUT ||
        s_lvl_phase == LVL_PLAY_INIT)
        roomrom_hud_refresh_marker(nes_ram[0x00EBu],   /* RoomId */
                                   (unsigned char)(s_scene == SCENE_UW),
                                   nes_ram[0x0015u]);
    /* Phase 7 root-cause fix #6 2026-05-16 â€” sync C-side
     * players[0] and s_room_id into NES_RAM cells before
     * gameplay tick. NES Z1 native code reads these cells
     * directly; without the sync collision detection thinks
     * Link is at (0,0), enemy AI sees RoomId=0 (overworld
     * starting room) regardless of where the player is, and
     * room-specific behaviors all key off wrong room.
     *
     * NES Variables.inc: ObjX[0]=$0070, ObjY[0]=$0084 (Link
     * is slot 0 in the per-slot arrays), RoomId=$00EB.
     * CurLevel ($0010) and Link face ($008C ObjDir[0]) are
     * already seeded by roomrom_debug_enter; refresh here
     * each frame in case they get out-of-sync with the C
     * source-of-truth. */
    nes_ram[0x0070u] = (unsigned char)players[0].x;
    nes_ram[0x0084u] = (unsigned char)players[0].y;
    /* SENTINEL $07F9 = s_room_id at tick-start sync (each tick) */
    DBG_SENTINEL(0x19u) = s_room_id;
    nes_ram[0x00EBu] = s_room_id;

    /* Tier 0 (plan v6) cave-entrance detection. Only fires in
     * SCENE_OW. Looks up tile under Link via existing collision
     * primitive (collision_get_collidable_tile_still slot 0 =
     * Link). NES Z_05.asm:7320-7327 entrance tile set: $24
     * armos warp, $88 rock-pile, $70-$73 stairs. On positive
     * hit: save OW return state, transition to SCENE_CAVE,
     * call cave_init. cave_exit (existing C+START chord)
     * restores OW + return position. */
    /* NES Z_05.asm:7240-7244 alignment gate: only check cave-
     * entrance tile when (ObjY & $0F) == $0D. Link must be
     * sub-tile-aligned (Y low nibble = $D = 13 px into a
     * 16-px row) before warp-tile check runs. Without this
     * gate, Genesis triggers at first row=3 frame (Y=$54,
     * low nibble $4 = off-center, Link beside arch) instead
     * of waiting until Y=$4D (low nibble $D = centered under
     * arch entrance metatile). Verified via NES probe: NES
     * triggers at Y=$4D, Genesis was triggering at Y=$54.
     *
     * APPENDIX C3 fix: ALTTP move-style (8.8 fixed-point,
     * ~1.5 px/frame, diagonal allowed) can SKIP exact $0D
     * value. Widen tolerance to $0C..$0E (3-px window) so
     * ALTTP players still trigger cave entry. NES-style stays
     * exact ($0D only) by virtue of 1-px/frame walk speed
     * matching the gate. */
    unsigned char y_low = (unsigned char)((unsigned char)players[0].y & 0x0Fu);
    unsigned char gate_pass = (s_move_style == MOVE_STYLE_ALTTP)
        ? (unsigned char)(y_low >= 0x0Cu && y_low <= 0x0Eu)
        : (unsigned char)(y_low == 0x0Du);
    /* T-171: the rest of NES CheckWarps (Z_05.asm:7213): not just out of
     * underground, grid offset 0, and X on the square grid ($10; 8 in OW
     * room $22, the wide L6 entrance). Without the X gate Link warped
     * from beside the stairs and was snapped onto them (NES capture, OW
     * $4B: walking left from $C0 the NES moves on, the Genesis warped at
     * once). The ALTTP move style keeps its snap below. */
    if (s_move_style != MOVE_STYLE_ALTTP) {
        const u8 x = (u8)players[0].x;
        const u8 x_mask = (s_room_id == 0x22u) ? 0x07u : 0x0Fu;
        if ((x & x_mask) != 0u || s_link_grid_offset != 0 || nes_ram[0x005Au] != 0u)
            gate_pass = 0u;
    }
    /* UpdatePlayer returns while Link is halted (ObjState & $C0 = $40):
     * no Link_EndMoveAndAnimate, so no CheckWarps (T-171: the whirlwind
     * carries a halted Link across the screen; t171_flute_whirlwind t290
     * recorded UndergroundEntranceTile on the way). */
    /* CheckWarps keeps the exit latch until Link completes a tile step.
     * The native cave shortcut must obey the same gate as dungeon entry;
     * otherwise an idle Link on the exit pad immediately descends again. */
    if (s_scene == SCENE_OW && gate_pass &&
        nes_ram[0x005Au] == 0u && s_link_grid_offset == 0 &&
        (nes_ram[0x00ACu] & 0xC0u) != 0x40u) {
        /* @CheckWarps keeps ObjCollidedTile across CheckWarps (PHA/PLA). */
        const u8 coll_saved = nes_ram[0x049Eu];
        unsigned char standing_tile =
            collision_get_collidable_tile_still(0u);
        nes_ram[0x049Eu] = coll_saved;
        record_warp_tile_ow();
        /* Tier 0 verify sentinel: $07FD = last standing tile
         * Link was on. Helps debug entrance detection. */
        DBG_SENTINEL(0x1Du) = standing_tile;
        cave_id_t cid = cave_entrance_check(standing_tile, s_room_id);
        if (cid != (cave_id_t)0) {
            /* HandleWarpOW saves the current room's kill count before
             * SetTargetMode; cave departures must retain that low flag. */
            room_save_kill_count_ow(s_room_id);
            /* NES enters caves ONLY at a $10-aligned column:
             * HandleWarpOW/CheckWarps gate on ObjX&$0F==0
             * (Z_05.asm:7213), so the descent (Mode $10) and
             * InitModeB both run at the 16px-grid column ($70),
             * never a mid-grid X. The Gen gate fires at Link's
             * exact walk X ($78), so snap to the NES warp grid
             * HERE -- before the descent mirror (line ~475), the
             * arch hi-prio marking, and the saved exit-return
             * column -- so ObjX byte-matches NES ($78 & $F0 = $70)
             * across the whole descend/hold/emerge window. */
            players[0].x =
                (short)((unsigned char)players[0].x & 0xF0u);
            /* Mirror the aligned X into nes_ram ObjX[0] NOW: line
             * ~2146 already ran this tick with the pre-snap $78, and
             * the gated player->nes_ram sync is suppressed once
             * cave_fade is active (descend handler owns it) -- so
             * without this the mirror holds the stale $78 for the few
             * setup frames before the first descend_step. */
            nes_ram[0x0070u] = (unsigned char)players[0].x;
            /* Tier 0 verify sentinel: $07FC = cave-entry fire
             * counter. Increments each time cave entry triggers
             * so the smoke probe can confirm entrance path ran
             * (separate signal from $0350 which is aliased to
             * enemy slot 1 type). */
            DBG_SENTINEL(0x1Cu) =
                (unsigned char)(DBG_SENTINEL(0x1Cu) + 1u);
            s_cave_return_room = s_room_id;
            s_cave_entrance_tile = standing_tile;   /* T-135 */
            nes_ram[0x0065u] = standing_tile;   /* HandleWarpOW */
            s_cave_return_face = players[0].face;
            s_cave_return_x    = (unsigned char)players[0].x;
            s_cave_return_y    = (unsigned char)players[0].y;
            /* Tier 1: hand off to cave_fade sequencer.
             * Mark 2x2 BG cells around cave-entrance arch with
             * high priority so Link sprite (prio=0) renders
             * BEHIND the arch lip during descend â€” NES sprite-
             * priority effect. plane row = (Y >> 3) + 7 HUD. */
            mark_link_behind_bg();  /* UpdateMode10Stairs_Full, all entrances */
            /* NES InitMode10 fires the stairs SFX only for a $24 pad
             * (EffectRequest=$08, Z_05.asm:1400). Play the
             * Genesis synth stairs SFX (#8 -> XGM id 71). We do NOT
             * mirror the NES EffectRequest cell ($0603): on NES it is a
             * transient request the sound engine consumes+clears next
             * frame ($80->$08->$00); the SGDK-XGM path has no such cell,
             * so a static write would DIVERGE worse than leaving it. SFX
             * parity is functional (audio_requests + the $07F0 sentinel);
             * EffectRequest is REPORTED, not byte-gated (like GameMode). */
            if (standing_tile == 0x24u)
                nes_ram[0x0603u] |= 0x08u;   /* InitMode10 stairs effect */
            cave_fade_set_callbacks(&k_cave_fade_callbacks);
            nes_ram[0x0012u] = 0x10u;         /* T-011: NES mode $10 stairs */
            end_prepare_mode();
            cave_fade_begin_enter(cid, standing_tile);
            inventory_rupee_tick(nes_ram[0x0015u]); /* @FinishUpdatePlay */
            return 1u;
        }
    }

    /* Plan v5a T1.2 + T1.3 â€” refresh heart cells + Link face
     * each tick. ObjDir[0] was seeded once at debug-enter, but
     * goes stale on any C-side face change; AI chase targets
     * read this cell every frame. Hearts mirror inventory so
     * any future NES HUD-readout consumer sees live values. */
    inventory_sync_from_native();
    nes_ram_sync_link_face();
    /* T-013: the vanilla sword runs in the NES cells ($0D slot) itself;
     * this copy of the swing-start facing is for the Redux swing only
     * (it undid a mid-swing turn: sword ObjDir $08 vs NES $02). */
    if (roomrom_main_current_redux_flag()) nes_ram_sync_sword();

    /* Tier 0 (plan v6) pause gate: when g_paused != OFF, skip
     * gameplay tick (enemy AI + collision + Link state machine).
     * NES Z_07.asm:472 Paused dispatch matches: gameplay update
     * skipped, status-mode draw still runs (HUD redraw below
     * stays active). Bare-START toggle at line 2052 flips the
     * flag; potion-drink involuntary pause cleared by
     * HeartPartial fill (Phase 6.10 deferral). Without this gate
     * enemy AI keeps running while player thinks game is paused
     * = death-while-paused bug. */
    if (!roomrom_pause_is_active()) {
        enemy_loop_tick();
        /* T-119: NES UpdateMode5Play runs CheckShutters +
         * UpdateDoors after the object loop (Z_07.asm:1983). */
        enemy_loop_play_tail(s_scene == SCENE_UW ? 1u : 0u);
        if (s_scene == SCENE_UW) uw_door_state_update();
    }
    /* @TransferStatusBarMap (Z_07.asm:2011), before @FinishUpdatePlay's
     * room item (a map taken this frame is shown next frame): with no dynamic transfer
     * pending, a set StatusBarMapTrigger is cleared and the level's status
     * bar map cued ($44; the Genesis HUD draws the map itself) (T-171:
     * t013_route t6987, the trigger never cleared). */
    if (nes_ram[0x0301u] == 0u && nes_ram[0x04E5u] != 0u) {
        nes_ram[0x04E5u] = 0u;
        nes_ram[0x0014u] = 0x44u;
    }
    /* NES source: Z_04.asm:Ganon_Dying; Z_01.asm:TryTakeRoomItem;
     * Z_07.asm AnimateItemObject (drained draw_animate_item_object).
     * Slot 19 owns live state/position; Ganon moves the Power Triforce to
     * his ashes after room entry. T-130: drawn by the NES item writers
     * (ItemIdToSlot -> frame tile -> Anim_WriteSpecificItemSprites) into the
     * render cache instead of a per-item-id Genesis table (the key, $19,
     * fell back to a boomerang placeholder there). */
    enemy_render_weapon_reset(0x13u);
    check_lift_item();               /* T-011: NES order, before the room item */
    /* Z_07.asm MoveAndDrawRoomItem: not taken, ObjState[$13] active and
     * RoomItemId $AB != $3F. A like-like ($17), stalfos ($2A) or gibdo
     * ($30) in object slot 1 carries the item: it takes that monster's
     * position and draws as object 1 (t131_uw_doors room $74: the key
     * follows the stalfos from ($50,$BD)). */
    if (s_scene == SCENE_UW && !roomrom_uw_item_taken(s_room_id) &&
        (nes_ram[0x00BFu] & 0x80u) == 0u && nes_ram[0x00ABu] != 0x3Fu) {
        unsigned char saved_cur = nes_ram[0x0340u];
        unsigned char t1 = nes_ram[0x0350u];     /* ObjType+1 */
        unsigned char obj = 0x13u;
        if (t1 == 0x17u || t1 == 0x2Au || t1 == 0x30u) {
            nes_ram[0x0083u] = nes_ram[0x0071u]; /* ObjX+19 = ObjX+1 */
            nes_ram[0x0097u] = nes_ram[0x0085u]; /* ObjY+19 = ObjY+1 */
            obj = 0x01u;
        }
        nes_ram[0x0340u] = obj;                  /* CurObjIndex */
        draw_animate_item_object(nes_ram[0x00ABu], obj);
        nes_ram[0x0340u] = saved_cur;
        /* Taken: MoveAndDrawRoomItem drew it before TryTakeRoomItem, so it
         * stays on screen this frame (t013_route t6987 map); the reset at
         * the top of the next tick removes it. */
        (void)roomrom_uw_item_try_pickup(
                roomrom_uw_room_render_get_level(),
                s_room_id, (unsigned char)players[0].x,
                (unsigned char)players[0].y);
    }
    /* NES MoveAndDrawRoomItem / TryTakeRoomItem also run on the OW: the
     * room-$24 Armos activates slot 19 with item $14 (bracelet), and
     * CreateRoomObjects in mode 5 activates the room-$5F heart container
     * (T-056 t056_ladder_ow; this was special-cased to room $24). */
    if (s_scene == SCENE_OW &&
        (nes_ram[0x00BFu] & 0x80u) == 0u && nes_ram[0x00ABu] != 0x3Fu &&
        progress_get_room_flag_uw_item_state() == 0u) {
        unsigned char saved_cur = nes_ram[0x0340u];
        unsigned char t1 = nes_ram[0x0350u];     /* ObjType+1 */
        unsigned char obj = 0x13u;
        if (t1 == 0x17u || t1 == 0x2Au || t1 == 0x30u) {
            nes_ram[0x0083u] = nes_ram[0x0071u];
            nes_ram[0x0097u] = nes_ram[0x0085u];
            obj = 0x01u;
        }
        nes_ram[0x0340u] = obj;
        draw_animate_item_object(nes_ram[0x00ABu], obj);
        nes_ram[0x0340u] = saved_cur;
        cave_try_take_room_item();
        if (progress_get_room_flag_uw_item_state() != 0u)
            inventory_hud_mark_dirty();          /* drawn this frame, as above */
    }
    /* NES UpdateMode5Play ends in UpdateHeartsAndRupees (Z_07.asm:2033),
     * after the object loop and TryTakeRoomItem: a rupee picked up this
     * frame is counted this frame (t013_route t6773). */
    inventory_rupee_tick(nes_ram[0x0015u]);
    return 0u;
}

/* Mode dispatch, audio routing, sprite sweep and transfer-buffer
 * drain: run every gameplay frame after the objects. */
static void play_finish(void)
{
    /* Phase 7 root-cause fix #5b 2026-05-16 â€” restore GameMode
     * ($0012) before dispatch. game_main.c probe_check
     * stamps RAM($0012)=$CD as an A4-readback sentinel each
     * frame after roomrom_debug_tick returns; the next frame's
     * mode_dispatch_update would see $CD -> default no-op
     * branch -> Mode 5 Play body never fires. Restore Mode 5
     * (Play) before dispatch consumes the cell. probe_check
     * sentinel write still verifies A4 readback per
     * tools/build/test_debug_contract.py contract; gameplay
     * just normalizes the cell before use. */
    /* Plan v5a T3.1 â€” gate sentinel restore behind
     * s_in_gameplay. Future Mode 3/4 (Unfurl/Enter) ports
     * would otherwise see $CD->$05 force-snap mid-transition.
     * Restore only when debug_enter has handed off to the
     * gameplay loop. */
    if (s_in_gameplay && nes_ram[0x0012u] == 0xCDu) {
        nes_ram[0x0012u] = (s_scene == SCENE_CAVE) ? 0x0Bu : 0x05u;
        nes_ram[0x0013u] = 0x00u;
    }
    /* Plan v5b Tier-5 T5.5 â€” audio dispatcher: gamemode+scene
     * tuple change -> single music_play() per audio_routing.md.
     * Edge-fires only; same-tuple frames are silent. Per-tick
     * cost: 3 byte compares. Idempotent: re-entry safe via
     * audio_dispatch_reset() in roomrom_debug_enter. MUST run
     * AFTER GameMode-$CD restore â€” otherwise audio sees raw
     * a4_probe sentinel ($CD) every frame, falls to default
     * branch, never resolves to gameplay song. */
    audio_dispatch_tick((unsigned char)s_scene, s_room_id);
    mode_dispatch_update();
    /* 2026-05-15 perf: switched from enemy_render_sweep_oam_to_sat
     * (iterated 64 NES OAM entries â†’ up to ~50 SAT writes/frame,
     * costing ~30% frame budget) to enemy_render_native_sweep
     * (iterates 11 alive ENEMY_LOOP slots â†’ up to 11 SAT writes).
     * anim_write_sprite_drained latches per-slot tile/attrs/x/y
     * into a side-channel cache; native sweep emits 1 SAT entry
     * per alive enemy from the cache. NES OAM scatter still
     * happens for downstream compat but is no longer consumed. */
    /* Select the room's sprite producer separately from CHR residency.
     * Patra publishes native pairs; sweeping its OAM too duplicates them.
     * Other bosses retain manual OAM through splits and death. */
    {
        unsigned char boss_room = (unsigned char)(s_scene == SCENE_UW &&
                                                  enemy_render_needs_oam_sweep());
        if (boss_room) enemy_render_sweep_oam_to_sat();
        else if (s_lvl_phase == LVL_NONE) enemy_render_native_sweep();
        /* T-132: NES modes $10/2/3 update no objects; their sprites stay. */
    }

    /* Plan v5b â€” drain TRANSFER_BUF after all gameplay writers
     * have committed. world_animate_world_fading, Mode 11 dead-
     * Link palette cue, room column-attr writes all stage
     * records into nes_ram[$0301..]; without this drain the
     * records land in a dead buffer and Genesis CRAM never
     * updates. Bounded walk; $3F (palette) entries forward to
     * render_cram_write_color, $20-$2F (nametable) entries are
     * skipped pending plane bridge. */
    /* T-172: NES UpdateHeartsAndRupees ends the play update and its
     * status-bar records show after the next NMI: refresh the HUD after
     * every writer of this tick (it ran before the objects, so damage
     * showed a frame late and pickups before them a frame early). */
    roomrom_hud_refresh_play();
    transfer_buf_drain();
}

/* NES NMI frame work (Z_07.asm:468-515): timers, then Random. */
static void nes_frame_timers_and_random(void)
{
    /* Phase 7 root-cause fix #2 2026-05-16 â€” port NES Z_07.asm:468
     * @UpdateTimers from the NES NMI handler. Per-frame decrement
     * of every non-zero byte in NES $26..$3C (StunCycle through
     * FluteTimer, including DoorTimer $27, ObjTimer $28..$33 for
     * 12 slots, ObjTimer+1 $29..$34 hi-bytes, ObjStunTimer $3D..
     * (StunCycle gates the extra range) so enemy state machines
     * tick + transitions fire. Without this loop NES Z1 timers
     * froze: enemies stuck in animation phase 0, doors stuck open,
     * stun never released, flute timer perma-locked.
     *
     * StunCycle ($26) wraps every 9 frames; on wrap, extends the
     * loop range up to $4E (ChaseLongTimer + others) per NES asm.
     *
     * T-013: NES @UpdateTimers skips the whole block while MenuState or
     * Paused is set (Z_07.asm:469); the Genesis pause flag is both (the
     * pause menu). Timers ran through the menu (t013_save StunCycle $26
     * t62, ChaseLongTimer $4A t71). */
    if (!roomrom_pause_is_active() && nes_ram[0x00E0u] == 0u) {
        unsigned char loop_end;
        unsigned char stun = nes_ram[0x0026u];
        stun = (unsigned char)(stun - 1u);
        nes_ram[0x0026u] = stun;
        if ((signed char)stun >= 0) {
            loop_end = 0x3Cu;  /* short loop $3C..$27 */
        } else {
            nes_ram[0x0026u] = 0x09u;  /* reset stun cycle */
            loop_end = 0x4Eu;  /* extended loop $4E..$27 */
        }
        /* T-125: pointer walk (independent cells, order-free). T-172: four
         * cells at a time skip when all are 0 ($28..$3B and $3C..$4B are
         * long-aligned); most timers are idle in a busy room too. */
        {
            unsigned char *t = (unsigned char *)(unsigned long)&nes_ram[0x0027u];
            unsigned char *const end = (unsigned char *)(unsigned long)
                &nes_ram[(unsigned short)loop_end + 1u];
            if (*t != 0u) --*t;                       /* $27 */
            ++t;
            while (t + 4 <= end) {
                if (*(const unsigned long *)(const void *)t != 0u) {
                    if (t[0]) --t[0];
                    if (t[1]) --t[1];
                    if (t[2]) --t[2];
                    if (t[3]) --t[3];
                }
                t += 4;
            }
            while (t != end) {
                if (*t != 0u) --*t;
                ++t;
            }
        }
    }

    /* Phase 7 root-cause fix #3 2026-05-16 â€” port NES Z_07.asm:499
     * @ScrambleRandom from the NES NMI handler. The drained gameplay
     * loop never advanced NES Random[$18..$24], so every drop-table
     * roll, AI direction roll, item-spawn coin flip pulled the same
     * value forever. Symptom: identical drops every kill, enemy AI
     * directionality biased / locked.
     *
     * Discard return value â€” NES NMI scramble runs unconditionally
     * regardless of whether anyone reads the result. Same effect. */
    (void)rng_next();
}

static u32 s_tick_vtimer = 0u;   /* T-125: vtimer at tick start */

void roomrom_debug_tick(void)
{
        /* DEBUG SENTINEL â€” capture players[0]+s_room_id at TOP-of-tick
         * BEFORE any per-frame work. If post-Mode values ($78,$DD,$73)
         * are preserved here but reverted by line 1745 sync, the writer
         * lives inside this function. */
        DBG_SENTINEL(0x15u) = (unsigned char)players[0].x;
        DBG_SENTINEL(0x16u) = (unsigned char)players[0].y;
        DBG_SENTINEL(0x17u) = s_room_id;
        s_tick_start_link_y = players[0].y;   /* T-011: cave edge check */
        s_tick_start_grid  = s_link_grid_offset;
        s_tick_start_frac  = s_link_pos_frac;
        s_tick_start_anim  = nes_ram[0x03D0u];
        s_tick_start_frame = nes_ram[0x03E4u];
        /* T-125 frame budget probe, in the 6502 stack page (unused by the
         * port, masked by the lockstep diff): $01FE = frames the previous
         * tick ran past its own (0 = it fit), $01FF = VDP V counter when
         * it finished. */
        nes_ram[0x01FFu] = (u8)(*(volatile u16 *)0xC00008u >> 8);
        nes_ram[0x01FEu] = (u8)(vtimer - s_tick_vtimer);
        /* NES frame work follows a new NMI. Wait for one hardware frame,
         * then service its current blank rather than forcing another
         * edge if SGDK's wait setup crossed it (T-172 ladder near $DF).
         * The vtimer guard prevents two game ticks in one blank; late CPU
         * work still costs hardware frames and remains visible to probes. */
        while (vtimer == s_tick_vtimer) { /* SGDK volatile V-Int counter. */ }
        SYS_doVBlankProcessEx(ON_VBLANK);
        render_plane_defer_flush();   /* T-172: last tick's transfer cells (NMI) */
        if (s_curtain_song_pending) {
            u8 song = s_curtain_song_pending;
            s_curtain_song_pending = 0u;
            audio_music_play(song);
        }
        roomrom_hud_play_flush();     /* T-172: last play tick's status bar */
        state_dump_poll(STATE_DUMP_CTX_GAME);   /* A+B+C+Start: freeze + dump */
        if (s_arch_restore_ticks && --s_arch_restore_ticks == 0u)
            cave_fade_restore_arch();           /* after a step out */
        if (circle_transition_active()) {
            /* Freeze load clocks as well as actors/input during the iris.
             * Their native load schedule starts again after full black. */
            const u32 elapsed = vtimer - s_tick_vtimer;
            s_tick_vtimer = vtimer;
            if (s_load_tl) s_load_vt0 += elapsed;
            s_lvl_exit_vt0 += elapsed;
            s_cave_exit_vt0 += elapsed;
            render_dma_stats_frame_end(); /* each iris tick is still a hardware frame */
            nes_pad_read_between_modes();
            circle_transition_tick();
            return;
        }
        s_tick_vtimer = vtimer;
        /* Phase Q v2: roll per-frame DMA byte tally into peak tracker
         * and reset accumulator for next frame. Probes read peak via
         * render_dma_stats_get(). */
        render_dma_stats_frame_end();
        s_frame_counter++;
        /* Phase 7 root-cause fix 2026-05-16 â€” port NES Z_07.asm:519
         * `INC FrameCounter` from the NES NMI handler. The drained
         * gameplay loop never advanced NES $0015, so every NES Z1
         * timing path that reads FrameCounter (sprite anim cadence,
         * hit-flash palette cycle, drop-RNG seed, cave-person draw
         * gate at Z_03.asm:328+, FrameCounter & 0x03 enemy palette,
         * &c.) saw a perma-zero value. Symptom: enemies render with
         * frozen palette, drop tables never roll, animation frames
         * lock to first frame, hit-flash invisible. Mirror the NMI
         * increment here in roomrom_debug_tick (the per-frame body)
         * so all consumers see a normal 0..$FF cycling counter. */
        if (!nes_load_clock())   /* T-171: a running NES load timeline owns it */
            nes_frame_nmi();

        roomrom_palette_tick_frame(s_frame_counter);
        /* PR-4a: advance scene-bank DMA state machine. Runs after
         * SYS_doVBlankProcess so the SGDK DMA queue is drained before
         * we issue our own ops. PR-5: boss state machine ticks in
         * sequence; the boss machine waits for pending enemy DMA because
         * both requests can originate in the same room-load frame. */
        /* PR-5 probe trigger: probe pokes a scene_id (1 byte) into
         * $FF73FE; we enqueue a boss request and clear the cell. The
         * matching ack byte at $FF73FF tracks how many requests we have
         * fired (probe can read it to know the request landed). Used
         * solely by tools/build/probe_boss_bank_dispatch.lua. */
        if (roomrom_debug_probe_flag(ROOMROM_DEBUG_PROBE_BOSS_TRIGGER)) {
            volatile unsigned char *trig = (volatile unsigned char *)0x00FF73FEUL;
            volatile unsigned char *ack  = (volatile unsigned char *)0x00FF73FFUL;
            unsigned char req = *trig;
            if (req != 0u) {
                level_chr_boss_request((roomrom_scene_id_t)req);
                *trig = 0u;
                *ack = (unsigned char)(*ack + 1u);
            }
        }
        level_chr_swap_tick();
        roomrom_scene_uw_sprite_base_tick();   /* T-129 */
        level_chr_boss_tick();
        if (s_scene == SCENE_UW && nes_ram[0x0012u] != 9u) uw_door_state_tick();

        if (cellar_mode_tick()) {
            nes_pad_read_between_modes();
            transfer_buf_drain();
            return;
        }

        /* T-111: InitMode7 Sub6 / InitMode4 Sub3 fades hold everything. */
        if (s_uw_fade_phase != UW_FADE_NONE) {
            nes_pad_read_between_modes();
            if (world_animate_world_fading() == 0u) {
                if (s_uw_fade_phase == UW_FADE_AFTER_SCROLL) nes_ram[0x051Fu] = 0u;
                s_uw_fade_phase = UW_FADE_NONE;
            }
            transfer_buf_drain();
            return;
        }

        /* NES InitMode3 for any GameMode 3 not started by a Genesis load
         * path (mode 8 CONTINUE, a game-over continue, a staged StartRoomId
         * reload as in the t054 presets): IsUpdatingMode 0 -> Sub0-8. */
        if (s_lvl_phase == LVL_NONE && nes_ram[0x0012u] == 0x03u &&
            nes_ram[0x0011u] == 0u) {
            s_lvl_exiting = 0u;
            s_lvl_phase = LVL_MODE3_INIT;
        }
        if (s_lvl_phase == LVL_NONE && nes_ram[0x0012u] == 0x02u &&
            nes_ram[0x0011u] == 0u) {
            s_lvl_exiting = 0u;
            s_lvl_phase = LVL_MODE2_INIT;
        }

        /* T-132: level entry (stairs, load, curtain). */
        if (s_lvl_phase != LVL_NONE) {
            nes_pad_read_between_modes();
            level_entry_tick();
            transfer_buf_drain();
            return;
        }

        /* T-013: GameMode $12 runs only its mode routine (NES UpdateMode
         * dispatch: no Link, objects or HUD input that frame). */
        if (nes_ram[0x0012u] == 0x12u) {
            nes_pad_read_between_modes();
            mode12_endlevel_update();
            transfer_buf_drain();
            return;
        }

        /* NES source: Z_07 UpdateMode dispatch -> Z_02 UpdateMode13WinGame.
         * Drained C: mode_wingame + existing sprite publishers.
         * Coverage: init, curtain, thanks, flash/peace text; credits/reset open.
         * Stance: EXTEND the exclusive mode branches, like modes $11/$12.
         * Ending never runs player movement, weapons, AI or HUD refresh. */
        if (nes_ram[0x0012u] == 0x13u) {
            u8 slot;
            u8 pose;
            nes_pad_read_between_modes();
            mode13_wingame_update();
            pose = mode13_wingame_draws_link();
            if (pose == 3u) { transfer_buf_drain(); return; }
            for (slot = ROOMROM_SPRITE_SLOT_LINK_FIRST;
                 slot <= ROOMROM_SPRITE_SLOT_GAMEPLAY_LAST; ++slot) {
                if (slot != ROOMROM_SPRITE_SLOT_LINK &&
                    slot != ROOMROM_SPRITE_SLOT_LINK_R)
                    VDP_setSpritePosition(slot, -32, -32);
            }
            if (pose == 1u)
                roomrom_sprites_set_link_lift((short)nes_ram[0x0070u],
                                             (short)nes_ram[0x0084u], 0u);
            else if (pose == 2u)
                roomrom_draw_link_from_nes(); /* Independent of prior death FX. */
            else
                roomrom_sprites_set_link_pose(-32, -32, players[0].face, 0u);
            enemy_render_native_sweep();
            VDP_updateSprites(80u, DMA_QUEUE);
            transfer_buf_drain();
            return;
        }

        /* T-097: GameMode $11 (Link died) runs only its mode routine; the
         * sprites follow the NES cells (roomrom_mode11_draw). */
        if (nes_ram[0x0012u] == 0x11u) {
            nes_pad_read_between_modes();
            mode11_death_update();
            roomrom_mode11_draw();
            transfer_buf_drain();
            return;
        }

        /* T-013 P2.6: GameMode 8 (continue / save / retry question) and
         * $0D (save) run only their mode routine too. Mode 8 leaves to
         * $0D (save), 0 submode 1 (retry: the Genesis File Select,
         * game_main.c) or 3 (continue: level reload + curtain). */
        if (nes_ram[0x0012u] == 0x08u || nes_ram[0x0012u] == 0x0Du) {
            nes_pad_read_between_modes();
            if (nes_ram[0x0012u] == 0x08u) mode8_continue_question_update();
            else mode13_save_update();
            transfer_buf_drain();
            if (nes_ram[0x0012u] == 0x03u) begin_mode8_continue();
            return;
        }

        /* T-057: Paused 2 (WieldPotion). NES @CheckMenuAndPause: no Link,
         * objects or timers (@UpdateTimers above); MaskCurPpuMaskGrayscale
         * and UpdateHeartsAndRupees only, sprites left as drawn. The
         * heart fill clears Paused when the hearts are full. */
        if (nes_ram[0x00E0u] == 2u) {
            nes_pad_read_between_modes();
            inventory_rupee_tick(nes_ram[0x0015u]);
            inventory_sync_from_native();
            roomrom_hud_refresh_dynamic();
            transfer_buf_drain();
            return;
        }

        /* S6.6 transition state machine. */
        if (s_scroll_state != SCROLL_NONE) {
            short h_scroll;
            short v_scroll;
            nes_pad_read_between_modes();   /* NES reads the pad; no player update */

            u8 ow_done = 0u;
            if (nes_scroll_enabled()) {
                u8 c;
                short distance;
                /* NES Link_EndMoveAndAnimate[BetweenRooms] calls in the
                 * mode/submode this frame runs (read before the tick):
                 * InitMode6 2 (DrawSpritesBetweenRooms + its own), mode 6
                 * update 1 unless the grid offset is 0/+-8 (next mode),
                 * InitMode7 Sub1 2, other InitMode7 submodes 0, mode 7
                 * update 1 (UpdateMode7SubmodeAndDrawLink). OW only; the
                 * BetweenRooms form returns in the UW. */
                u8 gm = nes_ram[0x0012u], sub = nes_ram[0x0013u], upd = nes_ram[0x0011u];
                u8 grid = nes_ram[0x0394u];
                u8 anims = 0u;
                u8 r;
                if (s_scene == SCENE_UW) {
                    /* UW Link_EndMoveAndAnimateBetweenRooms returns; only
                     * the mode 6/4 walk frames animate (below). */
                } else if (gm == 0x06u)
                    anims = upd ? ((grid == 0u || grid == 0x08u || grid == 0xF8u) ? 0u : 1u) : 2u;
                else if (gm == 0x07u)
                    anims = upd ? 1u : (sub == 1u ? 2u : 0u);
                (void)sub;
                r = ow_scroll_tick(&players[0].x, &players[0].y);
                {
                    u8 n = ow_scroll_take_catch_up();   /* T-145 */
                    while (n--) {
                        nes_ram[0x0015u] = (unsigned char)(nes_ram[0x0015u] + 1u);
                        nes_frame_timers_and_random();
                    }
                }
                ow_done = (u8)(r == OW_SCROLL_PLAY);
                while (anims--) roomrom_combat_end_move_and_animate();
                if (r == OW_SCROLL_WALK) {
                    /* UpdateMode4and6EnterLeave: MoveObject along ObjDir,
                     * then Link_EndMoveAndAnimate (no grid truncation
                     * outside mode 5). */
                    link_dir_t d = link_dir_of_lowest_bit(nes_ram[0x0098u]);
                    nes_ram[0x03F8u] = nes_ram[0x0098u];
                    s_link_grid_offset = (signed char)nes_ram[0x0394u];
                    s_link_pos_frac = nes_ram[0x03A8u];
                    s_link_dir = d;
                    link_nes_move_object_raw(d);
                    nes_ram[0x0394u] = (u8)s_link_grid_offset;
                    nes_ram[0x03A8u] = s_link_pos_frac;
                    nes_ram[0x0070u] = (u8)players[0].x;
                    nes_ram[0x0084u] = (u8)players[0].y;
                    roomrom_combat_end_move_and_animate();
                } else if (r == OW_SCROLL_ENTER) {
                    /* InitMode_EnterRoom: ResetPlayerState (Link ObjState,
                     * InvClock); a halted Link ($40, e.g. by an old man's
                     * text) otherwise kept that state into the next room. */
                    room_reset_player_state();
                    if (!s_lvl_enter_only) scroll_finalize_room();
                    /* Mode 4 InitMode_EnterRoom sets up objects again after
                     * mode 3 laid out the room. Continue can re-enter the
                     * same room, so bypass the scroll duplicate guard. */
                    else {
                        roomrom_hud_b_item_update();
                        enemy_loop_room_reenter(s_room_id, (unsigned char)s_scene,
                            s_scene == SCENE_UW ? roomrom_uw_room_render_get_level() : 0u,
                            s_scene == SCENE_UW ? roomrom_uw_room_render_get_quest() : 0u);
                        request_boss_chr_if_boss_room();
                    }
                    if (s_scene == SCENE_UW) {
                        ow_scroll_enter_room_uw(&players[0].x);
                        s_link_grid_offset = (signed char)nes_ram[0x0394u];
                        s_link_pos_frac = 0u;
                        /* UW InitMode_EnterRoom leaves Link's ObjAnimCounter
                         * at 4 (its BetweenRooms draw does not animate); the
                         * walk-in frames animate it. The OW draw animates
                         * once: room_init's 3. */
                        nes_ram[0x03D0u] = 4u;
                        nes_ram[0x0070u] = (u8)players[0].x;
                    }
                }
                c = ow_scroll_column();
                if (c < 16u) {
                    u8 slot = s_scroll_state < SCROLL_V_DOWN
                        ? (u8)(s_active_slot_x ^ 1u) : s_active_slot_x;
                    if (s_scene == SCENE_UW)
                        roomrom_uw_room_render_fill_one_col_at(s_transition_target,
                            c, plane_col_for_slot(c, slot), s_transition_row_base);
                    else
                        roomrom_ow_room_render_fill_one_col_at(s_transition_target,
                            c, plane_col_for_slot(c, slot), s_transition_row_base);
                }
                distance = (short)ow_scroll_display_pixels();
                h_scroll = s_scroll_start_x;
                v_scroll = s_scroll_start_y;
                if (s_scroll_state == SCROLL_H_RIGHT) h_scroll -= distance;
                if (s_scroll_state == SCROLL_H_LEFT) h_scroll += distance;
                if (s_scroll_state == SCROLL_V_DOWN) v_scroll += distance;
                if (s_scroll_state == SCROLL_V_UP) v_scroll -= distance;
                /* After the room is entered (mode 4 InitMode_EnterRoom,
                 * scroll_finalize_room above) the camera rests on the new
                 * room. Mode 4 Sub1-3 (dark-room re-copy, fade) run before
                 * it with IsUpdatingMode 0: the camera stays at the end of
                 * the scroll (it snapped back to the old room for those
                 * frames, transition_smooth.py: every transition +-176 /
                 * 256 px for 2 frames). */
                if (nes_ram[0x0012u] != 0x06u && nes_ram[0x0012u] != 0x07u &&
                    (s_lvl_enter_only || nes_ram[0x0012u] != 0x04u ||
                     nes_ram[0x0011u] != 0u)) {
                    h_scroll = s_active_scroll_x;
                    v_scroll = s_active_scroll_y;
                }
                enemy_render_reset_oam();
                enemy_render_native_sweep();
                s_link_frame = (u8)((nes_ram[0x03E4u] & 1u) ^
                    ((players[0].face == LINK_FACE_LEFT ||
                      players[0].face == LINK_FACE_RIGHT) ? 1u : 0u));
                if (s_scene == SCENE_UW && ow_scroll_link_hidden())
                    roomrom_sprites_set_link_pose((short)-32, (short)-32,
                                                  players[0].face, 0u);
                else
                    roomrom_sprites_set_link_pose(players[0].x,
                                                  ow_scroll_display_link_y(players[0].y),
                                                  players[0].face, s_link_frame);
                VDP_updateSprites(80u, DMA_QUEUE);
                transfer_buf_drain();            /* mode 7/4 fade palettes */
            } else scroll_advance_fixed_point(&h_scroll, &v_scroll);
            set_bg_scroll_with_sprites(h_scroll, v_scroll);
            /* NES Z1 UW: Link is drawn behind door tiles during the
             * scroll (Z_07.asm ShowLinkSpritesBehindHorizontalDoors).
             * We approximate by hiding the sprite off-screen for the
             * scroll duration, then snapping to the new-room entry
             * position on finalize. */
            if (!nes_scroll_enabled())
                roomrom_sprites_set_link_pose((short)-32, (short)-32,
                                              players[0].face, 0u);

            if (nes_scroll_enabled() && ow_done) {
                /* InitMode5Play: DrawSpritesBetweenRooms then
                 * Link_EndMoveAndAnimate, with ObjInputDir still the
                 * walk-in direction (T-171: t171_warp_l2 NES $03D0
                 * 5 -> 4 at FC $C4, t131_uw_doors t788). */
                roomrom_combat_end_move_and_animate();
                room_init_mode5_play_palette_row7();
                init_mode5_play_create_room_objects();
                /* RunCrossRoomTasks -> CheckInitWhirlwindAndBeginUpdate:
                 * a whirlwind teleport drops Link off here (T-171). */
                (void)trap_init_whirlwind_at_destination();

                if (s_lvl_enter_only) roomrom_hud_set_counts_hidden(0u);
                s_lvl_enter_only = 0u;
                /* T-131: the room was made current at mode 4 entry
                 * (InitMode_EnterRoom); the UW walk-in has ended. */
                /* ObjPosFrac carries over from the walk-in (NES). */
                s_link_grid_offset = 0;
                s_link_pos_frac = nes_ram[0x03A8u];
                s_link_subx = s_link_suby = 0u;
                nes_ram[0x0394u] = 0u;
                nes_ram[0x70u] = (u8)players[0].x;
                nes_ram[0x84u] = (u8)players[0].y;
                s_scroll_state = SCROLL_NONE;
                /* InitMode5Play's Link_EndMoveAndAnimate reaches
                 * @CheckWarps in mode 5 (T-171). */
                record_warp_tile_ow();
            } else if ((nes_scroll_enabled() && ow_done) ||
                (!nes_scroll_enabled() &&
                 (s_scroll_frame >= (u8)(s_scroll_total_frames - 1u) ||
                  (h_scroll == s_scroll_target_x && v_scroll == s_scroll_target_y)))) {
                scroll_finalize_room();
                s_scroll_state = SCROLL_NONE;
            } else {
                s_scroll_frame++;
            }
            return;
        }

        /* UpdateMode5Play: while the flute timer runs, nothing updates
         * (Z_07.asm:1772; T-171 the flute played over a live room). */
        if (nes_ram[0x0012u] == 0x05u && nes_ram[0x0011u] != 0u &&
            nes_ram[0x003Cu] != 0u && !roomrom_pause_is_active()) {
            nes_pad_read_between_modes();
            transfer_buf_drain();
            return;
        }

        /* T-111: UpdateMode5Play runs only UpdateCandle while the room
         * brightens (BrighteningRoom). */
        if (s_scene == SCENE_UW && uw_dark_brightening() &&
            !roomrom_pause_is_active()) {
            uw_dark_update_candle();
            transfer_buf_drain();
            return;
        }

        /* Tier 1 cave-fade tick: advance the descend / swap sequencer
         * each frame while active. Combat / enemy AI / cave-entry
         * detect are gated below on !cave_fade_is_active so they
         * freeze during the animation, but sprite render +
         * transfer_buf_drain still run so Link's per-frame Y bump
         * (descend step) is visible.
         *
         * APPENDIX R5 fix: gate cave_fade_tick on !pause. NES Z_07.asm:472
         * Paused != 0 freezes EVERYTHING including Mode 10 stairs anim.
         * Without this gate, pressing Start mid-descend freezes BG but
         * Link sprite continues descending = visual desync. */
        if (cave_fade_is_active() && !roomrom_pause_is_active()) {
            cave_fade_tick();
            /* InitMode10 runs one frame, then mode $10 updates (T-171). */
            if (nes_ram[0x0012u] == 0x10u) nes_ram[0x0011u] = 1u;
        }

        u16 joy = JOY_readJoypad(JOY_1);
        u16 pressed = joy & ~s_joy_prev;
        s_joy_prev = joy;
        /* DEBUG SENTINEL â€” publish raw joy + pressed bits into NES RAM
         * sentinel cells $07F0..$07F3 so probes can verify SGDK polling
         * captures 6-button (Mode/X/Y/Z) bits. Latched (not edge-only). */
        DBG_SENTINEL(0x10u) = (unsigned char)(joy & 0xFFu);
        DBG_SENTINEL(0x11u) = (unsigned char)((joy >> 8) & 0xFFu);
        if (pressed) {
            DBG_SENTINEL(0x12u) = (unsigned char)(pressed & 0xFFu);
            DBG_SENTINEL(0x13u) = (unsigned char)((pressed >> 8) & 0xFFu);
        }

        /* Phase 9 Task 9.4 â€” OPTION_ID_AB_SWAP: swap A and B button bits
         * after edge-detect so the entire downstream input dispatch sees
         * a single consistent button-mapping. s_joy_prev keeps raw bits
         * so direction-mask helpers (input_mask_from_buttons) remain
         * unaffected; only A/B-using sites in this function pick up the
         * swap. Toggling the option mid-session may cause one transient
         * frame of edge-detect skew, accepted as the simplest impl. */
        if (options_consumer_get_ab_swap()) {
            u16 ab_mask = (u16)(BUTTON_A | BUTTON_B);
            u16 joy_ab = (u16)(joy & ab_mask);
            u16 pressed_ab = (u16)(pressed & ab_mask);
            u16 joy_ab_swap = 0u;
            u16 pressed_ab_swap = 0u;
            if (joy_ab & BUTTON_A) joy_ab_swap |= (u16)BUTTON_B;
            if (joy_ab & BUTTON_B) joy_ab_swap |= (u16)BUTTON_A;
            if (pressed_ab & BUTTON_A) pressed_ab_swap |= (u16)BUTTON_B;
            if (pressed_ab & BUTTON_B) pressed_ab_swap |= (u16)BUTTON_A;
            joy = (u16)((joy & ~ab_mask) | joy_ab_swap);
            pressed = (u16)((pressed & ~ab_mask) | pressed_ab_swap);
        }

        /* Plan v5a T1.1 â€” mirror post-swap joypad bits into NES
         * $00F8 (ButtonsPressed, edge) / $00FA (ButtonsDown, held).
         * Offsets per Variables.inc:74-75 + z_07.asm:124-125 (plan v5
         * listed $FA/$FB; drain wins per CLAUDE.md). Post-AB-swap so
         * downstream NES native consumers see the same A/B contract
         * the C side just acted on. `pressed` derived from the
         * original s_joy_prev above (already overwritten with `joy`)
         * and re-swapped with `joy` if AB swap is active. */
        if (cave_fade_is_active()) {
            /* T-134: NES modes $10/$0B (stairs, cave load) read the pad
             * but run no UpdatePlayer, and ObjInputDir ($3F8) is only set
             * in mode 5: holding a direction must not move Link against
             * the descent. */
            unsigned char input_dir = nes_ram[0x03F8u];
            nes_ram_sync_input(joy, pressed);
            nes_ram[0x03F8u] = input_dir;
            joy = 0u;
            pressed = 0u;
        } else {
            nes_ram_sync_input(joy, pressed);
        }
        nes_pad2_read();

        /* NES source: Z_07.asm:UpdateMode5Play @CheckMenu.
         * Drained C: existing inventory_subscreen_tick + pause owner.
         * Coverage: PARTIAL (T-165 weapons ran before the menu gate).
         * Stance: EXTEND this required host input handoff. Keep pad polling,
         * Start and menu navigation; skip gameplay/debug actions while paused. */
        if (roomrom_pause_is_active()) goto handle_pause_start;

        /* SCENE_CAVE harness: tick the native cave gamemode each frame.
         * C+START exit chord hands off to cave_fade sequencer. Other
         * input (D-pad movement, bare-START pause/inventory) falls
         * through to the standard input handlers below so Link can
         * walk in the cave room + open the inventory subscreen.
         *
         * Tier 1 fix 2026-05-22: was returning early after cave_tick,
         * which froze Link sprite + suppressed all input. NES caves
         * allow walking + pause/inventory, only sword swing is
         * swallowed (handled by combat_link_locked check below). */
        if (s_scene == SCENE_CAVE) {
            /* The cave person updates in the object loop (T-133). */
            if (g_debug_session && (pressed & BUTTON_START) && (joy & BUTTON_C)) {
                cave_fade_set_callbacks(&k_cave_fade_callbacks);
                cave_fade_begin_exit(s_cave_return_room);
                return;
            }
            /* Fall through to D-pad / pause / sprite render below. */
        }

        /* Interactive-test gate. When probe Lua writes 'IT' magic at
         * $FF77F0..1, X/Y/Z/Mode chord handlers are suppressed so the
         * user's physical Y/Z input pass through to probe verdict
         * capture without flipping move-style / scene / cave. */
        {
            volatile unsigned char *it_arm = (volatile unsigned char *)0x00FF77F0UL;
            if (it_arm[0] == 0x49u && it_arm[1] == 0x54u) {
                /* In interactive-test mode: no-op X/Y/Mode/scene-chord
                 * handlers. Joypad still polls; probe Lua reads Y/Z
                 * presses each frame. */
                goto skip_debug_handlers;
            }
        }

        if (g_debug_session && (pressed & BUTTON_X)) {
            s_mode = (s_mode == MODE_WALK) ? MODE_TELEPORT : MODE_WALK;
            return;
        }

        if (g_debug_session && (pressed & BUTTON_Y)) {
            s_move_style = (s_move_style == MOVE_STYLE_NES)
                         ? MOVE_STYLE_ALTTP : MOVE_STYLE_NES;
            /* Reset sub-pixel/grid state so style switch is clean. */
            s_link_pos_frac = 0u;
            s_link_subx = 0u;
            s_link_suby = 0u;
            s_link_grid_offset = 0;
            s_link_dir = LINK_DIR_NONE;
            return;
        }

        /* Task 5.5 debug stub (UW only): B+Z held + C edge-press -> bomb
         * stub: open BOMBABLE door in Link's facing direction via
         * uw_door_state_open_by_mask. (A+B+C+START is the state dump,
         * state_dump_poll at the top of the tick; the shutter stub it
         * replaced left s_uw_shutter_trigger_count at 0.) */
        if (g_debug_session && s_scene == SCENE_UW && (pressed & BUTTON_C) &&
            (joy & BUTTON_B) && (joy & BUTTON_Z)) {
            unsigned char dir;
            switch (players[0].face) {
                case LINK_FACE_RIGHT: dir = DOOR_DIR_E; break;
                case LINK_FACE_LEFT:  dir = DOOR_DIR_W; break;
                case LINK_FACE_UP:    dir = DOOR_DIR_N; break;
                case LINK_FACE_DOWN:  dir = DOOR_DIR_S; break;
                default:              dir = DOOR_DIR_S; break;
            }
            uw_door_state_open_by_mask((unsigned char)DOOR_DIR_BIT(dir));
            return;
        }

        /* C held + START press = SCENE_CAVE toggle. Detected before the
         * START-alone branch so the chord doesn't fall through to the
         * regular OW<->UW toggle. cave_id 0x6A is the first valid NES
         * cave room type per Z_01.asm:80 â€” pick something deterministic
         * for the harness. */
        if (g_debug_session && (pressed & BUTTON_START) && (joy & BUTTON_C)) {
            if (s_scene == SCENE_OW) {
                const cave_id_t cid_toggle = (cave_id_t)0x6A;
                (void)cave_init(cid_toggle);
                s_scene = SCENE_CAVE;
                /* Task #44 â€” native NES cave column override (matches
                 * natural cave-entry path at line ~1817). Was previously
                 * painting OW room $6A tiles (lake/road); now uses
                 * RoomLayoutOWCave0/1 + OW room $44 palette per
                 * Z_05.asm:6628 InitModeB pipeline. */
                roomrom_cave_room_render_fill_plane_a((unsigned char)cid_toggle);
                roomrom_ow_room_render_publish_play_area_tiles();
                cave_palette_apply();
            } else if (s_scene == SCENE_CAVE) {
                cave_exit();
                s_scene = SCENE_OW;
                /* T0.2 â€” leaving cave: republish OW tiles on plane A
                 * via the existing full-room fill path. */
                roomrom_ow_room_render_fill_plane_a(s_room_id);
                roomrom_ow_room_render_publish_play_area_tiles();
                roomrom_ow_room_render_load_palette(s_room_id);
            }
            /* HUD underlay retired 2026-05-15: cave_init/cave_exit do not
             * pass through load_room, so re-assert the BG_A underlay so
             * the HUD plane keeps its opaque backing through the toggle. */
            clear_hud_underlay_for_row_base(s_active_row_base);
            return;
        }

        /* MODE edge-press = scene toggle (was START 2026-04..2026-05-08).
         * Frees START to behave like NES Start (pause/inventory/etc).
         * Z held + START = quest toggle (handled below). C held + START
         * handled above. */
        if (g_debug_session && (pressed & BUTTON_MODE) && !(joy & BUTTON_Z) && !(joy & BUTTON_C)) {
            /* DEBUG SENTINEL â€” count how many times the Mode handler enters. */
            DBG_SENTINEL(0x14u) = (unsigned char)(DBG_SENTINEL(0x14u) + 1u);
            s_scene = (s_scene == SCENE_OW) ? SCENE_UW : SCENE_OW;
            /* SENTINEL $07E8 = s_scene IMMEDIATELY AFTER toggle */
            DBG_SENTINEL(0x08u) = (unsigned char)s_scene;
            /* UW first room from NES LevelInfo_StartRoomId ($6BAD) seeded
             * by level_info_install_uw below. Bootstrap default = $73
             * (NES Z1 L1 Q1 StartRoomId) so first toggle paints a real
             * room even if the table-install race ever returns zeros. */
            if (s_scene == SCENE_UW) {
                /* G4: target UW level from debug cell $07FA (probe-poked via
                 * M68K BUS $FF8000+$07FA; default L1). $07F6/$07F7 are
                 * per-frame player-X/Y sentinels (main.c:1893-1894) so they
                 * get clobbered â€” use the free $07FA/$07FB pair. Start room
                 * is re-read from installed LevelInfo_StartRoomId ($6BAD)
                 * right after level_info_install_uw below; $73 is only the
                 * pre-install placeholder. */
                unsigned char uw_lvl = DBG_SENTINEL(0x1Au);
                if (uw_lvl < 1u || uw_lvl > 9u) uw_lvl = 1u;
                nes_ram[0x0010u] = uw_lvl;          /* CurLevel for the install */
                DBG_SENTINEL(0x1Bu) = uw_lvl;          /* echo for probe verify */
                s_room_id = 0x73u;
                /* Spawn Link at the south doorway of the entrance room
                 * (NES InitMode3_Sub2 entry: ObjX=$78, ObjY=$DD). */
                players[0].x = 0x78;
                players[0].y = 0xDD;
                players[0].face = LINK_FACE_UP;
                /* SENTINEL â€” proves UW first-block executed */
                DBG_SENTINEL(0x00u) = 0xAAu;
                DBG_SENTINEL(0x01u) = (unsigned char)s_room_id;
                DBG_SENTINEL(0x02u) = (unsigned char)players[0].x;
                DBG_SENTINEL(0x03u) = (unsigned char)players[0].y;
            } else {
                s_room_id = 0x77;
                /* T0.1 verify: spawn Link on cave-entry tile $24 at
                 * cache col=8/9 row=3 (visible in room $77 north).
                 * Tileâ†’pixel: col 8 â†’ linkX in [$40..$47], row 3 â†’
                 * linkY satisfies (linkY+$0B-$40)/8==3 â†’ linkY=$4D..$54.
                 * Use ($44, $50) to land dead center of the entry pad. */
                players[0].x = 0x44;
                players[0].y = 0x50;
                players[0].face = LINK_FACE_DOWN;
                DBG_SENTINEL(0x00u) = 0xBBu;
            }
            /* SENTINEL â€” values RIGHT BEFORE upload_scene_chr */
            DBG_SENTINEL(0x04u) = (unsigned char)s_room_id;
            DBG_SENTINEL(0x05u) = (unsigned char)players[0].x;
            upload_scene_chr();
            /* SENTINEL â€” values AFTER upload_scene_chr */
            DBG_SENTINEL(0x06u) = (unsigned char)s_room_id;
            DBG_SENTINEL(0x07u) = (unsigned char)players[0].x;
            /* P5: scene change uses coordinator to re-upload sprite CHR
             * with correct variant. combat redux kept separate. */
            roomrom_scene_load(
                (s_scene == SCENE_UW) ? ROOMROM_SCENE_UW_L1
                                      : ROOMROM_SCENE_OVERWORLD,
                current_redux_flag());
            /* SENTINEL after scene_load */
            DBG_SENTINEL(0x0Au) = (unsigned char)s_room_id;
            roomrom_combat_set_redux(current_redux_flag());
            /* SENTINEL after combat_set_redux */
            DBG_SENTINEL(0x0Bu) = (unsigned char)s_room_id;
            /* SENTINEL $07E9 = s_scene at second block (should match $07E8) */
            DBG_SENTINEL(0x09u) = (unsigned char)s_scene;
            /* 2026-05-17 â€” level_info_install_* RESTORED; mirror writes
             * to $FF867E..$FF8C7D land in SGDK heap free-pool. */
            if (s_scene == SCENE_UW) {
                /* CurLevel ($0010) was set from cell $07FA in the seed block. */
                level_info_install_uw(nes_ram[0x0010u], s_current_quest);
                /* NES InitMode2: start room = LevelInfo_StartRoomId ($6BAD,
                 * record offset $2F, ROM-verified). Room-flags pointer
                 * $6BAF/$6BB0 stays as installed from ROM. */
                s_room_id = nes_ram[0x6BADu];
            } else {
                nes_ram[0x0010u] = 0u;
                level_info_install_ow();
            }
            /* SENTINEL DISTINCT values to detect overwrite vs no-write */
            DBG_SENTINEL(0x0Cu) = 0xC1u;  /* before load_room marker */
            load_room(s_room_id);
            DBG_SENTINEL(0x0Du) = 0xC2u;  /* after load_room marker */
            DBG_SENTINEL(0x0Fu) = (unsigned char)s_room_id;
            roomrom_combat_set_uw(s_scene == SCENE_UW);
            enemy_loop_room_init(s_room_id, (unsigned char)s_scene,
                s_scene == SCENE_UW ? roomrom_uw_room_render_get_level() : 0u,
                s_scene == SCENE_UW ? roomrom_uw_room_render_get_quest() : 0u);
            DBG_SENTINEL(0x0Eu) = 0xC3u;  /* after enemy_loop_room_init marker */
            /* Phase 10.3 audio per-event wiring: scene-toggle entry
             * fires music_play per docs/audit/audio_routing.md table.
             * UW = $40 dungeon song; OW = $01 overworld song
             * (per NES Z_07.asm LevelSongIds[0]=$01; driver maps $01
             * â†’ first_ow path phrase $08).
             * extern decl at top of main.c via inventory.h includes
             * â€” music_play is in audio_driver.asm + linked into
             * Zelda.md via tools/build/build_rom.py compile_asm
             * MRI path (commit 6191e911). */
            audio_music_play((s_scene == SCENE_UW) ? 0x40 : 0x01);
            return;
        }

        if (g_debug_session && (pressed & BUTTON_C)) {
            if (s_scene == SCENE_UW) {
                u8 map_id = roomrom_uw_room_render_get_map();
                roomrom_uw_room_render_set_map(map_id ^ 1u);
            } else {
                u8 map_id = roomrom_ow_room_render_get_map();
                roomrom_ow_room_render_set_map(map_id ^ 1u);
            }
            upload_scene_chr();
            /* P5: map toggle re-uploads sprite CHR via coordinator.
             * Combat keeps its existing redux flag (v11+ alt-swing). */
            roomrom_scene_load(
                (s_scene == SCENE_UW) ? ROOMROM_SCENE_UW_L1
                                      : ROOMROM_SCENE_OVERWORLD,
                current_redux_flag());
            roomrom_combat_set_redux(current_redux_flag());
            load_room(s_room_id);
            roomld_setup_obj_room_bounds();   /* debug toggle: no room entry */
            return;
        }

        /* S7: A swings sword (NES-faithful single A-press). UW level cycle
         * moved to MODE button below. Movement is suppressed during the
         * swing so Link snaps to the swing pose for COMBAT_EXTEND_FRAMES. */
        /* T-131: NES Link_HandleInput filters A/B and the input
         * directions at the room border first (Link_FilterInput). */
        /* T-225: Z_05 Link_HandleInput samples idle once before A then B.
         * A can change $AC; that must not cancel B from the same input. */
        const unsigned char input_was_idle = (nes_ram[0x00ACu] == 0u);
        link_filter_input();
        if ((nes_ram[0x00F8u] & 0x80u) && !roomrom_combat_link_locked()) {
            roomrom_combat_try_swing(players[0].face, players[0].x, players[0].y);
        }

        /* B-item slot:
         *   Z press (alone)   = cycle B-item forward
         *   Z held + START    = quest toggle (handled below; suppress cycle)
         *   B press           = use current B-item */
        if (g_debug_session && (pressed & BUTTON_Z) && !(joy & BUTTON_START)) {
            unsigned char nxt = (unsigned char)(s_b_item + 1u);
            if (nxt >= (unsigned char)B_ITEM_COUNT) nxt = (unsigned char)B_ITEM_BOOMERANG;
            s_b_item = (b_item_t)nxt;
        }
        /* T-116: Link_HandleInput reads A/B only while Link is idle ($AC 0). */
        if ((nes_ram[0x00F8u] & 0x40u) && input_was_idle) {
            /* T-092: NES WieldItem uses SelectedItemSlot ($656), which the
             * pause subscreen sets; the debug Z cycle only overrides it in a
             * debug session. */
            if (!g_debug_session) {
                unsigned char slot = nes_ram[0x0656u];
                s_b_item = (slot < 9u) ? (b_item_t)k_inv_cursor_to_b_item[slot]
                                       : B_ITEM_NONE;
            }
            switch (s_b_item) {
            case B_ITEM_BOOMERANG:
                /* WieldBoomerang replaces food in slot $0F (T-057). */
                roomrom_boomerang_throw(players[0].face,
                                        players[0].x, players[0].y);
                break;
            case B_ITEM_ARROW:
                if (!roomrom_arrow_active()) {
                    roomrom_arrow_fire(players[0].face,
                                       players[0].x, players[0].y);
                }
                break;
            case B_ITEM_BOMB:
                if (!roomrom_bomb_active()) {
                    roomrom_bomb_place(players[0].face,
                                       players[0].x, players[0].y);
                }
                break;
            case B_ITEM_CANDLE:
                /* The room brightens when the fire stands (UpdateFire ->
                 * UpdateCandle, T-111), not on the button press. */
                roomrom_candle_fire_spawn(players[0].face,
                                          players[0].x, players[0].y);
                break;
            case B_ITEM_ROD:
                roomrom_combat_wield_rod();      /* T-116: NES WieldRod */
                break;
            case B_ITEM_FLUTE:
                weapon_wield_flute();            /* NES WieldFlute (T-171) */
                break;
            case B_ITEM_FOOD:
                roomrom_food_wield();            /* NES WieldFood (T-057) */
                break;
            case B_ITEM_POTION:
                weapon_wield_potion();           /* NES WieldPotion (T-057) */
                break;
            default:             break;
            }
        }

        /* Z held + START press = quest toggle (UW only). Z-held suppresses
         * the item-cycle path above so the press is unambiguous. */
        if (g_debug_session && (pressed & BUTTON_START) && (joy & BUTTON_Z) && s_scene == SCENE_UW) {
            u8 q = roomrom_uw_room_render_get_quest();
            q = (q == ROOMROM_UW_QUEST_MIN) ? ROOMROM_UW_QUEST_MAX
                                             : ROOMROM_UW_QUEST_MIN;
            /* NES source: Z_05.asm:InitMode2Load / InitMode_EnterRoom.
             * Drained C: roomrom_main_apply_warp_outcome.
             * Coverage: PARTIAL debug quest entry; Stance: EXTEND.
             * Reinstall tables, transient state, graphics and enemies through
             * the same handoff as other entries, not a renderer-only change. */
            rr_warp_outcome_t out;
            out.dest_scene = SCENE_UW;
            out.dest_level = roomrom_uw_room_render_get_level();
            out.dest_quest = q;
            out.dest_room_id = s_room_id;
            out.dest_link_x = players[0].x;
            out.dest_link_y = players[0].y;
            out.dest_link_face = (unsigned char)players[0].face;
            out.dest_redux_flag = current_redux_flag();
            roomrom_main_apply_warp_outcome(&out);
            return;
        }

    handle_pause_start:
        /* Task 6.10.1: bare START edge-press = NES Select-equivalent.
         * Toggles voluntary pause when no other button is held. All
         * START-with-modifier handlers already returned above, so a
         * bare START reaches here only when no chord matched. */
        if ((pressed & BUTTON_START) &&
            !(joy & (BUTTON_A | BUTTON_B | BUTTON_C |
                     BUTTON_X | BUTTON_Y | BUTTON_Z | BUTTON_MODE))) {
            unsigned char was_paused = roomrom_pause_is_active();
            roomrom_pause_toggle_voluntary();
            /* P6.2 (2026-05-19): bare-START transition hooks inventory
             * subscreen enter/exit. NES Z1 renders inventory on pause;
             * our subscreen renderer paints Plane A on enter, leaves
             * it static; exit defers re-paint to room renderer. */
            if (!was_paused && roomrom_pause_is_active()) {
                pause_cram_save();
                inventory_subscreen_enter();
            } else if (was_paused && !roomrom_pause_is_active()) {
                /* P6.3 scroll-out: start the animation. load_room
                 * deferred until scroll completes â€” main.c poll loop
                 * picks up the scrolled_out signal next frame. */
                inventory_subscreen_exit();
                /* Re-set paused active so the input handler keeps
                 * calling subscreen_tick during scroll-out frames.
                 * The toggle above turned pause OFF; we restore it
                 * here so input path still calls tick. */
                roomrom_pause_toggle_voluntary();
            }
            return;
        }

    skip_debug_handlers:
        ;  /* Empty stmt â€” labels need a stmt to attach to. */
        /* Tier 0 (plan v6) pause gate: when paused, swallow all
         * gameplay input (D-pad + combat buttons). START already
         * handled above + un-pauses. Other chord handlers (mode/
         * teleport/movestyle) skipped while paused.
         *
         * P6.5 (2026-05-19): route input to inventory subscreen tick
         * for cursor + B-item selection. D-pad moves cursor over B-item
         * row; A selects. Joy byte (lo 8 bits) matches SGDK BUTTON_*
         * layout: UP=$01 DOWN=$02 LEFT=$04 RIGHT=$08 B=$10 C=$20 A=$40
         * START=$80.
         *
         * P6.3 (2026-05-19): if subscreen scroll-out just completed,
         * clear pause flag + reload room. Otherwise tick the subscreen
         * (drives scroll state machine + input). */
        if (roomrom_pause_is_active()) {
            if (inventory_subscreen_scrolled_out()) {
                /* NES source: Z_05.asm:UpdateMenuScrollUp returns after
                 * clearing MenuState; BeginUpdateWorld runs next tick.
                 * Drained C: inventory_subscreen_scrolled_out handoff.
                 * Coverage: PARTIAL (T-165 resumed on the closing tick).
                 * Stance: EXTEND the existing completion boundary. */
                roomrom_pause_toggle_voluntary();
                pause_restore_room();
                return;
            }
            inventory_subscreen_tick((unsigned char)(joy & 0x00FFu));
            /* UpdateMenuActive (Z_05.asm): after the submenu draw and
             * selection, controller 2 Up+A held -> EndGameMode, MenuState
             * 0, GameMode 8, SongEnvelopeSelector 0, SilenceSound. */
            if (inventory_subscreen_menu_active() &&
                (nes_ram[0x00FBu] & 0x88u) == 0x88u) {
                inventory_subscreen_abort();
                g_paused = ROOMROM_PAUSE_OFF;
                nes_ram[0x0011u] = 0u;                 /* EndGameMode */
                nes_ram[0x0013u] = 0u;
                nes_ram[0x00E1u] = 0u;                 /* MenuState */
                nes_ram[0x0012u] = 0x08u;
                nes_ram[0x0619u] = 0u;                 /* SongEnvelopeSelector */
                nes_ram[0x0604u] = 0x80u;              /* SilenceSound */
                nes_ram[0x0603u] = 0x80u;
                return;
            }
            return;
        }

        /* NES UpdatePlayer starts with DecrementInvincibilityTimer
         * (Z_07.asm:2045, 5756): Link's $04F0 drops on even FrameCounter
         * values. T-012: keyed on the Genesis tick count and run after
         * Link's collisions, it fell a frame off the NES (t012_route
         * t1321). */
        if (!cave_fade_is_active()) {
            /* BeginUpdateWorld (Z_07.asm:1841): with the clock (InvClock
             * $66C) Link's invincibility timer gains $10 every frame. */
            if (nes_ram[0x066Cu] != 0u)
                nes_ram[0x04F0u] = (unsigned char)(nes_ram[0x04F0u] + 0x10u);
            if (nes_ram[0x04F0u] != 0u && (nes_ram[0x0015u] & 1u) == 0u)
                nes_ram[0x04F0u]--;
        }

        /* Level cycle (was MODE-only) removed -- MODE is reserved hardware.
         * Reach a different level via teleport (X mode + DPAD) which warps
         * across the 16x8 room grid. */

        const u8 link_halted = (u8)((nes_ram[0x00ACu] & 0xC0u) == 0x40u);

        /* S7: while sword is mid-swing, swallow D-pad so Link freezes on
         * his swing pose. Combat module ticks below + clears sword on
         * retract, returning control. */
        /* T-013: NES UpdatePlayer returns while Link is halted; in the
         * sword/item states ($1x/$2x) Link_HandleInput still turns him and
         * Walker_Move only drops the move (below). */
        if ((roomrom_main_current_redux_flag() && roomrom_combat_link_locked()) ||
            link_halted) {
            joy = (u16)(joy & ~(BUTTON_LEFT|BUTTON_RIGHT|BUTTON_UP|BUTTON_DOWN));
        }

        if (s_mode == MODE_TELEPORT) {
            /* D-pad edge-press warps room across the 16x8 grid. */
            u8 col = s_room_id & 0x0F;
            u8 row = s_room_id >> 4;
            if      ((pressed & BUTTON_LEFT)  && col > 0)  col--;
            else if ((pressed & BUTTON_RIGHT) && col < 15) col++;
            else if ((pressed & BUTTON_UP)    && row > 0)  row--;
            else if ((pressed & BUTTON_DOWN)  && row < 7)  row++;
            else return;
            s_room_id = (u8)((row << 4) | col);
            load_room(s_room_id);
            /* 2026-05-17 â€” teleport now spawns enemies for the destination
             * room (mirrors scroll path at line ~1727). Without this, jumping
             * rooms via debug teleport leaves ObjType[] empty. */
            enemy_loop_room_init(s_room_id, (unsigned char)s_scene,
                s_scene == SCENE_UW ? roomrom_uw_room_render_get_level() : 0u,
                s_scene == SCENE_UW ? roomrom_uw_room_render_get_quest() : 0u);
        } else if (s_move_style == MOVE_STYLE_ALTTP) {
            /* ALTTP-style 8-direction movement, ported from
             * github.com/snesrev/zelda3 src/player.c Link_HandleVelocity
             * + Link_MovePosition + kSpeedMod. 8.8 fixed-point sub-pixel
             * per axis. Cardinal vel = 24 (kSpeedMod[0]); diagonal vel = 16
             * (kSpeedMod[1]) so sqrt(2) doesn't double diagonal speed. */

            u8 dir_bits = 0u;   /* bit 0=R, bit 1=L, bit 2=D, bit 3=U */
            if (joy & BUTTON_RIGHT) dir_bits |= 0x1u;
            if (joy & BUTTON_LEFT)  dir_bits |= 0x2u;
            if (joy & BUTTON_DOWN)  dir_bits |= 0x4u;
            if (joy & BUTTON_UP)    dir_bits |= 0x8u;

            {
                u8 has_h = (dir_bits & 0x3u) != 0u;
                u8 has_v = (dir_bits & 0xCu) != 0u;
                s8 vel = (has_h && has_v) ? (s8)16 : (s8)24;
                s8 vx = 0, vy = 0;

                if (dir_bits & 0x3u) vx = (dir_bits & 0x2u) ? (s8)-vel : vel;
                if (dir_bits & 0xCu) vy = (dir_bits & 0x8u) ? (s8)-vel : vel;
                uw_doorway_adjust_velocity(&vx, &vy);

                /* Facing: keep current if compatible with motion; else pick
                 * H over V (matches general 4-frame sprite limitation). */
                if      (vx > 0) players[0].face = LINK_FACE_RIGHT;
                else if (vx < 0) players[0].face = LINK_FACE_LEFT;
                else if (vy > 0) players[0].face = LINK_FACE_DOWN;
                else if (vy < 0) players[0].face = LINK_FACE_UP;

                if (vx || vy) {
                    if (++s_link_anim_tick >= LINK_ANIM_PERIOD) {
                        s_link_frame ^= 1u;
                        s_link_anim_tick = 0u;
                    }
                } else {
                    s_link_frame = 0u;
                    s_link_anim_tick = 0u;
                }

                /* ALTTP Link_MovePosition formula (8.8 fixed-point):
                 * tmp = subpixel + vel*16 + coord*256
                 * subpixel = tmp & 0xFF; coord = tmp >> 8.
                 * Per-axis collision check after each step enables wall-slide. */
                if (vx) {
                    short old_x = players[0].x;
                    u8    old_sub = s_link_subx;
                    link_dir_t hdir = (vx > 0) ? LINK_DIR_RIGHT : LINK_DIR_LEFT;
                    int tmp = (int)s_link_subx + ((int)vx * 16)
                            + ((int)players[0].x << 8);
                    s_link_subx = (u8)(tmp & 0xFF);
                    players[0].x = (short)(tmp >> 8);
                    if (!link_walkable_at(players[0].x, players[0].y, hdir)) {
                        players[0].x = old_x;
                        s_link_subx = old_sub;
                    }
                }
                if (vy) {
                    short old_y = players[0].y;
                    u8    old_sub = s_link_suby;
                    link_dir_t vdir = (vy > 0) ? LINK_DIR_DOWN : LINK_DIR_UP;
                    int tmp = (int)s_link_suby + ((int)vy * 16)
                            + ((int)players[0].y << 8);
                    s_link_suby = (u8)(tmp & 0xFF);
                    players[0].y = (short)(tmp >> 8);
                    if (!link_walkable_at(players[0].x, players[0].y, vdir)) {
                        players[0].y = old_y;
                        s_link_suby = old_sub;
                    }
                }
            }

            edge_load_or_clamp();
            if (!roomrom_combat_link_locked()) {
                /* Per APPENDIX plan revert (2026-05-22): single SAT entry
                 * unconditionally. NES PutLinkBehindBackground sets behind
                 * bit on BOTH Link halves (slots $12+$13) = whole Link
                 * behind BG. Genesis equivalent = single low-prio sprite
                 * + wide BG-prio stamp. Split-sprite removed. */
                {
                    /* NES Anim_WriteSpritePair uses invincibility timer
                     * low bits for Link's four sprite sub-palettes. */
                    unsigned char stun = nes_ram[0x04F0u];
                    if (stun != 0u) {
                        roomrom_sprites_set_link_hurt_pose(players[0].x,
                                                           players[0].y,
                                                           players[0].face,
                                                           s_link_frame, stun);
                    } else {
                        roomrom_sprites_set_link_pose(players[0].x, players[0].y,
                                                      players[0].face, s_link_frame);
                    }
                }
            }
        } else if (!link_halted) {
            /* NES Z_07 UpdatePlayer returns after the invincibility timer
             * when halted. Skip the whole Walker_Move path, including shove;
             * UpdateDock/Wallmaster own the carried position. T-056 raft
             * tick468: input masking alone kept applying 4px knockback. */
            /* NES-faithful Link movement, ported from
             *   Z_05.asm Link_HandleInput / Link_ModifyDirAtGridPoint
             *   Z_07.asm Walker_Move / MoveObject / AddQSpeedToPositionFraction
             *
             * Per-frame:
             * 1. If on a grid intersection (offset == 0), pick a single-axis
             *    direction from current input (no diagonal). H over V on tie.
             * 2. If input released, stop instantly (even mid-grid). Walker_Move
             *    @ChooseObjDirOrInputDir: input=0 -> moving_dir=0.
             * 3. At grid offset 0, check the next tile before movement.
             * 4. If moving, run 4 quarter-steps. Right/down add QSpeed, left/up
             *    subtract QSpeed. Carry/borrow advances 1 px and changes signed
             *    ObjGridOffset until it reaches +8 or -8, then returns to 0. */

            u16 input = joy & (BUTTON_LEFT|BUTTON_RIGHT|BUTTON_UP|BUTTON_DOWN);
            /* NES Walker_Move (Z_07.asm:2616) clears scratch [0E] (door
             * block / doorway index) for Link every frame; MoveShot later in
             * the frame reads it (T-102, t116_shot_hit grid offset). */
            nes_ram[0x000Eu] = 0u;
            u8 h_dir = (input & BUTTON_LEFT) ? 1u
                     : ((input & BUTTON_RIGHT) ? 2u : 0u);
            u8 v_dir = (input & BUTTON_UP)   ? 1u
                     : ((input & BUTTON_DOWN) ? 2u : 0u);

            {
                link_dir_t input_dir = LINK_DIR_NONE;
                link_dir_t moving_dir = LINK_DIR_NONE;

                if (h_dir && v_dir) {
                    /* NES Link_ModifyDirAtGridPoint with 2 inputs: in OW,
                     * picks "last walkable" (h_dir last in bit-iteration).
                     * H over V matches OW behavior. */
                    input_dir = (h_dir == 1u) ? LINK_DIR_LEFT : LINK_DIR_RIGHT;
                } else if (h_dir) {
                    input_dir = (h_dir == 1u) ? LINK_DIR_LEFT : LINK_DIR_RIGHT;
                } else if (v_dir) {
                    input_dir = (v_dir == 1u) ? LINK_DIR_UP : LINK_DIR_DOWN;
                }

                /* T-131: ObjInputDir ($3F8, ButtonsDown & $0F, masked by
                 * Link_FilterInput) after Link_ModifyDirInDoorway (UW). */
                unsigned char in_bits = 0u;
                /* Link_HandleInput @CheckMovement: while shoved (ObjShoveDir
                 * $C0 != 0) Link does not turn (T-171 t171_aquamentus_sword
                 * t702: knocked back mid-swing with Up held, NES kept
                 * facing right). */
                if (input != 0u && nes_ram[0x00C0u] == 0u) {
                    nes_ram[0x0098u] = link_nes_bit_of(s_link_dir);
                    link_modify_dir_in_doorway();
                    in_bits = (unsigned char)(nes_ram[0x03F8u] & 0x0Fu);
                    input_dir = link_dir_of_lowest_bit(in_bits);
                }
                if (in_bits != 0u) {
                    if (s_link_grid_offset == 0 || s_link_dir == LINK_DIR_NONE) {
                        /* T-122: NES Link_ModifyDirAtGridPoint. */
                        link_nes_modify_dir_at_grid_point(in_bits, &moving_dir);
                    } else {
                        link_nes_modify_dir_on_grid_line(input_dir);
                        moving_dir = s_link_dir;
                    }
                }

                /* NES Walker_Move (Z_07.asm:2612): Link in state $1x/$2x
                 * keeps the facing Link_HandleInput chose, [0F] = 0. */
                const u8 item_use_hold = link_item_use_blocks_move();
                if (moving_dir != LINK_DIR_NONE) {
                    /* NES draws Link facing ObjDir (s_link_dir), which the
                     * no-walkable grid-point case leaves unchanged (T-122). */
                    switch (s_link_dir) {
                    case LINK_DIR_LEFT:  players[0].face = LINK_FACE_LEFT;  break;
                    case LINK_DIR_RIGHT: players[0].face = LINK_FACE_RIGHT; break;
                    case LINK_DIR_UP:    players[0].face = LINK_FACE_UP;    break;
                    case LINK_DIR_DOWN:  players[0].face = LINK_FACE_DOWN;  break;
                    default: break;
                    }
                }
                if (item_use_hold) moving_dir = LINK_DIR_NONE;

                /* T-120: Walker_Move for Link (Z_07.asm:2632): tile objects
                 * (block/rock/gravestone/armos in state 1) within $10 px
                 * and, with a person or grumble moblin in slot 1, the
                 * person line reset the movement direction [0F]. */
                if (moving_dir != LINK_DIR_NONE) {
                    unsigned char t1 = nes_ram[0x0350u];
                    nes_ram[0x000Fu] = link_nes_bit_of(moving_dir);
                    progress_check_tile_objects_blocking();
                    if (t1 == 0x36u || (t1 >= 0x4Bu && t1 < 0x53u))
                        uw_person_check_person_blocking();
                    /* Caves (modes $B/$C): CheckSubroom always runs
                     * CheckPersonBlocking (Z_05.asm:3113), so Link stops
                     * below the cave person's row. */
                    if (s_scene == SCENE_CAVE)
                        uw_person_check_person_blocking();
                    if (nes_ram[0x000Fu] == 0u) moving_dir = LINK_DIR_NONE;
                }

                if (moving_dir != LINK_DIR_NONE && shortcut_cave_exit_if_stair())
                    moving_dir = LINK_DIR_NONE;

                s_ow_edge = 0u;
                /* GoWalkableDir returns before CheckScreenEdge while the
                 * ladder is out (LadderSlot $64, T-056). */
                if (s_scene == SCENE_OW && moving_dir != LINK_DIR_NONE &&
                    nes_ram[0x0064u] == 0u) {
                    u8 d = moving_dir == LINK_DIR_RIGHT ? 1u :
                           moving_dir == LINK_DIR_LEFT ? 2u :
                           moving_dir == LINK_DIR_DOWN ? 4u : 8u;
                    s_ow_edge = ow_scroll_edge(players[0].x, players[0].y,
                                               d, s_link_grid_offset, s_room_id);
                    if (s_ow_edge) moving_dir = LINK_DIR_NONE;
                    if (s_ow_edge == 0x80u) s_ow_edge = 0u;
                }

                if (s_scene == SCENE_UW && nes_ram[0x00C0u] == 0u) {
                    /* T-131: NES Walker_Move for Link in the UW:
                     * BoundByRoom outside doorways, CheckDoorway, then
                     * Walker_CheckTileCollision (in a doorway the tile test
                     * is skipped; CheckScreenEdge starts the next room). */
                    unsigned char edge;
                    nes_ram[0x0070u] = (unsigned char)players[0].x;
                    nes_ram[0x0084u] = (unsigned char)players[0].y;
                    nes_ram[0x0394u] = (unsigned char)s_link_grid_offset;
                    if (s_link_dir != LINK_DIR_NONE)
                        nes_ram[0x0098u] = link_nes_bit_of(s_link_dir);
                    nes_ram[0x000Fu] = link_nes_bit_of(moving_dir);
                    if (cellar_check_exit()) {
                        /* CheckSubroom changes RoomId before this tick's
                         * host-to-NES publication in play_update_objects. */
                        s_room_id = nes_ram[0x00EBu];
                        roomrom_main_link_sync_from_nes();
                        edge = 0u;
                    } else if (link_uw_walker_checks())
                        edge = nes_ram[0x0098u];
                    else
                        edge = link_walker_check_tile_collision(s_link_grid_offset);
                    if (nes_ram[0x0098u] != link_nes_bit_of(s_link_dir)) {
                        s_link_dir = link_dir_of_lowest_bit(nes_ram[0x0098u]);
                        switch (s_link_dir) {
                        case LINK_DIR_LEFT:  players[0].face = LINK_FACE_LEFT;  break;
                        case LINK_DIR_RIGHT: players[0].face = LINK_FACE_RIGHT; break;
                        case LINK_DIR_UP:    players[0].face = LINK_FACE_UP;    break;
                        case LINK_DIR_DOWN:  players[0].face = LINK_FACE_DOWN;  break;
                        default: break;
                        }
                    }
                    moving_dir = link_dir_of_lowest_bit(nes_ram[0x000Fu]);
                    if (edge) {
                        s_uw_edge = edge;
                        moving_dir = LINK_DIR_NONE;
                    }
                } else if (moving_dir != LINK_DIR_NONE && s_link_grid_offset == 0) {
                    if (!link_walkable_at(players[0].x, players[0].y, moving_dir)) {
                        /* NES PlayerUnwalkable checks passive OW statues
                         * before stopping Link. The drained checker creates
                         * Armos/Flying Ghini from collided $BC..$C3 tiles. */
                        if (s_scene == SCENE_OW) {
                            nes_ram[0x0070u] = (unsigned char)players[0].x;
                            nes_ram[0x0084u] = (unsigned char)players[0].y;
                            nes_ram[0x0098u] = link_nes_bit_of(moving_dir);
                            nes_ram[0x0394u] = 0u;
                            trap_check_passive_tile_objects();
                        }
                        moving_dir = LINK_DIR_NONE;
                    }
                }

                /* T-056: Walker_Move runs CheckLadder for Link after
                 * Walker_CheckTileCollision, before MoveObject (Z_07.asm).
                 * Not while shoved (Walker_Move takes Obj_Shove instead) or
                 * halted (UpdatePlayer returns first). [0F] in and out. */
                if (nes_ram[0x0064u] != 0u && nes_ram[0x00C0u] == 0u &&
                    (nes_ram[0x00ACu] & 0xC0u) != 0x40u) {
                    nes_ram[0x0070u] = (unsigned char)players[0].x;
                    nes_ram[0x0084u] = (unsigned char)players[0].y;
                    nes_ram[0x0394u] = (unsigned char)s_link_grid_offset;
                    if (s_link_dir != LINK_DIR_NONE)
                        nes_ram[0x0098u] = link_nes_bit_of(s_link_dir);
                    nes_ram[0x000Fu] = link_nes_bit_of(moving_dir);
                    {
                        const unsigned char moved_slot = link_ladder_check();
                        if (moved_slot != 0u) {
                            object_move_object(moved_slot);
                            moving_dir = LINK_DIR_NONE;
                        } else {
                            moving_dir = link_dir_of_lowest_bit(nes_ram[0x000Fu]);
                        }
                    }
                }

                /* Link knockback shove â€” NES Z1 Obj_Shove runs every frame
                 * for slot 0 reading ObjShoveDir ($00C0) + ObjShoveDistance
                 * ($00D3). link_collision_link_be_harmed (drained NES path)
                 * sets these on monster contact. While shove is active,
                 * player input is suppressed (NES stun semantics) and
                 * Link is shifted in the shove direction. */
                {
                    extern void c_obj_shove(unsigned int slot);
                    unsigned char shove_dir = nes_ram[0x00C0u];
                    if (shove_dir != 0u) {
                        /* Mirror C-side players[0] into nes_ram so
                         * c_obj_shove starts from the live position. */
                        nes_ram[0x0070u] = (unsigned char)players[0].x;
                        nes_ram[0x0084u] = (unsigned char)players[0].y;
                        /* T-113: Obj_Shove reads and advances Link's
                         * ObjGridOffset ($394); it must see the live
                         * offset and its edits must survive the
                         * end-of-tick publish below. */
                        nes_ram[0x0394u] = (unsigned char)s_link_grid_offset;
                        c_obj_shove(0u);
                        players[0].x = (short)nes_ram[0x0070u];
                        players[0].y = (short)nes_ram[0x0084u];
                        s_link_grid_offset = (signed char)nes_ram[0x0394u];
                        /* NES Walker_Move uses Obj_Shove instead of input
                         * while shove direction is nonzero. */
                        moving_dir = LINK_DIR_NONE;
                    }
                }

                if (moving_dir != LINK_DIR_NONE) {
                    link_nes_move_object(moving_dir);
                } else {
                    /* NES AnimateObjectWalking (Z_07.asm:5045) advances
                     * ObjAnimCounter only while the object is MOVING; on stop it
                     * FREEZES the walk pose at the current frame â€” it does NOT
                     * snap to frame 0 (live-proven: cave emerge settle holds
                     * ObjAnimCounter=5 / ObjAnimFrame=1 frozen). Match that: hold
                     * s_link_frame, reset only the sub-frame tick so the next
                     * walk re-times cleanly. Fixes two things:
                     *  - cave_fade descent: the anim is owned by
                     *    cave_fade_anim_tick_handler; the old `s_link_frame = 0`
                     *    clobbered it every no-input frame (static frame-0 sink).
                     *  - cave post-emerge settle: NES holds the last emerge walk
                     *    frame while Link stands; the old reset snapped Gen to
                     *    frame 0 (the bulk of the emerge sprite diff). */
                    s_link_anim_tick = 0u;
                }
            }

            edge_load_or_clamp();
            /* T-116: DrawLink uses NES ObjAnimFrame ($3E4) every frame, not
             * only while moving: the item-use states set it to 1
             * (AnimateLinkObjState). It advances on the NES
             * AnimateObjectWalking cadence in link_anim_state_step. The baked
             * left/right poses are stored in the other order (pose frame 0 =
             * NES frame 1, tiles $04/$06; verify_sprites newgame f128-137),
             * up/down match. */
            s_link_frame = (u8)((nes_ram[0x03E4u] & 1u) ^
                ((players[0].face == LINK_FACE_LEFT ||
                  players[0].face == LINK_FACE_RIGHT) ? 1u : 0u));
            /* Publish completed movement and its grid phase for room entry.
             * The tick-start copy alone exposed last frame's coordinates. */
            nes_ram[0x70u] = (u8)players[0].x;
            nes_ram[0x84u] = (u8)players[0].y;
            nes_ram[0x394u] = (u8)s_link_grid_offset;
            nes_ram[0x3A8u] = s_link_pos_frac;
            /* T-125: Link is drawn once, after AnimateLinkBase below. */
            if (!roomrom_combat_link_locked()) s_link_draw_pending = 1u;
        }

        /* T-102: NES UpdateMode5Play order: UpdatePlayer (input, movement,
         * Link animation) first, then weapons and objects. UpdatePlayer
         * that changes the mode (a room scroll started) skips them
         * (IsUpdatingMode, Z_07.asm:1851). */
        u8 warp_ticked = 0u;
        if (!roomrom_pause_is_active() && !cave_fade_is_active() &&
            s_lvl_phase != LVL_CAVE_EXIT && nes_ram[0x0012u] != 0x0Au) {
            /* CheckCaveEdge -> GoToModeAFromCave leaves UpdatePlayer before
             * Link_EndMoveAndAnimate (T-171: t134 t443 ObjAnimCounter). */
            /* T-056: Link_EndMoveAndAnimate's ladder setup runs after
             * the move (grid truncated), before AnimateLinkBase. */
            if (s_link_dir != LINK_DIR_NONE)
                nes_ram[0x0098u] = link_nes_bit_of(s_link_dir);
            if (s_mode == MODE_WALK && s_move_style == MOVE_STYLE_NES)
                link_ladder_end_move();
            roomrom_combat_end_move_and_animate();
            /* NES draws Link after AnimateLinkBase (SetUpWalkingSprites):
             * a frame toggled this tick shows now. */
            if (s_mode == MODE_WALK && s_move_style == MOVE_STYLE_NES &&
                !roomrom_combat_link_locked()) {
                s_link_frame = (u8)((nes_ram[0x03E4u] & 1u) ^
                    ((players[0].face == LINK_FACE_LEFT ||
                      players[0].face == LINK_FACE_RIGHT) ? 1u : 0u));
                s_link_draw_pending = 1u;
            }
            draw_link_pending();
            /* T-012: NES UpdatePlayer enters the stairs/cave mode itself;
             * a mode change there skips weapons and objects that frame
             * (Z_07.asm:1851 IsUpdatingMode). The warp coordinator is that
             * check here, run after Link moved. */
            {
                const u8 gm = nes_ram[0x0012u];
                roomrom_world_transition_tick();
                warp_ticked = 1u;
                if (nes_ram[0x0012u] == gm && !cave_fade_is_active() &&
                    s_lvl_phase == LVL_NONE &&
                    !roomrom_world_transition_is_active() &&
                    s_scroll_state == SCROLL_NONE) {
                    if (play_update_objects()) return;
                } else {
                    /* A mode change in UpdatePlayer skips the objects but
                     * @FinishUpdatePlay still runs UpdateHeartsAndRupees. */
                    inventory_rupee_tick(nes_ram[0x0015u]);
                }
            }
            play_finish();
        }
        draw_link_pending();

        /* Task 5.4: warp coordinator runs AFTER movement settles. The
         * tick is a no-op in non-OW scenes and when the OW raw-tile
         * cache isn't stable (mid-scroll). When it fires, the apply
         * step runs synchronously inside the coordinator and the next
         * frame begins in the new scene. */
        if (!warp_ticked) roomrom_world_transition_tick();

        /* Task 5.7: push-block state machine runs after world_transition
         * (so a coordinator-driven scene swap clears push state cleanly
         * via the room-change reset). Internally guards on UW + WALK. */
        roomrom_pushblock_tick();


        /* 2026-05-15 perf: DMA only the SAT slots actually in use this
         * frame. native sweep publishes g_enemy_render_last_sat_slot =
         * highest slot used + 1 (terminator). DMA bytes drop from
         * 64*8=512 to ~12*8=96 per frame on busy rooms = ~6% frame
         * budget recovered. Floor of 10 keeps Link + gameplay sprites
         * (slots 0..9) always covered. */
        /* HUD B-item icon per-frame update (slot 10). */
        roomrom_hud_b_item_update();

        {
            unsigned short dma_count = g_enemy_render_last_sat_slot;
            /* Floor: every gameplay slot (masks, Link, items, HUD). */
            if (dma_count < ROOMROM_SPRITE_SLOT_ENEMY_FIRST)
                dma_count = ROOMROM_SPRITE_SLOT_ENEMY_FIRST;
            VDP_updateSprites(dma_count, DMA_QUEUE);
        }

        /* Task 5.4: passive state mirror for BizHawk Lua probes.
         * 2026-05-15: gate INLINED here so function-call overhead (jsr+ret
         * + stack frame setup) is avoided on default-gameplay frames.
         * Probes write arm magic at $FF73F8..F9 before reading mirror. */
        {
            volatile unsigned char *ctrl =
                (volatile unsigned char *)ROOMROM_DEBUG_PROBE_CONTROL_BASE;
            if (ctrl[0] == ROOMROM_DEBUG_PROBE_ARM0 &&
                ctrl[1] == ROOMROM_DEBUG_PROBE_ARM1) {
                roomrom_debug_publish_state_mirror();
                /* Plan v5 Phase D1: probe-driven warp trigger for dual
                 * NES/Gen room-sweep diff. Probe writes:
                 *   ctrl[3]=dest_scene, ctrl[4]=dest_level,
                 *   ctrl[5]=dest_quest, ctrl[6]=dest_room_id,
                 *   ctrl[7]=$5A trigger
                 * We fire roomrom_main_apply_warp_outcome() â€” same code
                 * path as in-game warp coordinator, so dumped state
                 * matches what a real player traversal would produce
                 * (palette, CHR, enemy spawns, NES RAM mirror sync). */
                if (ctrl[7] == 0x5Au) {
                    rr_warp_outcome_t out;
                    out.dest_scene      = ctrl[3];
                    out.dest_level      = ctrl[4];
                    out.dest_quest      = ctrl[5];
                    out.dest_room_id    = ctrl[6];
                    out.dest_link_x     = 120;
                    /* Match the normal UW warp grid (transition.c: spawn Y133). */
                    out.dest_link_y     = (ctrl[3] == SCENE_UW) ? 133 : 128;
                    out.dest_link_face  = ROOMROM_MAIN_LINK_FACE_DOWN;
                    out.dest_redux_flag = 0u;
                    roomrom_main_apply_warp_outcome(&out);
                    ctrl[7] = 0u;  /* ack consumed */
                }
            }
        }
}

#ifndef ROOMROM_NO_STANDALONE_MAIN
int main(bool hardReset)
{
    (void)hardReset;

    roomrom_debug_enter();
    while (TRUE) {
        roomrom_debug_tick();
    }

    return 0;
}
#endif
