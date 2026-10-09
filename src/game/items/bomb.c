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
#include "../enemies/enemy_render.h"
#include "../world/bg_palette.h"
#include "../dungeon/door_state.h"
#include "../dungeon/cellar_meta.h"
#include "../dungeon/uw_render.h"


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

#define OBJ_STATE_(s)  nes_ram[NES_OBJ_STATE_BASE + (s)]
#define OBJ_TIMER_(s)  nes_ram[NES_OBJ_TIMER_BASE + (s)]

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
    nes_ram[NES_OBJ_DIR_BASE + slot] = link_dir;
    OBJ(NES_OBJ_X, slot) = (unsigned char)(link_x + dx);
    OBJ(NES_OBJ_Y, slot) = (unsigned char)(link_y + dy);
    /* NES PlaceWeapon leaves its offset choices in [01] ($10) / [02] ($F0)
     * and ChooseOffsetForDirectionH's [00] = 0. A magic shot that becomes
     * a fire inside CheckMonsterCollisions (HandleShotBlocked -> WieldCandle)
     * leaves [02] = $F0 as the monster's middle X for the bomb/fire checks
     * that follow in the same frame (T-171 asm_equiv: those checks hit and
     * killed the monster again on the Genesis). */
    nes_ram[0x0000u] = 0x00u;
    nes_ram[0x0001u] = 0x10u;
    nes_ram[0x0002u] = 0xF0u;
}

/* Anim_WriteSpritePair palette patch: ObjInvincibilityTimer low bits. */
unsigned char bomb_fire_flash_attrs(unsigned char slot, unsigned char attrs)
{
    unsigned char t = nes_ram[NES_OBJ_INV_TIMER_BASE + slot];
    if (t == 0u) return attrs;
    return (unsigned char)((attrs & 0xFCu) | (t & 0x03u));
}

void roomrom_bomb_init(void)
{
    OBJ_STATE_(BOMB_SLOT_A) = 0u;
    OBJ_STATE_(BOMB_SLOT_B) = 0u;
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

    if (nes_ram[NES_INV_BOMBS] == 0u) return;
    st = OBJ_STATE_(x);
    if (st != 0u && (st & 0xF0u) == 0x10u) {
        x = BOMB_SLOT_B;
        st = OBJ_STATE_(x);
        if (st != 0u && (st & 0xF0u) == 0x10u) return;
    }
    /* The other slot must not hold a bomb that is not yet detonating. */
    other = OBJ_STATE_(x ^ 0x01u);
    if (other != 0u && other < 0x13u) return;

    nes_ram[NES_INV_BOMBS]--;
    g_inventory.bombs = nes_ram[NES_INV_BOMBS];
    inventory_hud_mark_dirty();
    nes_ram[NES_TUNE0_REQUEST] = 0x20u;
    OBJ_TIMER_(x) = 0u;
    OBJ_STATE_(x) = 0x11u;
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

/* Bomb_CheckState4, UW half: open a bombable wall near the blast. */
static void bomb_check_wall(unsigned char x)
{
    signed char y;
    if (nes_ram[NES_CUR_LEVEL] == 0u) return;   /* OW: tile objects own it */
    /* NES skips mode $09 cellars. Native controller entry uses that
     * mode too; metadata alone misses L3Q1's out-of-list raft cellar.
     * Keep the legacy direct-scene fixture fallback. */
    if (nes_ram[0x0012u] == 9u || roomrom_uw_room_is_cellar(roomrom_uw_room_render_get_level(),
                                  roomrom_uw_room_render_get_quest(),
                                  nes_ram[NES_CUR_ROOM_ID])) return;
    for (y = 3; y >= 0; --y) {
        unsigned char bit;
        unsigned char dir;
        if (abs8((unsigned char)(k_wall_hotspot_x[y] - OBJ(NES_OBJ_X, x))) >= 0x18u)
            continue;
        if (abs8((unsigned char)(k_wall_hotspot_y[y] - OBJ(NES_OBJ_Y, x))) >= 0x18u)
            continue;
        bit = k_reverse_dirs[y];
        /* Already opened or a door command pending: nothing to do. */
        if (uw_door_state_get_opened() & bit) return;
        if (nes_ram[0x0054u] != 0u) return;   /* TriggeredDoorCmd */
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
        nes_ram[NES_PPU_MASK_SHADOW] |= 0x01u;
    } else if (t == 0x12u || t == 0x0Du) {
        nes_ram[NES_PPU_MASK_SHADOW] &= (unsigned char)~0x01u;
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
        if (nes_ram[NES_STATUS_BAR_DRAWING] == 0u) px = (unsigned char)(px + 4u);
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
    unsigned char ox = OBJ(NES_OBJ_X, x);
    unsigned char oy = OBJ(NES_OBJ_Y, x);
    signed char c;

    bomb_flash_effect(x);
    draw_bomb_or_cloud(x, ox, oy);
    if ((OBJ_STATE_(x) & 0x0Fu) == 0x02u) return;
    /* DrawOtherBombClouds: clouds 2,1,0; odd frames use the second set. */
    for (c = 2; c >= 0; --c) {
        unsigned char i = (unsigned char)c;
        if (nes_ram[NES_FRAME_COUNTER_CELL] & 0x01u) i = (unsigned char)(i + 6u);
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
        OBJ_TIMER_(x) = k_bomb_times[(unsigned char)(minor - 1u) & 0x03u];
        OBJ_STATE_(x)++;
        minor = (unsigned char)(OBJ_STATE_(x) & 0x0Fu);
        if (minor == 0x03u) {
            nes_ram[NES_EFFECT_REQUEST] |= 0x10u;
        }
        if (minor == 0x05u) {
            OBJ_STATE_(x) = 0u;   /* ResetObjState; A = 0 */
            OBJ_TIMER_(x) = 0u;
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
    if (nes_ram[NES_CUR_ROOM_ID] != s_last_room ||
        nes_ram[NES_CUR_LEVEL] != s_last_level) {
        s_last_room = nes_ram[NES_CUR_ROOM_ID];
        s_last_level = nes_ram[NES_CUR_LEVEL];
        OBJ_STATE_(BOMB_SLOT_A) = 0u;
        OBJ_STATE_(BOMB_SLOT_B) = 0u;
    }
    update_bomb_or_fire(BOMB_SLOT_A);
    update_bomb_or_fire(BOMB_SLOT_B);
    roomrom_ppu_mask_grayscale_sync();
}
