#include "sword_shot.h"
#include "bomb.h"                          /* bomb_fire_flash_attrs */
#include "../../abi/platform_abi.h"
#include "../core/core_dispatch.h"         /* core_play_sample, core_handle_shot_blocked */
#include "../world/object_dispatch.h"      /* object_move_shot (NES MoveShot) */
#include "../world/draw_dispatch.h"        /* draw_item_frame_tile */
#include "../enemies/enemy_render.h"       /* weapon sprite cache */

/* T-116: Link's sword shot / magic shot as the NES object in slot $0E.
 *
 * NES source: Z_07.asm MakeSwordShot (4581), @MakeMagicShot (4521),
 * SetUpWeaponWithState (4536), UpdateSwordShotOrMagicShot (3408),
 * DrawSwordShotOrMagicShot (3437), HandleShotBlocked,
 * SetShotSpreadingState, SpreadShot (3577); Z_01.asm MoveShot,
 * PlaceWeapon, Anim_WriteSpecificItemSprites.
 * Drained C: MoveShot (object_move_shot), HandleShotBlocked
 * (core_handle_shot_blocked). Stance: REPLACE the Genesis-native beam
 * (3 px/frame in private statics, generous screen bounds, no spread) and
 * the native magic shot (magic_shot.c). State lives in the NES cells for
 * slot $0E, which the drained CheckMonsterSwordShotOrMagicShotCollision
 * reads and changes.
 *
 * States: $10 sword shot flying, $11 spreading (ObjDir holds the corner
 * offset $FE..$E8), $80 magic shot flying. */
#define SS          0x0Eu
#define SS_STATE    OBJ(0x00ACu, SS)
#define SS_X        OBJ(0x0070u, SS)
#define SS_Y        OBJ(0x0084u, SS)
#define SS_DIR      OBJ(0x0098u, SS)
#define SS_GRID     OBJ(0x0394u, SS)
#define SS_QSPEED   OBJ(0x03BCu, SS)

#define NES_CUR_LEVEL          0x0010u
#define NES_FRAME_COUNTER      0x0015u
#define NES_LEFT_ALIGN_HALF    0x0504u   /* LeftAlignHalfWidthObj */
#define NES_TUNE0_REQUEST      0x0604u

/* RDirectionToWeaponBaseAttribute / RDirectionToWeaponFrame (reverse
 * direction index: up, down, left, right). */
static const unsigned char k_rdir_base_attr[4] = { 0x00u, 0x80u, 0x00u, 0x00u };
static const unsigned char k_rdir_frame[4]     = { 0u, 0u, 1u, 1u };
/* SwordShotSpreadBaseAttr: H, H-V, V, 0. */
static const unsigned char k_spread_attr[4]    = { 0x40u, 0xC0u, 0x80u, 0x00u };

static unsigned char rdir_index(unsigned char dir)
{
    if (dir & 0x08u) return 0u;
    if (dir & 0x04u) return 1u;
    if (dir & 0x02u) return 2u;
    return 3u;
}

void sword_shot_init(void)
{
    SS_STATE = 0u;
    enemy_render_weapon_reset(SS);
}

/* Anim_WriteItemSprites -> Anim_WriteSpecificItemSprites for slot $0E:
 * narrow tiles ($F3, [$20,$62)) are one sprite at X+4, [$62,$7C) a
 * mirrored pair (slim, 7 px apart, below $6C), >= $7C a flippable pair
 * of tile and tile+2. Anim_WriteSpritePair applies the hit flash. */
static void write_item(unsigned char item_slot, unsigned char frame,
                       unsigned char x, unsigned char y,
                       unsigned char attr, unsigned char flip)
{
    unsigned char t = draw_item_frame_tile(item_slot, frame);
    unsigned char a = bomb_fire_flash_attrs(SS, attr);
    if (t == 0xF3u || (t >= 0x20u && t < 0x62u)) {
        if (nes_ram[NES_LEFT_ALIGN_HALF] == 0u) x = (unsigned char)(x + 4u);
        enemy_render_weapon_add_item(SS, t, a, x, y);
    } else if (t < 0x7Cu) {
        unsigned char sep = (t < 0x6Cu) ? 7u : 8u;
        enemy_render_weapon_add_item(SS, t, a, x, y);
        enemy_render_weapon_add_item(SS, t, (unsigned char)(a ^ 0x40u),
                                     (unsigned char)(x + sep), y);
    } else {
        unsigned char l = t, r = (unsigned char)(t + 2u);
        if (flip) {
            l = r; r = t;
            a = (unsigned char)(a ^ 0x40u);
        }
        enemy_render_weapon_add_item(SS, l, a, x, y);
        enemy_render_weapon_add_item(SS, r, a, (unsigned char)(x + 8u), y);
    }
}

/* SetUpWeaponWithState: PlaceWeapon $10 px from Link in his direction;
 * a horizontal shot starting at X < $14 or >= $EC is not made. Magic
 * shots (high bit) move at q-speed $A0, sword shots $C0; the grid offset
 * starts at Link's. */
static void set_up_weapon_with_state(unsigned char st)
{
    unsigned char d = nes_ram[0x0098u];
    SS_STATE = st;
    SS_DIR = d;
    SS_X = (unsigned char)(nes_ram[0x0070u] + ((d & 0x01u) ? 0x10u : (d & 0x02u) ? 0xF0u : 0u));
    SS_Y = (unsigned char)(nes_ram[0x0084u] + ((d & 0x04u) ? 0x10u : (d & 0x08u) ? 0xF0u : 0u));
    if ((d & 0x03u) && (SS_X < 0x14u || SS_X >= 0xECu)) {
        SS_STATE = 0u;                            /* ResetObjState */
        return;
    }
    SS_QSPEED = (st & 0x80u) ? 0xA0u : 0xC0u;
    SS_GRID = nes_ram[0x0394u];
}

void sword_shot_make(unsigned char allowed)
{
    if (SS_STATE != 0u) return;
    if (!allowed) return;
    core_play_sample(0x01u);                      /* sword shot sound */
    set_up_weapon_with_state(0x10u);
}

void magic_shot_make(void)
{
    unsigned char st = SS_STATE;
    if (st != 0u && (st & 0x80u)) return;
    nes_ram[NES_TUNE0_REQUEST] = 0x04u;           /* magic shot tune */
    set_up_weapon_with_state(0x80u);
}

/* DrawSwordShotOrMagicShot. */
static void draw_shot(void)
{
    unsigned char d = SS_DIR;
    unsigned char y = SS_Y;
    unsigned char idx = rdir_index(d);
    unsigned char attr;
    if (d & 0x03u) y = (unsigned char)(y + 3u);
    attr = (unsigned char)((nes_ram[NES_FRAME_COUNTER] & 0x03u) | k_rdir_base_attr[idx]);
    write_item((SS_STATE & 0x80u) ? 0x23u : 0x22u, k_rdir_frame[idx],
               SS_X, y, attr, (unsigned char)(idx == 2u));
}

/* SpreadShot: four corners around (X, Y), offsets ObjDir, then one
 * negated in turn; the base offset grows by one each frame until $E8. */
static void spread_shot(void)
{
    unsigned char off[4];                /* [02] = off[2], [03] = off[3] */
    signed char i;
    off[2] = off[3] = SS_DIR;
    for (i = 3; i >= 0; --i) {
        unsigned char sx = (unsigned char)(SS_X + off[2]);
        unsigned char dist;
        unsigned char draw = 1u;
        if (sx >= SS_X) {
            if (sx >= 0xFCu) draw = 0u;
            dist = (unsigned char)(sx - SS_X);
        } else {
            dist = (unsigned char)(SS_X - sx);
        }
        if (draw && dist >= 0x20u) draw = 0u;
        if (draw) {
            unsigned char sy = (unsigned char)(SS_Y + off[3]);
            if (nes_ram[NES_CUR_LEVEL] != 0u && (sy < 0x3Eu || sy >= 0xE8u)) draw = 0u;
            if (draw)
                write_item(0x23u, 2u, sx, sy,
                           (unsigned char)((nes_ram[NES_FRAME_COUNTER] & 0x03u) |
                                           k_spread_attr[(unsigned char)i]),
                           0u);
        }
        {
            /* Loop 3 negates [03], 2 negates [02], 1 negates [03]. */
            unsigned char k = (i == 1) ? 3u : (unsigned char)i;
            if (k >= 2u) off[k] = (unsigned char)(0u - off[k]);
        }
    }
    SS_DIR = (unsigned char)(SS_DIR - 1u);
    if (SS_DIR == 0xE8u) SS_STATE = 0u;           /* DeactivateLinkShot */
}

void sword_shot_update(void)
{
    unsigned char st = SS_STATE;
    enemy_render_weapon_reset(SS);
    if (st == 0u) return;
    if (st & 0x01u) {
        spread_shot();
        return;
    }
    RAM(NES_OBJ_DIR) = SS_DIR;
    object_move_shot(SS_DIR, SS);
    if (RAM(NES_OBJ_DIR) == 0u) {
        core_handle_shot_blocked(SS);
        return;
    }
    if ((SS_GRID & 0x07u) == 0u) SS_GRID = 0u;
    draw_shot();
}
