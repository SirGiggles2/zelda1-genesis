#include "../combat/combat_runtime.h"   /* T-116 Link item-use state */
#include "boomerang.h"
#include "../world/render/sprite_render.h"
#include "../world/object_dispatch.h"      /* object_move_shot / object_move_object */
#include "../world/draw_dispatch.h"        /* draw_item_frame_tile */
#include "../combat/targeting_dispatch.h"  /* GetDirectionsAndDistancesToTarget */
#include "../enemies/enemy_render.h"       /* enemy_render_item_sat */
#include "../../state/inventory.h"
#include "../../state/weapon_state.h"
#include "../../state/player_state.h"
#include "../../abi/platform_abi.h"

/* T-116: Link's boomerang as the NES object in slot $0F.
 *
 * NES source: Z_05.asm WieldBoomerang, Z_07.asm UpdateArrowOrBoomerang
 * ($1x out, $2x spark, $3x slow down, $4x/$5x return),
 * AnimateBoomerangAndCheckCollision / CalcBoomerangFrame, Z_01.asm
 * SetBoomerangSpeed, PlayBoomerangSfx, MoveShot.
 * Drained C: MoveShot / MoveObject (object_dispatch.c), targeting
 * (GetDirectionsAndDistancesToTarget, CalcDiagonalSpeedIndex). The
 * monster-boomerang drain (enrt_update_arrow_or_boomerang) serves slots
 * below $0D only. Stance: REPLACE the Genesis-native 64-frame boomerang.
 * State lives in the NES cells for slot $0F, which the drained
 * CheckMonsterBoomerangOrFoodCollision reads. */
#define BM          0x0Fu
#define BM_REC      (&g_player_boomerang)
#define BM_STATE    (BM_REC->state)
#define BM_X        (BM_REC->x)
#define BM_Y        (BM_REC->y)
#define BM_DIR      (BM_REC->dir)
#define BM_GRID     (BM_REC->grid)
#define BM_QSPEED   (BM_REC->qspeed)
#define BM_CNT      (BM_REC->count)
#define BM_LIMIT    (BM_REC->limit)
#define BM_TIMER    (BM_REC->timer)

#define NES_INV_BOOMERANG        0x0674u
#define NES_INV_MAGIC_BOOMERANG  0x0675u
#define NES_ITEM_OBJ_TIMER       0x003Bu   /* ObjTimer+19 (sfx throttle) */

static const unsigned char k_limits[2] = { 0x31u, 0xFFu };          /* BoomerangLimits */
static const unsigned char k_frame_cycle[9] = {
    0x00u, 0x01u, 0x02u, 0x01u, 0x00u, 0x01u, 0x02u, 0x01u, 0x03u
};
static const unsigned char k_attr_cycle[9] = {
    0x00u, 0x00u, 0x00u, 0x40u, 0x40u, 0xC0u, 0x80u, 0x80u, 0x01u
};
static const unsigned char k_qspeed_y[9] = {
    0x00u, 0x20u, 0x36u, 0x4Cu, 0x60u, 0x68u, 0x70u, 0x78u, 0x80u
};
static const unsigned char k_qspeed_x[9] = {
    0x80u, 0x78u, 0x70u, 0x68u, 0x60u, 0x4Cu, 0x36u, 0x20u, 0x00u
};

/* Legacy $0F remains the movement/targeting/collision bridge. Publish the
 * native record before those consumers and read pose back afterwards. */
static void publish_boomerang(void)
{
    const PlayerBoomerangState *b = BM_REC;
    F_BM_00BB_P = b->state; /* native_ram bulk:dyn_addr */
    F_BM_007F_P = b->x; /* native_ram bulk:dyn_addr */
    F_BM_0093_P = b->y; /* native_ram bulk:dyn_addr */
    F_BM_00A7_P = b->dir; /* native_ram bulk:dyn_addr */
    F_BM_03A3_P = b->grid; /* native_ram bulk:dyn_addr */
    F_BM_03CB_P = b->qspeed; /* native_ram bulk:dyn_addr */
    F_BM_03B7_P = b->pos_frac; /* native_ram bulk:dyn_addr */
    F_BM_03DF_P = b->count; /* native_ram bulk:dyn_addr */
    F_BM_038F_P = b->limit; /* native_ram bulk:dyn_addr */
    F_BM_0037_P = b->timer; /* native_ram bulk:dyn_addr */
}

static void sync_boomerang_pose(void)
{
    PlayerBoomerangState *b = BM_REC;
    b->x = F_BM_007F_N;
    b->y = F_BM_0093_N;
    b->dir = F_BM_00A7_N;
    b->grid = F_BM_03A3_N;
    b->qspeed = F_BM_03CB_N;
    b->pos_frac = F_BM_03B7_N;
}

void roomrom_boomerang_init(void)
{
    player_boomerang_reset();
    publish_boomerang();
    roomrom_sprites_clear_boomerang();
}

/* WieldBoomerang. */
void roomrom_boomerang_throw(link_face_t face, short link_x, short link_y)
{
    roomrom_boomerang_throw_for_player(0u, face, link_x, link_y);
}

/* NES source: WieldBoomerang / return-to-thrower. Drained C: existing
 * lifecycle below. Coverage: shared caster identity, return and catch;
 * Stance: EXTEND. One team object, updated once in world order. */
void roomrom_boomerang_throw_for_player(unsigned char player, link_face_t face, short link_x, short link_y)
{
    unsigned char st = BM_STATE;
    unsigned char d;
    (void)face; (void)link_x; (void)link_y;
    if (player >= 4u) return;
    if ((F_NES_INV_BOOMERANG_N | F_ram_0675_N) == 0u) return;
    if (st != 0u && (st & 0x80u) == 0u) return;
    BM_REC->owner = player;
    BM_STATE = 0x10u;
    BM_LIMIT = k_limits[F_ram_0675_N ? 1u : 0u];
    /* PlaceWeaponForPlayerState, then Link's counter = 1 (same effect as
     * the AndAnim form). */
    link_place_weapon_for_player_state(1u);
    d = F_ram_0098;                         /* PlaceWeapon */
    BM_DIR = d;
    BM_X = (unsigned char)(F_ram_0070 + ((d & 0x01u) ? 0x10u : (d & 0x02u) ? 0xF0u : 0u));
    BM_Y = (unsigned char)(F_ram_0084 + ((d & 0x04u) ? 0x10u : (d & 0x08u) ? 0xF0u : 0u));
    BM_QSPEED = 0xC0u;
    BM_CNT = 3u;
    /* Input direction (maybe diagonal), else Link's facing. */
    BM_DIR = (F_ram_03F8 & 0x0Fu) ? (unsigned char)(F_ram_03F8 & 0x0Fu)
                                         : F_ram_0098;
    publish_boomerang();
}

unsigned char roomrom_boomerang_active(void)
{
    return BM_STATE != 0u ? 1u : 0u;
}

/* T-057: food (bait) shares slot $0F with the boomerang; its states are
 * $80..$82 (high bit set). NES WieldFood (Z_05.asm) and the food branch
 * of UpdateBoomerangOrFood (Z_07.asm). */
#define FOOD_TIMER  BM_TIMER               /* ObjTimer+$0F */
static unsigned char s_food_chase;

void roomrom_food_wield(void)
{
    roomrom_food_wield_for_player(0u);
}

void roomrom_food_wield_for_player(unsigned char player)
{
    unsigned char d;
    if (player >= 4u) return;
    if (BM_STATE != 0u) return;
    BM_REC->owner = player;
    FOOD_TIMER = 0xFFu;                           /* first state: $FF frames */
    BM_STATE = 0x80u;                             /* PlaceWeaponForPlayerStateAndAnimAndWeaponState */
    link_place_weapon_for_player_state(1u);
    d = F_ram_0098;                         /* PlaceWeapon */
    BM_DIR = d;
    BM_X = (unsigned char)(F_ram_0070 + ((d & 0x01u) ? 0x10u : (d & 0x02u) ? 0xF0u : 0u));
    BM_Y = (unsigned char)(F_ram_0084 + ((d & 0x04u) ? 0x10u : (d & 0x08u) ? 0xF0u : 0u));
    publish_boomerang();
}

/* Three states of $FF timer frames, then ResetObjState. Moblins, Goriyas,
 * Octoroks, Darknuts... (template $03..$0A), Vires and Keese chase the food:
 * ChaseTargetX/Y. Drawn with the red sprite palette, food item slot 6. */
static void food_update(void)
{
    unsigned char t;
    if (FOOD_TIMER == 0u) {
        BM_STATE = (unsigned char)(BM_STATE + 1u);
        if ((BM_STATE & 0x0Fu) == 0x03u) {
            BM_STATE = 0u;
            publish_boomerang();
            roomrom_sprites_clear_boomerang();
            return;
        }
        FOOD_TIMER = 0xFFu;
    }
    t = F_DUNGEON_ROOM_TEMPLATE_TYPE;                         /* RoomObjTemplateType */
    if ((t >= 0x03u && t < 0x0Bu) || t == 0x12u || t == 0x1Bu || t == 0x1Cu) {
        s_food_chase = 1u;
        F_CHASE_TARGET_X_R = BM_X;
        F_CHASE_TARGET_Y_R = BM_Y;
    }
    /* Item slot 6 frame 0 is the narrow tile $22: X+4. */
    roomrom_sprites_set_boomerang_nes((short)(BM_X + 4u), (short)BM_Y,
        enemy_render_item_sat(draw_item_frame_tile(0x06u, 0u), 0x02u));
    publish_boomerang();
}

/* NES updates the weapons after copying Link's position to ChaseTargetX/Y;
 * the Genesis copy runs later in the object loop, which re-applies the
 * food target here. */
void roomrom_food_apply_chase_target(void)
{
    if (!s_food_chase) return;
    F_CHASE_TARGET_X_R = BM_X;
    F_CHASE_TARGET_Y_R = BM_Y;
}

unsigned char roomrom_food_chase_active(void)
{
    return s_food_chase;
}

/* CalcBoomerangFrame -> Anim_WriteItemSprites, item slot $1D. */
static void draw_boomerang_nes(void)
{
    unsigned char y = (unsigned char)(BM_STATE & 0x0Fu);
    unsigned char frame, attr, tile;
    if (y > 8u) y = 8u;
    frame = k_frame_cycle[y];
    attr = k_attr_cycle[y];
    if (attr != 0x08u) attr = (unsigned char)(attr + F_ram_0675_N);
    if ((BM_STATE & 0xF0u) == 0x20u) attr = 0x01u;
    tile = draw_item_frame_tile(0x1Du, frame);
    /* Narrow item tiles ($36/$38/$3A/$3C): X+4. */
    roomrom_sprites_set_boomerang_nes((short)(BM_X + 4u), (short)BM_Y,
                                      enemy_render_item_sat(tile, attr));
}

/* AnimateBoomerangAndCheckCollision (player slot: no Link test). */
static void animate_and_draw(void)
{
    BM_CNT = (unsigned char)(BM_CNT - 1u);
    if (BM_CNT == 0u) {
        BM_CNT = 2u;
        BM_STATE = (unsigned char)((BM_STATE + 1u) & 0x77u);
        /* PlayBoomerangSfx: throttled by ObjTimer+19. */
        if (F_NES_ITEM_OBJ_TIMER_N == 0u) {
            F_ram_0603 |= 0x02u;   /* PlayEffect $02 */
            F_NES_ITEM_OBJ_TIMER_N = 0x0Au;
        }
    }
    draw_boomerang_nes();
    publish_boomerang();
}

/* HandleArrowOrBoomerangBlocked -> DrawBoomerangAndCheckCollision. */
static void handle_blocked(void)
{
    BM_CNT = 3u;
    BM_STATE = (unsigned char)(BM_STATE + 0x10u);
    draw_boomerang_nes();
    publish_boomerang();
}

/* SetBoomerangSpeed. */
static void set_speed(unsigned char q)
{
    BM_QSPEED = q;
    if ((BM_STATE & 0xF0u) != 0x40u) return;
    BM_QSPEED = (unsigned char)(BM_QSPEED >> 1);
    BM_LIMIT = (unsigned char)(BM_LIMIT - 1u);
    if (BM_LIMIT == 0u) BM_STATE = 0x50u;
    publish_boomerang();
}

void roomrom_boomerang_update(short link_x, short link_y)
{
    unsigned char st = BM_STATE;
    unsigned char hi = (unsigned char)(st & 0xF0u);
    (void)link_x; (void)link_y;
    s_food_chase = 0u;
    if (st == 0u) {
        publish_boomerang();
        roomrom_sprites_clear_boomerang();
        return;
    }
    if (st & 0x80u) { food_update(); return; }
    publish_boomerang();
    F_ram_0000_R = 0u;
    if (hi == 0x10u) {
        unsigned char d = BM_DIR;
        unsigned char grid, mag;
        F_ram_000E_R = 0u;
        if (d & 0x03u) {
            F_ram_000F_R2 = d;
            object_move_shot((unsigned char)(d & 0x03u), BM);
            sync_boomerang_pose();
            F_ram_000E_R = (unsigned char)(F_ram_000E_R + 1u);
        }
        if (F_ram_000E_R & 0x80u) { handle_blocked(); return; }
        if (d & 0x0Cu) {
            F_ram_000F_R2 = d;
            object_move_shot((unsigned char)(d & 0x0Cu), BM);
            sync_boomerang_pose();
        }
        if (F_ram_000E_R & 0x80u) { handle_blocked(); return; }
        grid = BM_GRID;
        mag = (grid & 0x80u) ? (unsigned char)(0u - grid) : grid;
        if (mag < BM_LIMIT) { animate_and_draw(); return; }
        BM_LIMIT = 0x10u;
        BM_STATE = 0x20u;
        handle_blocked();                       /* -> $30 */
        return;
    }
    if (hi == 0x20u) {                          /* CheckState20 */
        BM_STATE = 0x28u;
        BM_CNT = (unsigned char)(BM_CNT - 1u);
        if (BM_CNT != 0u) { draw_boomerang_nes(); return; }
        BM_STATE = 0x40u;
        handle_blocked();                       /* -> $50, fast return */
        return;
    }
    if (hi == 0x30u) {                          /* CheckState30: slow down */
        BM_GRID = 0u;
        BM_QSPEED = 0x40u;
        F_ram_000F_R2 = BM_DIR;
        publish_boomerang();
        if (!((BM_DIR & 0x02u) && BM_X < 0x02u)) {
            object_move_object((unsigned short)BM);
            sync_boomerang_pose();
            BM_LIMIT = (unsigned char)(BM_LIMIT - 1u);
            if (BM_LIMIT != 0u) { animate_and_draw(); return; }
        }
        BM_LIMIT = 0x20u;
        BM_STATE = 0x40u;
        animate_and_draw();
        return;
    }
    /* $4x / $5x: return to Link. */
    BM_GRID = 0u;
    if (BM_REC->owner == 0u) {
        /* Preserve P1's exact legacy producer/phase while it remains live. */
        targeting_get_directions_and_distances_to_target(0u, BM);
    } else {
        const PlayerState *thrower = &PLAYER(BM_REC->owner);
        targeting_get_directions_and_distances_to_point(
            (unsigned char)thrower->x, (unsigned char)thrower->y, BM);
    }
    if (F_ram_0000_R == 0x02u) {
        /* Caught: Link enters the catching state. */
        BM_LIMIT = 0u;
        if (BM_REC->owner == 0u) {
            F_ram_00AC = (unsigned char)(F_ram_00AC | 0x20u);
            F_ram_03D0 = 1u;
        } else {
            PlayerState *thrower = &PLAYER(BM_REC->owner);
            thrower->state = (unsigned char)(thrower->state | 0x20u);
            thrower->anim_counter = 1u;
        }
        BM_STATE = 0u;
        publish_boomerang();
        roomrom_sprites_clear_boomerang();
        return;
    }
    {
        unsigned char idx = (unsigned char)targeting_calc_diagonal_speed_index(4u);
        if (idx > 8u) idx = 8u;
        set_speed(k_qspeed_y[idx]);
        F_ram_000F_R2 = F_ZP_TMPA_R;
        BM_DIR = F_ZP_TMPA_R;
        publish_boomerang();
        object_move_object((unsigned short)BM);
        sync_boomerang_pose();
        set_speed(k_qspeed_x[idx]);
        F_ram_000F_R2 = F_ZP_TMPB_R;
        BM_DIR = F_ZP_TMPB_R;
        publish_boomerang();
        object_move_object((unsigned short)BM);
        sync_boomerang_pose();
    }
    animate_and_draw();
}
