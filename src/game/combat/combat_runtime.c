/* Phase 12.2 SGDK-1 cleanup: dropped <genesis.h>; route PAL_setColors
 * through render_cram_subrange_upload(). */
#include "combat_runtime.h"
#include "../world/render/sprite_render.h"
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

/* Latched per-frame sword pose for nes_ram_sync_sword (Plan v5). */
static unsigned char  s_pub_state = 0u;
static short          s_pub_x     = 0;
static short          s_pub_y     = 0;
static link_face_t    s_pub_face  = LINK_FACE_DOWN;

unsigned char roomrom_combat_get_swing_state(void) { return s_pub_state; }
short         roomrom_combat_get_swing_x(void)     { return s_pub_x; }
short         roomrom_combat_get_swing_y(void)     { return s_pub_y; }
link_face_t   roomrom_combat_get_swing_face(void)  { return s_pub_face; }

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
static const signed char redux_x[4][8] = {
    /* DOWN  */ { -9, -9, -5, -3, -2, -1,  0,  1 },
    /* UP    */ { 10, 10,  7,  6,  4,  2,  0, -1 },
    /* LEFT  */ {  1, -1, -5, -6, -7, -8,-10,-11 },
    /* RIGHT */ {  1,  3,  5,  6,  7,  8, 10, 11 },
};

static const signed char redux_y[4][8] = {
    /* DOWN  */ {  3,  5,  9, 10, 11, 12, 13, 13 },
    /* UP    */ {  2,  0, -5, -6, -7, -8, -9,-10 },
    /* LEFT  */ {-10,-10, -9, -8, -6, -4,  2,  3 },
    /* RIGHT */ {-10,-10, -8, -7, -5, -3,  2,  3 },
};

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
static const unsigned char redux_face[4][8] = {
    {2,2,4,4,4,4,4,4}, {1,1,8,8,8,8,8,8},
    {8,8,2,2,2,2,2,2}, {8,8,1,1,1,1,1,1}
};

void roomrom_combat_init(void)
{
    RAM(0x00ACu + 0x0Du) = 0u;   /* T-116: sword slot $0D */
    s_pub_state = 0u;
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
    return (unsigned char)(RAM(0x004Cu) | RAM(0x052Eu));
}

/* ---- T-116: NES Link item-use state and sword (object slot $0D) ----
 * NES source: Z_05.asm WieldSword / WieldWeapon, Z_01.asm
 * PlaceWeaponForPlayerState[AndAnim] / PlaceWeapon, Z_07.asm
 * UpdateSwordOrRod, AnimateLinkBase / AnimateLinkObjState, Walker_Move
 * (movement gate). State lives in the NES cells so collision and lockstep
 * see it: Link ObjState $AC / ObjAnimCounter $3D0 / ObjAnimFrame $3E4,
 * sword ObjState $B9, counter $3DD, X $7D, Y $91, dir $A5, q-speed $3C9.
 * Redux extends this same native state machine. */
#define L_STATE     RAM(0x00ACu)
#define L_ANIMCNT   RAM(0x03D0u)
#define L_ANIMFRAME RAM(0x03E4u)
#define L_DIR       RAM(0x0098u)
#define SW_SLOT     0x0Du
#define SW_STATE    RAM(0x00ACu + SW_SLOT)
#define SW_CNT      RAM(0x03D0u + SW_SLOT)
#define SW_X        RAM(0x0070u + SW_SLOT)
#define SW_Y        RAM(0x0084u + SW_SLOT)
#define SW_DIR      RAM(0x0098u + SW_SLOT)
#define SW_QSPEED   RAM(0x03BCu + SW_SLOT)

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
    unsigned char gm = RAM(0x0012u);
    if (L_STATE == 0u && gm != 0x04u && gm != 0x10u &&
        (RAM(0x03F8u) & 0x0Fu) == 0u) return;
    L_ANIMCNT = (unsigned char)(L_ANIMCNT - 1u);
    if (L_ANIMCNT != 0u) return;
    animate_link_obj_state();
    L_ANIMCNT = 6u;
    L_ANIMFRAME = (unsigned char)(L_ANIMFRAME ^ 1u);
}

void link_anim_state_step(void)
{
    unsigned char gm = RAM(0x0012u);
    /* UpdatePlayer returns before Link_EndMoveAndAnimate while Link is
     * halted (ObjState & $C0 = $40, e.g. the pond fairy). */
    if ((L_STATE & 0xC0u) == 0x40u) return;
    if (L_STATE == 0u && gm != 0x04u && gm != 0x10u &&
        (RAM(0x03F8u) & 0x0Fu) == 0u) return;
    L_ANIMCNT = (unsigned char)(L_ANIMCNT - 1u);
    if (L_ANIMCNT != 0u) return;
    animate_link_obj_state();
    L_ANIMCNT = 6u;
    L_ANIMFRAME = (unsigned char)(L_ANIMFRAME ^ 1u);
}

/* Walker_Move: Link does not move while using or catching an item. */
unsigned char link_item_use_blocks_move(void)
{
    unsigned char major = (unsigned char)(L_STATE & 0xF0u);
    return (major == 0x10u || major == 0x20u) ? 1u : 0u;
}

static void update_sword_or_rod(unsigned char x);

void roomrom_combat_try_swing(link_face_t face, short link_x, short link_y)
{
    (void)face; (void)link_x; (void)link_y;
    if (nes_ram[0x0657u] == 0u) return;   /* T-092: NES WieldSword, no sword */
    /* Link_HandleInput: A only in the idle state, sword not blocked. */
    if (L_STATE != 0u) return;
    if (sword_blocked_by_bubble() != 0u) return;
    /* WieldSword. */
    if (SW_STATE != 0u) return;
    SW_CNT = 5u;
    /* WieldWeapon(state 1): q-speed $C0, PlaceWeaponForPlayerStateAndAnim,
     * vertical directions move the weapon right 3 pixels. */
    SW_STATE = 1u;
    SW_QSPEED = 0xC0u;
    link_place_weapon_for_player_state(1u);
    {
        unsigned char d = L_DIR;
        SW_DIR = d;
        SW_X = (unsigned char)(RAM(0x0070u) + ((d & 0x01u) ? 0x10u : (d & 0x02u) ? 0xF0u : 0u));
        SW_Y = (unsigned char)(RAM(0x0084u) + ((d & 0x04u) ? 0x10u : (d & 0x08u) ? 0xF0u : 0u));
        if (d & 0x0Cu) SW_X = (unsigned char)(SW_X + 3u);
    }
    nes_ram[0x0603u] |= 0x01u;       /* WieldSword: PlayEffect $01 */
}

/* WieldRod (Z_05.asm:3028): rod slot $12, state $31 for 5 frames, then
 * UpdateRodOrArrow runs the sword's state machine on it. */
#define ROD_SLOT 0x12u
void roomrom_combat_wield_rod(void)
{
    unsigned char d;
    if (RAM(0x00ACu + ROD_SLOT) != 0u) return;
    RAM(0x03D0u + ROD_SLOT) = 5u;
    /* WieldWeapon($31). */
    RAM(0x00ACu + ROD_SLOT) = 0x31u;
    RAM(0x03BCu + ROD_SLOT) = 0xC0u;
    link_place_weapon_for_player_state(1u);
    d = L_DIR;
    RAM(0x0098u + ROD_SLOT) = d;
    RAM(0x0070u + ROD_SLOT) = (unsigned char)(RAM(0x0070u) + ((d & 0x01u) ? 0x10u : (d & 0x02u) ? 0xF0u : 0u));
    RAM(0x0084u + ROD_SLOT) = (unsigned char)(RAM(0x0084u) + ((d & 0x04u) ? 0x10u : (d & 0x08u) ? 0xF0u : 0u));
    if (d & 0x0Cu) RAM(0x0070u + ROD_SLOT) = (unsigned char)(RAM(0x0070u + ROD_SLOT) + 3u);
}

/* UpdateRodOrArrow for rod states $3x. */
void roomrom_combat_update_rod(void)
{
    update_sword_or_rod(ROD_SLOT);
}

/* Link_EndMoveAndAnimate's AnimateLinkBase, at the end of UpdatePlayer.
 * Runs even on a frame whose movement started a room scroll. */
void roomrom_combat_end_move_and_animate(void)
{
    /* Link_EndMoveAndAnimate returns while WhirlwindTeleportingState
     * ($522) != 0: no warps, no animation (Z_07.asm; T-171
     * t171_flute_whirlwind t346, ObjAnimCounter). */
    if (nes_ram[0x0522u] != 0u) return;
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
    if (RAM(0x0529u) != 0u) return 1u;
    {
        unsigned char hv = g_inventory.heart_values;
        unsigned char cur = heart_values_cur(hv);
        unsigned char max = heart_values_max(hv);
        return (cur == max && g_inventory.heart_partial >= 0x80u) ? 1u : 0u;
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
        if (major == 0x10u || major == 0x20u)
            roomrom_sprites_set_link_attack_pose(link_x, link_y, face);
    }
    update_sword_or_rod(SW_SLOT);
    sword_shot_update();          /* NES order: $0D then $0E */
    s_pub_state = (unsigned char)(SW_STATE & 0x0Fu);
    s_pub_x     = (short)SW_X;
    s_pub_y     = (short)SW_Y;
    s_pub_face  = (SW_DIR & 8u) ? LINK_FACE_UP :
                  (SW_DIR & 4u) ? LINK_FACE_DOWN :
                  (SW_DIR & 2u) ? LINK_FACE_LEFT : LINK_FACE_RIGHT;
}

/* UpdateSwordOrRod (Z_07.asm:4351) for the sword ($0D) or the rod ($12).
 * The rod draws item slot 8 with base attribute | 1 (palette row 5) and
 * makes a magic shot at state 3; the sword makes a sword shot. */
static void update_sword_or_rod(unsigned char x)
{
    unsigned char st = (unsigned char)(RAM(0x00ACu + x) & 0x0Fu);
    unsigned char idx, dir, attr, tile, wide;
    short dx;
    if (st == 0u) return;
    RAM(0x03D0u + x) = (unsigned char)(RAM(0x03D0u + x) - 1u);
    if (RAM(0x03D0u + x) == 0u) {
        /* State 2 lasts 8 frames, the later ones 1; Link's counter too. */
        unsigned char c = (st == 1u) ? 8u : 1u;
        L_ANIMCNT = c;
        RAM(0x03D0u + x) = c;
        RAM(0x00ACu + x) = (unsigned char)(RAM(0x00ACu + x) + 1u);
        if ((RAM(0x00ACu + x) & 0x0Fu) >= 6u) {
            RAM(0x00ACu + x) = 0u;       /* ResetObjState */
            if (x == SW_SLOT) roomrom_sprites_clear_sword();
            else              roomrom_sprites_clear_arrow();
            return;
        }
    }
    st = (unsigned char)(RAM(0x00ACu + x) & 0x0Fu);
    if (st == 5u) {                      /* not drawn */
        if (x == SW_SLOT) roomrom_sprites_clear_sword();
        else              roomrom_sprites_clear_arrow();
        return;
    }
    /* Redux modifies only sword state 2; windup/retraction continue
     * through the native owner and keep their native timer/body behavior. */
    if (s_redux && x == SW_SLOT && st == 2u) {
        unsigned char fr = (unsigned char)(8u - SW_CNT);
        unsigned char fi = (L_DIR & 8u) ? LINK_FACE_UP :
                           (L_DIR & 4u) ? LINK_FACE_DOWN :
                           (L_DIR & 2u) ? LINK_FACE_LEFT : LINK_FACE_RIGHT;
        unsigned char kind = redux_sprite[fi][fr];
        unsigned char pal = sword_subpal_for_items(RAM(0x0657u));
        short sx, sy;
        SW_X = (unsigned char)(RAM(0x0070u) + redux_x[fi][fr]);
        SW_Y = (unsigned char)(RAM(0x0084u) + redux_y[fi][fr]);
        SW_DIR = redux_face[fi][fr];
        sx = (short)(SW_X + ((kind == 1u) ? 0 : 4));
        sy = (short)((short)SW_Y + s_uw_y_bias);
        if (kind == 0u)
            roomrom_sprites_set_sword_nes(sx, sy, 0u,
                enemy_render_item_sat(0x20u, (unsigned char)(pal |
                    (redux_hflip[fi][fr] << 6) | (redux_vflip[fi][fr] << 7))));
        else if (kind == 1u)
            roomrom_sprites_set_sword_horizontal(sx, sy, redux_hflip[fi][fr], pal);
        else
            roomrom_sprites_set_sword_diagonal(sx, sy, redux_hflip[fi][fr],
                                               redux_vflip[fi][fr], pal);
        return;
    }
    RAM(0x0098u + x) = L_DIR;
    idx = (unsigned char)((st - 1u) * 4u + rdir_index(L_DIR));
    RAM(0x0070u + x) = (unsigned char)(RAM(0x0070u) + k_pw_off_x[idx]);
    RAM(0x0084u + x) = (unsigned char)(RAM(0x0084u) + k_pw_off_y[idx]);
    if (s_redux && st == 1u) {
        /* magic_beam_align reuses these dummy-state offset cells as
         * sword/rod palettes. Retain the source's unrendered ObjX/Y. */
        unsigned char di = rdir_index(L_DIR);
        unsigned char dummy = (di == 1u) ? 0xC0u : 0u;
        RAM(0x0070u + x) = (unsigned char)(RAM(0x0070u) + dummy);
        RAM(0x0084u + x) = (unsigned char)(RAM(0x0084u) + dummy + 1u);
    }
    dir = (st == 1u) ? 0x08u : RAM(0x0098u + x);
    idx = rdir_index(dir);
    /* @CalcSwordAttrs: base attribute + sword level - 1; the rod uses
     * base attribute | 1. Left flips. */
    if (x == SW_SLOT)
        attr = (unsigned char)(k_rdir_base_attr[idx] + nes_ram[0x0657u] - 1u);
    else
        attr = (unsigned char)(k_rdir_base_attr[idx] | 0x01u);
    if (s_redux && idx == 1u) attr = (unsigned char)(attr | 0x40u);
    if (idx == 2u) attr = (unsigned char)(attr | 0x40u);
    if (st == 1u) {                      /* windup: not shown */
        if (x == SW_SLOT) roomrom_sprites_clear_sword();
        else              roomrom_sprites_clear_arrow();
        return;
    }
    /* Anim_WriteItemSprites, item slot 0 (sword) / 8 (rod): frame 0
     * vertical (narrow, X+4) or 1 horizontal (wide pair). */
    tile = draw_item_frame_tile((x == SW_SLOT) ? 0u : 8u, k_rdir_frame[idx]);
    wide = k_rdir_frame[idx];
    dx = wide ? 0 : 4;
    if (x == SW_SLOT)
        roomrom_sprites_set_sword_nes((short)(RAM(0x0070u + x) + dx),
                                      (short)RAM(0x0084u + x), wide,
                                      enemy_render_item_sat(tile, attr));
    else
        roomrom_sprites_set_arrow_nes((short)(RAM(0x0070u + x) + dx),
                                      (short)RAM(0x0084u + x), wide,
                                      enemy_render_item_sat(tile, attr));
    if (st == 3u) {
        if (x == SW_SLOT) sword_shot_make(sword_style_allows_beam());   /* MakeSwordShot */
        else              magic_shot_make();                            /* @MakeMagicShot */
    }
}
