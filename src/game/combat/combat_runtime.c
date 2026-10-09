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

/* NES Z1 doesn't visibly draw a sword windup (sword raised UP above
 * Link for all facings before extending). Verified 2026-05-01 via
 * BizHawk OAM scan of NES Z1 sword swing: gameplay sword sprite first
 * appears at state 2 extend in facing direction — disassembly's state 1
 * branch may exist but isn't visible on real hardware. State 1 skipped
 * here to match observed NES behavior. */
#define COMBAT_STATE1_FRAMES   0u
#define COMBAT_STATE2_FRAMES   8u
#define COMBAT_STATE3_FRAMES   1u
#define COMBAT_STATE4_FRAMES   1u
#define COMBAT_STATE5_FRAMES   1u
#define COMBAT_TOTAL_FRAMES    (COMBAT_STATE1_FRAMES + COMBAT_STATE2_FRAMES \
                                + COMBAT_STATE3_FRAMES + COMBAT_STATE4_FRAMES \
                                + COMBAT_STATE5_FRAMES)

typedef enum {
    COMBAT_IDLE  = 0,
    COMBAT_ACTIVE
} combat_state_t;

static combat_state_t s_state    = COMBAT_IDLE;
static unsigned char  s_frame    = 0u;   /* 0..COMBAT_TOTAL_FRAMES-1 */
static link_face_t    s_face     = LINK_FACE_DOWN;

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

/* engine currently boots with wood sword (Items=1). NES
 * @CalcSwordAttrs (Z_07.asm:4471) computes sub-pal = base_attr +
 * Items - 1 with base_attr = 0 (RDirectionToWeaponBaseAttribute),
 * so sub-pal == Items - 1. Future white sword (Items=2) -> 1,
 * magic sword (Items=3) -> 2. Defensive: clamp >3 (NES Items
 * tops out at 3 for sword). */
static unsigned char sword_subpal_for_items(unsigned char items_val) {
    if (items_val == 0u) return 0u;          /* defensive: holds sprite at 0 */
    if (items_val > 4u) items_val = 4u;       /* clamp */
    return (unsigned char)(items_val - 1u);
}

/* Track current sword level (1=wood, 2=white, 3=magic). Default 1.
 * Future inventory wiring populates this from $0657 ITEMS register. */
static unsigned char s_sword_level = 1u;

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

/* Redux ALttP-style 8-frame arc swing.
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
#define REDUX_TOTAL_FRAMES   8u
#define REDUX_BEAM_SPAWN     7u

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
    /* LEFT  */ { 0, 0, 0, 0, 0, 1, 1, 1 },
    /* RIGHT */ { 0, 0, 1, 1, 1, 1, 0, 0 },
};

static const unsigned char redux_vflip[4][8] = {
    /* DOWN  */ { 0, 0, 1, 1, 1, 1, 1, 1 },
    /* UP    */ { 0, 0, 0, 0, 0, 0, 0, 0 },
    /* LEFT  */ { 0, 0, 0, 0, 0, 0, 0, 0 },
    /* RIGHT */ { 0, 0, 0, 0, 0, 0, 0, 0 },
};

/* Per-state, per-facing X offset. Index: [state-1][face].
 * face order: 0=DOWN, 1=UP, 2=LEFT, 3=RIGHT.
 * NES tables are in reverse direction order (up, down, left, right);
 * reordered here to match engine's link_face_t enum. */
static const signed char sword_offset_x[4][4] = {
    /*               DOWN UP   LEFT  RIGHT */
    /* state 1 */ {  +1, -1,    0,   -8 },
    /* state 2 */ {  +1, -1,  -11,  +11 },
    /* state 3 */ {  +1, -1,   -7,   +7 },
    /* state 4 */ {  +1, -1,   -3,   +3 },
};

static const signed char sword_offset_y[4][4] = {
    /*               DOWN UP   LEFT  RIGHT */
    /* state 1 */ { -14, -9,  -11,  -11 },
    /* state 2 */ { +13,-10,   +3,   +3 },
    /* state 3 */ {  +9, -9,   +3,   +3 },
    /* state 4 */ {  +5, -1,   +3,   +3 },
};

void roomrom_combat_init(void)
{
    RAM(0x00ACu + 0x0Du) = 0u;   /* T-116: sword slot $0D */
    s_state = COMBAT_IDLE;
    s_frame = 0u;
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
 * Redux keeps its own 8-frame arc path below. */
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
    (void)link_x; (void)link_y;
    if (nes_ram[0x0657u] == 0u) return;   /* T-092: NES WieldSword, no sword */
    if (s_redux) {
        if (s_state != COMBAT_IDLE) return;
        if (sword_blocked_by_bubble() != 0u) return;
        s_state = COMBAT_ACTIVE;
        s_frame = 0u;
        s_face  = face;
        nes_ram[0x0603u] |= 0x01u;   /* WieldSword: PlayEffect $01 */
        return;
    }
    /* Link_HandleInput: A only in the idle state, sword not blocked. */
    if (L_STATE != 0u) return;
    if (sword_blocked_by_bubble() != 0u) return;
    /* WieldSword. */
    if (SW_STATE != 0u) return;
    s_face = face;
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
    /* Redux keeps the NES walk cadence; its swing never sets ObjState. */
    link_anim_state_step();
}

unsigned char roomrom_combat_link_locked(void)
{
    if (s_redux) return s_state != COMBAT_IDLE;
    return link_item_use_blocks_move();
}

/* Returns 1..5 for active states, 0 for idle. */
static unsigned char compute_state(unsigned char frame)
{
    unsigned char acc = 0u;
    acc = (unsigned char)(acc + COMBAT_STATE1_FRAMES);
    if (frame < acc) return 1u;
    acc = (unsigned char)(acc + COMBAT_STATE2_FRAMES);
    if (frame < acc) return 2u;
    acc = (unsigned char)(acc + COMBAT_STATE3_FRAMES);
    if (frame < acc) return 3u;
    acc = (unsigned char)(acc + COMBAT_STATE4_FRAMES);
    if (frame < acc) return 4u;
    return 5u;
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
    unsigned char st;
    short sx, sy;

    if (s_redux && s_state == COMBAT_IDLE) {
        s_pub_state = 0u;
        sword_shot_update();
        return;
    }

    /* Redux 8-frame ALttP-style arc swing — separate state machine. */
    if (s_redux) {
        unsigned char fr = s_frame;
        unsigned char face_idx = (unsigned char)s_face;
        unsigned char sprite_type;
        short narrow_x_shift;
        if (fr >= REDUX_TOTAL_FRAMES) fr = (unsigned char)(REDUX_TOTAL_FRAMES - 1u);

        roomrom_sprites_set_link_attack_pose(link_x, link_y, s_face);

        sprite_type = redux_sprite[face_idx][fr];
        /* NES @Narrow path adds +4 to X to center the half-width sprite
         * within its 16x16 bounding box (Z_01.asm:5289). Applies to
         * vertical (sprite type 0) and diagonal (type 2). Wide
         * horizontal (type 1) is not centered — its 2 sprites already
         * span the full 16-pixel width. */
        narrow_x_shift = (sprite_type == 1u) ? (short)0 : (short)4;

        sx = (short)(link_x + redux_x[face_idx][fr] + narrow_x_shift);
        sy = (short)(link_y + redux_y[face_idx][fr] + s_uw_y_bias);

        switch (sprite_type) {
        case 0:
            roomrom_sprites_set_sword_vertical(sx, sy, redux_vflip[face_idx][fr],
                                               sword_subpal_for_items(s_sword_level));
            break;
        case 1:
            roomrom_sprites_set_sword_horizontal(sx, sy, redux_hflip[face_idx][fr],
                                                 sword_subpal_for_items(s_sword_level));
            break;
        case 2:
            roomrom_sprites_set_sword_diagonal(sx, sy,
                                               redux_hflip[face_idx][fr],
                                               redux_vflip[face_idx][fr],
                                               sword_subpal_for_items(s_sword_level));
            break;
        }

        if (s_frame == REDUX_BEAM_SPAWN)
            sword_shot_make(sword_style_allows_beam());
        sword_shot_update();

        s_pub_state = 2u;
        s_pub_x     = sx;
        s_pub_y     = sy;
        s_pub_face  = s_face;

        s_frame++;
        if (s_frame >= REDUX_TOTAL_FRAMES) {
            s_state = COMBAT_IDLE;
            s_frame = 0u;
            s_pub_state = 0u;
            roomrom_sprites_clear_sword();
        }
        return;
    }

    (void)st;
    (void)sx; (void)sy;
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
    s_pub_face  = s_face;
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
    RAM(0x0098u + x) = L_DIR;
    idx = (unsigned char)((st - 1u) * 4u + rdir_index(L_DIR));
    RAM(0x0070u + x) = (unsigned char)(RAM(0x0070u) + k_pw_off_x[idx]);
    RAM(0x0084u + x) = (unsigned char)(RAM(0x0084u) + k_pw_off_y[idx]);
    dir = (st == 1u) ? 0x08u : RAM(0x0098u + x);
    idx = rdir_index(dir);
    /* @CalcSwordAttrs: base attribute + sword level - 1; the rod uses
     * base attribute | 1. Left flips. */
    if (x == SW_SLOT)
        attr = (unsigned char)(k_rdir_base_attr[idx] + nes_ram[0x0657u] - 1u);
    else
        attr = (unsigned char)(k_rdir_base_attr[idx] | 0x01u);
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
                                      (short)((short)RAM(0x0084u + x) + s_uw_y_bias), wide,
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
