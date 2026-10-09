/* enemy_wizzrobe_runtime.c — Wizzrobe family drain.
 *
 * NES sources:
 *   Z_04.asm:7034 UpdateBlueWizzrobe
 *   Z_04.asm:7055 BlueWizzrobe_WalkOrTeleport
 *   Z_04.asm:7100 BlueWizzrobe_TurnSometimesAndMoveAndCheckTile
 *   Z_04.asm:7159 BlueWizzrobe_AdvanceCounterAndTurnTowardLinkIfNeeded
 *   Z_04.asm:7169 BlueWizzrobe_TurnTowardLink
 *   Z_04.asm:7204 BlueWizzrobeTeleportOffsetsX/Y
 *   Z_04.asm:7212 BlueWizzrobe_Move
 *   Z_04.asm:7241 BlueWizzrobe_ChooseTeleportTarget
 *   Z_04.asm:7285 BeginTeleporting
 *   Z_04.asm:7300 BlueWizzrobe_AlignWithNearestSquareAndRandomizeTimer
 *   Z_04.asm:7307 BlueWizzrobe_AlignWithNearestSquare
 *   Z_04.asm:7325 RedWizzrobe_AlignAndSetY
 *   Z_04.asm:7332 WizzrobeCollisionOffsetsX/Y
 *   Z_04.asm:7346 Wizzrobe_GetCollidableTile
 *   Z_04.asm:7386 Wizzrobe_GetBaseCollidableTile
 *   Z_04.asm:7397 BlueWizzrobe_TryShooting
 *   Z_04.asm:7457 ShootMagicShot58 / ShootMagicShot
 *   Z_04.asm:7474 UpdateRedWizzrobe
 *   Z_04.asm:7595 Wizzrobe_DrawAndCheckCollisions
 *
 * Coverage:   FULL (UpdateBlueWizzrobe + UpdateRedWizzrobe verbatim;
 *             Wizzrobe_DrawAndCheckCollisions verbatim).
 * Stance:     GREENFIELD (no prior Wizzrobe drain rows).
 *
 * Wires $23 BlueWizzrobe + $24 RedWizzrobe in enemy_loop.c dispatch.
 * Ganon_MoveAndShoot and burst rays now import these shared movement
 * primitives, including the NES collidable-tile response.
 *
 * RAM cell mapping (NES Variables.inc + ObjVars.inc):
 *   ObjRemDistance              ($0394) -> ENEMY_MOVE_TIMER (alias)
 *   BlueWizzrobe_ObjTurnCounter ($0412) -> OBJ(0x0412, slot)
 *   RedWizzrobe_ObjAnimCounter  ($0412) -> OBJ(0x0412, slot)
 *   RedWizzrobe_ObjFadeCounter  ($0394) -> ENEMY_MOVE_TIMER (alias)
 *   Wizzrobe_ObjLastTile        ($041F) -> OBJ(0x041F, slot)
 *   ObjTimer                    ($0028) -> ENEMY_OBJ_TIMER (alias)
 *   ObjState                    ($00AC) -> ENEMY_STATE_TIMER
 *   ObjDir                      ($0098) -> ENEMY_DIR
 *   ObjX/Y                      ($0070/$0084) -> ENEMY_X/Y
 *   FrameCounter                ($0015)
 *   InvClock                    ($0656) -> RAM(0x0656)
 *   ObjInvincibilityMask        ($04B2) -> OBJ(0x04B2, slot)
 *   ObjInvincibilityTimer       ($04F0) -> ENEMY_HIT_REACTION
 */

#include "enemy_runtime_private.h"
#include "enemy_wizzrobe_runtime.h"
#include "platform_abi.h"
#include "combat/collision_dispatch.h"

/* C-side primitives referenced. Use the umbrella c_check_monster_
 * collisions (already linked) instead of per-weapon NES wrappers —
 * those aren't reachable from the native-runtime path. The umbrella
 * runs the same 4-weapon battery internally. */
extern unsigned char z07_anim_fetch_obj_pos(unsigned int slot);

/* NES Z_04.asm:7204 BlueWizzrobeTeleportOffsetsX. */
static const signed char k_bw_off_x[11] = {
    0, 1, -1, 0, 0, 1, -1, 0, 0, 1, -1
};
/* NES Z_04.asm:7208 BlueWizzrobeTeleportOffsetsY. */
static const signed char k_bw_off_y[11] = {
    0, 0, 0, 0, 1, 1, 1, 0, -1, -1, -1
};

/* NES Z_04.asm:7232 BlueWizzrobeTeleportMaxOffsetsX. */
static const signed char k_bw_max_off_x[4] = {
    (signed char)0xE0, (signed char)0x20, (signed char)0xE0, (signed char)0x20
};
/* NES Z_04.asm:7235 BlueWizzrobeTeleportMaxOffsetsY. */
static const signed char k_bw_max_off_y[4] = {
    (signed char)0xE0, (signed char)0xE0, (signed char)0x20, (signed char)0x20
};
/* NES Z_04.asm:7238 BlueWizzrobeTeleportDirs (8-way: A,9,6,5). */
static const unsigned char k_bw_teleport_dirs[4] = {
    0x0Au, 0x09u, 0x06u, 0x05u
};

/* NES Z_04.asm:7332 WizzrobeCollisionOffsetsX. */
static const signed char k_wiz_coll_off_x[10] = {
    0x0F, 0x00, 0x00, 0x04, 0x08, 0x00, 0x00, 0x04, 0x08, 0x00
};
/* NES Z_04.asm:7336 WizzrobeCollisionOffsetsY. */
static const signed char k_wiz_coll_off_y[10] = {
    0x04, 0x04, 0x00, 0x08, 0x08, 0x08, 0x00, (signed char)0xF8, 0x00, 0x00
};

/* NES Z_04.asm:7502 RedWizzrobeOffsetsX. */
static const signed char k_rw_off_x[16] = {
    0x00, 0x00, (signed char)0xE0, 0x20, 0x00, 0x00, (signed char)0xC0, 0x40,
    0x00, 0x00, (signed char)0xD0, 0x30, 0x00, 0x00, (signed char)0xB0, 0x50
};
/* NES Z_04.asm:7506 RedWizzrobeOffsetsY. */
static const signed char k_rw_off_y[16] = {
    (signed char)0xE0, 0x20, 0x00, 0x00, (signed char)0xC0, 0x40, 0x00, 0x00,
    (signed char)0xD0, 0x30, 0x00, 0x00, (signed char)0xB0, 0x50, 0x00, 0x00
};
/* NES Z_04.asm:7510 RedWizzrobeDirections. */
static const unsigned char k_rw_dirs[4] = {
    0x04u, 0x08u, 0x01u, 0x02u
};

#define WIZ_DIR(s)            ENEMY_DIR(s)
#define WIZ_X(s)              ENEMY_X(s)
#define WIZ_Y(s)              ENEMY_Y(s)
#define WIZ_OBJ_TIMER(s)      OBJ(0x0028u, (s))
#define WIZ_OBJ_STATE(s)      ENEMY_STATE_TIMER(s)
#define WIZ_REM_DIST(s)       OBJ(0x0394u, (s))
#define WIZ_TURN_COUNTER(s)   OBJ(0x0412u, (s))
#define WIZ_ANIM_COUNTER(s)   OBJ(0x0412u, (s))  /* alias for Red */
#define WIZ_FADE_COUNTER(s)   OBJ(0x0394u, (s))  /* alias for Red */
#define WIZ_LAST_TILE(s)      OBJ(0x041Fu, (s))
#define WIZ_INV_MASK(s)       OBJ(0x04B2u, (s))
#define WIZ_INV_TIMER(s)      ENEMY_HIT_REACTION(s)
#define WIZ_LINK_X            RAM(0x0070u)
#define WIZ_LINK_Y            RAM(0x0084u)
#define WIZ_FRAME_COUNTER     RAM(0x0015u)
#define WIZ_INV_CLOCK         RAM(0x066Cu)  /* InvClock (T-147: was $0656 SelectedItemSlot) */
#define WIZ_FIRST_UNWALK      RAM(0x034Au)
#define WIZ_RANDOM(s)         OBJ(0x0018u, (s))  /* Random+slot */

/* NES Z_04.asm:7212 BlueWizzrobe_Move. */
void enrt_blue_wizzrobe_move(unsigned int slot)
{
    unsigned char dir = (unsigned char)WIZ_DIR(slot);
    if (dir < 11u) {
        WIZ_X(slot) = (unsigned char)(WIZ_X(slot) + k_bw_off_x[dir]);
        WIZ_Y(slot) = (unsigned char)(WIZ_Y(slot) + k_bw_off_y[dir]);
    }
}

/* NES Z_04.asm:7325 RedWizzrobe_AlignAndSetY:
 *   AND #$F0 / SEC / SBC #$03 / STA ObjY,X */
static void red_wizzrobe_align_and_set_y(unsigned int slot, unsigned char a)
{
    WIZ_Y(slot) = (unsigned char)((a & 0xF0u) - 0x03u);
}

/* NES Z_04.asm:7307 BlueWizzrobe_AlignWithNearestSquare. */
static void blue_wizzrobe_align_with_nearest_square(unsigned int slot)
{
    unsigned char ax = (unsigned char)(WIZ_X(slot) + 0x08u);
    WIZ_X(slot) = (unsigned char)(ax & 0xF0u);
    unsigned char ay = (unsigned char)(WIZ_Y(slot) + 0x08u);
    red_wizzrobe_align_and_set_y(slot, ay);
}

/* NES Z_04.asm:7300 BlueWizzrobe_AlignWithNearestSquareAndRandomizeTimer:
 *   ObjTimer = Random | $70. Then align. */
static void blue_wizzrobe_align_and_randomize_timer(unsigned int slot)
{
    WIZ_OBJ_TIMER(slot) = (unsigned char)(WIZ_RANDOM(slot) | 0x70u);   /* NES LDA Random,X (T-147) */
    blue_wizzrobe_align_with_nearest_square(slot);
}

/* NES Z_04.asm:7169 BlueWizzrobe_TurnTowardLink. Bit 6 of turn counter
 * gates vertical vs horizontal. */
static void blue_wizzrobe_turn_toward_link(unsigned int slot)
{
    unsigned char counter = (unsigned char)WIZ_TURN_COUNTER(slot);
    unsigned char a;
    if ((counter & 0x40u) == 0u) {
        /* Horizontal. */
        a = 0x02u;
        if ((unsigned char)WIZ_X(slot) < (unsigned char)WIZ_LINK_X) {
            a >>= 1;
        }
    } else {
        /* Vertical. */
        a = 0x08u;
        if ((unsigned char)WIZ_Y(slot) < (unsigned char)WIZ_LINK_Y) {
            a >>= 1;
        }
    }
    if (a != (unsigned char)WIZ_DIR(slot)) {
        WIZ_DIR(slot) = a;
        blue_wizzrobe_align_with_nearest_square(slot);
    }
}

/* NES Z_04.asm:7386 Wizzrobe_GetBaseCollidableTile.
 * Returns C=0 if walkable. */
static unsigned char wizzrobe_get_base_collidable_tile(unsigned int slot)
{
    unsigned char tile = collision_get_collidable_tile_still(slot);
    WIZ_LAST_TILE(slot) = tile;
    return (unsigned char)(tile >= (unsigned char)WIZ_FIRST_UNWALK);
}

/* NES Z_04.asm:7357 Wizzrobe_GetCollidableTileForDir.
 * Saves/restores X,Y around base collision check. */
static unsigned char wizzrobe_get_collidable_tile_for_dir(unsigned int slot,
                                                          unsigned char y_dir)
{
    /* DEY: 8-way index = dir - 1. */
    unsigned char idx = (unsigned char)(y_dir - 1u);
    signed char off_x, off_y;
    if (idx == 0xFFu) {
        /* NES dir=0 underflows DEY. Absolute,Y uses unsigned $FF, so
         * it reads 255 bytes AFTER each table, not before it. USA ROM
         * file $11F28+$FF / $11F32+$FF holds $F0 / $9F. Ganon takes
         * this path in Q1 L9; retain the NES's in-bank lookup. */
        off_x = (signed char)0xF0;
        off_y = (signed char)0x9F;
    } else {
        if (idx >= 10u) return 0u;
        off_x = k_wiz_coll_off_x[idx];
        off_y = k_wiz_coll_off_y[idx];
    }

    unsigned char saved_x = (unsigned char)WIZ_X(slot);
    unsigned char saved_y = (unsigned char)WIZ_Y(slot);
    WIZ_X(slot) = (unsigned char)(saved_x + off_x);
    WIZ_Y(slot) = (unsigned char)(saved_y + off_y);
    unsigned char c = wizzrobe_get_base_collidable_tile(slot);
    WIZ_X(slot) = saved_x;
    WIZ_Y(slot) = saved_y;
    return c;
}

/* NES Z_04.asm:7346 Wizzrobe_GetCollidableTile (uses ObjDir). */
static unsigned char wizzrobe_get_collidable_tile(unsigned int slot)
{
    return wizzrobe_get_collidable_tile_for_dir(slot, (unsigned char)WIZ_DIR(slot));
}

/* NES Z_04.asm:7285 BeginTeleporting. */
static void begin_teleporting(unsigned int slot)
{
    WIZ_REM_DIST(slot) = 0x20u;
    WIZ_TURN_COUNTER(slot) = (unsigned char)(WIZ_TURN_COUNTER(slot) ^ 0x40u);
    WIZ_OBJ_TIMER(slot) = 0u;
    blue_wizzrobe_align_with_nearest_square(slot);
}

/* NES Z_04.asm:7241 BlueWizzrobe_ChooseTeleportTarget. */
static void blue_wizzrobe_choose_teleport_target(unsigned int slot)
{
    unsigned char rand = (unsigned char)WIZ_RANDOM(slot);   /* NES LDA Random,X (T-147) */
    unsigned char rand_idx = (unsigned char)(rand & 0x03u);

    unsigned char orig_x = (unsigned char)WIZ_X(slot);
    unsigned char orig_y = (unsigned char)WIZ_Y(slot);
    WIZ_X(slot) = (unsigned char)(orig_x + k_bw_max_off_x[rand_idx]);
    WIZ_Y(slot) = (unsigned char)(orig_y + k_bw_max_off_y[rand_idx]);

    unsigned char dir_check = k_bw_teleport_dirs[rand_idx];
    unsigned char c = wizzrobe_get_collidable_tile_for_dir(slot, dir_check);

    WIZ_Y(slot) = orig_y;
    WIZ_X(slot) = orig_x;

    if (c != 0u) {
        blue_wizzrobe_align_and_randomize_timer(slot);
        return;
    }
    WIZ_DIR(slot) = k_bw_teleport_dirs[rand_idx];
    begin_teleporting(slot);
}

/* NES Z_04.asm:7100 BlueWizzrobe_TurnSometimesAndMoveAndCheckTile.
 * Includes the @HitWall / @HitBlockOrWater branches at 7104+. */
static void blue_wizzrobe_move_and_check_tile(unsigned int slot)
{
    enrt_blue_wizzrobe_move(slot);
    /* Z_04.asm:7104-7124 collidable-tile check. */
    if (wizzrobe_get_collidable_tile(slot) == 0u) return;

    unsigned char last = (unsigned char)(WIZ_LAST_TILE(slot) & 0xFCu);
    if (last == 0xB0u || last >= 0xF4u) {
        /* @HitBlockOrWater. */
        if (WIZ_REM_DIST(slot) != 0u) return;
        begin_teleporting(slot);
        return;
    }
    /* @HitWall: flip facing axis. Z_04.asm:7126-7148. */
    unsigned char dir = (unsigned char)WIZ_DIR(slot);
    if ((dir & 0x0Cu) != 0u) {
        dir = (unsigned char)(dir ^ 0x0Cu);
        WIZ_DIR(slot) = dir;
    }
    if ((dir & 0x03u) != 0u) {
        dir = (unsigned char)(dir ^ 0x03u);
        WIZ_DIR(slot) = dir;
    }
    enrt_blue_wizzrobe_move(slot);
}

/* NES Z_04.asm:7159 BlueWizzrobe_AdvanceCounterAndTurnTowardLinkIfNeeded. */
static void blue_wizzrobe_advance_counter_and_turn(unsigned int slot)
{
    unsigned char counter = (unsigned char)(WIZ_TURN_COUNTER(slot) + 1u);
    WIZ_TURN_COUNTER(slot) = counter;
    if ((counter & 0x3Fu) == 0u) {
        blue_wizzrobe_turn_toward_link(slot);
    }
}

/* NES Z_04.asm:7100 — shared with Ganon_MoveAndShoot. */
void enrt_blue_wizzrobe_turn_sometimes_and_move_and_check_tile(unsigned int slot)
{
    blue_wizzrobe_advance_counter_and_turn(slot);
    blue_wizzrobe_move_and_check_tile(slot);
}

/* NES Z_04.asm:7055 BlueWizzrobe_WalkOrTeleport. */
static void blue_wizzrobe_walk_or_teleport(unsigned int slot)
{
    unsigned char timer = (unsigned char)WIZ_OBJ_TIMER(slot);
    if (timer == 0u) {
        /* Teleporting. */
        if (WIZ_REM_DIST(slot) != 0u) {
            WIZ_REM_DIST(slot) = (unsigned char)(WIZ_REM_DIST(slot) - 1u);
            blue_wizzrobe_move_and_check_tile(slot);
            return;
        }
        /* RemDist == 0: align + new timer + turn. */
        blue_wizzrobe_align_and_randomize_timer(slot);
        blue_wizzrobe_turn_toward_link(slot);
        return;
    }
    /* Walking branch. */
    if (timer >= 0x10u) {
        /* Every other frame. */
        if ((WIZ_FRAME_COUNTER & 1u) != 0u) {
            /* NES BCS L_BlueWizzrobe_TurnTowardLinkIfNeeded: turn only on
             * a multiple of $40 (T-147: the drain turned every odd frame,
             * t129 t4661 dir $02 -> $01 and a square snap). */
            if (((unsigned char)WIZ_TURN_COUNTER(slot) & 0x3Fu) == 0u)
                blue_wizzrobe_turn_toward_link(slot);
            return;
        }
        enrt_blue_wizzrobe_turn_sometimes_and_move_and_check_tile(slot);
        return;
    }
    if (timer == 1u) {
        blue_wizzrobe_choose_teleport_target(slot);
    }
}

/* NES Z_04.asm:7462 ShootMagicShot — InvClock gate + sound + shoot. */
static void shoot_magic_shot(unsigned int slot, unsigned char shot_type)
{
    if ((unsigned char)WIZ_INV_CLOCK != 0u) return;
    RAM(0x0604u) = 0x04u;  /* Tune0Request ($0604; was $0608, T-171) = magic sound */
    /* Store shot type at [00] per NES STA $00 prologue. */
    RAM(0x0000u) = shot_type;
    (void)c_shoot_limited(slot);
}

/* NES Z_04.asm:7397 BlueWizzrobe_TryShooting. */
static void blue_wizzrobe_try_shooting(unsigned int slot)
{
    if (WIZ_REM_DIST(slot) != 0u) return;
    if ((WIZ_FRAME_COUNTER & 0x1Fu) != 0u) return;

    /* Same square row? (both Y & $F0 equal) */
    if (((unsigned char)WIZ_Y(slot) & 0xF0u) ==
        ((unsigned char)WIZ_LINK_Y & 0xF0u)) {
        unsigned char a = 0x02u;
        if ((unsigned char)WIZ_X(slot) < (unsigned char)WIZ_LINK_X) {
            a >>= 1;
        }
        if (a == (unsigned char)WIZ_DIR(slot)) {
            shoot_magic_shot(slot, 0x58u);
        }
        return;
    }
    /* NES BlueWizzrobe_CheckSquareColumn masks only the monster X.
     * Link must be exactly on that column boundary; masking Link too
     * incorrectly permits vertical shots while he is between columns. */
    if (((unsigned char)WIZ_X(slot) & 0xF0u) !=
        (unsigned char)WIZ_LINK_X) return;
    unsigned char a = 0x08u;
    if ((unsigned char)WIZ_Y(slot) < (unsigned char)WIZ_LINK_Y) {
        a >>= 1;
    }
    if (a == (unsigned char)WIZ_DIR(slot)) {
        shoot_magic_shot(slot, 0x58u);
    }
}

/* NES Z_04.asm:7595 Wizzrobe_DrawAndCheckCollisions. */
static void wizzrobe_draw_and_check_collisions(unsigned int slot)
{
    WIZ_INV_MASK(slot) = 0xF6u;  /* Sword/bomb-only. */
    c_get_object_middle(slot);
    if (WIZ_INV_TIMER(slot) == 0u) {
        /* NES Z_04.asm:7605-7612: exactly these four checks, in this
         * order (no boomerang/arrow check, Link collision once below).
         * The CheckMonsterCollisions umbrella added both and a second
         * Link check (T-171). */
        collision_check_monster_sword_shot_or_magic_shot_collision(slot, 0x0Eu);
        collision_check_monster_bomb_or_fire_collision(slot, 0x10u);
        collision_check_monster_bomb_or_fire_collision(slot, 0x11u);
        collision_check_monster_sword_collision(slot, 0x0Du);
    }
    c_check_link_collision(slot);

    /* Anim: every 4 frames toggle frame 0/1 (for Red); reuse anim
     * counter low bits. For Blue this still uses Z_04.asm:7620
     * RedWizzrobe_ObjAnimCounter aliased to BlueWizzrobe_ObjTurnCounter
     * at $0412 — both types share frame logic. */
    unsigned char frame = (unsigned char)((WIZ_ANIM_COUNTER(slot) >> 2) & 0x01u);
    (void)z07_anim_fetch_obj_pos(slot);
    unsigned char dir = (unsigned char)WIZ_DIR(slot);
    if ((dir & 0x08u) != 0u) {
        /* Facing up: frames 2-3, mirrored. */
        c_draw_object_mirrored_with_frame((unsigned int)(frame + 2u), slot);
    } else {
        /* Horizontal: NES LSR / AND #$01 flips when facing left ($02). */
        RAM(0x000Fu) = (unsigned char)((dir >> 1) & 0x01u);
        c_draw_object_not_mirrored_with_frame((unsigned int)frame, slot);
    }
}

/* NES Z_04.asm:7039 Wizzrobe_DrawAndCheckCollisionsIntermittently:
 * If teleport-distance remaining is even, draw + collide; else skip. */
static void wizzrobe_draw_intermittently(unsigned int slot)
{
    if ((WIZ_REM_DIST(slot) & 1u) != 0u) return;
    wizzrobe_draw_and_check_collisions(slot);
}

/* NES Z_04.asm:7034 UpdateBlueWizzrobe. */
void enrt_update_blue_wizzrobe(unsigned int slot)
{
    if ((unsigned char)WIZ_INV_CLOCK != 0u) {
        wizzrobe_draw_and_check_collisions(slot);
        return;
    }
    blue_wizzrobe_walk_or_teleport(slot);
    blue_wizzrobe_try_shooting(slot);
    wizzrobe_draw_intermittently(slot);
}

/* NES Z_04.asm:7513 UpdateRedWizzrobe_1 / fade group. */
static void red_wizzrobe_state_1(unsigned int slot)
{
    if (WIZ_OBJ_STATE(slot) == 0x7Fu) {
        WIZ_OBJ_STATE(slot) = 0x4Fu;
    }
    WIZ_FADE_COUNTER(slot) = (unsigned char)(WIZ_FADE_COUNTER(slot) + 1u);
    /* DrawAndCheckCollisionsIntermittently: reuse fade-counter low bit. */
    if ((WIZ_FADE_COUNTER(slot) & 1u) != 0u) return;
    wizzrobe_draw_and_check_collisions(slot);
}

/* NES Z_04.asm:7532 UpdateRedWizzrobe_3 / state $FF: re-place. */
static void red_wizzrobe_state_3(unsigned int slot)
{
    unsigned char state = (unsigned char)WIZ_OBJ_STATE(slot);
    if (state != 0xFFu) {
        /* Y inc != 0 path: jmp to UpdateRedWizzrobe_1. */
        red_wizzrobe_state_1(slot);
        return;
    }
    /* State $FF: random place. */
    unsigned char rand = (unsigned char)WIZ_RANDOM(slot);   /* NES LDA Random,X (T-147) */
    unsigned char dir_idx = (unsigned char)(rand & 0x03u);
    WIZ_DIR(slot) = k_rw_dirs[dir_idx];

    unsigned char off_idx = (unsigned char)(rand & 0x0Fu);
    unsigned char new_x = (unsigned char)(WIZ_LINK_X + k_rw_off_x[off_idx]);
    WIZ_X(slot) = (unsigned char)(new_x & 0xF0u);

    unsigned char ay = (unsigned char)(WIZ_LINK_Y + 0x03u);
    ay = (unsigned char)(ay + k_rw_off_y[off_idx]);
    red_wizzrobe_align_and_set_y(slot, ay);

    if (WIZ_Y(slot) >= 0x5Du && WIZ_Y(slot) < 0xC4u) {
        if (wizzrobe_get_collidable_tile(slot) == 0u) {
            /* UpdateRedWizzrobe_0: RTS. */
            return;
        }
    }
    WIZ_OBJ_STATE(slot) = (unsigned char)(WIZ_OBJ_STATE(slot) + 1u);
}

/* NES Z_04.asm:7585 UpdateRedWizzrobe_2: state $B0 = shoot magic $59. */
static void red_wizzrobe_state_2(unsigned int slot)
{
    if ((unsigned char)WIZ_OBJ_STATE(slot) == 0xB0u) {
        shoot_magic_shot(slot, 0x59u);
    }
    wizzrobe_draw_and_check_collisions(slot);
}

/* NES Z_04.asm:7474 UpdateRedWizzrobe. */
void enrt_update_red_wizzrobe(unsigned int slot)
{
    if ((unsigned char)WIZ_INV_CLOCK != 0u) {
        wizzrobe_draw_and_check_collisions(slot);
        return;
    }
    WIZ_ANIM_COUNTER(slot) = (unsigned char)(WIZ_ANIM_COUNTER(slot) + 1u);
    WIZ_OBJ_STATE(slot) = (unsigned char)(WIZ_OBJ_STATE(slot) - 1u);

    /* State >> 6 -> 0..3. */
    unsigned char idx = (unsigned char)((WIZ_OBJ_STATE(slot) >> 6) & 0x03u);
    switch (idx) {
    case 0u:
        /* UpdateRedWizzrobe_0: RTS. */
        break;
    case 1u:
        red_wizzrobe_state_1(slot);
        break;
    case 2u:
        red_wizzrobe_state_2(slot);
        break;
    case 3u:
    default:
        red_wizzrobe_state_3(slot);
        break;
    }
}
