#include "../combat/combat_runtime.h"   /* T-116 Link item-use state */
#include "arrow.h"
#include "../../abi/platform_abi.h"
#include "../world/render/sprite_render.h"
#include "../world/object_dispatch.h"      /* object_move_shot (NES MoveShot) */
#include "../world/draw_dispatch.h"        /* draw_item_frame_tile */
#include "../enemies/enemy_render.h"       /* enemy_render_item_sat */
#include "../../state/inventory.h"
#include "../../state/game_profile.h"
#include "bomb.h"                          /* T-260 roomrom_bomb_blast_at */
#include "combo_ram.h"                     /* T-260 explosive arrow state */

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
#define AR_STATE    F_ram_00AC_O(AR)
#define AR_X        F_ram_0070_O(AR)
#define AR_Y        F_ram_0084_O(AR)
#define AR_DIR      F_ram_0098_O(AR)
#define AR_GRID     F_ram_0394_O4(AR)
#define AR_QSPEED   F_ram_03BC_O3(AR)
#define AR_CNT      F_ram_03D0_O(AR)

#define NES_BOW          0x065Au
#define NES_INV_ARROW    0x0659u
#define NES_INV_RUPEES   0x066Du

/* RDirectionToWeaponFrame / BaseAttribute / OffsetsX / OffsetsY
 * (reverse direction index: up, down, left, right). */
static const unsigned char k_frame[4]  = { 0u, 0u, 1u, 1u };
static const unsigned char k_attr[4]   = { 0x00u, 0x80u, 0x00u, 0x00u };
static const unsigned char k_off_x[4]  = { 0xFCu, 0xFCu, 0x00u, 0x00u };
static const unsigned char k_off_y[4]  = { 0x00u, 0x00u, 0x03u, 0x03u };

static void arrow_spawn(void);

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
    roomrom_arrow_explosive_clear();        /* T-260 */
}

/* WieldArrow. */
void roomrom_arrow_fire(link_face_t face, short link_x, short link_y)
{
    unsigned char st = AR_STATE;
    (void)face; (void)link_x; (void)link_y;
    if (F_ram_065A_N == 0u) return;
    if (st != 0u && (st & 0x80u) == 0u) return;   /* slot busy (ASL / BCC) */
    /* NES source: Redux arrows.asm bank5:$8E80/$8E8A.
     * Drained C: roomrom_arrow_fire (native WieldArrow).
     * Coverage: FULL(success, empty ammo, no bow and busy weapon slot).
     * Stance: EXTEND shared weapon lifecycle with profile-owned payment. */
    if (game_profile_is_remix()) {
        if (F_ram_0677_R == 0u) return;
        --F_ram_0677_R;
    } else {
        if (F_ram_066D_N == 0u) return;
        inventory_rupee_debit(1u);
    }
    arrow_spawn();
}

/* T-260: explosive arrow for the bomb-arrow combo. The recipe already
 * checked bow/slot and charged 1 arrow + 1 bomb; every arrow end ($1x ->
 * $2x: monster hit, wall, screen edge) hands off to a bomb blast there. */
#define ARROW_PLAYERS COMBO_PLAYERS
/* State in ComboRam (native block): explode 1 flying, 2 blast pending. */
#define s_explode    (COMBO_RAM->explode)
#define s_blast_x    (COMBO_RAM->blast_x)
#define s_blast_y    (COMBO_RAM->blast_y)
#define s_fire_room  (COMBO_RAM->fire_room)
#define s_fire_level (COMBO_RAM->fire_level)

void roomrom_arrow_fire_explosive(unsigned char player)
{
    if (!roomrom_bomb_reserve(player)) return;
    arrow_spawn();
    s_explode[player & (ARROW_PLAYERS - 1u)] = 1u;
    s_fire_room[player & (ARROW_PLAYERS - 1u)] = F_ram_00EB_N3;
    s_fire_level[player & (ARROW_PLAYERS - 1u)] = F_ram_0010_N2;
}

unsigned char roomrom_arrow_explosive_live(unsigned char player)
{
    return s_explode[player & (ARROW_PLAYERS - 1u)];
}

void roomrom_arrow_explosive_clear(void)
{
    unsigned char i;
    for (i = 0u; i < ARROW_PLAYERS; ++i) {
        s_explode[i] = 0u;
        COMBO_RAM->blast_slot[i] = 0u;
    }
}

void roomrom_arrow_explosive_contact(unsigned char player)
{
    if (!COMBO_RAM->borrowed_arrow && s_explode[player & 3u] == 1u) {
        AR_STATE = 0x20u;
        AR_CNT = 3u;
    }
}

static void arrow_explosive_tick(unsigned char player)
{
    unsigned char *e = &s_explode[player & (ARROW_PLAYERS - 1u)];
    /* Leaving the room drops the arrow and its blast (ResetInvObjState). */
    if (s_fire_room[player & 3u] != F_ram_00EB_N3 ||
        s_fire_level[player & 3u] != F_ram_0010_N2) {
        *e = 0u;
        COMBO_RAM->blast_slot[player & 3u] = 0u;
        return;
    }
    if (*e == 1u && (AR_STATE & 0xF0u) != 0x10u) {
        /* Arrow ended: blast centred on the arrow object (vertical arrows
         * carry WieldArrow's +3 X nudge). */
        s_blast_x[player & 3u] = (unsigned char)(AR_X - ((AR_DIR & 0x0Cu) ? 3u : 0u));
        s_blast_y[player & 3u] = AR_Y;
        *e = 2u;
    }
    /* The reserved shared slot keeps impact detonation available. */
    if (*e == 2u && roomrom_bomb_blast_at(player, s_blast_x[player & 3u], s_blast_y[player & 3u]))
        *e = 0u;
}

static void arrow_spawn(void)
{
    unsigned char d;
    F_ram_0603 |= 0x02u;                    /* PlayEffect $02 (arrow) */
    /* WieldWeapon($10). */
    AR_STATE = 0x10u;
    AR_QSPEED = 0xC0u;
    link_place_weapon_for_player_state(1u);
    d = F_ram_0098;
    AR_DIR = d;
    AR_X = (unsigned char)(F_ram_0070 + ((d & 0x01u) ? 0x10u : (d & 0x02u) ? 0xF0u : 0u));
    AR_Y = (unsigned char)(F_ram_0084 + ((d & 0x04u) ? 0x10u : (d & 0x08u) ? 0xF0u : 0u));
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

void roomrom_arrow_update_for_player(unsigned char player)
{
    unsigned char st;
    if (s_explode[player & (ARROW_PLAYERS - 1u)]) arrow_explosive_tick(player);
    st = AR_STATE;
    unsigned char hi = (unsigned char)(st & 0xF0u);
    unsigned char ridx, attr;
    if (hi >= 0x30u) {                             /* UpdateRodOrArrow: rod */
        roomrom_combat_update_rod_for_player(player);
        return;
    }
    if (st == 0u) {
        roomrom_sprites_clear_arrow();
        return;
    }
    if (hi == 0x10u) {
        unsigned char d = AR_DIR;
        unsigned char blocked = 0u;
        F_ram_000E_R = 0u;
        if (d & 0x03u) {
            F_ram_000F_R2 = d;
            object_move_shot((unsigned char)(d & 0x03u), AR);
            if (F_ram_000E_R & 0x80u) blocked = 1u;
            F_ram_000E_R = (unsigned char)(F_ram_000E_R + 1u);
        }
        if (!blocked && (d & 0x0Cu)) {
            F_ram_000F_R2 = d;
            object_move_shot((unsigned char)(d & 0x0Cu), AR);
            if (F_ram_000E_R & 0x80u) blocked = 1u;
        }
        if (!blocked) {
            /* DrawArrow: left flips; attr = base + InvArrow - 1. */
            ridx = rdir_index(d);
            attr = (unsigned char)(k_attr[ridx] + F_ram_0659_N - 1u);
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
    attr = (unsigned char)(F_ram_0659_N - 1u);
    draw_arrow_nes(2u, attr, ridx);
}

void roomrom_arrow_update(void)
{
    roomrom_arrow_update_for_player(0u);
}
