#include "sword_shot.h"
#include "bomb.h"                          /* bomb_fire_flash_attrs */
#include "../../abi/platform_abi.h"
#include "../../state/weapon_state.h"
#include "../core/core_dispatch.h"         /* core_play_sample, core_handle_shot_blocked */
#include "../world/object_dispatch.h"      /* object_move_shot (NES MoveShot) */
#include "../world/draw_dispatch.h"        /* draw_item_frame_tile */
#include "../enemies/enemy_render.h"       /* weapon sprite cache */
#include "../world/render/mp_sprite_runtime.h"
#include "../world/render/sprite_slots.h"
#include "render_abi.h"
#include "../combat/combat_runtime.h"      /* roomrom_combat_is_redux */

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
 * the native magic shot (magic_shot.c). Native state owns slot $0E's
 * lifecycle, pose and fraction; the declared movement/collision bridge
 * publishes inputs and commits remaining drained consumer outputs.
 * Coverage: shared native lifecycle and explicit per-player consumers;
 * full party timing and transition qualification remain separate gates.
 *
 * States: $10 sword shot flying, $11 spreading (ObjDir holds the corner
 * offset $FE..$E8), $80 magic shot flying. */
#define SS          0x0Eu
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

/* The legacy $0E slot is still the movement/collision bridge. Publish the
 * native record before those consumers run and read pose back afterwards. */
static void publish_beam(const PlayerBeamState *b)
{
    F_SS_00BA_P = b->state; /* native_ram bulk:dyn_addr */
    F_SS_007E_P = b->x; /* native_ram bulk:dyn_addr */
    F_SS_0092_P = b->y; /* native_ram bulk:dyn_addr */
    F_ram_00A6_P = b->dir; /* native_ram bulk:dyn_addr */
    F_SS_03A2_P = b->grid; /* native_ram bulk:dyn_addr */
    F_SS_03CA_P = b->qspeed; /* native_ram bulk:dyn_addr */
    F_SS_03B6_P = b->pos_frac; /* native_ram bulk:dyn_addr */
}

static void sync_beam_pose(PlayerBeamState *b)
{
    b->x = F_SS_007E_N;
    b->y = F_SS_0092_N;
    b->dir = F_ram_00A6_N;
    b->grid = F_SS_03A2_N;
    b->qspeed = F_SS_03CA_N;
    b->pos_frac = F_SS_03B6_N;
}

void sword_shot_commit_collision(unsigned char player)
{
    PlayerBeamState *b;
    if (player >= 4u) return;
    b = &g_player_beam[player];
    b->state = F_SS_00BA_N;
    b->dir = F_ram_00A6_N;
}

void sword_shot_init(void)
{
    player_beam_reset_all();
    publish_beam(&g_player_beam[0]);
    enemy_render_weapon_reset(SS);
}

/* NES source: Anim_WriteSpecificItemSprites/SpreadShot. Drained C:
 * write_item/spread_shot below. Coverage: four owned emitted pieces;
 * Stance: EXTEND the same tile/attribute/coordinate rules for every owner. */
typedef struct { unsigned char player, piece; } BeamDraw;

static void clear_beam_draw(unsigned char player)
{
    if (player == 0u) { enemy_render_weapon_reset(SS); return; }
    mp_sprite_clear_beam(player);
}
void sword_shot_hide_player(unsigned char player)
{
    clear_beam_draw(player);
}
static void add_beam_item(BeamDraw *draw, unsigned char tile, unsigned char attr,
                          unsigned char x, unsigned char y)
{
    if (draw->piece >= 4u) return;
    if (draw->player == 0u) enemy_render_weapon_add_item(SS, tile, attr, x, y);
    else if (y != 0xF0u)
        mp_sprite_submit(ROOMROM_MP_WEAPON_TOKEN(draw->player, draw->piece),
                         x, y, RENDER_SPRITE_SIZE(1, 2), enemy_render_item_sat(tile, attr));
    ++draw->piece;
}

void sword_shot_swap_in(unsigned char player, BeamLegacyContext *saved)
{
    sync_beam_pose(&saved->beam);
    saved->beam.state = F_SS_00BA_N;
    saved->beam.owner = 0u;
    saved->inv_timer = F_ram_04FE_N;
    if (player >= 4u) return;
    publish_beam(&g_player_beam[player]);
    /* Beam slots are not damage recipients. No reaction producer targets
     * slot14; positive NES flight/hit/magic captures retain timer0. Keep
     * an extra owner's render call from reading a poisoned P1 reaction. */
    if (player != 0u) F_ram_04FE_N = 0u;
}
void sword_shot_swap_out(const BeamLegacyContext *saved)
{
    publish_beam(&saved->beam);
    F_ram_04FE_N = saved->inv_timer;
}

/* Anim_WriteItemSprites -> Anim_WriteSpecificItemSprites for slot $0E:
 * narrow tiles ($F3, [$20,$62)) are one sprite at X+4, [$62,$7C) a
 * mirrored pair (slim, 7 px apart, below $6C), >= $7C a flippable pair
 * of tile and tile+2. Anim_WriteSpritePair applies the hit flash. */
static void write_item(BeamDraw *draw, unsigned char item_slot, unsigned char frame,
                       unsigned char x, unsigned char y,
                       unsigned char attr, unsigned char flip)
{
    unsigned char t = draw_item_frame_tile(item_slot, frame);
    unsigned char a = bomb_fire_flash_attrs(SS, attr);
    if (t == 0xF3u || (t >= 0x20u && t < 0x62u)) {
        if (F_ram_0504_N2 == 0u) x = (unsigned char)(x + 4u);
        add_beam_item(draw, t, a, x, y);
    } else if (t < 0x7Cu) {
        unsigned char sep = (t < 0x6Cu) ? 7u : 8u;
        add_beam_item(draw, t, a, x, y);
        add_beam_item(draw, t, (unsigned char)(a ^ 0x40u),
                                     (unsigned char)(x + sep), y);
    } else {
        unsigned char l = t, r = (unsigned char)(t + 2u);
        if (flip) {
            l = r; r = t;
            a = (unsigned char)(a ^ 0x40u);
        }
        add_beam_item(draw, l, a, x, y);
        add_beam_item(draw, r, a, (unsigned char)(x + 8u), y);
    }
}

/* SetUpWeaponWithState: PlaceWeapon $10 px from Link in his direction;
 * a horizontal shot starting at X < $14 or >= $EC is not made. Magic
 * shots (high bit) move at q-speed $A0, sword shots $C0; the grid offset
 * starts at Link's. */
static void set_up_weapon_with_state(PlayerBeamState *beam, unsigned char st,
    unsigned char x, unsigned char y, unsigned char d, unsigned char grid)
{
    beam->state = st;
    beam->dir = d;
    beam->x = (unsigned char)(x + ((d & 0x01u) ? 0x10u : (d & 0x02u) ? 0xF0u : 0u));
    beam->y = (unsigned char)(y + ((d & 0x04u) ? 0x10u : (d & 0x08u) ? 0xF0u : 0u));
    /* Redux magic_beam_align.asm (magic_shot_align): a vertical shot is nudged
     * sideways after PlaceWeapon: Up -1, Down +1 (rod shot, state $80) or +2
     * (sword shot, state $10). Horizontal shots are not moved. The original
     * game has no such patch. */
    if (roomrom_combat_is_redux() && d >= 0x04u) {
        beam->x = (unsigned char)(beam->x + ((d >= 0x08u) ? 0xFFu : (st & 0x80u) ? 0x01u : 0x02u));
    }
    if ((d & 0x03u) && (beam->x < 0x14u || beam->x >= 0xECu)) {
        beam->state = 0u;                            /* ResetObjState */
        publish_beam(beam);
        return;
    }
    beam->qspeed = (st & 0x80u) ? 0xA0u : 0xC0u;
    beam->grid = grid;
    publish_beam(beam);
}

void sword_shot_make_for_player(unsigned char player, unsigned char allowed,
    unsigned char x, unsigned char y, unsigned char dir, unsigned char grid)
{
    PlayerBeamState *beam;
    if (player >= 4u || !allowed) return;
    beam = &g_player_beam[player];
    if (beam->state != 0u) return;
    beam->owner = player;
    core_play_sample(0x01u);
    set_up_weapon_with_state(beam, 0x10u, x, y, dir, grid);
}
void sword_shot_make(unsigned char allowed)
{
    sword_shot_make_for_player(0u, allowed, F_ram_0070, F_ram_0084,
                               F_ram_0098, F_ram_0394);
}
void magic_shot_make_for_player(unsigned char player,
    unsigned char x, unsigned char y, unsigned char dir, unsigned char grid)
{
    PlayerBeamState *beam;
    if (player >= 4u) return;
    beam = &g_player_beam[player];
    if (beam->state != 0u && (beam->state & 0x80u)) return;
    beam->owner = player;
    F_ram_0604_N = 0x04u;
    set_up_weapon_with_state(beam, 0x80u, x, y, dir, grid);
}
void magic_shot_make(void)
{
    magic_shot_make_for_player(0u, F_ram_0070, F_ram_0084,
                               F_ram_0098, F_ram_0394);
}

/* DrawSwordShotOrMagicShot. */
static void draw_shot(const PlayerBeamState *beam, BeamDraw *draw)
{
    unsigned char d = beam->dir;
    unsigned char y = beam->y;
    unsigned char idx = rdir_index(d);
    unsigned char attr;
    if (d & 0x03u) y = (unsigned char)(y + 3u);
    /* Redux dungeon_draw_beam.asm ($F397): the beam sprite sits two pixels higher in a
     * dungeon. Original keeps the vanilla position. */
    if (roomrom_combat_is_redux() && F_ram_0010_N2 != 0u)
        y = (unsigned char)(y - 2u);
    attr = (unsigned char)((F_ram_0015_N3 & 0x03u) | k_rdir_base_attr[idx]);
    write_item(draw, (beam->state & 0x80u) ? 0x23u : 0x22u, k_rdir_frame[idx],
               beam->x, y, attr, (unsigned char)(idx == 2u));
}

/* SpreadShot: four corners around (X, Y), offsets ObjDir, then one
 * negated in turn; the base offset grows by one each frame until $E8. */
static void spread_shot(PlayerBeamState *beam, BeamDraw *draw)
{
    unsigned char off[4];                /* [02] = off[2], [03] = off[3] */
    signed char i;
    off[2] = off[3] = beam->dir;
    for (i = 3; i >= 0; --i) {
        unsigned char sx = (unsigned char)(beam->x + off[2]);
        unsigned char dist;
        unsigned char visible = 1u;
        if (sx >= beam->x) {
            if (sx >= 0xFCu) visible = 0u;
            dist = (unsigned char)(sx - beam->x);
        } else {
            dist = (unsigned char)(beam->x - sx);
        }
        if (visible && dist >= 0x20u) visible = 0u;
        if (visible) {
            unsigned char sy = (unsigned char)(beam->y + off[3]);
            if (F_ram_0010_N2 != 0u && (sy < 0x3Eu || sy >= 0xE8u)) visible = 0u;
            if (visible)
                write_item(draw, 0x23u, 2u, sx, sy,
                           (unsigned char)((F_ram_0015_N3 & 0x03u) |
                                           k_spread_attr[(unsigned char)i]),
                           0u);
        }
        {
            /* Loop 3 negates [03], 2 negates [02], 1 negates [03]. */
            unsigned char k = (i == 1) ? 3u : (unsigned char)i;
            if (k >= 2u) off[k] = (unsigned char)(0u - off[k]);
        }
    }
    beam->dir = (unsigned char)(beam->dir - 1u);
    if (beam->dir == 0xE8u) beam->state = 0u;           /* DeactivateLinkShot */
    publish_beam(beam);
}

void sword_shot_update_for_player(unsigned char player)
{
    PlayerBeamState *beam;
    BeamDraw draw = { player, 0u };
    unsigned char st;
    if (player >= 4u) return;
    beam = &g_player_beam[player];
    /* Legacy collision may have deactivated or spread the shot after the
     * previous native update. Pull that state back before publishing. */
    if (player == 0u) sword_shot_commit_collision(0u);
    st = beam->state;
    clear_beam_draw(player);
    if (st == 0u) return;
    if (st & 0x01u) {
        spread_shot(beam, &draw);
        return;
    }
    publish_beam(beam);
    F_ram_000F_R2 = beam->dir;
    object_move_shot(beam->dir, SS);
    sync_beam_pose(beam);
    if (F_ram_000F_R2 == 0u) {
        core_handle_shot_blocked(SS);
        sword_shot_commit_collision(player);
        publish_beam(beam);
        return;
    }
    if ((beam->grid & 0x07u) == 0u) beam->grid = 0u;
    draw_shot(beam, &draw);
    publish_beam(beam);
}

void sword_shot_update(void)
{
    sword_shot_update_for_player(0u);
}
