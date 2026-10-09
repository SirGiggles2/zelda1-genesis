/* collision_dispatch.c — native collision subsystem dispatch (Phase 4).
 *
 * Drain MATCH per drain in src/oracle/combat/collision_runtime.c
 * (verified-by-use; in production via Zelda.md gameplay).
 */

#include "collision_dispatch.h"
#include <stdint.h>
#include "platform_abi.h"
#include "combat_state.h"      /* COMBAT_*, COMBAT_HITBOX_X/Y, COMBAT_THRESHOLD_X/Y, COMBAT_COLLIDED, COMBAT_ABS_DX/DY, COMBAT_PART_INDEX */
#include "enemy_state.h"       /* ENEMY_GLEEOK_NECK_Y_PTR_LO/HI, ENEMY_COLLIDED_TILE, ENEMY_DARK_ROOM_FLAG, ENEMY_CANDLE_ROOM_ID, ENEMY_PLAYER_OBJ_X/Y */
#include "object_state.h"      /* OBJ_TILE_X/_Y, OBJ_X, OBJ_Y, OBJ_DIR, OBJ_STATE */
#include "world_state.h"       /* LINK_DIR */
#include "core/core_dispatch.h"        /* core_play_parry_tune, core_handle_shot_blocked */
#include "combat/combat_dispatch.h"    /* combat_play_parry_sound_for_damage_type, combat_deal_damage */
#include "combat/link_collision_dispatch.h" /* link_collision_begin_shove */
#include "enemies/enemy_dispatch.h"    /* enemy_gohma_handle_weapon_collision */

/* NES Z_07.asm PlayAreaColumnAddrs (line 336): 32 LE 16-bit pointers
 * pointing into nes_ram (SRAM range $6500..$67DA). Baked inline. */
static const unsigned char k_play_area_column_addrs[64] = {
    0x30u, 0x65u, 0x46u, 0x65u, 0x5Cu, 0x65u, 0x72u, 0x65u,
    0x88u, 0x65u, 0x9Eu, 0x65u, 0xB4u, 0x65u, 0xCAu, 0x65u,
    0xE0u, 0x65u, 0xF6u, 0x65u, 0x0Cu, 0x66u, 0x22u, 0x66u,
    0x38u, 0x66u, 0x4Eu, 0x66u, 0x64u, 0x66u, 0x7Au, 0x66u,
    0x90u, 0x66u, 0xA6u, 0x66u, 0xBCu, 0x66u, 0xD2u, 0x66u,
    0xE8u, 0x66u, 0xFEu, 0x66u, 0x14u, 0x67u, 0x2Au, 0x67u,
    0x40u, 0x67u, 0x56u, 0x67u, 0x6Cu, 0x67u, 0x82u, 0x67u,
    0x98u, 0x67u, 0xAEu, 0x67u, 0xC4u, 0x67u, 0xDAu, 0x67u
};

/* NES Z_07.asm WalkableTiles (line 2099): 9 bytes. */
/* NES WalkableTiles (9 entries) as a lookup by tile id (T-125: the 9-entry
 * scan ran for every object on every frame). */
static const unsigned char k_walkable_tile[256] = {
    [0x8Du] = 1u, [0x91u] = 1u, [0x9Cu] = 1u, [0xACu] = 1u, [0xADu] = 1u,
    [0xCCu] = 1u, [0xD2u] = 1u, [0xD5u] = 1u, [0xDFu] = 1u
};

/* SwordDamagePoints[3] — sword level 1/2/3 damage. NES drain
 * at collision_runtime.c:6. */
static const unsigned char k_sword_damage_points[3] = {
    0x10u, 0x20u, 0x40u
};

unsigned char collision_do_objects_collide_with_thresholds(void)
{
    /* drain at collision_runtime.c:8-28. */
    COMBAT_COLLIDED = 0u;
    {
        const unsigned char dx =
            (unsigned char)(ENEMY_GLEEOK_NECK_Y_PTR_LO - COMBAT_HITBOX_X);
        const unsigned char abs_dx =
            (dx & 0x80u) ? (unsigned char)((~dx + 1u) & 0xFFu) : dx;
        COMBAT_ABS_DX = abs_dx;
        if (abs_dx >= COMBAT_THRESHOLD_X) {
            return COMBAT_COLLIDED;
        }
    }
    {
        const unsigned char dy =
            (unsigned char)(ENEMY_GLEEOK_NECK_Y_PTR_HI - COMBAT_HITBOX_Y);
        const unsigned char abs_dy =
            (dy & 0x80u) ? (unsigned char)((~dy + 1u) & 0xFFu) : dy;
        COMBAT_ABS_DY = abs_dy;
        if (abs_dy >= COMBAT_THRESHOLD_Y) {
            return COMBAT_COLLIDED;
        }
    }
    COMBAT_COLLIDED = (uint8_t)(COMBAT_COLLIDED + 1u);
    return COMBAT_COLLIDED;
}

unsigned char collision_do_objects_collide(unsigned int threshold)
{
    /* drain at collision_runtime.c:30-34. */
    COMBAT_THRESHOLD_X = (uint8_t)threshold;
    COMBAT_THRESHOLD_Y = (uint8_t)threshold;
    return collision_do_objects_collide_with_thresholds();
}

/* Leaves the column address in [00:01] as the NES does (CheckLadder reads
 * [00] after GetCollidingTileMoving; T-171 compares it everywhere). */
static inline __attribute__((always_inline)) unsigned char
collidable_tile(unsigned int hotspot_offset, unsigned int slot)
{
    /* drain at collision_runtime.c:239-308. NES GetCollidableTile. */
    COMBAT_HITBOX_X = (uint8_t)hotspot_offset;
    const unsigned char y_pos = (unsigned char)OBJ_TILE_Y(slot);
    const unsigned char adjusted_y = (uint8_t)(y_pos + 0x0Bu);
    const unsigned char dir = COMBAT_PART_INDEX;

    unsigned char tile_y = adjusted_y;
    unsigned char tile_x;

    if (dir & 0x0Cu) {
        if (dir & 0x04u) {
            if (adjusted_y < 0xDDu) {
                tile_y =
                    (uint8_t)(adjusted_y + (unsigned char)hotspot_offset);
            }
        } else {
            tile_y = (uint8_t)(adjusted_y + (unsigned char)hotspot_offset);
        }
        tile_x = (unsigned char)OBJ_TILE_X(slot);
    } else {
        tile_x = (unsigned char)OBJ_TILE_X(slot);
        if (dir & 0x01u) {
            if (tile_x < 0xF0u) {
                tile_x = (uint8_t)(tile_x + (unsigned char)hotspot_offset);
            }
        } else {
            if (tile_x >= 0x10u) {
                tile_x = (uint8_t)(tile_x + (unsigned char)hotspot_offset);
            }
        }
    }

    const unsigned char col_idx = (unsigned char)((tile_x & 0xF8u) >> 2);
    const unsigned short col_addr =
        (unsigned short)(((unsigned short)k_play_area_column_addrs[col_idx]) |
                         ((unsigned short)k_play_area_column_addrs[col_idx + 1u] << 8));
    nes_ram[0x0000u] = k_play_area_column_addrs[col_idx];
    nes_ram[0x0001u] = k_play_area_column_addrs[col_idx + 1u];

    /* NES SBC #$40 wraps at 8 bits before the LSRs (T-147: a red leever
     * placed at Y $FD -> adjusted Y $08 -> row $19, the next column's row
     * 3; the int subtraction gave row $F9). */
    const unsigned char row_idx = (unsigned char)((unsigned char)(tile_y - 0x40u) >> 3);

    unsigned char tile = (unsigned char)nes_ram[col_addr + row_idx];
    ENEMY_COLLIDED_TILE(slot) = tile;

    if (dir & 0x0Cu) {
        const unsigned char next_row = (uint8_t)(row_idx + 0x16u);
        const unsigned char next_tile =
            (unsigned char)nes_ram[col_addr + next_row];
        if (next_tile >= tile) {
            RAM(0x049E + slot) = next_tile;
        }
    }

    tile = (unsigned char)ENEMY_COLLIDED_TILE(slot);

    if (ENEMY_DARK_ROOM_FLAG == 0u) {
        tile = (unsigned char)ENEMY_COLLIDED_TILE(slot);
        if (k_walkable_tile[tile]) {
            tile = 0x26u;
        }
        ENEMY_COLLIDED_TILE(slot) = tile;

        if (slot == 0u) {
            if (ENEMY_CANDLE_ROOM_ID == 0x1Fu) {
                if (dir & 0x0Cu) {
                    if (ENEMY_PLAYER_OBJ_X == 0x80u &&
                        ENEMY_PLAYER_OBJ_Y < 0x56u) {
                        ENEMY_COLLIDED_TILE(0) = 0x26u;
                    }
                }
            }
        }
    }

    return (unsigned char)ENEMY_COLLIDED_TILE(slot);
}

unsigned char collision_get_collidable_tile(unsigned int hotspot_offset,
                                            unsigned int slot)
{
    return collidable_tile(hotspot_offset, slot);
}

unsigned char collision_get_collidable_tile_still(unsigned int slot)
{
    /* drain at collision_runtime.c:310-313. */
    COMBAT_PART_INDEX = 0u;
    return collision_get_collidable_tile(0u, slot);
}

/* NES GetCollidingTileMoving: hotspot -8 (Link) / -$10 for up, left or no
 * direction in [0F], 8 for down, $10 for right. */
unsigned char collision_get_colliding_tile_moving(unsigned int slot)
{
    /* drain at collision_runtime.c:315-330. */
    unsigned char hotspot = (slot == 0u) ? 0xF8u : 0xF0u;
    const unsigned char dir = COMBAT_PART_INDEX;
    if (dir & 0x05u) hotspot = (dir & 0x04u) ? 8u : 16u;
    return collidable_tile(hotspot, slot);
}

/* --------------------------------------------------------------- */
/* Monster-vs-weapon battery — drain at collision_runtime.c:36-235.*/
/* --------------------------------------------------------------- */

void collision_handle_monster_weapon_collision(unsigned int monster_slot,
                                               unsigned int weapon_slot)
{
    /* drain at collision_runtime.c:36-63. */
    if ((unsigned char)MON_INVINCIBILITY(monster_slot) &
        (unsigned char)COMBAT_DAMAGE_TYPE) {
        combat_play_parry_sound_for_damage_type();
        return;
    }
    const unsigned char mtype = (unsigned char)MON_TYPE(monster_slot);
    if (mtype == 0x33u || mtype == 0x34u) {
        enemy_gohma_handle_weapon_collision(monster_slot, weapon_slot);
        return;
    }
    if (mtype == 0x13u || mtype == 0x12u) {
        if (weapon_slot != 0x0Fu) {
            OBJ_DIR(monster_slot) = (uint8_t)OBJ_DIR(weapon_slot);
        }
        combat_deal_damage(monster_slot);
        return;
    }
    if (mtype == 0x0Bu || mtype == 0x0Cu) {
        const unsigned char combined =
            (unsigned char)((unsigned char)OBJ_DIR(weapon_slot) |
                            (unsigned char)OBJ_DIR(monster_slot));
        if (combined == 0x0Cu || combined == 0x03u) {
            combat_play_parry_sound_for_damage_type();
            return;
        }
    }
    combat_deal_damage(monster_slot);
}

void collision_check_monster_weapon_collision(unsigned int monster_slot,
                                              unsigned int weapon_y_mid)
{
    /* drain at collision_runtime.c:65-89. */
    COMBAT_HITBOX_Y = (uint8_t)weapon_y_mid;
    COMBAT_COLLIDED = 0u;
    const unsigned int weapon_slot = (unsigned int)COMBAT_WEAPON_SLOT;
    if ((unsigned char)OBJ_STATE(weapon_slot) == 0u) {
        return;
    }
    if (!collision_do_objects_collide_with_thresholds()) {
        return;
    }
    if (weapon_slot == 0x0Fu) {
        const unsigned char inv =
            (unsigned char)((unsigned char)MON_INVINCIBILITY(monster_slot) &
                            (unsigned char)COMBAT_DAMAGE_TYPE);
        if (inv) {
            core_play_parry_tune();
        }
        OBJ_STATE(weapon_slot) = 80u;
        if (inv) {
            return;
        }
        COMBAT_DAMAGE_AMOUNT = 0u;
        MON_STUN_TIMER(monster_slot) = 16u;
    }
    collision_handle_monster_weapon_collision(monster_slot, weapon_slot);
}

/* T-125: inlined into CheckMonsterCollisions (per-monster, per-frame). */
__attribute__((always_inline)) void collision_check_monster_slender_weapon_collision2(unsigned int monster_slot)
{
    /* drain at collision_runtime.c:91-105. */
    const unsigned int weapon_slot = (unsigned int)COMBAT_WEAPON_SLOT;
    const unsigned char dir = (unsigned char)((unsigned char)LINK_DIR & 0x0Cu);
    unsigned char wx;
    unsigned char wy;
    if (dir != 0u) {
        wx = (unsigned char)((unsigned char)OBJ_X(weapon_slot) + 6u);
        wy = (unsigned char)((unsigned char)OBJ_Y(weapon_slot) + 8u);
    } else {
        wx = (unsigned char)((unsigned char)OBJ_X(weapon_slot) + 8u);
        wy = (unsigned char)((unsigned char)OBJ_Y(weapon_slot) + 6u);
    }
    COMBAT_HITBOX_X = wx;
    collision_check_monster_weapon_collision(monster_slot, (unsigned int)wy);
}

void collision_check_monster_slender_weapon_collision(unsigned int monster_slot,
                                                      unsigned int damage_points)
{
    /* drain at collision_runtime.c:107-111. */
    COMBAT_DAMAGE_AMOUNT = (uint8_t)damage_points;
    COMBAT_THRESHOLD_Y = (uint8_t)COMBAT_THRESHOLD_X;
    collision_check_monster_slender_weapon_collision2(monster_slot);
}

void collision_parry_or_shove(unsigned int monster_slot,
                              unsigned int weapon_slot)
{
    /* drain at collision_runtime.c:113-123. */
    const unsigned char mtype = (unsigned char)MON_TYPE(monster_slot);
    if (mtype == 0x0Bu || mtype == 0x0Cu) {
        const unsigned char combined =
            (unsigned char)((unsigned char)OBJ_DIR(weapon_slot) |
                            (unsigned char)OBJ_DIR(monster_slot));
        if (combined == 0x0Cu || combined == 0x03u) {
            core_play_parry_tune();
            return;
        }
    }
    link_collision_begin_shove(monster_slot);
}

void collision_check_monster_stabbing_collision(unsigned int monster_slot,
                                                unsigned int damage_points)
{
    /* drain at collision_runtime.c:125-142. */
    COMBAT_DAMAGE_AMOUNT = (uint8_t)damage_points;
    {
        const unsigned char dir =
            (unsigned char)((unsigned char)LINK_DIR & 0x0Cu);
        if (dir != 0u) {
            COMBAT_THRESHOLD_X = 12u;
            COMBAT_THRESHOLD_Y = 16u;
        } else {
            COMBAT_THRESHOLD_X = 16u;
            COMBAT_THRESHOLD_Y = 12u;
        }
    }
    collision_check_monster_slender_weapon_collision2(monster_slot);
    if (!(unsigned char)COMBAT_COLLIDED) {
        return;
    }
    collision_parry_or_shove(monster_slot,
                             (unsigned int)(unsigned char)COMBAT_WEAPON_SLOT);
}

/* T-125: inlined into CheckMonsterCollisions (per-monster, per-frame). */
__attribute__((always_inline)) void collision_check_monster_sword_collision(unsigned int monster_slot,
                                             unsigned int weapon_slot)
{
    /* drain at collision_runtime.c:144-149. */
    COMBAT_WEAPON_SLOT = (uint8_t)weapon_slot;
    COMBAT_DAMAGE_TYPE = 1u;
    if ((unsigned char)OBJ_STATE(weapon_slot) != 2u) {
        return;
    }
    const unsigned char level = (unsigned char)ITEM_SWORD_LEVEL;
    const unsigned char idx = (level >= 1u && level <= 3u) ? (level - 1u) : 0u;
    collision_check_monster_stabbing_collision(
        monster_slot, (unsigned int)k_sword_damage_points[idx]);
}

void collision_check_monster_shot_collision(unsigned int monster_slot,
                                            unsigned int weapon_slot,
                                            unsigned int damage_points)
{
    /* drain at collision_runtime.c:151-166. */
    collision_check_monster_slender_weapon_collision(monster_slot, damage_points);
    if (!(unsigned char)COMBAT_COLLIDED) {
        return;
    }
    if (weapon_slot != 0x12u) {
        collision_parry_or_shove(monster_slot, weapon_slot);
        return;
    }
    if ((unsigned char)MON_TYPE(monster_slot) == 0x16u) {
        MON_HP(monster_slot) = 0u;
        combat_deal_damage(monster_slot);
        return;
    }
    OBJ_STATE(weapon_slot) = 32u;
    OBJ_ANIM_TIMER(weapon_slot) = 3u;
    collision_parry_or_shove(monster_slot, weapon_slot);
}

/* T-125: inlined into CheckMonsterCollisions (per-monster, per-frame). */
__attribute__((always_inline)) void collision_check_monster_arrow_or_rod_collision(unsigned int monster_slot,
                                                    unsigned int weapon_slot)
{
    /* drain at collision_runtime.c:168-181. */
    COMBAT_WEAPON_SLOT = (uint8_t)weapon_slot;
    const unsigned char state = (unsigned char)OBJ_STATE(weapon_slot);
    if (state >= 0x30u) {
        COMBAT_DAMAGE_TYPE = 1u;
        collision_check_monster_stabbing_collision(monster_slot, 32u);
        return;
    }
    if (state >= 0x20u) {
        return;
    }
    COMBAT_DAMAGE_TYPE = 4u;
    COMBAT_THRESHOLD_X = 11u;
    const unsigned int dmg =
        ((unsigned char)ITEM_ARROW_OR_ROD_LEVEL == 1u) ? 32u : 64u;
    collision_check_monster_shot_collision(monster_slot, weapon_slot, dmg);
}

/* T-125: inlined into CheckMonsterCollisions (per-monster, per-frame). */
__attribute__((always_inline)) void collision_check_monster_boomerang_or_food_collision(
    unsigned int monster_slot, unsigned int weapon_slot)
{
    /* drain at collision_runtime.c:183-191. */
    if ((unsigned char)OBJ_STATE(weapon_slot) & 0x80u) {
        return;
    }
    COMBAT_WEAPON_SLOT = (uint8_t)weapon_slot;
    COMBAT_DAMAGE_TYPE = 2u;
    COMBAT_THRESHOLD_X = 10u;
    COMBAT_THRESHOLD_Y = 10u;
    COMBAT_HITBOX_X =
        (uint8_t)((unsigned char)OBJ_X(weapon_slot) + 4u);
    collision_check_monster_weapon_collision(
        monster_slot,
        (unsigned int)(unsigned char)((unsigned char)OBJ_Y(weapon_slot) + 8u));
}

/* T-125: inlined into CheckMonsterCollisions (per-monster, per-frame). */
__attribute__((always_inline)) void collision_check_monster_sword_shot_or_magic_shot_collision(
    unsigned int monster_slot, unsigned int weapon_slot)
{
    /* drain at collision_runtime.c:193-211. */
    COMBAT_WEAPON_SLOT = (uint8_t)weapon_slot;
    COMBAT_DAMAGE_TYPE = 16u;
    const unsigned char state = (unsigned char)OBJ_STATE(weapon_slot);
    if (state & 1u) {
        return;
    }
    COMBAT_THRESHOLD_X = 12u;
    unsigned int damage;
    if (state & 0x80u) {
        damage = 32u;
    } else {
        const unsigned char level = (unsigned char)ITEM_SWORD_LEVEL;
        COMBAT_DAMAGE_TYPE = 1u;
        damage = (level == 3u) ? 64u : (level == 2u) ? 32u : 16u;
    }
    collision_check_monster_shot_collision(monster_slot, weapon_slot, damage);
    if (!(unsigned char)COMBAT_COLLIDED) {
        return;
    }
    core_handle_shot_blocked(14u);
}

/* T-125: inlined into CheckMonsterCollisions (per-monster, per-frame). */
__attribute__((always_inline)) void collision_check_monster_bomb_or_fire_collision(
    unsigned int monster_slot, unsigned int weapon_slot)
{
    /* drain at collision_runtime.c:213-235. */
    COMBAT_WEAPON_SLOT = (uint8_t)weapon_slot;
    COMBAT_DAMAGE_TYPE = 32u;
    COMBAT_DAMAGE_AMOUNT = 16u;
    COMBAT_THRESHOLD_X = 14u;
    const unsigned char state = (unsigned char)OBJ_STATE(weapon_slot);
    if (state >= 0x20u) {
        /* fall through to hitbox calc */
    } else if (state == 0x13u) {
        COMBAT_DAMAGE_TYPE = 8u;
        COMBAT_DAMAGE_AMOUNT = 64u;
        COMBAT_THRESHOLD_X = 24u;
    } else {
        return;
    }
    COMBAT_HITBOX_X =
        (uint8_t)((unsigned char)OBJ_X(weapon_slot) + 8u);
    COMBAT_HITBOX_Y =
        (uint8_t)((unsigned char)OBJ_Y(weapon_slot) + 8u);
    COMBAT_THRESHOLD_Y = (uint8_t)COMBAT_THRESHOLD_X;
    if (!collision_do_objects_collide_with_thresholds()) {
        return;
    }
    collision_handle_monster_weapon_collision(monster_slot, weapon_slot);
    if ((unsigned char)MON_INVINCIBILITY(monster_slot) &
        (unsigned char)COMBAT_DAMAGE_TYPE) {
        return;
    }
    link_collision_begin_shove(monster_slot);
}
