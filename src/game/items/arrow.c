#include "../combat/combat_runtime.h"   /* T-116 Link item-use state */
#include "arrow.h"
#include "../../abi/platform_abi.h"
#include "../world/render/sprite_render.h"
#include "../world/object_dispatch.h"      /* object_move_shot (NES MoveShot) */
#include "../world/draw_dispatch.h"        /* draw_item_frame_tile */
#include "../enemies/enemy_render.h"       /* enemy_render_item_sat */
#include "../../state/inventory.h"

/* T-116: Link's arrow as the NES object in slot $12.
 *
 * NES source: Z_05.asm WieldArrow / WieldWeapon, Z_07.asm UpdateRodOrArrow
 * -> UpdateArrowOrBoomerang (state $1x fly, $2x spark), DrawArrow /
 * OffsetAndDrawArrow, HandleArrowOrBoomerangBlocked.
 * Drained C: MoveShot (object_move_shot); the monster-arrow drain
 * (enrt_update_arrow_or_boomerang) keys on ObjType $5B and cannot serve
 * the player slot. Stance: REPLACE the Genesis-native 3 px/frame arrow.
 * State lives in the NES cells ($12: ObjState $BE, X $82, Y $96, dir $AA,
 * grid offset $3A6, q-speed $3CE, anim counter $3E2), which the drained
 * CheckMonsterArrowOrRodCollision reads. */
#define AR          0x12u
#define AR_STATE    OBJ(0x00ACu, AR)
#define AR_X        OBJ(0x0070u, AR)
#define AR_Y        OBJ(0x0084u, AR)
#define AR_DIR      OBJ(0x0098u, AR)
#define AR_GRID     OBJ(0x0394u, AR)
#define AR_QSPEED   OBJ(0x03BCu, AR)
#define AR_CNT      OBJ(0x03D0u, AR)

#define NES_BOW          0x065Au
#define NES_INV_ARROW    0x0659u
#define NES_INV_RUPEES   0x066Du

/* RDirectionToWeaponFrame / BaseAttribute / OffsetsX / OffsetsY
 * (reverse direction index: up, down, left, right). */
static const unsigned char k_frame[4]  = { 0u, 0u, 1u, 1u };
static const unsigned char k_attr[4]   = { 0x00u, 0x80u, 0x00u, 0x00u };
static const unsigned char k_off_x[4]  = { 0xFCu, 0xFCu, 0x00u, 0x00u };
static const unsigned char k_off_y[4]  = { 0x00u, 0x00u, 0x03u, 0x03u };

static unsigned char rdir_index(unsigned char dir)
{
    if (dir & 0x08u) return 0u;
    if (dir & 0x04u) return 1u;
    if (dir & 0x02u) return 2u;
    return 3u;
}

void roomrom_arrow_init(void)
{
    AR_STATE = 0u;
    roomrom_sprites_clear_arrow();
}

/* WieldArrow. */
void roomrom_arrow_fire(link_face_t face, short link_x, short link_y)
{
    unsigned char st = AR_STATE;
    unsigned char d;
    (void)face; (void)link_x; (void)link_y;
    if (nes_ram[NES_BOW] == 0u) return;
    if (st != 0u && (st & 0x80u) == 0u) return;   /* slot busy (ASL / BCC) */
    if (nes_ram[NES_INV_RUPEES] == 0u) return;
    nes_ram[0x0603u] |= 0x02u;                    /* PlayEffect $02 (arrow) */
    inventory_rupee_debit(1u);                    /* INC RupeesToSubtract */
    /* WieldWeapon($10). */
    AR_STATE = 0x10u;
    AR_QSPEED = 0xC0u;
    link_place_weapon_for_player_state(1u);
    d = nes_ram[0x0098u];
    AR_DIR = d;
    AR_X = (unsigned char)(nes_ram[0x0070u] + ((d & 0x01u) ? 0x10u : (d & 0x02u) ? 0xF0u : 0u));
    AR_Y = (unsigned char)(nes_ram[0x0084u] + ((d & 0x04u) ? 0x10u : (d & 0x08u) ? 0xF0u : 0u));
    if (d & 0x0Cu) AR_X = (unsigned char)(AR_X + 3u);
}

unsigned char roomrom_arrow_active(void)
{
    unsigned char hi = (unsigned char)(AR_STATE & 0xF0u);
    return (hi == 0x10u || hi == 0x20u) ? 1u : 0u;
}

/* OffsetAndDrawArrow -> Anim_WriteItemSprites, item slot 2. frame: 0
 * vertical ($28), 1 horizontal ($86 wide pair), 2 spark ($3C). */
static void draw_arrow_nes(unsigned char frame, unsigned char attr, unsigned char ridx)
{
    unsigned char x = (unsigned char)(AR_X + k_off_x[ridx]);
    unsigned char y = (unsigned char)(AR_Y + k_off_y[ridx]);
    unsigned char tile = draw_item_frame_tile(2u, frame);
    unsigned char wide = (tile >= 0x7Cu) ? 1u : 0u;
    if ((AR_STATE & 0xF0u) == 0x20u) attr = 0x01u;   /* spark: palette row 5 */
    if (!wide) x = (unsigned char)(x + 4u);            /* narrow item: X+4 */
    roomrom_sprites_set_arrow_nes((short)x, (short)y, wide,
                                  enemy_render_item_sat(tile, attr));
}

void roomrom_arrow_update(void)
{
    unsigned char st = AR_STATE;
    unsigned char hi = (unsigned char)(st & 0xF0u);
    unsigned char ridx, attr;
    if (hi >= 0x30u) {                             /* UpdateRodOrArrow: rod */
        roomrom_combat_update_rod();
        return;
    }
    if (st == 0u) {
        roomrom_sprites_clear_arrow();
        return;
    }
    if (hi == 0x10u) {
        unsigned char d = AR_DIR;
        unsigned char blocked = 0u;
        RAM(NES_SHOT_COLLISION_FLAG) = 0u;
        if (d & 0x03u) {
            RAM(NES_OBJ_DIR) = d;
            object_move_shot((unsigned char)(d & 0x03u), AR);
            if (RAM(NES_SHOT_COLLISION_FLAG) & 0x80u) blocked = 1u;
            RAM(NES_SHOT_COLLISION_FLAG) = (unsigned char)(RAM(NES_SHOT_COLLISION_FLAG) + 1u);
        }
        if (!blocked && (d & 0x0Cu)) {
            RAM(NES_OBJ_DIR) = d;
            object_move_shot((unsigned char)(d & 0x0Cu), AR);
            if (RAM(NES_SHOT_COLLISION_FLAG) & 0x80u) blocked = 1u;
        }
        if (!blocked) {
            /* DrawArrow: left flips; attr = base + InvArrow - 1. */
            ridx = rdir_index(d);
            attr = (unsigned char)(k_attr[ridx] + nes_ram[NES_INV_ARROW] - 1u);
            if (d == 0x02u) attr = (unsigned char)(attr | 0x40u);
            draw_arrow_nes(k_frame[ridx], attr, ridx);
            return;
        }
        /* HandleArrowOrBoomerangBlocked: 3-frame spark ($2x). */
        AR_CNT = 3u;
        AR_STATE = (unsigned char)(AR_STATE + 0x10u);
    } else {
        /* CheckState20: spark; deactivate when the counter runs out. */
        AR_STATE = 0x28u;
        AR_CNT = (unsigned char)(AR_CNT - 1u);
        if (AR_CNT == 0u) {
            AR_STATE = 0u;                          /* ResetObjState */
            roomrom_sprites_clear_arrow();
            return;
        }
    }
    /* @PrepareArrow: spark frame, attr 0 + InvArrow - 1 (then row 5). */
    ridx = rdir_index(AR_DIR);
    attr = (unsigned char)(nes_ram[NES_INV_ARROW] - 1u);
    draw_arrow_nes(2u, attr, ridx);
}
