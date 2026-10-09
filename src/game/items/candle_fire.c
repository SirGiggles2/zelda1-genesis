/* candle_fire.c — NES candle fire in object slots $10/$11 (T-110).
 *
 * NES source: Z_01.asm WieldCandle (3948), Z_07.asm UpdateFire (4622),
 *             Anim_AdvanceAnimCounterAndSetObjPosForSpriteDescriptor (5116),
 *             DrawObjectWithAnimAndSpecificSprites (Z_01.asm:5045).
 * Drained C: none for WieldCandle/UpdateFire. MoveObject, DoObjectsCollide,
 *            BeginShove and Link_BeHarmed are drained and called here.
 * Coverage: FULL for the OW fire. Stance: REPLACE the Genesis-only fire
 *           (48 px travel, 30-frame stand, private state).
 *
 * WieldCandle: first empty slot of $10/$11; blue candle refuses when
 * UsedCandle ($513) is set. State $21 moves at q-speed $20 (half a pixel
 * a frame) until |grid offset| = $10, then state $22 stands $3F frames.
 * Draw: anim index $41 -> ObjAnimFrameHeap[$08] = $5C / $5E, attrs
 * ObjAnimAttrHeap[$08] = $02, h-flip = ObjAnimFrame toggled every 4
 * frames. A fire touching Link shoves him and deals $0080.
 *
 * UW: a standing fire runs UpdateCandle (T-111, src/game/dungeon/uw_dark.c).
 */

#include "../combat/combat_runtime.h"   /* T-116 Link item-use state */
#include "candle_fire.h"
#include "bomb.h"
#include <stdint.h>
#include "platform_abi.h"
#include "../../state/inventory.h"
#include "../../state/combat_state.h"
#include "../../state/enemy_state.h"
#include "../enemies/enemy_render.h"
#include "../world/object_dispatch.h"
#include "../combat/collision_dispatch.h"
#include "../combat/link_collision_dispatch.h"
#include "../dungeon/uw_dark.h"             /* T-111 UpdateCandle */

#define NES_OBJ_TIMER_BASE      0x0028u
#define NES_OBJ_DIR_BASE        0x0098u
#define NES_OBJ_ANIM_COUNTER    0x03D0u
#define NES_OBJ_ANIM_FRAME      0x03E4u
#define NES_USED_CANDLE         0x0513u
#define NES_INV_CANDLE          0x065Bu
#define NES_EFFECT_REQUEST      0x0603u

#define FIRE_TILE_LEFT          0x5Cu
#define FIRE_ATTRS              0x02u

#define OBJ_STATE_(s)  nes_ram[NES_OBJ_STATE_BASE + (s)]
#define OBJ_TIMER_(s)  nes_ram[NES_OBJ_TIMER_BASE + (s)]

void roomrom_candle_fire_init(void)
{
    /* Slots $10/$11 are shared with bombs; roomrom_bomb_init owns the
     * reset. */
}

unsigned char roomrom_candle_fire_used_this_room(void)
{
    return nes_ram[NES_USED_CANDLE];
}

void roomrom_candle_fire_mark_used(void)
{
    nes_ram[NES_USED_CANDLE] = 0x01u;
}

void roomrom_candle_fire_room_reset(void)
{
    /* UsedCandle lies in the $0300-$051F block InitMode_EnterRoom clears. */
    nes_ram[NES_USED_CANDLE] = 0x00u;
}

/* WieldCandle. */
void roomrom_candle_fire_spawn(link_face_t face, short link_x, short link_y)
{
    unsigned char x = 0x10u;
    if (OBJ_STATE_(x) != 0u) {
        x = 0x11u;
        if (OBJ_STATE_(x) != 0u) return;
    }
    if (nes_ram[NES_INV_CANDLE] == 0x01u &&
        nes_ram[NES_USED_CANDLE] != 0u) return;
    nes_ram[NES_USED_CANDLE] = 0x01u;
    OBJ(NES_OBJ_GRID_OFFSET, x) = 0u;
    OBJ(NES_OBJ_POS_FRAC, x) = 0u;
    OBJ(NES_OBJ_QSPD_FRAC, x) = 0x20u;
    OBJ_STATE_(x) = 0x21u;
    nes_ram[NES_EFFECT_REQUEST] |= 0x04u;
    nes_ram[NES_OBJ_ANIM_COUNTER + x] = 0x04u;
    link_place_weapon_for_player_state(0u);   /* PlaceWeaponForPlayerState (T-116) */
    bomb_fire_place_weapon(x, bomb_fire_nes_dir_for_face(face),
                           (unsigned char)link_x, (unsigned char)link_y);
}

/* HandleShotBlocked's book fire: WieldCandle with UsedCandle cleared and
 * Link's state saved/restored around it (Z_07.asm), so only the slot
 * search matters. Returns the slot WieldCandle leaves in X: the new fire
 * (state $21), or $11 when both fire slots are busy. */
unsigned char candle_fire_wield_from_shot(void)
{
    unsigned char x = 0x10u;
    if (OBJ_STATE_(x) != 0u) {
        x = 0x11u;
        if (OBJ_STATE_(x) != 0u) return x;
    }
    OBJ(NES_OBJ_GRID_OFFSET, x) = 0u;
    OBJ(NES_OBJ_POS_FRAC, x) = 0u;
    OBJ(NES_OBJ_QSPD_FRAC, x) = 0x20u;
    OBJ_STATE_(x) = 0x21u;
    nes_ram[NES_EFFECT_REQUEST] |= 0x04u;
    nes_ram[NES_OBJ_ANIM_COUNTER + x] = 0x04u;
    bomb_fire_place_weapon(x, nes_ram[NES_OBJ_DIR_BASE],
                           nes_ram[NES_OBJ_X], nes_ram[NES_OBJ_Y]);
    return x;
}

unsigned char roomrom_candle_fire_active(void)
{
    return (unsigned char)(((OBJ_STATE_(0x10u) & 0xF0u) == 0x20u) ||
                           ((OBJ_STATE_(0x11u) & 0xF0u) == 0x20u));
}

void roomrom_candle_fire_update(void)
{
    /* UpdateFire runs inside roomrom_bomb_update (UpdateBombOrFire). */
}

static void draw_fire_and_check_link(unsigned char x)
{
    unsigned char frame_ctr;
    unsigned char left = FIRE_TILE_LEFT;
    unsigned char right = (unsigned char)(FIRE_TILE_LEFT + 2u);
    unsigned char attrs = bomb_fire_flash_attrs(x, FIRE_ATTRS);
    unsigned char ox = OBJ(NES_OBJ_X, x);
    unsigned char oy = OBJ(NES_OBJ_Y, x);

    /* Anim_AdvanceAnimCounterAndSetObjPosForSpriteDescriptor(4). */
    frame_ctr = (unsigned char)(nes_ram[NES_OBJ_ANIM_COUNTER + x] - 1u);
    nes_ram[NES_OBJ_ANIM_COUNTER + x] = frame_ctr;
    if (frame_ctr == 0u) {
        nes_ram[NES_OBJ_ANIM_COUNTER + x] = 0x04u;
        nes_ram[NES_OBJ_ANIM_FRAME + x] ^= 0x01u;
    }
    /* Anim_WriteHorizontallyFlippableSpritePair with [0F] = ObjAnimFrame. */
    if (nes_ram[NES_OBJ_ANIM_FRAME + x] != 0u) {
        unsigned char t = left; left = right; right = t;
        attrs ^= 0x40u;
    }
    enemy_render_weapon_add_obj(x, left, attrs, ox, oy);
    enemy_render_weapon_add_obj(x, right, attrs, (unsigned char)(ox + 8u), oy);

    /* Collision with Link. */
    if (nes_ram[NES_OBJ_INV_TIMER_BASE + 0u] != 0u) return;
    /* GetWideObjectMiddle: Link into [04]/[05], fire into [02]/[03]. */
    COMBAT_HITBOX_X = (uint8_t)(OBJ(NES_OBJ_X, 0u) + 8u);
    COMBAT_HITBOX_Y = (uint8_t)(OBJ(NES_OBJ_Y, 0u) + 8u);
    ENEMY_GLEEOK_NECK_Y_PTR_LO = (uint8_t)(ox + 8u);
    ENEMY_GLEEOK_NECK_Y_PTR_HI = (uint8_t)(oy + 8u);
    COMBAT_WEAPON_SLOT = x;
    if (!collision_do_objects_collide(0x0Eu)) return;
    COMBAT_WEAPON_SLOT = 0u;               /* [00] = 0: Link is shoved */
    link_collision_begin_shove(x);
    COMBAT_THRESHOLD_X = 0x00u;            /* [0D] */
    COMBAT_THRESHOLD_Y = 0x80u;            /* [0E] */
    link_collision_link_be_harmed(x);
}

/* UpdateFire. */
void bomb_fire_update_fire(unsigned char x)
{
    if (OBJ_STATE_(x) == 0x21u) {
        unsigned char saved = OBJ(NES_OBJ_GRID_OFFSET, x);
        unsigned char moved;
        OBJ(NES_OBJ_GRID_OFFSET, x) = 0u;
        nes_ram[NES_OBJ_DIR] = nes_ram[NES_OBJ_DIR_BASE + x];
        object_move_object(x);
        moved = (unsigned char)(saved + OBJ(NES_OBJ_GRID_OFFSET, x));
        OBJ(NES_OBJ_GRID_OFFSET, x) = moved;
        if (((moved & 0x80u) ? (unsigned char)(0u - moved) : moved) != 0x10u) {
            draw_fire_and_check_link(x);
            return;
        }
        OBJ_TIMER_(x) = 0x3Fu;
        OBJ_STATE_(x)++;
    }
    /* Standing fire. */
    if (OBJ_TIMER_(x) == 0u) {
        OBJ_STATE_(x) = 0u;                /* ResetObjState */
        return;
    }
    /* In the UW a standing fire brightens a dark room (UpdateCandle). */
    if (nes_ram[0x0010u] != 0u) uw_dark_update_candle();
    draw_fire_and_check_link(x);
}
