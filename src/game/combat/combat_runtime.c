/* Phase 12.2 SGDK-1 cleanup: dropped <genesis.h>; route PAL_setColors
 * through render_cram_subrange_upload(). */
#include "combat_runtime.h"
#include "sword_runtime.h"
#include "../world/render/sprite_render.h"
#include "../world/render/sprite_slots.h"
#include "../world/bg_palette.h"  /* Phase 12.2 promoted */
#include "../../state/inventory.h"
#include "../options/options_consumer.h"
#include "../options/options_state.h"
#include "platform_abi.h"
#include "render_abi.h"

/* NES source: Redux sword_draw.asm:sword_detect and magic_beam_align.asm.
 * Drained C: update_sword_or_rod and roomrom_combat_try_swing below.
 * Coverage: PARTIAL(core states/pose; target hitboxes and lifecycle pending).
 * Stance: EXTEND native weapon owner; remove independent core shortcut. */
#include "../enemies/enemy_render.h"   /* T-116 item SAT */
#include "../items/sword_shot.h"        /* T-116 slot $0E */
#include "../world/draw_dispatch.h"     /* draw_item_frame_tile */

/* engine S7 v4 combat — sword swing.
 *
 * NES Z1 model (reference/aldonunez/Z_05.asm WieldSword + Z_07.asm
 * UpdateSwordOrRod + PlayerToWeaponOffsets[XY]):
 *
 *   State 1 (5 frames): windup. Sword raised UP regardless of facing.
 *                       Link in attack pose facing his current direction.
 *   State 2 (8 frames): full extend in facing direction.
 *   State 3 (1 frame):  mid-retract.
 *   State 4 (1 frame):  almost retracted.
 *   State 5 (1 frame):  invisible — sword sprite hidden, Link returns
 *                       to walk pose.
 *
 * Total swing window: 5 + 8 + 1 + 1 + 1 = 16 frames. Re-swing locked
 * the entire 16 frames. Link's body stays in attack pose for states
 * 1-4 (15 frames), reverts to walk pose at state 5.
 *
 * Sword position offsets (from PlayerToWeaponOffsetsX/Y at Z_07:4337):
 *   reverse-direction order = up, down, left, right
 *   state 1: X=-1,+1, 0,-8   Y=-9,-14,-11,-11
 *   state 2: X=-1,+1,-11,+11 Y=-10,+13,+3,+3
 *   state 3: X=-1,+1,-7,+7   Y=-9,+9,+3,+3
 *   state 4: X=-1,+1,-3,+3   Y=-1,+5,+3,+3
 *
 * For state 1 the sword is drawn UP (vertical, no flip) regardless of
 * Link's facing — the windup. The position offset for state 1 still
 * varies per facing (Link's hand position differs).
 */

/* Native per-player sword ownership; legacy slots are consumer adapters. */
#include "../../state/weapon_state.h"

unsigned char roomrom_combat_get_swing_state(void) { return g_player_sword[0].state; }
short         roomrom_combat_get_swing_x(void)     { return g_player_sword[0].x; }
short         roomrom_combat_get_swing_y(void)     { return g_player_sword[0].y; }
link_face_t   roomrom_combat_get_swing_face(void)  { return (link_face_t)g_player_sword[0].face; }

/* Sword visual Y bias; see recompute_y_bias (0 except Redux UW). */
static short s_uw_y_bias = 0;

/* NES source: Redux sword_draw.asm:load_attr and Z_07:CalcSwordAttrs.
 * Drained C: roomrom_combat_update Redux sprite selection.
 * Coverage: FULL equipment subpalette; Stance: EXTEND current owner.
 * NES
 * @CalcSwordAttrs (Z_07.asm:4471) computes sub-pal = base_attr +
 * Items - 1 with base_attr = 0 (RDirectionToWeaponBaseAttribute),
 * so sub-pal == Items - 1. White sword (Items=2) -> 1,
 * magic sword (Items=3) -> 2. Defensive: clamp >3 (NES Items
 * tops out at 3 for sword). */
static unsigned char sword_subpal_for_items(unsigned char items_val) {
    if (items_val == 0u) return 0u;          /* defensive: holds sprite at 0 */
    if (items_val > 4u) items_val = 4u;       /* clamp */
    return (unsigned char)(items_val - 1u);
}

/* Track UW separately so set_redux can recompute bias in case the redux
 * flag changes after set_uw was called. */
static unsigned char s_in_uw = 0u;

static void recompute_y_bias(void);

void roomrom_combat_set_uw(unsigned char in_uw)
{
    s_in_uw = in_uw ? 1u : 0u;
    recompute_y_bias();
}

static unsigned char s_redux = 0u;
unsigned char roomrom_combat_is_redux(void) { return s_redux; }
/* T-251: extra players run the same combat core but draw through the MP
 * renderer; suppress the P1 attack-pose write during their context. */
static unsigned char s_draw_enabled = 1u;
void roomrom_combat_set_draw_enabled(unsigned char enabled) { s_draw_enabled = enabled; }
void roomrom_combat_set_redux(unsigned char redux)
{
    s_redux = redux ? 1u : 0u;
    recompute_y_bias();
}

/* Compute sword visual Y bias.
 *
 * Vanilla Z1 (Z_07.asm Link_EndMoveAndAnimate @Animate): Link is drawn
 * +2 px in the overworld and cellars; the sword is drawn at its ObjY.
 * Genesis now applies Link's +2 in sprite_render.c (T-092), so the
 * vanilla sword needs no bias anywhere.
 *
 * Redux (Zelda1-Redux/code/gameplay/sword_draw.asm:104): the OW sword is
 * not shifted, the UW sword is shifted Y -= 2 (Link keeps vanilla's +2).
 *   Vanilla: bias 0
 *   Redux OW: 0, Redux UW: -2
 */
static void recompute_y_bias(void)
{
    s_uw_y_bias = (s_redux && s_in_uw) ? (short)-2 : (short)0;
}

/* Mandatory Redux core 8-frame state-2 sweep; optional ALttP is separate.
 *
 * Source: Zelda1-Redux/code/gameplay/sword_draw.asm wide_sword_xpos/ypos/
 * sprite/face/flip/flip_h16 tables. Each direction has 8 frames; the sword
 * arcs from one orthogonal side (windup) through diagonal to extended in
 * the facing direction.
 *
 * Direction order: NES tables use UP, DOWN, LEFT, RIGHT (reverse-direction
 * index). engine link_face_t uses DOWN, UP, LEFT, RIGHT — tables below
 * are reordered to match.
 */

/* Per-face per-frame sword X/Y offsets from Link's top-left. */
/* 0=vertical (8x16), 1=horizontal (16x16), 2=diagonal (16x16). */
static const unsigned char redux_sprite[4][8] = {
    /* DOWN  */ { 1, 1, 2, 2, 2, 2, 0, 0 },
    /* UP    */ { 1, 1, 2, 2, 2, 2, 0, 0 },
    /* LEFT  */ { 0, 0, 2, 2, 2, 2, 1, 1 },
    /* RIGHT */ { 0, 0, 2, 2, 2, 2, 1, 1 },
};

/* Genesis-side combined flip flags per frame. NES handles flips
 * differently for narrow (vertical/diagonal) vs wide (horizontal):
 *   - Narrow tile (in [$20,$62)): goes to Anim_WriteSpritePair directly.
 *     wide_sword_flip_h16 is NOT applied. hflip = wide_sword_flip & $40.
 *   - Wide tile (>= $7C): goes to Anim_WriteHorizontallyFlippableSpritePair
 *     which toggles hflip when h16=1. Genesis hflip on 16x16 sprite
 *     replicates this: hflip = (flip & $40 ? 1 : 0) XOR h16.
 * Tables below pre-compute the per-frame hflip with this distinction. */
static const unsigned char redux_hflip[4][8] = {
    /* DOWN  */ { 1, 1, 0, 0, 0, 0, 1, 1 },
    /* UP    */ { 0, 0, 1, 1, 1, 1, 0, 0 },
    /* LEFT  */ { 0, 0, 0, 0, 0, 0, 1, 1 },
    /* RIGHT */ { 0, 0, 1, 1, 1, 1, 0, 0 },
};

static const unsigned char redux_vflip[4][8] = {
    /* DOWN  */ { 0, 0, 1, 1, 1, 1, 1, 1 },
    /* UP    */ { 0, 0, 0, 0, 0, 0, 0, 0 },
    /* LEFT  */ { 0, 0, 0, 0, 0, 0, 0, 0 },
    /* RIGHT */ { 0, 0, 0, 0, 0, 0, 0, 0 },
};

/* Weapon orientation is independent of Link facing during the sweep.
 * Rows match link_face_t (DOWN, UP, LEFT, RIGHT). */
void roomrom_combat_init(void)
{
    F_ram_00B9_R = 0u;   /* T-116: sword slot $0D */
    player_sword_reset_all();
    roomrom_sprites_clear_sword();
    sword_shot_init();           /* T-116: shot slot $0E */
}

extern void audio_sfx_play(unsigned char sfx);

/* NES Z_05.asm:6889-6891 Link_HandleInput sword-block gate:
 *   LDA SwordBlockedLongTimer ($4C)
 *   ORA SwordBlocked          ($52E)
 *   BNE :+        ; skip WieldSword if either non-zero
 * BlueBubble2 ($2D) set SwordBlocked = $type - $2C on touch (enemy_walker_
 * runtime.c:34). BlueBubble2_DropEffect ($1158) sets SwordBlockedLongTimer.
 * Without this gate Link swings through bubble-flash with sword intact. */
static unsigned char sword_blocked_by_bubble(void)
{
    return (unsigned char)(F_ENEMY_BUBBLE_EFFECT_R2 | F_ram_052E_R3);
}

/* ---- T-116: NES Link item-use state and sword (object slot $0D) ----
 * NES source: Z_05.asm WieldSword / WieldWeapon, Z_01.asm
 * PlaceWeaponForPlayerState[AndAnim] / PlaceWeapon, Z_07.asm
 * UpdateSwordOrRod, AnimateLinkBase / AnimateLinkObjState, Walker_Move
 * (movement gate). State lives in the NES cells so collision and lockstep
 * see it: Link ObjState $AC / ObjAnimCounter $3D0 / ObjAnimFrame $3E4,
 * sword ObjState $B9, counter $3DD, X $7D, Y $91, dir $A5, q-speed $3C9.
 * Redux extends this same native state machine. */
#define L_STATE     F_ram_00AC_R
#define L_ANIMCNT   F_ram_03D0_R
#define L_ANIMFRAME F_ram_03E4_R
#define L_DIR       F_ram_0098_R
#define SW_SLOT     0x0Du
#define ROD_SLOT    0x12u
#define SW_QSPEED   F_SW_QSPEED_R

static link_face_t nes_dir_to_face(unsigned char dir)
{
    return (dir & 8u) ? LINK_FACE_UP :
           (dir & 4u) ? LINK_FACE_DOWN :
           (dir & 2u) ? LINK_FACE_LEFT : LINK_FACE_RIGHT;
}

static void publish_sword_state(const PlayerSwordState *sw)
{
    F_ram_00B9_P = sw->state; /* native_ram bulk:dyn_addr */
    /* Slot $0D ObjAnimCounter is P1's cell; extra players must not write it. */
    if (sw == &g_player_sword[0]) F_SW_SLOT_03DD_P = sw->timer; /* native_ram bulk:dyn_addr */
}

static void publish_sword_pose(const PlayerSwordState *sw)
{
    F_ram_007D_P = (unsigned char)sw->x; /* native_ram bulk:dyn_addr */
    F_ram_0091_P = (unsigned char)sw->y; /* native_ram bulk:dyn_addr */
    F_ram_00A5_P = sw->dir; /* native_ram bulk:dyn_addr */
}

static void publish_sword(const PlayerSwordState *sw)
{
    publish_sword_state(sw);
    publish_sword_pose(sw);
}

/* PlayerToWeaponOffsetsX/Y: 4 states x reverse direction (up, down,
 * left, right). */
static const unsigned char k_pw_off_x[16] = {
    0xFFu, 0x01u, 0x00u, 0xF8u, 0xFFu, 0x01u, 0xF5u, 0x0Bu,
    0xFFu, 0x01u, 0xF9u, 0x07u, 0xFFu, 0x01u, 0xFDu, 0x03u
};
static const unsigned char k_pw_off_y[16] = {
    0xF7u, 0xF2u, 0xF5u, 0xF5u, 0xF6u, 0x0Du, 0x03u, 0x03u,
    0xF7u, 0x09u, 0x03u, 0x03u, 0xFFu, 0x05u, 0x03u, 0x03u
};
/* RDirectionToWeaponBaseAttribute / RDirectionToWeaponFrame. */
static const unsigned char k_rdir_base_attr[4] = { 0x00u, 0x80u, 0x00u, 0x00u };
static const unsigned char k_rdir_frame[4]     = { 0u, 0u, 1u, 1u };

/* GetOppositeDir's reverse direction index: up 0, down 1, left 2, right 3. */
static unsigned char rdir_index(unsigned char dir)
{
    if (dir & 0x08u) return 0u;
    if (dir & 0x04u) return 1u;
    if (dir & 0x02u) return 2u;
    return 3u;
}

/* PlaceWeaponForPlayerState[AndAnim]: Link enters the wielding state. */
void link_place_weapon_for_player_state(unsigned char and_anim)
{
    if (and_anim) L_ANIMCNT = 1u;
    L_STATE = 0x10u;
}

/* AnimateLinkObjState. */
static void animate_link_obj_state(void)
{
    unsigned char st = L_STATE;
    unsigned char major = (unsigned char)(st & 0x30u);
    if (major == 0x10u || major == 0x20u) {
        L_STATE = (st & 0x0Fu) ? (unsigned char)(st | 0x30u) : (unsigned char)(st + 1u);
        L_ANIMFRAME = 1u;
    } else if (major == 0x30u) {
        L_STATE = (unsigned char)(st & 0xC0u);
    }
}

/* AnimateLinkBase -> AnimateObjectWalking: Link's counter rolls down
 * while he is not idle, in modes 4 / $10, or while a direction is held
 * (ObjInputDir $3F8); at 0 the object state advances (AnimateLinkObjState)
 * and the counter rolls over to 6 with the movement frame toggled. */
/* AnimateLinkBase (Z_07.asm:5023): not idle, or mode 4/$10, or a
 * direction held -> AnimateObjectWalking. */
void roomrom_combat_animate_link_base(void)
{
    unsigned char gm = F_ram_0012_R;
    if (L_STATE == 0u && gm != 0x04u && gm != 0x10u &&
        (F_ram_03F8_R & 0x0Fu) == 0u) return;
    L_ANIMCNT = (unsigned char)(L_ANIMCNT - 1u);
    if (L_ANIMCNT != 0u) return;
    animate_link_obj_state();
    L_ANIMCNT = 6u;
    L_ANIMFRAME = (unsigned char)(L_ANIMFRAME ^ 1u);
}

/* NES source: Z_07.asm AnimateLinkBase/AnimateLinkObjState.
 * Drained C: link_anim_state_step and animate_link_obj_state above.
 * Coverage: FULL animation arithmetic for explicit and legacy actors.
 * Stance: ADOPT identical byte wrap, halt and mode admission. */
static void animate_link_state(volatile unsigned char *state,
    volatile unsigned char *counter, volatile unsigned char *frame,
    unsigned char mode, unsigned char input_dir)
{
    const unsigned char st = *state;
    if ((st & 0xC0u) == 0x40u) return;
    if (st == 0u && mode != 0x04u && mode != 0x10u &&
        (input_dir & 0x0Fu) == 0u) return;
    *counter = (unsigned char)(*counter - 1u);
    if (*counter != 0u) return;
    {
        const unsigned char major = (unsigned char)(st & 0x30u);
        if (major == 0x10u || major == 0x20u) {
            *state = (st & 0x0Fu) ? (unsigned char)(st | 0x30u) :
                                   (unsigned char)(st + 1u);
            *frame = 1u;
        } else if (major == 0x30u) {
            *state = (unsigned char)(st & 0xC0u);
        }
    }
    *counter = 6u;
    *frame = (unsigned char)(*frame ^ 1u);
}

void link_anim_state_step(void)
{
    animate_link_state(&L_STATE, &L_ANIMCNT, &L_ANIMFRAME,
                      F_ram_0012_R, F_ram_03F8_R);
}

/* Walker_Move: Link does not move while using or catching an item. */
unsigned char link_item_use_blocks_move(void)
{
    unsigned char major = (unsigned char)(L_STATE & 0xF0u);
    return (major == 0x10u || major == 0x20u) ? 1u : 0u;
}

static void update_sword_or_rod(unsigned char x);
static void update_rod(unsigned char player);

void roomrom_combat_try_player_swing(unsigned char player)
{
    PlayerSwordState *sw;
    if (player >= 4u) return;
    sw = &g_player_sword[player];
    if (F_ram_0657 == 0u || L_STATE != 0u ||
        sword_blocked_by_bubble() != 0u || sw->state != 0u) return;
    sword_runtime_start(sw, player, F_ram_0070_R, F_ram_0084_R, L_DIR);
    SW_QSPEED = 0xC0u;
    link_place_weapon_for_player_state(1u);
    publish_sword(sw);
    F_ram_0603 |= 0x01u;
}
void roomrom_combat_try_swing(link_face_t face, short link_x, short link_y)
{
    (void)face; (void)link_x; (void)link_y;
    roomrom_combat_try_player_swing(0u);
}

/* WieldRod (Z_05.asm:3028): rod slot $12, state $31 for 5 frames, then
 * UpdateRodOrArrow runs the sword's state machine on it. */
void roomrom_combat_wield_rod(void)
{
    unsigned char d;
    if (F_ROD_SLOT_00BE_R != 0u) return;
    F_ROD_SLOT_03E2_R = 5u;
    /* WieldWeapon($31). */
    F_ROD_SLOT_00BE_R = 0x31u;
    F_ROD_SLOT_03CE_R = 0xC0u;
    link_place_weapon_for_player_state(1u);
    d = L_DIR;
    F_ROD_SLOT_00AA_R = d;
    F_ROD_SLOT_0082_R = (unsigned char)(F_ram_0070_R + ((d & 0x01u) ? 0x10u : (d & 0x02u) ? 0xF0u : 0u));
    F_ROD_SLOT_0096_R = (unsigned char)(F_ram_0084_R + ((d & 0x04u) ? 0x10u : (d & 0x08u) ? 0xF0u : 0u));
    if (d & 0x0Cu) F_ROD_SLOT_0082_R = (unsigned char)(F_ROD_SLOT_0082_R + 3u);
}

/* UpdateRodOrArrow for rod states $3x. */
void roomrom_combat_update_rod(void)
{
    update_rod(0u);
}
void roomrom_combat_update_rod_for_player(unsigned char player)
{
    if (player < 4u) update_rod(player);
}

/* Link_EndMoveAndAnimate's AnimateLinkBase, at the end of UpdatePlayer.
 * Runs even on a frame whose movement started a room scroll. */
void roomrom_combat_end_move_and_animate(void)
{
    /* Link_EndMoveAndAnimate returns while WhirlwindTeleportingState
     * ($522) != 0: no warps, no animation (Z_07.asm; T-171
     * t171_flute_whirlwind t346, ObjAnimCounter). */
    if (F_ram_0522 != 0u) return;
    /* Both profiles use native Link attack states and animation cadence. */
    link_anim_state_step();
}

unsigned char roomrom_combat_link_locked(void)
{
    return link_item_use_blocks_move();
}

/* Phase 9 Task 9.4 OPTION_ID_SWORD_STYLE gate.
 *
 * VANILLA      : NES Z1 behavior — beam spawns only when hearts == max.
 * STAB_ONLY    : never spawn beam (melee only).
 * BEAM_ALWAYS  : always spawn beam regardless of HP (Redux easy-mode).
 *
 * The pre-9.4 implementation always spawned the beam (the
 * "engine approximates Z1's full HP check" comment below); this
 * function replaces that approximation with the option-driven gate. */
static unsigned char sword_style_allows_beam(void)
{
    unsigned char style = options_consumer_get_sword_style();
    if (style == OPTIONS_SWORD_STAB_ONLY)   return 0u;
    if (style == OPTIONS_SWORD_BEAM_ALWAYS) return 1u;
    /* VANILLA: NES MakeSwordShot: $0529 set, or full hearts (HeartValues
     * high nibble equals low nibble, HeartPartial at least half-full). */
    if (F_ram_0529_R != 0u) return 1u;
    {
        unsigned char hv = g_inventory.heart_values;
        unsigned char cur = heart_values_cur(hv);
        unsigned char max = heart_values_max(hv);
        /* Redux misc.asm (bank 7 $F879): CMP #$C0 instead of CMP #$80. */
        return (cur == max && g_inventory.heart_partial >= (s_redux ? 0xC0u : 0x80u)) ? 1u : 0u;
    }
}

void roomrom_combat_update(short link_x, short link_y, link_face_t face)
{
    /* Vanilla: NES UpdateSwordOrRod (T-116). Runs after Link's input,
     * movement and AnimateLinkBase (roomrom_combat_end_move_and_animate,
     * T-102), then the weapons update in slot order. */
    {
        unsigned char major = (unsigned char)(L_STATE & 0x30u);
        /* DrawLink faces ObjDir for every item use (sword, rod, ...). */
        if (s_draw_enabled && (major == 0x10u || major == 0x20u))
            /* Vertical frames flip by ObjAnimFrame ([0F], SetUpWalkingSprites):
             * 1 until the first AnimateLinkObjState roll (candle use, NES
             * OAM $1A/$18 attr $40). */
            roomrom_sprites_set_link_attack_pose(link_x, link_y, face,
                (face == LINK_FACE_UP || face == LINK_FACE_DOWN) ?
                    (unsigned char)(L_ANIMFRAME & 1u) : 0u);
    }
    update_sword_or_rod(SW_SLOT);
    /* T378: slot18's arrow/rod dispatcher owns the rod update. Calling
     * it here too halves the countdown and spawns magic before slot14
     * movement, six ticks ahead of the captured NES lifecycle. */
    sword_shot_update();          /* NES order: $0D then $0E */
}

/* The caller supplies the currently loaded legacy Link context. Only this
 * adapter publishes slot $0D for unmigrated collision; the pure lifecycle/pose
 * kernel owns each player's native sword and never borrows player zero. */
static void clear_player_sword(unsigned char player)
{
    if (player == 0u) roomrom_sprites_clear_sword();
    else roomrom_sprites_set_mp_sword(player, -32, -32, 0u, 0u);
}
static void draw_player_sword_nes(unsigned char player, short x, short y,
    unsigned char wide, unsigned short attr)
{
    if (player == 0u) roomrom_sprites_set_sword_nes(x, y, wide, attr);
    else roomrom_sprites_set_mp_sword(player, x, y, wide, attr);
}
static void update_player_sword(unsigned char player,
    unsigned char actor_x, unsigned char actor_y, unsigned char actor_dir,
    unsigned char actor_grid, volatile unsigned char *animation_counter)
{
    PlayerSwordState *sw = &g_player_sword[player];
    unsigned char st = sw->state;
    unsigned char idx, dir, attr, tile, wide;
    unsigned char state_changed;
    short dx;
    if (player == 0u) enemy_render_weapon_reset(SW_SLOT);
    if (st == 0u) return;
    state_changed = sword_runtime_tick(sw);
    /* UpdateSwordOrRod DEC ObjAnimCounter,X every active tick (Z_07.asm:4360):
     * publish the post-tick native timer to the image cell, P1 only. */
    if (player == 0u) F_SW_SLOT_03DD_P = sw->timer; /* native_ram bulk:dyn_addr */
    if (state_changed != 0u) {
        *animation_counter = state_changed;
        if (sw->state >= 6u) {
            publish_sword_state(sw);
            sw->state = 0u;
            publish_sword_state(sw);
            clear_player_sword(player);
            publish_sword_pose(sw);
            return;
        }
    }
    if (state_changed != 0u) publish_sword_state(sw);
    st = sw->state;
    if (st == 5u) {
        clear_player_sword(player);
        publish_sword_pose(sw);
        return;
    }
    sword_runtime_pose(sw, actor_x, actor_y, actor_dir, s_redux);
    if (s_redux && st == 2u) {
        unsigned char fr = (unsigned char)(8u - sw->timer);
        unsigned char fi = (unsigned char)nes_dir_to_face(actor_dir);
        unsigned char kind = redux_sprite[fi][fr];
        unsigned char pal = sword_subpal_for_items(F_ram_0657_R2);
        short sx, sy;
        sx = (short)(sw->x + ((kind == 1u) ? 0 : 4));
        sy = (short)(sw->y + s_uw_y_bias);
        if (kind == 0u)
            draw_player_sword_nes(player, sx, sy, 0u,
                enemy_render_item_sat(0x20u, (unsigned char)(pal |
                    (redux_hflip[fi][fr] << 6) | (redux_vflip[fi][fr] << 7))));
        else if (kind == 1u)
            roomrom_sprites_set_sword_horizontal(sx, sy, redux_hflip[fi][fr], pal);
        else
            roomrom_sprites_set_sword_diagonal(sx, sy, redux_hflip[fi][fr],
                                               redux_vflip[fi][fr], pal);
        publish_sword_pose(sw);
        return;
    }
    dir = (st == 1u) ? 0x08u : sw->dir;
    idx = rdir_index(dir);
    attr = (unsigned char)(k_rdir_base_attr[idx] + F_ram_0657 - 1u);
    if (s_redux && idx == 1u) attr = (unsigned char)(attr | 0x40u);
    if (idx == 2u) attr = (unsigned char)(attr | 0x40u);
    if (st == 1u) {
        clear_player_sword(player);
        publish_sword_pose(sw);
        return;
    }
    tile = draw_item_frame_tile(0u, k_rdir_frame[idx]);
    wide = k_rdir_frame[idx];
    dx = wide ? 0 : 4;
    if (wide && player != 0u) {
        roomrom_sprites_set_sword_horizontal(sw->x, sw->y,
            (unsigned char)((attr >> 6) & 1u), (unsigned char)(attr & 3u));
    } else if (wide) {
        unsigned char l = tile, r = (unsigned char)(tile + 2u);
        short sx = sw->x, sy = sw->y;
        if (attr & 0x40u) { l = r; r = tile; }
        draw_player_sword_nes(player, sx, sy, 0u, enemy_render_item_sat(l, attr));
        enemy_render_weapon_add_item(SW_SLOT, r, attr, (unsigned char)(sx + 8), (unsigned char)sy);
    } else {
        draw_player_sword_nes(player, (short)(sw->x + dx), sw->y, wide,
                                      enemy_render_item_sat(tile, attr));
    }
    if (st == 3u) sword_shot_make_for_player(player, sword_style_allows_beam(),
        actor_x, actor_y, actor_dir, actor_grid);
    publish_sword_pose(sw);
}

/* Keep the authenticated T-366 P1 combat phase hook and slot argument. */
__attribute__((noinline, used))
static void update_sword_or_rod(unsigned char slot)
{
    (void)slot;
    update_player_sword(0u, F_ram_0070_R, F_ram_0084_R, L_DIR,
                        F_ram_0394_R, &L_ANIMCNT);
}
/* NES source: Z_04.asm:Gleeok_CheckCollisions, selected Link/weapon view.
 * Drained C: publish_sword above; native sword record remains the owner.
 * Coverage: PARTIAL collision-only party binding at boss segment boundary.
 * Stance: EXTEND publication without movement, animation or lifecycle ticks. */
void roomrom_combat_publish_player_sword(unsigned char player)
{
    if (player < 4u) publish_sword(&g_player_sword[player]);
}

void roomrom_combat_update_player_sword(unsigned char player)
{
    if (player == 0u || player >= 4u) return;
    roomrom_sprites_set_sword_slot((unsigned short)(ROOMROM_SPRITE_SLOT_MP_SWORD2 + player - 1u));
    /* The removed sword bank cannot import stale state. This is the only
     * extra-player publication into the remaining collision adapter. */
    publish_sword(&g_player_sword[player]);
    update_player_sword(player, F_ram_0070_R, F_ram_0084_R, L_DIR,
                        F_ram_0394_R, &L_ANIMCNT);
    roomrom_sprites_set_sword_slot(ROOMROM_SPRITE_SLOT_SWORD);
}

/* NES source: Z_07.asm AnimateLinkBase -> UpdateSwordOrRod.
 * Drained C: the unchanged legacy wrappers above and shared native kernels.
 * Coverage: stationary extra actors; movement/admission and beam tick stay caller-owned.
 * Stance: EXTEND explicit actor input without borrowing slot-zero Link state. */
void roomrom_combat_continue_stationary_player(unsigned char player,
    PlayerState *actor, unsigned char nes_dir)
{
    if (player == 0u || player >= 4u || actor == 0) return;
    if (F_ram_0522 == 0u)
        animate_link_state(&actor->state, &actor->anim_counter, &actor->anim_frame,
                          F_ram_0012_R, 0u);
    roomrom_sprites_set_sword_slot((unsigned short)(ROOMROM_SPRITE_SLOT_MP_SWORD2 + player - 1u));
    publish_sword(&g_player_sword[player]);
    update_player_sword(player, (unsigned char)actor->x, (unsigned char)actor->y,
                        nes_dir, (unsigned char)actor->grid_offset, &actor->anim_counter);
    roomrom_sprites_set_sword_slot(ROOMROM_SPRITE_SLOT_SWORD);
}

/* UpdateSwordOrRod (Z_07.asm:4351) rod half. The P1 sword is native in
 * update_sword_native; this path only serves slot $12. The rod draws item
 * slot 8 with base attribute | 1 (palette row 5) and makes a magic shot at
 * state 3. */
static void update_rod(unsigned char player)
{
    const unsigned char x = ROD_SLOT;
    unsigned char st = (unsigned char)(F_ram_00AC_R5(x) & 0x0Fu);
    unsigned char idx, dir, attr, tile, wide;
    short dx;

    if (st == 0u) return;
    F_ram_03D0_R2(x) = (unsigned char)(F_ram_03D0_R2(x) - 1u);
    if (F_ram_03D0_R2(x) == 0u) {
        /* State 2 lasts 8 frames, the later ones 1; Link's counter too. */
        unsigned char c = (st == 1u) ? 8u : 1u;
        L_ANIMCNT = c;
        F_ram_03D0_R2(x) = c;
        F_ram_00AC_R5(x) = (unsigned char)(F_ram_00AC_R5(x) + 1u);
        if ((F_ram_00AC_R5(x) & 0x0Fu) >= 6u) {
            F_ram_00AC_R5(x) = 0u;       /* ResetObjState */
            roomrom_sprites_clear_arrow();
            return;
        }
    }
    st = (unsigned char)(F_ram_00AC_R5(x) & 0x0Fu);
    if (st == 5u) {                      /* not drawn */
        roomrom_sprites_clear_arrow();
        return;
    }
    F_ram_0098_R3(x) = L_DIR;
    idx = (unsigned char)((st - 1u) * 4u + rdir_index(L_DIR));
    F_ram_0070_R4(x) = (unsigned char)(F_ram_0070_R + k_pw_off_x[idx]);
    F_ram_0084_R4(x) = (unsigned char)(F_ram_0084_R + k_pw_off_y[idx]);
    if (s_redux && st == 1u) {
        /* magic_beam_align reuses these dummy-state offset cells as
         * rod palettes. Retain the source's unrendered ObjX/Y. */
        unsigned char di = rdir_index(L_DIR);
        unsigned char dummy = (di == 1u) ? 0xC0u : 0u;
        F_ram_0070_R4(x) = (unsigned char)(F_ram_0070_R + dummy);
        F_ram_0084_R4(x) = (unsigned char)(F_ram_0084_R + dummy + 1u);
    }
    dir = (st == 1u) ? 0x08u : F_ram_0098_R3(x);
    idx = rdir_index(dir);
    /* @CalcSwordAttrs: the rod uses base attribute | 1. Left flips. */
    attr = (unsigned char)(k_rdir_base_attr[idx] | 0x01u);
    if (s_redux && idx == 1u) attr = (unsigned char)(attr | 0x40u);
    if (idx == 2u) attr = (unsigned char)(attr | 0x40u);
    if (st == 1u) {                      /* windup: not shown */
        roomrom_sprites_clear_arrow();
        return;
    }
    /* Anim_WriteItemSprites, item slot 8: frame 0 vertical (narrow, X+4)
     * or 1 horizontal (wide pair). */
    tile = draw_item_frame_tile(8u, k_rdir_frame[idx]);
    wide = k_rdir_frame[idx];
    dx = wide ? 0 : 4;
    roomrom_sprites_set_arrow_nes((short)(F_ram_0070_R4(x) + dx),
                                  (short)F_ram_0084_R4(x), wide,
                                  enemy_render_item_sat(tile, attr));
    if (st == 3u) magic_shot_make_for_player(player, F_ram_0070_R, F_ram_0084_R,
        L_DIR, F_ram_0394_R);     /* @MakeMagicShot */
}
