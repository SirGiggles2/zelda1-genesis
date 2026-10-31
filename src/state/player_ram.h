#ifndef PLAYER_RAM_H
#define PLAYER_RAM_H

#include "player_records.h"
#include "game_ram_reserve.h"

/* T-367 fast path unit 2: layout only. Existing owners stay authoritative. */
#define PLAYER_RAM_COUNT             4u
#define PLAYER_RAM_STRIDE            0x80u
#define PLAYER_RAM_SNAPSHOT_OFF      0x0200u
#define PLAYER_RAM_SNAPSHOT_BYTES    0x0200u
#define PLAYER_RAM_MASK_OFF          0x0400u
#define PLAYER_RAM_MASK_BYTES        0x0040u
#define PLAYER_RAM_REFS_OFF          0x0440u
#define PLAYER_RAM_OBJECT_COUNT      20u
#define PLAYER_RAM_USED              0x04E0u
#define PLAYER_RAM_CANARY_OFF        0x04E0u
#define PLAYER_RAM_SELECTOR_OFF      0x04E4u  /* g_cur_player: A4-relative, after used canary */
#define PLAYER_RAM_END_CANARY_OFF    (GR_RESERVED - 4u)
#define PLAYER_REF_NONE              0xFFu
#define PLAYER_REF_POINT             0xFEu

typedef struct ArrowRodRam {
    unsigned char x, y, dir, state, anim_counter, qspeed, grid, pos_frac;
} ArrowRodRam;

typedef struct ObjectPlayerRefs {
    unsigned char capture_player, target_player, status_player, pickup_player;
    unsigned short generation, flags;
} ObjectPlayerRefs;

typedef struct PlayerRam {
    PlayerState actor;
    unsigned char obj_dir, shot_collision_flag, ladder_slot, collided_tile;
    unsigned char doorway_nes, paralyzed, sword_blocked, warp_exit_type;
    PlayerSwordState sword;
    PlayerBeamState beam;
    ArrowRodRam arrow_rod;
    signed short tick_start_y;
    unsigned char tick_start_grid, tick_start_frac, tick_start_anim, tick_start_frame;
    unsigned short tick_start_held;
    unsigned char sword_qspeed, bubble_block_timer;
    unsigned char s_link_subx, s_link_suby; /* unit 4c: ALTTP per-axis sub-pixel storage (live; record 0) */
    /* unit 4c: native host representations (independent of actor.* fields) */
    unsigned char host_dir, host_doorway;
    signed char host_grid;
    unsigned char host_pos_frac, host_frame, host_anim_tick, host_draw_pending;
    /* NES source: CheckWarps/Link_EndMoveAndAnimate exit guard.
     * Drained C: LinkCtx and transition OW step observation.
     * Coverage: PARTIAL per-actor functional warp context, same128B stride.
     * Stance: EXTEND named reserve bytes, no RAM5-11 migration. */
    unsigned char reserved[45];
    unsigned char warp_exit_host;
    signed char warp_prev_grid;
} PlayerRam;

#define GR_ASSERT_OFF(t, f, n) _Static_assert(__builtin_offsetof(t, f) == (n), #t "." #f)
_Static_assert(sizeof(unsigned char) == 1u && sizeof(short) == 2u, "native widths");
_Static_assert(sizeof(PlayerState) == 28u, "actor stride");
_Static_assert(sizeof(PlayerSwordState) == 10u, "sword stride");
_Static_assert(sizeof(PlayerBeamState) == 8u, "beam stride");
_Static_assert(sizeof(ArrowRodRam) == 8u, "arrow stride");
_Static_assert(sizeof(ObjectPlayerRefs) == 8u, "reference stride");
_Static_assert(sizeof(PlayerRam) == PLAYER_RAM_STRIDE, "player stride");
_Static_assert(__alignof__(PlayerState) == 2u && __alignof__(PlayerSwordState) == 2u
               && __alignof__(PlayerRam) == 2u && __alignof__(ObjectPlayerRefs) == 2u,
               "native short alignment; no packed records");
GR_ASSERT_OFF(PlayerState, x, 0u);
GR_ASSERT_OFF(PlayerState, x_hi, 0u);
GR_ASSERT_OFF(PlayerState, x_lo, 1u);
GR_ASSERT_OFF(PlayerState, y, 2u);
GR_ASSERT_OFF(PlayerState, y_hi, 2u);
GR_ASSERT_OFF(PlayerState, y_lo, 3u);
GR_ASSERT_OFF(PlayerState, pos_frac, 4u);
GR_ASSERT_OFF(PlayerState, qspeed_frac, 5u);
GR_ASSERT_OFF(PlayerState, dir, 6u);
GR_ASSERT_OFF(PlayerState, face, 7u);
GR_ASSERT_OFF(PlayerState, input_dir, 8u);
GR_ASSERT_OFF(PlayerState, moving_dir, 9u);
GR_ASSERT_OFF(PlayerState, state, 10u);
GR_ASSERT_OFF(PlayerState, go_straight, 11u);
GR_ASSERT_OFF(PlayerState, doorway_dir, 12u);
GR_ASSERT_OFF(PlayerState, grid_offset, 13u);
GR_ASSERT_OFF(PlayerState, anim_counter, 14u);
GR_ASSERT_OFF(PlayerState, anim_frame, 15u);
GR_ASSERT_OFF(PlayerState, invincibility_timer, 16u);
GR_ASSERT_OFF(PlayerState, invincibility_mask, 17u);
GR_ASSERT_OFF(PlayerState, shove_dir, 18u);
GR_ASSERT_OFF(PlayerState, shove_distance, 19u);
GR_ASSERT_OFF(PlayerState, shoot_timer, 20u);
GR_ASSERT_OFF(PlayerState, halt_flag, 21u);
GR_ASSERT_OFF(PlayerState, hp, 22u);
GR_ASSERT_OFF(PlayerState, anim_tick, 23u);
GR_ASSERT_OFF(PlayerState, draw_pending, 24u);
GR_ASSERT_OFF(PlayerState, render_frame, 25u);
GR_ASSERT_OFF(PlayerState, go_straight_when_diag, 26u);
GR_ASSERT_OFF(PlayerSwordState, state, 0u);
GR_ASSERT_OFF(PlayerSwordState, timer, 1u);
GR_ASSERT_OFF(PlayerSwordState, dir, 2u);
GR_ASSERT_OFF(PlayerSwordState, face, 3u);
GR_ASSERT_OFF(PlayerSwordState, owner, 4u);
GR_ASSERT_OFF(PlayerSwordState, x, 6u);
GR_ASSERT_OFF(PlayerSwordState, x_hi, 6u);
GR_ASSERT_OFF(PlayerSwordState, x_lo, 7u);
GR_ASSERT_OFF(PlayerSwordState, y, 8u);
GR_ASSERT_OFF(PlayerSwordState, y_hi, 8u);
GR_ASSERT_OFF(PlayerSwordState, y_lo, 9u);
GR_ASSERT_OFF(PlayerBeamState, state, 0u);
GR_ASSERT_OFF(PlayerBeamState, dir, 1u);
GR_ASSERT_OFF(PlayerBeamState, grid, 2u);
GR_ASSERT_OFF(PlayerBeamState, qspeed, 3u);
GR_ASSERT_OFF(PlayerBeamState, owner, 4u);
GR_ASSERT_OFF(PlayerBeamState, x, 5u);
GR_ASSERT_OFF(PlayerBeamState, y, 6u);
GR_ASSERT_OFF(PlayerBeamState, pos_frac, 7u);
GR_ASSERT_OFF(ArrowRodRam, x, 0u);
GR_ASSERT_OFF(ArrowRodRam, y, 1u);
GR_ASSERT_OFF(ArrowRodRam, dir, 2u);
GR_ASSERT_OFF(ArrowRodRam, state, 3u);
GR_ASSERT_OFF(ArrowRodRam, anim_counter, 4u);
GR_ASSERT_OFF(ArrowRodRam, qspeed, 5u);
GR_ASSERT_OFF(ArrowRodRam, grid, 6u);
GR_ASSERT_OFF(ArrowRodRam, pos_frac, 7u);
GR_ASSERT_OFF(ObjectPlayerRefs, capture_player, 0u);
GR_ASSERT_OFF(ObjectPlayerRefs, target_player, 1u);
GR_ASSERT_OFF(ObjectPlayerRefs, status_player, 2u);
GR_ASSERT_OFF(ObjectPlayerRefs, pickup_player, 3u);
GR_ASSERT_OFF(ObjectPlayerRefs, generation, 4u);
GR_ASSERT_OFF(ObjectPlayerRefs, flags, 6u);
GR_ASSERT_OFF(PlayerRam, actor, 0u);
GR_ASSERT_OFF(PlayerRam, obj_dir, 28u);
GR_ASSERT_OFF(PlayerRam, shot_collision_flag, 29u);
GR_ASSERT_OFF(PlayerRam, ladder_slot, 30u);
GR_ASSERT_OFF(PlayerRam, collided_tile, 31u);
GR_ASSERT_OFF(PlayerRam, doorway_nes, 32u);
GR_ASSERT_OFF(PlayerRam, paralyzed, 33u);
GR_ASSERT_OFF(PlayerRam, sword_blocked, 34u);
GR_ASSERT_OFF(PlayerRam, warp_exit_type, 35u);
GR_ASSERT_OFF(PlayerRam, sword, 36u);
GR_ASSERT_OFF(PlayerRam, beam, 46u);
GR_ASSERT_OFF(PlayerRam, arrow_rod, 54u);
GR_ASSERT_OFF(PlayerRam, tick_start_y, 62u);
GR_ASSERT_OFF(PlayerRam, tick_start_grid, 64u);
GR_ASSERT_OFF(PlayerRam, tick_start_frac, 65u);
GR_ASSERT_OFF(PlayerRam, tick_start_anim, 66u);
GR_ASSERT_OFF(PlayerRam, tick_start_frame, 67u);
GR_ASSERT_OFF(PlayerRam, tick_start_held, 68u);
GR_ASSERT_OFF(PlayerRam, sword_qspeed, 70u);
GR_ASSERT_OFF(PlayerRam, bubble_block_timer, 71u);
GR_ASSERT_OFF(PlayerRam, s_link_subx, 72u);
GR_ASSERT_OFF(PlayerRam, s_link_suby, 73u);
GR_ASSERT_OFF(PlayerRam, host_dir, 74u);
GR_ASSERT_OFF(PlayerRam, host_doorway, 75u);
GR_ASSERT_OFF(PlayerRam, host_grid, 76u);
GR_ASSERT_OFF(PlayerRam, host_pos_frac, 77u);
GR_ASSERT_OFF(PlayerRam, host_frame, 78u);
GR_ASSERT_OFF(PlayerRam, host_anim_tick, 79u);
GR_ASSERT_OFF(PlayerRam, host_draw_pending, 80u);
GR_ASSERT_OFF(PlayerRam, reserved, 81u);
GR_ASSERT_OFF(PlayerRam, warp_exit_host, 126u);
GR_ASSERT_OFF(PlayerRam, warp_prev_grid, 127u);
_Static_assert(81u + sizeof(((PlayerRam *)0)->reserved) + 2u == PLAYER_RAM_STRIDE, "record reserve tail");

_Static_assert(PLAYER_RAM_COUNT * sizeof(PlayerRam) == PLAYER_RAM_SNAPSHOT_OFF, "four records");
_Static_assert(PLAYER_RAM_SNAPSHOT_OFF + PLAYER_RAM_SNAPSHOT_BYTES == PLAYER_RAM_MASK_OFF, "snapshot extent");
_Static_assert(PLAYER_RAM_MASK_OFF + PLAYER_RAM_MASK_BYTES == PLAYER_RAM_REFS_OFF, "mask extent");
_Static_assert(PLAYER_RAM_MASK_BYTES * 8u == PLAYER_RAM_SNAPSHOT_BYTES, "one bit per D/P byte");
_Static_assert(PLAYER_RAM_REFS_OFF + PLAYER_RAM_OBJECT_COUNT * sizeof(ObjectPlayerRefs) == PLAYER_RAM_USED, "sidecar extent");
_Static_assert(PLAYER_RAM_USED == PLAYER_RAM_CANARY_OFF, "used boundary guard");
_Static_assert(PLAYER_RAM_CANARY_OFF + 4u <= PLAYER_RAM_END_CANARY_OFF, "separate outer guards");
_Static_assert(PLAYER_RAM_SELECTOR_OFF >= PLAYER_RAM_CANARY_OFF + 4u && PLAYER_RAM_SELECTOR_OFF < PLAYER_RAM_END_CANARY_OFF, "selector in reserve");
_Static_assert(PLAYER_RAM_END_CANARY_OFF + 4u == GR_RESERVED, "reserve end guard");
_Static_assert(PLAYER_RAM_USED <= GR_RESERVED, "layout fits reserved block");
_Static_assert(GR_OFF + PLAYER_RAM_USED < 0x8000u, "used positive A4 reach");
_Static_assert(GR_OFF + GR_RESERVED < 0x8000u, "positive A4 reach");
_Static_assert((GR_OFF % 2u) == 0u && (PLAYER_RAM_REFS_OFF % 2u) == 0u, "aligned views");

/* nes_ram is declared before these bindings; primitive shapes never include ABI. */
#include "platform_abi.h"
#define G_PLAYERS ((PlayerRam *)(void *)(nes_ram + GR_OFF))
#define G_PLAYER_RAM_SNAPSHOT ((unsigned char *)(void *)(nes_ram + GR_OFF + PLAYER_RAM_SNAPSHOT_OFF))
#define G_PLAYER_RAM_MASK ((unsigned char *)(void *)(nes_ram + GR_OFF + PLAYER_RAM_MASK_OFF))
#define G_OBJECT_PLAYER_REFS ((ObjectPlayerRefs *)(void *)(nes_ram + GR_OFF + PLAYER_RAM_REFS_OFF))
/* Selector lives in the native block (plan 478: A4-relative byte load), zeroed by C4; not .bss. */
#define g_cur_player (nes_ram[GR_OFF + PLAYER_RAM_SELECTOR_OFF])

/* Boot-only, AFTER successful C4 zeroing. No ordinary-tick readers or resets. */
void player_ram_init_inert(void);
unsigned char player_ram_canaries_ok(void);

#endif /* PLAYER_RAM_H */
