#include "enemy_runtime_private.h"
#include "combat_state.h"
#include "room_state.h"
#include "sprite_state.h"


static unsigned char enrt_rotate_dir_right(unsigned char dir) {
    dir = (unsigned char)(dir >> 1);
    if (dir == 0)
        dir = 8;
    return dir;
}

void enrt_init_lamnola(unsigned int slot) {
    signed char seg;
    unsigned char type;

    (void)slot;

    for (seg = 9; seg >= 0; --seg) {
        unsigned char uidx = (unsigned char)seg;
        RAM(0x0071u + uidx) = 64;
        RAM(0x0085u + uidx) = 0x8D;
        RAM(0x0099u + uidx) = 0;
        RAM(0x0406u + uidx) = 0;
        RAM(0x0493u + uidx) = 0;
        RAM(0x04C0u + uidx) = RAM(0x04C0u);
        RAM(0x0486u + uidx) = RAM(0x0486u);
        RAM(0x0350u + uidx) = RAM(0x0350u);
    }

    RAM(0x009Du) = 8;
    RAM(0x0385u) = 8;
    RAM(0x00A2u) = 8;
    RAM(0x038Au) = 8;

    type = RAM(0x0350u);
    ENEMY_LAMNOLA_TYPE = type;
    ENEMY_LAMNOLA_SPEED = (unsigned char)(type - 0x39u);

    RAM(0x034Eu) = 8;
}

void enrt_update_lamnola(unsigned int slot) {
    unsigned char dir = ENEMY_DIR(slot);
    unsigned char saved_x;
    unsigned char saved_dir;
    unsigned char tile;
    unsigned char speed;
    signed char tail;

    if (dir == 0) {
        return;
    }

    if (ENEMY_PAUSE_FLAG == 0) {
        enrt_lamnola_move(slot);
        if (slot == 5 || slot == 0x0A) {
            enrt_lamnola_update_head(slot);
        }
    }

    saved_x = ENEMY_X(slot);
    ENEMY_X(slot) = (unsigned char)(saved_x + 4);

    speed = ENEMY_LAMNOLA_SPEED;
    RAM(0x0003u) = (unsigned char)(speed ^ 0x03u);

    tile = (slot == 5 || slot == 0x0A) ? 0x9E : 0xA0;
    c_anim_write_sprite(tile, slot);

    ENEMY_X(slot) = saved_x;

    saved_dir = ENEMY_DIR(slot);
    c_check_monster_collisions(slot);
    ENEMY_DIR(slot) = saved_dir;

    if (ENEMY_METASTATE(slot) == 0) {
        return;
    }

    c_reset_shove_info(slot);
    OBJ(0x0485u, slot) = 32;

    tail = (slot < 6u) ? (signed char)-1 : (signed char)4;
    do {
        ++tail;
    } while (RAM(0x0350u + (unsigned char)tail) != ENEMY_LAMNOLA_TYPE);

    RAM(0x0029u + (unsigned char)tail) = 17;

    RAM(0x04F1u + (unsigned char)tail) = OBJ(0x04F0u, slot);
    RAM(0x0071u + (unsigned char)tail) = ENEMY_X(slot);
    RAM(0x0085u + (unsigned char)tail) = ENEMY_Y(slot);

    if (tail == 4 || tail == 9) {
        return;
    }

    RAM(0x0350u + (unsigned char)tail) = 93;
    c_reset_obj_metastate(slot);
}

void enrt_lamnola_update_head(unsigned int slot) {
    unsigned char cur_dir;
    unsigned char chosen_dir;
    unsigned char rng;
    unsigned char horiz_dir;
    unsigned char vert_dir;
    unsigned char tile;
    unsigned char i;
    unsigned char chain_slot = 0;

    if ((ENEMY_X(slot) & 0x07) != 0)
        return;
    if ((((unsigned char)(ENEMY_Y(slot) + 3)) & 0x07) != 0)
        return;

    if (slot != 5)
        chain_slot = 5;
    for (i = 0; i < 4; ++i, ++chain_slot)
        ENEMY_MANHANDLA_SEG_DIR(chain_slot) = OBJ(0x009A, chain_slot);
    RAM(0x0000) = 0;        /* NES loop counter [00] ends at 0 (T-171) */

    if ((ENEMY_X(slot) & 0x0F) != 0)
        return;
    if ((((unsigned char)(ENEMY_Y(slot) + 3)) & 0x0F) != 0)
        return;

    cur_dir = ENEMY_DIR(slot);
    RAM(0x0000) = (unsigned char)((cur_dir >> 1) & 0x05u);   /* NES [00] */
    ENEMY_LAMNOLA_VIABLE_DIR_MASK = (unsigned char)(0x0Fu ^ c_get_opposite_dir(cur_dir));

    if (ENEMY_RNG_A(slot) < 0x80) {
        horiz_dir = 1;
        if (ENEMY_PLAYER_OBJ_X < ENEMY_X(slot))
            horiz_dir = 2;
        vert_dir = 4;
        if (ENEMY_PLAYER_OBJ_Y < ENEMY_Y(slot))
            vert_dir = 8;
        RAM(0x0002) = horiz_dir;            /* NES [02]/[03] (T-171) */
        RAM(0x0003) = vert_dir;
        /* NES BIT ObjDir: the horizontal turn needs *Link* facing that
         * way (slot 0), not the head (T-171). */
        chosen_dir = horiz_dir;
        if ((ENEMY_LAMNOLA_VIABLE_DIR_MASK & horiz_dir) == 0
         || (ENEMY_DIR(0) & horiz_dir) == 0)
            chosen_dir = vert_dir;
    } else {
        chosen_dir = cur_dir;
        rng = ENEMY_RNG_B(slot);
        if (rng < 0x80) {
            for (;;) {
                chosen_dir = enrt_rotate_dir_right(chosen_dir);
                if ((ENEMY_LAMNOLA_VIABLE_DIR_MASK & chosen_dir) == 0)
                    continue;
                if (rng >= 0x40)
                    break;
                rng = 0x40;
            }
        }
    }

    for (;;) {
        ENEMY_DIR(slot) = chosen_dir;
        ENEMY_JUMPER_BLOCKED_FLAG = chosen_dir;
        if (c_bound_by_room(slot) != 0) {
            tile = c_get_colliding_tile_moving(slot);
            if (tile < ENEMY_DUNGEON_TILE_FLOOR)
                return;
        }

        chosen_dir = enrt_rotate_dir_right(chosen_dir);
        if ((ENEMY_LAMNOLA_VIABLE_DIR_MASK & chosen_dir) == 0)
            continue;
    }
}

void enrt_lamnola_move(unsigned int slot) {
    unsigned char dir = ENEMY_DIR(slot);
    unsigned char step = ENEMY_LAMNOLA_SPEED;

    if (dir & 0x01)
        ENEMY_X(slot) = (unsigned char)(ENEMY_X(slot) + step);
    if (dir & 0x02)
        ENEMY_X(slot) = (unsigned char)(ENEMY_X(slot) - step);
    if (dir & 0x04)
        ENEMY_Y(slot) = (unsigned char)(ENEMY_Y(slot) + step);
    if (dir & 0x08)
        ENEMY_Y(slot) = (unsigned char)(ENEMY_Y(slot) - step);
    RAM(0x0002) = 0x08;     /* NES mask $A1 << 3 left in [02] (T-171) */
}
