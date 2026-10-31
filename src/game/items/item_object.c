/* item_object.c — dropped-item slot tick (Plan v5c).
 *
 * NES source: reference/aldonunez/Z_04.asm:11236 UpdateItem +
 *             reference/aldonunez/Z_01.asm:4364 TryTakeItem.
 *
 * Drained C: src/oracle/enemies/enemy_flyer_runtime.c:enrt_set_up_fairy_object,
 *            enrt_move_flyer and shared flyer state leaves.
 * Coverage:  PARTIAL (natural Octorok $22 drop/pickup; fairy behavior now
 *            uses the source-backed setup and flyer controller).
 * Stance:    EXTEND.
 *
 * Wires enemy_update_fns[0x60] so dropped-item slots created by
 * SetUpDroppedItem (enemy_walker_bridge.c:854) tick autonomously:
 *   - every other frame decrement Item_ObjItemLifetime (META_ITEM_LIFETIME)
 *   - destroy slot at lifetime 0
 *   - Fairy $23: ControlFairyFlight + MoveFlyer before its draw/pickup path
 *   - active arrow, sword, boomerang, then Link bbox 9x9 vs item
 *   - if hit: item_take_item(id) + destroy slot
 */

#include <stdint.h>
#include "platform_abi.h"
#include "enemy_state.h"          /* ENEMY_X/Y/TYPE/ALIVE_FLAG/PLAYER_OBJ_* */
#include "combat_state.h"         /* OBJ_STATE alias */
#include "item_dispatch.h"        /* item_take_item */
#include "item_object.h"
#include "world/draw_dispatch.h"   /* draw_animate_item_object */
#include "../../state/player_state.h"
#include "../../state/weapon_state.h"
#ifdef ZELDA_DROP_TEST
#include "../enemies/enemy_loop.h"
#include "../../state/game_profile.h"
#endif

extern void c_control_fairy_flight(unsigned int slot);
extern void c_move_flyer(unsigned int slot);

#ifdef ZELDA_DROP_TEST
/* NES source: Z_04.asm:SetUpDroppedItem; Redux arrows.asm:DropList.
 * Drained C: enemy_loop_force_spawn_typed / item_object_update /
 *            enrt_set_up_fairy_object.
 * Coverage: PARTIAL (explicit playtest fixture; natural drop odds unchanged).
 * Stance: EXTEND existing constructors and pickup owners. Slots1..4 remain
 * active for clock freeze/contact testing; only slots5..11 are replaced. */
void item_object_spawn_test_drops(void)
{
    static const unsigned char ids[7] = {
        0x00u, 0x0Fu, 0x18u, 0x21u, 0x22u, 0x23u, 0x55u
    };
    unsigned char i;
    unsigned char count = game_profile_is_remix() ? 7u : 6u;
    extern void enrt_set_up_fairy_object(unsigned int slot);
    for (i = 0u; i < count; ++i) {
        unsigned int slot = 5u + i;
        enemy_loop_force_spawn_typed(slot, 0x60u,
            (unsigned char)(56u + (i & 3u) * 48u),
            (unsigned char)(136u + (i >> 2) * 32u), 0u);
        F_ram_00AC_O(slot) = ids[i];
        F_ram_03A8_O2(slot) = 0xFFu;
        if (ids[i] == 0x23u) enrt_set_up_fairy_object(slot);
    }
}
#endif

/* META_ITEM_LIFETIME = Item_ObjItemLifetime ($03A8+slot).
 * META_ITEM_ID       = Item_ObjItemId      ($00AC+slot, aliases OBJ_STATE).
 *
 * Same defines used by enemy_walker_bridge.c::native_set_up_dropped_item. */
#define META_ITEM_LIFETIME(slot)  F_ram_03A8_O3((slot))
#define META_ITEM_ID(slot)        F_ram_00AC_O2((slot))

/* DestroyMonster_Bank4 — clear slot. Inline to avoid the static binding
 * in enemy_walker_bridge.c. Same fields as native_destroy_monster. */
extern void core_set_type_and_clear_object(unsigned int type, unsigned int slot);

static void item_obj_destroy(unsigned int slot)
{
    /* NES DestroyMonster = SetTypeAndClearObject(0): DestroyObject_WRAM
     * clears shove dir/distance, timer, state, invincibility TIMER ($4F0),
     * sets uninitialized $FF and metastate 1 (T-171: the copy here wrote
     * $4B2, kept the shove cells and set metastate 0; t013_route t4620). */
    core_set_type_and_clear_object(0u, slot);
}

/* NES Abs operates on the signed result of an 8-bit subtraction. */
static unsigned char u8_abs_diff(unsigned char a, unsigned char b)
{
    unsigned char diff = (unsigned char)(a - b);
    return ((diff & 0x80u) != 0u) ? (unsigned char)(0u - diff) : diff;
}

/* TryTakeItem (NES Z_01.asm:4364). Returns 1 if
 * item taken (caller destroys slot), 0 if no collision yet. */
static unsigned char item_obj_try_take(unsigned int slot,
                                      unsigned char taker_x,
                                      unsigned char taker_y)
{
    unsigned char lifetime = (unsigned char)META_ITEM_LIFETIME(slot);
    unsigned char item_x;
    unsigned char item_y;
    unsigned char item_id;

    /* NES grace period: lifetime $F0..$FF = newly spawned, not yet
     * pickable. Prevents Link from instantly grabbing drops that land
     * on him. */
    if (lifetime >= 0xF0u) return 0u;

    item_x = (unsigned char)ENEMY_X(slot);
    item_y = (unsigned char)ENEMY_Y(slot);

    /* NES: |Y_link+3 - Y_item| >= 9 → exit. Y bias of 3 accounts for
     * Link sprite center vs feet offset. */
    if (u8_abs_diff((unsigned char)(taker_y + 3u), item_y) >= 9u) return 0u;
    if (u8_abs_diff(taker_x, item_x) >= 9u) return 0u;

    /* Capture item_id BEFORE the deactivate writes (META_ITEM_ID aliases
     * OBJ_STATE = $00AC, and NES writes ObjState = $FF as part of
     * deactivate, clobbering item id — NES preserves it in scratch $04
     * before the write). */
    item_id = (unsigned char)META_ITEM_ID(slot);

    /* NES TryTakeItem deactivate: ObjState = $FF, ObjY = $FF. */
    OBJ_STATE(slot)  = 0xFFu;
    ENEMY_Y(slot)    = 0xFFu;

    item_take_item(item_id);
    return 1u;
}

/* UpdateItem (NES Z_04.asm:11236) — registered at enemy_update_fns[0x60]. */
void item_object_update(unsigned int slot)
{
    unsigned char lifetime;
    unsigned char frame_counter;
    static const unsigned char taker_slots[4] = { 18u, 13u, 15u, 0u };
    unsigned int i;

    /* Every other frame, decrement lifetime. NES:
     *   LDA FrameCounter
     *   LSR
     *   BCC :+
     *   DEC Item_ObjItemLifetime, X
     *
     * After LSR, BCC branches if the SHIFTED-OUT bit was 0 → i.e. if
     * (FrameCounter & 1) was 0. So decrement on ODD frames. */
    frame_counter = (unsigned char)F_ram_0015_R3;
    if ((frame_counter & 1u) != 0u) {
        /* DEC wraps: a lifetime of 0 on an odd frame becomes $FF. */
        lifetime = (unsigned char)META_ITEM_LIFETIME(slot);
        META_ITEM_LIFETIME(slot) = (uint8_t)(lifetime - 1u);
    }

    /* If lifetime reached 0, destroy slot. */
    if ((unsigned char)META_ITEM_LIFETIME(slot) == 0u) {
        item_obj_destroy(slot);
        return;
    }

    /* NES UpdateFairyObject (Z_04.asm:11505) runs its dedicated flight
     * controller and MoveFlyer before drawing. Its state machine reuses
     * the existing drained flyer leaves and native bridge wander routine. */
    /* The fairy draws itself (UpdateFairyObject ends in DrawFairy: own
     * slot, frame flipping every 4 frames); every other item goes through
     * NES Z_07.asm:1955 AnimateItemObject. META_ITEM_ID(slot) holds the
     * item id ($00AC+slot, aliases OBJ_STATE). */
    if ((unsigned char)META_ITEM_ID(slot) == 0x23u) {
        c_control_fairy_flight(slot);
        c_move_flyer(slot);
        draw_fairy(slot);
    } else {
        draw_animate_item_object((unsigned char)META_ITEM_ID(slot), slot);
    }

    /* NES UpdateItem checks arrow, sword, boomerang, then Link. Active
     * weapons publish their positions into these object slots; Link is
     * always eligible unless his high state bits indicate a halt. */
    /* [0D] counts the takers 4..1 (ItemTakerObjSlots-1,Y); [04] holds
     * the item id handed to TryTakeItem. Both are left as the NES leaves
     * them. */
    if (((unsigned char)OBJ_STATE(0u) & 0xC0u) != 0x40u) {
      for (i = 0u; i < 4u; ++i) {
        unsigned int taker = taker_slots[i];
        unsigned char state = (unsigned char)OBJ_STATE(taker);
        F_ram_000D_R = (uint8_t)(4u - i);
        if (taker == 0u || (state != 0u && (state & 0x80u) == 0u)) {
            F_ram_0004_R = (uint8_t)META_ITEM_ID(slot);
            if (item_obj_try_take(slot, (unsigned char)OBJ_X(taker),
                                  (unsigned char)OBJ_Y(taker))) {
                item_obj_destroy(slot);
                return;
            }
        }
      }
      F_ram_000D_R = 0u;
    }
    /* T-251 P6.1: extend the existing TryTakeItem owner to native party
     * bodies after the unchanged NES taker order. One shared award and
     * one destruction; a halted P1 cannot disable the other players.
     * NES source: Z_01.asm:TryTakeItem; Drained C: item_obj_try_take.
     * Coverage: PARTIAL additional body contacts; Stance: EXTEND. */
    for (i = 1u; i < g_player_count; ++i) {
        const PlayerState *player = &PLAYER(i);
        const PlayerSwordState *sword = &g_player_sword[i];
        const PlayerBoomerangState *boomerang = &g_player_boomerang;
        unsigned char x, y;
        if ((player->state & 0xC0u) == 0x40u) continue;
        /* Same NES arrow/sword/boomerang/body order, using the named
         * current producers instead of P1's one-way collision slots. */
        if ((roomrom_item_extra_arrow_pose((unsigned char)i, &x, &y) &&
             item_obj_try_take(slot, x, y)) ||
            (sword->state != 0u && (sword->state & 0x80u) == 0u &&
             item_obj_try_take(slot, (unsigned char)sword->x, (unsigned char)sword->y)) ||
            (boomerang->owner == i && boomerang->state != 0u &&
             (boomerang->state & 0x80u) == 0u &&
             item_obj_try_take(slot, boomerang->x, boomerang->y)) ||
            item_obj_try_take(slot, (unsigned char)player->x,
                              (unsigned char)player->y)) {
            item_obj_destroy(slot);
            return;
        }
    }
}
