/* bomb.c — NES bomb / fire object slots $10 and $11 (T-110).
 *
 * NES source: Z_01.asm WieldBomb (3804) + PlaceWeapon (3877),
 *             Z_07.asm UpdateBombOrFire (4758), UpdateBomb (4770),
 *             DrawBombOrCloudAt / DrawCloud / DrawOtherBombClouds (4890-4975),
 *             Z_01.asm UpdateBombFlashEffect (4038).
 * Drained C: weapon_dispatch.c weapon_wield_bomb (unlinked from the B-button
 *            path; different slot contract), oracle weapon_runtime.c
 *            weprt_wield_bomb (oracle only). Coverage: PARTIAL.
 * Stance: REPLACE the Genesis-only single bomb (60-frame fuse, private
 *         state, never visible to NES consumers). Bombs now live in NES
 *         ObjState/ObjTimer/ObjX/ObjY/ObjDir for slots $10/$11, which is
 *         what collision_check_monster_bomb_or_fire_collision (monster
 *         damage) and the OW rock-wall tile object read.
 *
 * Both slots may hold a bomb or a fire; candle_fire.c owns the fire half
 * (WieldCandle / UpdateFire). roomrom_bomb_update runs UpdateBombOrFire
 * for $10 then $11, the NES order in UpdateMode5Play @UpdateWeapons.
 *
 * Documented divergences:
 *  - Link's own ObjState/ObjAnimCounter item-use writes
 *    (PlaceWeaponForPlayerStateAndAnim) are not made: Genesis Link state
 *    is owned by the native Link update, which reads nothing from $AC for
 *    item use and would never clear a $10 there.
 *  - UW bombable wall: NES sets TriggeredDoorCmd $06 / TriggeredDoorDir
 *    for the door updater; Genesis does the same through
 *    uw_door_state_trigger_open and door_state.c UpdateDoors (T-119).
 *  - Sound: Tune0Request $20 and PlayEffect $10 are written to the NES
 *    request cells (played by src/game/audio/audio_requests.c).
 */

#include "../combat/combat_runtime.h"   /* T-116 Link item-use state */
#include "bomb.h"
#include "candle_fire.h"
#include "platform_abi.h"
#include "../../state/inventory.h"
#include "../../state/weapon_state.h"
#include "../../state/game_profile.h"
#include "../enemies/enemy_render.h"
#include "../world/bg_palette.h"
#include "../dungeon/door_state.h"
#include "../dungeon/cellar_meta.h"
#include "../dungeon/uw_render.h"
#include "combo_recipes.h"                 /* T-260 combo_burst_emit */
#include "combo_ram.h"


#define NES_OBJ_TIMER_BASE      0x0028u
#define NES_OBJ_DIR_BASE        0x0098u
#define NES_INV_BOMBS           0x0658u
#define NES_TUNE0_REQUEST       0x0604u
#define NES_EFFECT_REQUEST      0x0603u
#define NES_STATUS_BAR_DRAWING  0x0504u
#define NES_FRAME_COUNTER_CELL  0x0015u

#define BOMB_SLOT_A             0x10u
#define BOMB_SLOT_B             0x11u
#define BOMB_ITEM_TILE          0x34u   /* Anim_ItemFrameTiles[$03] */
#define BOMB_CLOUD_ATTR         0x01u   /* DrawCloud: [04]/[05] = 1 */

#define OBJ_STATE_(s)  (g_shared_effect[(s) - BOMB_SLOT_A].state)
#define OBJ_TIMER_(s)  (g_shared_effect[(s) - BOMB_SLOT_A].timer)

#define s_emit (COMBO_RAM->emit)   /* T-260: slot $10/$11 bomb fires the burst */

static const unsigned char k_bomb_times[4] = { 0x30u, 0x18u, 0x0Cu, 0x06u };
static const unsigned char k_wall_hotspot_x[4] = { 0x78u, 0x78u, 0x20u, 0xD0u };
static const unsigned char k_wall_hotspot_y[4] = { 0x5Du, 0xBDu, 0x8Du, 0x8Du };
static const unsigned char k_reverse_dirs[4] = { 0x08u, 0x04u, 0x02u, 0x01u };
/* BombCloudOffsetsY1, X1, Y2, X2 — contiguous in NES so index +6 reads
 * the second set. */
static const unsigned char k_cloud_offsets[12] = {
    0xF3u, 0x00u, 0x0Eu,  0xF9u, 0x0Eu, 0x07u,
    0xF3u, 0x00u, 0x0Eu,  0x07u, 0xF3u, 0xF9u,
};

static unsigned char abs8(unsigned char v)
{
    return (v & 0x80u) ? (unsigned char)(0u - v) : v;
}

unsigned char bomb_fire_nes_dir_for_face(link_face_t face)
{
    switch (face) {
    case LINK_FACE_RIGHT: return 0x01u;
    case LINK_FACE_LEFT:  return 0x02u;
    case LINK_FACE_DOWN:  return 0x04u;
    default:              return 0x08u;
    }
}

/* PlaceWeapon: $10 px from Link in his direction; weapon takes his dir. */
void bomb_fire_place_weapon(unsigned char slot, unsigned char link_dir,
                            unsigned char link_x, unsigned char link_y)
{
    unsigned char dx = 0u, dy = 0u;
    if (link_dir & 0x01u)      dx = 0x10u;
    else if (link_dir & 0x02u) dx = 0xF0u;
    if (link_dir & 0x04u)      dy = 0x10u;
    else if (link_dir & 0x08u) dy = 0xF0u;
    F_ram_0098_X(slot) = link_dir;
    F_ram_0070_O2(slot) = (unsigned char)(link_x + dx);
    F_ram_0084_O2(slot) = (unsigned char)(link_y + dy);
    /* NES PlaceWeapon leaves its offset choices in [01] ($10) / [02] ($F0)
     * and ChooseOffsetForDirectionH's [00] = 0. A magic shot that becomes
     * a fire inside CheckMonsterCollisions (HandleShotBlocked -> WieldCandle)
     * leaves [02] = $F0 as the monster's middle X for the bomb/fire checks
     * that follow in the same frame (T-171 asm_equiv: those checks hit and
     * killed the monster again on the Genesis). */
    F_ram_0000 = 0x00u;
    F_ram_0001 = 0x10u;
    F_ram_0002 = 0xF0u;
}

/* Anim_WriteSpritePair palette patch: ObjInvincibilityTimer low bits. */
unsigned char bomb_fire_flash_attrs(unsigned char slot, unsigned char attrs)
{
    unsigned char t = F_ram_04F0_X2(slot);
    if (t == 0u) return attrs;
    return (unsigned char)((attrs & 0xFCu) | (t & 0x03u));
}

void roomrom_bomb_init(void)
{
    shared_effect_set_state(BOMB_SLOT_A, 0u);
    shared_effect_set_state(BOMB_SLOT_B, 0u);
    s_emit[0] = s_emit[1] = 0u;
    combo_burst_clear();                    /* T-260 */
    enemy_render_weapon_reset(BOMB_SLOT_A);
    enemy_render_weapon_reset(BOMB_SLOT_B);
    roomrom_sprites_clear_bomb();
    roomrom_sprites_clear_explosion();
    roomrom_sprites_clear_candle_fire();
    roomrom_ppu_mask_grayscale_init();
}

/* WieldBomb. */
void roomrom_bomb_place(link_face_t face, short link_x, short link_y)
{
    unsigned char x = BOMB_SLOT_A;
    unsigned char st;
    unsigned char other;

    if (F_ram_0658_N == 0u) return;
    st = OBJ_STATE_(x);
    if (roomrom_bomb_slot_reserved(x) || (st != 0u && (st & 0xF0u) == 0x10u)) {
        x = BOMB_SLOT_B;
        st = OBJ_STATE_(x);
        if (roomrom_bomb_slot_reserved(x) || (st != 0u && (st & 0xF0u) == 0x10u)) return;
    }
    /* The other slot must not hold a bomb that is not yet detonating. */
    other = OBJ_STATE_(x ^ 0x01u);
    if (other != 0u && other < 0x13u) return;

    F_ram_0658_N--;
    g_inventory.bombs = F_ram_0658_N;
    inventory_hud_mark_dirty();
    F_ram_0604_N = 0x20u;
    shared_effect_set_timer(x, 0u);
    shared_effect_set_state(x, 0x11u);
    /* PlaceWeaponForPlayerStateAndAnimAndWeaponState (T-116). */
    link_place_weapon_for_player_state(1u);
    bomb_fire_place_weapon(x, bomb_fire_nes_dir_for_face(face),
                           (unsigned char)link_x, (unsigned char)link_y);
}

unsigned char roomrom_bomb_active(void)
{
    /* B-button gate used by main.c before calling roomrom_bomb_place.
     * WieldBomb does its own slot checks, so only report "busy" when
     * both slots already hold bombs. */
    unsigned char a = OBJ_STATE_(BOMB_SLOT_A);
    unsigned char b = OBJ_STATE_(BOMB_SLOT_B);
    return (unsigned char)(((a & 0xF0u) == 0x10u && a != 0u) &&
                           ((b & 0xF0u) == 0x10u && b != 0u));
}

unsigned char roomrom_bomb_slot_free(void)
{
    return (unsigned char)((OBJ_STATE_(BOMB_SLOT_A) == 0u && !roomrom_bomb_slot_reserved(BOMB_SLOT_A)) ||
                           (OBJ_STATE_(BOMB_SLOT_B) == 0u && !roomrom_bomb_slot_reserved(BOMB_SLOT_B)));
}

unsigned char roomrom_bomb_slot_reserved(unsigned char slot)
{
    unsigned char p;
    if (!game_profile_is_remix()) return 0u;
    for (p=0u;p<COMBO_PLAYERS;++p) if (COMBO_RAM->blast_slot[p]==slot) return 1u;
    return 0u;
}

unsigned char roomrom_bomb_reserve(unsigned char player)
{
    unsigned char s;
    if (player>=COMBO_PLAYERS) return 0u;
    for(s=BOMB_SLOT_A;s<=BOMB_SLOT_B;++s) {
        if (!OBJ_STATE_(s) && !roomrom_bomb_slot_reserved(s)) {
            COMBO_RAM->blast_slot[player]=s;
            return 1u;
        }
    }
    return 0u;
}

void roomrom_bomb_place_emitting(void)
{
    unsigned char x = BOMB_SLOT_A;
    if (OBJ_STATE_(x) != 0u || roomrom_bomb_slot_reserved(x)) {
        x = BOMB_SLOT_B;
        if (OBJ_STATE_(x) != 0u || roomrom_bomb_slot_reserved(x)) return;
    }
    F_ram_0604_N = 0x20u;
    shared_effect_set_timer(x, 0u);
    shared_effect_set_state(x, 0x11u);
    link_place_weapon_for_player_state(1u);
    bomb_fire_place_weapon(x, F_ram_0098, F_ram_0070, F_ram_0084);
    s_emit[x - BOMB_SLOT_A] = 1u;
}

unsigned char roomrom_bomb_emitting(void)
{
    return (unsigned char)(s_emit[0] | s_emit[1]);
}

unsigned char roomrom_bomb_blast_at(unsigned char player, unsigned char x, unsigned char y)
{
    unsigned char s = COMBO_RAM->blast_slot[player & 3u];
    if (s < BOMB_SLOT_A || s > BOMB_SLOT_B || OBJ_STATE_(s)) return 0u;
    COMBO_RAM->blast_slot[player & 3u] = 0u;
    F_ram_0098_X(s) = 0u;
    F_ram_0070_O2(s) = x;
    F_ram_0084_O2(s) = y;
    shared_effect_set_timer(s, 0u);
    shared_effect_set_state(s, 0x12u);   /* fuse already burnt */
    return 1u;
}

/* Bomb_CheckState4, UW half: open a bombable wall near the blast. */
static void bomb_check_wall(unsigned char x)
{
    signed char y;
    if (F_ram_0010_N2 == 0u) return;   /* OW: tile objects own it */
    /* NES skips mode $09 cellars. Native controller entry uses that
     * mode too; metadata alone misses L3Q1's out-of-list raft cellar.
     * Keep the legacy direct-scene fixture fallback. */
    if (F_ram_0012 == 9u || roomrom_uw_room_is_cellar(roomrom_uw_room_render_get_level(),
                                  roomrom_uw_room_render_get_quest(),
                                  F_ram_00EB_N3)) return;
    for (y = 3; y >= 0; --y) {
        unsigned char bit;
        unsigned char dir;
        if (abs8((unsigned char)(k_wall_hotspot_x[y] - F_ram_0070_O2(x))) >= 0x18u)
            continue;
        if (abs8((unsigned char)(k_wall_hotspot_y[y] - F_ram_0084_O2(x))) >= 0x18u)
            continue;
        bit = k_reverse_dirs[y];
        /* Already opened or a door command pending: nothing to do. */
        if (uw_door_state_get_opened() & bit) return;
        if (F_ram_0054 != 0u) return;   /* TriggeredDoorCmd */
        dir = (bit == DOOR_BIT_E) ? DOOR_DIR_E :
              (bit == DOOR_BIT_W) ? DOOR_DIR_W :
              (bit == DOOR_BIT_S) ? DOOR_DIR_S : DOOR_DIR_N;
        if (uw_door_state_get_type(dir) != DOOR_TYPE_BOMBABLE) return;
        uw_door_state_trigger_open(bit);   /* T-119: UpdateDoors animates */
        return;
    }
}

/* UpdateBombFlashEffect: grayscale on at timer $16/$11, off at $12/$0D. */
static void bomb_flash_effect(unsigned char x)
{
    unsigned char t;
    if (OBJ_STATE_(x) != 0x13u) return;
    t = OBJ_TIMER_(x);
    if (t == 0x16u || t == 0x11u) {
        F_ram_00FE_N2 |= 0x01u;
    } else if (t == 0x12u || t == 0x0Du) {
        F_ram_00FE_N2 &= (unsigned char)~0x01u;
    }
}

/* DrawBombOrCloudNoFlashing at (px, py): frame = minor state - 2. */
static void draw_bomb_or_cloud(unsigned char x, unsigned char px,
                               unsigned char py)
{
    unsigned char frame = (unsigned char)((OBJ_STATE_(x) & 0x0Fu) - 2u);
    if (frame == 0u) {
        /* Narrow item tile: one 8x16 sprite, X + 4 unless the status bar
         * is being drawn (Anim_WriteSpecificItemSprites). */
        if (F_ram_0504_N == 0u) px = (unsigned char)(px + 4u);
        enemy_render_weapon_add_item(x, BOMB_ITEM_TILE,
                                     bomb_fire_flash_attrs(x, BOMB_CLOUD_ATTR),
                                     px, py);
    } else {
        enemy_render_weapon_add_cloud(x, frame,
                                      bomb_fire_flash_attrs(x, BOMB_CLOUD_ATTR),
                                      px, py);
    }
}

static void draw_bomb(unsigned char x)
{
    unsigned char ox = F_ram_0070_O2(x);
    unsigned char oy = F_ram_0084_O2(x);
    signed char c;

    bomb_flash_effect(x);
    draw_bomb_or_cloud(x, ox, oy);
    if ((OBJ_STATE_(x) & 0x0Fu) == 0x02u) return;
    /* DrawOtherBombClouds: clouds 2,1,0; odd frames use the second set. */
    for (c = 2; c >= 0; --c) {
        unsigned char i = (unsigned char)c;
        if (F_ram_0015_N2 & 0x01u) i = (unsigned char)(i + 6u);
        draw_bomb_or_cloud(x,
                           (unsigned char)(ox + k_cloud_offsets[i + 3u]),
                           (unsigned char)(oy + k_cloud_offsets[i]));
    }
}

/* UpdateBomb. */
static void update_bomb(unsigned char x)
{
    if (OBJ_TIMER_(x) == 0u) {
        unsigned char minor = (unsigned char)(OBJ_STATE_(x) & 0x0Fu);
        shared_effect_set_timer(x, k_bomb_times[(unsigned char)(minor - 1u) & 0x03u]);
        shared_effect_set_state(x, (unsigned char)(OBJ_STATE_(x) + 1u));
        minor = (unsigned char)(OBJ_STATE_(x) & 0x0Fu);
        if (minor == 0x03u) {
            F_ram_0603_N |= 0x10u;
            if (s_emit[x - BOMB_SLOT_A]) {
                s_emit[x - BOMB_SLOT_A] = 0u;
                combo_burst_emit(F_ram_0070_O2(x), F_ram_0084_O2(x));
            }
        }
        if (minor == 0x05u) {
            shared_effect_set_state(x, 0u);   /* ResetObjState; A = 0 */
            shared_effect_set_timer(x, 0u);
            return;
        }
        if (minor == 0x04u) bomb_check_wall(x);
    }
    draw_bomb(x);
}

static void update_bomb_or_fire(unsigned char x)
{
    unsigned char st;
    enemy_render_weapon_reset(x);
    st = OBJ_STATE_(x);
    if (st == 0u) return;
    if ((st & 0xF0u) == 0x10u) update_bomb(x);
    else bomb_fire_update_fire(x);
}

/* ResetInvObjState clears the weapon slots when Link leaves a room
 * (Z_05.asm, after RunCrossRoomTasksAndBeginUpdateMode). Genesis has no
 * mode-4/7 spine yet (T-095), so the room/level change is detected here. */
static unsigned char s_last_room = 0xFFu;
static unsigned char s_last_level = 0xFFu;

void roomrom_bomb_update(void)
{
    if (F_ram_00EB_N3 != s_last_room ||
        F_ram_0010_N2 != s_last_level) {
        s_last_room = F_ram_00EB_N3;
        s_last_level = F_ram_0010_N2;
        shared_effect_set_state(BOMB_SLOT_A, 0u);
        shared_effect_set_state(BOMB_SLOT_B, 0u);
        s_emit[0] = s_emit[1] = 0u;
    }
    update_bomb_or_fire(BOMB_SLOT_A);
    update_bomb_or_fire(BOMB_SLOT_B);
    roomrom_ppu_mask_grayscale_sync();
}
