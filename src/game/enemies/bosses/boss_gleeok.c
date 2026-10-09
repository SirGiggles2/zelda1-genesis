/* Phase 8 Task 8.5 — Gleeok native bridge.
 *
 * NES source: Z_04.asm:7649 InitGleeok + Z_04.asm:8527 UpdateGleeokHead +
 *             Z_04.asm:8601 UpdateGleeok + Z_04.asm:8695 Gleeok_FetchNeckAddrs +
 *             Z_04.asm:8717 Gleeok_MoveNeck + Z_04.asm:8902 CalcSegmentLimits +
 *             Z_04.asm:8937 Gleeok_StretchNeck + Z_04.asm:9100 Draw{Head,Segment}AndCheckCollisions +
 *             Z_04.asm:9272 Gleeok_MoveHead + Z_04.asm:9369 Gleeok_DrawBody.
 * Drained C:  src/oracle/enemies/enemy_gleeok_runtime.c — segment-mgmt
 *             helpers PRIMARY (set_segment_x/y, contract_segment_*,
 *             ignore_segment, init_gleeok_head, update_gleeok,
 *             store_ref_seg_distance, check_collisions, dec_head_timer).
 * Coverage:   PARTIAL — InitGleeok + UpdateGleeokHead + 8 c_gleeok_*
 *             primitives + 3 z04_* aliases ported per-line in this TU.
 * Stance:     EXTEND — drain consumed verbatim for the segment-mgmt
 *             helpers; this bridge supplies the rest from NES asm.
 *
 * Per-segment Y address layout for misc bytes loaded into $0413..$0418
 * working area (Gleeok_ObjHeadInfo offsets):
 *   $0413 = DIRCOUNTERH  $0414 = DIRCOUNTERV  $0415 = SPEEDX
 *   $0416 = SPEEDY       $0417 = DIRCHANGECNT  $0418 = DELAY
 */

#include "boss_gleeok.h"
#include "platform_abi.h"
#include "scratch_state.h"            /* ZP_TMP0..5 */
#include "enemy_state.h"
#include "sprite_state.h"             /* OAM_BYTE */
#include "object_state.h"             /* OBJ_*  */
#include "core/core_dispatch.h"       /* core_write_blank_priority_sprites,
                                         core_reset_obj_metastate */
#include "world/draw_dispatch.h"      /* k_sprite_offsets */
#include "world/sprite_dispatch.h"    /* sprite_cycle_cur_sprite_index */

/* External drained primitives the bridge composes. */
extern void enrt_gleeok_store_ref_seg_distance(unsigned int signed_ref_dist);
extern void enrt_gleeok_check_collisions(unsigned int slot);
extern void enrt_gleeok_dec_head_timer(void);
extern void enrt_init_blue_keese(unsigned int slot);

/* Flyer primitives reused for UpdateGleeokHead (drained twins linked
 * via enemy_flyer_bridge.c / enemy_flyer_runtime.c). */
extern void enrt_flyer_speed_up(unsigned int slot);
extern void enrt_flyer_gleeok_head_decide_state(unsigned int slot);
extern void c_move_flyer(unsigned int slot);
extern void c_check_monster_collisions(unsigned int slot);
extern void c_shoot_fireball(unsigned int dir, unsigned int slot);
extern void c_reset_shove_info(unsigned int slot);

/* Flyer wander/chase live as static helpers in enemy_flyer_bridge.c.
 * The flyer-state JT for keese exposes them indirectly via c_control_*
 * — for the gleeok-head JT we promote local state-2/3 callers using
 * the same drained back-end (Flyer_Chase / Flyer_Wander -> chase/wander
 * tables already in flyer_bridge). Easiest re-export: alias by reading
 * ENEMY_DIR + the flyer state machine through enrt_flyer_speed_up
 * dispatch on prior slot's entry point. Keep this implementation
 * minimal — the existing keese-head JT in flyer_bridge handles state 2
 * via Flyer_Chase and state 3 via Flyer_Wander already; reuse them
 * by name via the inlined dispatcher below. */

/* LevelMasks[8] (NES Z_07.asm:747-748). The drained gleeok runtime
 * references this table for the per-neck dead-mask bit; no other TU
 * provides a global definition (only file-static copies in
 * progress_dispatch.c + engine item-room meta). Place it here so the
 * gleeok drain links cleanly. Marked extern so other consumers
 * (item/trap/progress runtimes) resolve to the same object. */
const unsigned char LevelMasks[8] = {
    0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x20u, 0x40u, 0x80u
};

/* Directions8 lives in enemy_flyer_bridge.c — declared here for
 * boss_gleeok_init's RNG-keyed dir pick. */
extern const unsigned char Directions8[8];

/* GleeokSegmentYs[6] (Z_04.asm:7646). Initial Y coordinates for the
 * 6 segments per neck during InitGleeok. */
static const unsigned char k_gleeok_segment_ys[6] = {
    0x6Fu, 0x74u, 0x79u, 0x7Eu, 0x83u, 0x88u
};

/* Per-neck base addresses (Z_04.asm:8583-8599). Each table has 4 entries,
 * one per neck (0..3). The address is RAM[(hi << 8) | lo]. */
static const unsigned char k_neck_x_lo[4]    = { 0x38u, 0x52u, 0x6Cu, 0x95u };
static const unsigned char k_neck_x_hi[4]    = { 0x04u, 0x04u, 0x04u, 0x03u };
static const unsigned char k_neck_y_lo[4]    = { 0x45u, 0x5Fu, 0x79u, 0xBDu };
static const unsigned char k_neck_y_hi[4]    = { 0x04u, 0x04u, 0x04u, 0x03u };
static const unsigned char k_neck_misc_lo[4] = { 0x20u, 0x2Du, 0x81u, 0xA9u };
static const unsigned char k_neck_misc_hi[4] = { 0x04u, 0x04u, 0x03u, 0x03u };

/* Body sprite tables (Z_04.asm GleeokBodyTiles0/1/2, contiguous in ROM).
 * The draw indexes GleeokBodyTiles0 with X = base offset {6,0,6,$C} + 0..5,
 * so frames 0/2 read Tiles1 and frame 3 Tiles2. T-171: only Tiles0 was
 * kept (index % 6), drawing frame 3 with Tiles0 (t171_boss_l4 NES $D2..,
 * Genesis $C0..). */
static const unsigned char k_body_tiles0[18] = {
    0xC0u, 0xC4u, 0xC8u, 0xC2u, 0xC6u, 0xCAu,   /* GleeokBodyTiles0 */
    0xCCu, 0xC4u, 0xCEu, 0xC2u, 0xC6u, 0xD0u,   /* GleeokBodyTiles1 */
    0xD2u, 0xD6u, 0xD8u, 0xD4u, 0xC6u, 0xD0u    /* GleeokBodyTiles2 */
};

/* Animation-frame base tile offsets (Z_04.asm:9360). */
static const unsigned char k_body_base_tile_offsets[4] = {
    0x06u, 0x00u, 0x06u, 0x0Cu
};

/* Per-neck head info offsets (Z_04.asm:7704-7733 init writes).
 *   GLEEOK_DIRCOUNTERH = 0
 *   GLEEOK_DIRCOUNTERV = 1
 *   GLEEOK_SPEEDX      = 2
 *   GLEEOK_SPEEDY      = 3
 *   GLEEOK_DIRCHANGE   = 4
 *   GLEEOK_DELAY       = 5 */
#define GLEEOK_OFF_DIRCNT_H  0u
#define GLEEOK_OFF_DIRCNT_V  1u
#define GLEEOK_OFF_SPEEDX    2u
#define GLEEOK_OFF_SPEEDY    3u
#define GLEEOK_OFF_DIRCHG    4u
#define GLEEOK_OFF_DELAY     5u

/* Per-neck head-info NES RAM bases (Z_04.asm:7704). */
static const unsigned short k_head_info_base[4] = {
    0x0420u, 0x042Du, 0x0381u, 0x03A9u
};

/* Working-area aliases used by Gleeok_MoveHead (loaded into $0413..$0418
 * per current neck via the indirect ($04),Y fetch in UpdateGleeok). */
#define GLEEOK_WORK_DIRCNT_H   RAM(0x0413u)
#define GLEEOK_WORK_DIRCNT_V   RAM(0x0414u)
#define GLEEOK_WORK_SPEEDX     RAM(0x0415u)
#define GLEEOK_WORK_SPEEDY     RAM(0x0416u)
#define GLEEOK_WORK_DIRCHG     RAM(0x0417u)
#define GLEEOK_WORK_DELAY      RAM(0x0418u)

/* Gleeok per-axis tier-limit RAM cells (Z_04.asm:8907/8916/8925).
 * X = 0 horizontal, X = 1 vertical -> +0/+1 offsets. */
#define GLEEOK_PRIMARY_LIMIT(axis)   RAM(0x04D9u + (axis))
#define GLEEOK_SECONDARY_LIMIT(axis) RAM(0x04DBu + (axis))
#define GLEEOK_TERTIARY_LIMIT(axis)  RAM(0x04DDu + (axis))

/* Frame counter ($0015) — the asm calls it FrameCounter; the C state
 * map already aliases it as ENEMY_CUR_SPRITE_ATTR_ROW (same address). */
#define GLEEOK_FRAME_COUNTER         RAM(0x0015u)

/* ------------------------------------------------------------------
 * Helpers: signed math + abs.
 * ------------------------------------------------------------------ */
static unsigned char gleeok_abs8(unsigned char a)
{
    return (a & 0x80u) ? (unsigned char)(0u - a) : a;
}

/* ------------------------------------------------------------------
 * z04 forwarders (NES name aliases used by the drained runtime).
 * ------------------------------------------------------------------ */
void z04_gleeok_set_segment_x(unsigned int val, unsigned int slot)
{
    /* Same body as enrt_gleeok_set_segment_x — the drain spells two
     * names because the NES asm has two label entry points. */
    ENEMY_GLEEOK_SEG_X(slot) = (unsigned char)val;
}

void z04_gleeok_set_segment_y(unsigned int val, unsigned int slot)
{
    ENEMY_GLEEOK_SEG_Y(slot) = (unsigned char)val;
}

void z04_init_blue_keese(unsigned int slot)
{
    enrt_init_blue_keese(slot);
}

/* ------------------------------------------------------------------
 * c_gleeok_fetch_neck_addrs (NES Z_04.asm:8695)
 *   Loads ZP_TMP0..5 with the X / Y / Misc per-neck NES RAM pointers
 *   for GleeokCurNeck.
 * ------------------------------------------------------------------ */
void c_gleeok_fetch_neck_addrs(void)
{
    const unsigned int n =
        (unsigned int)(unsigned char)ENEMY_GLEEOK_NECK_INDEX & 0x03u;
    ENEMY_GLEEOK_NECK_X_PTR_LO = k_neck_x_lo[n];
    ENEMY_GLEEOK_NECK_X_PTR_HI = k_neck_x_hi[n];
    ENEMY_GLEEOK_NECK_Y_PTR_LO = k_neck_y_lo[n];
    ENEMY_GLEEOK_NECK_Y_PTR_HI = k_neck_y_hi[n];
    ENEMY_GLEEOK_NECK_M_PTR_LO = k_neck_misc_lo[n];
    ENEMY_GLEEOK_NECK_M_PTR_HI = k_neck_misc_hi[n];
}

/* ------------------------------------------------------------------
 * c_gleeok_calc_segment_limits (NES Z_04.asm:8902)
 *   Stores three cumulative tier limits (cap 4 / 8 / $B) for axis.
 * ------------------------------------------------------------------ */
void c_gleeok_calc_segment_limits(unsigned int primary_dist,
                                  unsigned int axis)
{
    unsigned char a = (unsigned char)primary_dist;
    if (a >= 0x04u) a = 0x04u;
    GLEEOK_PRIMARY_LIMIT(axis) = a;

    a = (unsigned char)(a + 0x04u);
    if (a >= 0x08u) a = 0x08u;
    GLEEOK_SECONDARY_LIMIT(axis) = a;

    a = (unsigned char)(a + 0x04u);
    if (a >= 0x0Bu) a = 0x0Bu;
    GLEEOK_TERTIARY_LIMIT(axis) = a;
}

/* ------------------------------------------------------------------
 * c_gleeok_stretch_neck (NES Z_04.asm:8937)
 *   Picks one of 9 segment-motion routines based on H/V tier crossings.
 *
 * NOTE: NES asm at Z_04.asm:8944 has SBC ObjX+3,X (instead of ObjX+2,X)
 * — flagged UNKNOWN in the disasm. We preserve the buggy behavior:
 * H-distance is always 0, so JT idx never gains the H-tier bumps.
 * ------------------------------------------------------------------ */
static void gleeok_expand_segment(unsigned int slot);
static void gleeok_contract_segment_x_local(unsigned int slot);
static void gleeok_contract_segment_y_local(unsigned int slot);
static void gleeok_contract_segment_local(unsigned int slot);

void c_gleeok_stretch_neck(unsigned int slot)
{
    unsigned char idx = 0u;

    /* H-distance side (NES bug: SBC ObjX+3,X twice -> always 0). */
    {
        const unsigned char h = 0u;
        if (h >= GLEEOK_PRIMARY_LIMIT(0u))   idx++;
        if (h >= GLEEOK_SECONDARY_LIMIT(0u)) {
            /* NES asm has an "Unknown block" $C8 here = INY. */
            idx++;
        }
    }

    /* V-distance side. */
    {
        const unsigned char cur_y  = ENEMY_GLEEOK_SEG_Y_TARGET(slot);
        const unsigned char prev_y = ENEMY_GLEEOK_SEG_Y(slot);
        const unsigned char v = gleeok_abs8(
            (unsigned char)(cur_y - prev_y));
        if (v >= GLEEOK_PRIMARY_LIMIT(1u)) {
            idx = (unsigned char)(idx + 3u);
        }
        if (v >= GLEEOK_SECONDARY_LIMIT(1u)) {
            idx = (unsigned char)(idx + 3u);
        }
    }

    switch (idx) {
    case 0u: gleeok_expand_segment(slot);            return;
    case 1u: /* Ignore */                            return;
    case 2u: gleeok_contract_segment_x_local(slot);  return;
    case 3u: /* Ignore */                            return;
    case 4u: /* Ignore */                            return;
    case 5u: gleeok_contract_segment_x_local(slot);  return;
    case 6u: gleeok_contract_segment_y_local(slot);  return;
    case 7u: gleeok_contract_segment_y_local(slot);  return;
    case 8u: gleeok_contract_segment_local(slot);    return;
    default:                                         return;
    }
}

/* ExpandSegment (Z_04.asm:8994): 50% horizontal, 50% vertical. */
static void gleeok_expand_segment(unsigned int slot)
{
    if ((unsigned char)ENEMY_RNG_A(0) & 0x80u) {
        /* Vertical expand. */
        const unsigned char cur = ENEMY_GLEEOK_SEG_Y(slot);
        const unsigned char nxt = ENEMY_GLEEOK_SEG_Y_TARGET(slot);
        unsigned char y = (unsigned char)(cur + 2u);
        if (cur <= nxt) {
            /* NES BEQ/BCS: equal or below moves away by -2. */
            y = (unsigned char)(y - 4u);
        }
        ENEMY_GLEEOK_SEG_Y(slot) = y;
    } else {
        /* Horizontal expand (Z_04.asm:9029). */
        const unsigned char cur = ENEMY_GLEEOK_SEG_X(slot);
        const unsigned char nxt = ENEMY_GLEEOK_SEG_X_TARGET(slot);
        unsigned char x = (unsigned char)(cur + 2u);
        if (cur < nxt) {
            x = (unsigned char)(x - 4u);
        }
        ENEMY_GLEEOK_SEG_X(slot) = x;
    }
}

static void gleeok_contract_segment_x_local(unsigned int slot)
{
    /* Z_04.asm:9039 — move toward next segment X. */
    const unsigned char cur = ENEMY_GLEEOK_SEG_X(slot);
    const unsigned char nxt = ENEMY_GLEEOK_SEG_X_TARGET(slot);
    unsigned char x = (unsigned char)(cur + 2u);
    if (cur >= nxt) {
        x = (unsigned char)(x - 4u);
    }
    ENEMY_GLEEOK_SEG_X(slot) = x;
}

static void gleeok_contract_segment_y_local(unsigned int slot)
{
    /* Z_04.asm:9018 — move toward next segment Y. */
    const unsigned char cur = ENEMY_GLEEOK_SEG_Y(slot);
    const unsigned char nxt = ENEMY_GLEEOK_SEG_Y_TARGET(slot);
    unsigned char y = (unsigned char)(cur + 2u);
    if (cur > nxt) {
        y = (unsigned char)(y - 4u);
    }
    ENEMY_GLEEOK_SEG_Y(slot) = y;
}

static void gleeok_contract_segment_local(unsigned int slot)
{
    /* Z_04.asm:9057 — random axis. */
    if ((unsigned char)ENEMY_RNG_A(0) & 0x80u) {
        gleeok_contract_segment_y_local(slot);
    } else {
        gleeok_contract_segment_x_local(slot);
    }
}

/* ------------------------------------------------------------------
 * c_gleeok_move_neck (NES Z_04.asm:8717)
 *   Compute signed reference-segment distance (head→base / 4) then
 *   tail-call enrt_gleeok_store_ref_seg_distance.
 * ------------------------------------------------------------------ */
void c_gleeok_move_neck(void)
{
    const unsigned char head_x = ENEMY_GLEEOK_HEAD_X;
    const unsigned char base_x = ENEMY_GLEEOK_BASE_X;
    unsigned char diff = (unsigned char)(head_x - base_x);

    if ((diff & 0x80u) == 0u) {
        /* Positive: unsigned divide by 4 (LSR LSR). */
        diff = (unsigned char)(diff >> 2);
    } else {
        /* Negative: Negate / LSR LSR / Negate (signed div by 4). */
        diff = (unsigned char)(0u - diff);
        diff = (unsigned char)(diff >> 2);
        diff = (unsigned char)(0u - diff);
    }
    enrt_gleeok_store_ref_seg_distance((unsigned int)diff);
}

/* ------------------------------------------------------------------
 * c_gleeok_move_head (NES Z_04.asm:9272)
 *   Per-tick head movement: nudges X/Y by ±1 per the SPEEDX/Y flags;
 *   every 4th tick advances H/V dir counters and flips speeds at the
 *   $0C / $06 thresholds.
 * ------------------------------------------------------------------ */
static unsigned char gleeok_change_coord_by_speed_flag(unsigned char value,
                                                       unsigned char flag)
{
    /* Y == 0 -> +1; else -1. */
    return (flag == 0u) ? (unsigned char)(value + 1u)
                        : (unsigned char)(value - 1u);
}

void c_gleeok_move_head(void)
{
    if (GLEEOK_WORK_DELAY != 0u) {
        enrt_gleeok_dec_head_timer();
        return;
    }

    /* Update head X. */
    {
        const unsigned char x = ENEMY_GLEEOK_HEAD_X;
        const unsigned char fx = (unsigned char)GLEEOK_WORK_SPEEDX;
        ENEMY_GLEEOK_HEAD_X = gleeok_change_coord_by_speed_flag(x, fx);
    }
    /* Update head Y. */
    {
        const unsigned char y = ENEMY_GLEEOK_HEAD_Y;
        const unsigned char fy = (unsigned char)GLEEOK_WORK_SPEEDY;
        ENEMY_GLEEOK_HEAD_Y = gleeok_change_coord_by_speed_flag(y, fy);
    }

    /* Direction-change counter — every 4 frames check H/V counters. */
    {
        const unsigned char chg = (unsigned char)(GLEEOK_WORK_DIRCHG + 1u);
        GLEEOK_WORK_DIRCHG = chg;
        if (chg < 0x04u) return;
    }
    GLEEOK_WORK_DIRCHG = 0u;

    /* Horizontal counter check. */
    {
        const unsigned char h = (unsigned char)(GLEEOK_WORK_DIRCNT_H + 1u);
        GLEEOK_WORK_DIRCNT_H = h;
        if (h >= 0x0Cu) {
            GLEEOK_WORK_DIRCNT_H = 0u;
            GLEEOK_WORK_SPEEDX =
                (unsigned char)(GLEEOK_WORK_SPEEDX ^ 0xFFu);
        }
    }
    /* Vertical counter check. */
    {
        const unsigned char v = (unsigned char)(GLEEOK_WORK_DIRCNT_V + 1u);
        GLEEOK_WORK_DIRCNT_V = v;
        if (v >= 0x06u) {
            GLEEOK_WORK_DIRCNT_V = 0u;
            GLEEOK_WORK_SPEEDY =
                (unsigned char)(GLEEOK_WORK_SPEEDY ^ 0xFFu);
        }
    }
}

/* ------------------------------------------------------------------
 * Anim_WriteSpecificSprite + Anim_WriteLevelPaletteSprite locals.
 * NES Z_01.asm:2500/2509/2532/2546.
 *
 * Anim_WriteSpecificSprite(tile, slot, sprite_off, attrs):
 *   OAM[off+1] = tile;  OAM[off+3] = ObjX[slot];
 *   OAM[off+0] = ObjY[slot];  OAM[off+2] = attrs;
 *   sprite_cycle_cur_sprite_index();
 *
 * Anim_WriteLevelPaletteSprite(tile, slot):
 *   attrs = 3; if MON_HIT_REACTION(slot) != 0 ->
 *     attrs = ENEMY_CUR_SPRITE_ATTR_ROW & 3;
 *   off = k_sprite_offsets[RollingSpriteIndex];
 *   then WriteSpecificSprite.
 * ------------------------------------------------------------------ */
static void gleeok_anim_write_specific_sprite(unsigned char tile,
                                              unsigned int slot,
                                              unsigned char sprite_off,
                                              unsigned char attrs)
{
    unsigned char gx = (unsigned char)OBJ(NES_OBJ_X, slot);
    unsigned char gy = (unsigned char)OBJ(NES_OBJ_Y, slot);
    RAM(0x0003u) = attrs;               /* NES passes the attributes in [03] */
    OAM_BYTE((unsigned int)(sprite_off + 1u)) = tile;
    OAM_BYTE((unsigned int)(sprite_off + 3u)) = gx;
    OAM_BYTE((unsigned int)sprite_off) = gy;
    OAM_BYTE((unsigned int)(sprite_off + 2u)) = attrs;
    sprite_cycle_cur_sprite_index();
    /* Gleeok rooms draw from NES OAM (enemy_render_sweep_oam_to_sat). */
}

static void gleeok_anim_write_level_palette_sprite(unsigned char tile,
                                                   unsigned int slot)
{
    unsigned char attrs = 0x03u;
    if ((unsigned char)ENEMY_HIT_REACTION(slot) != 0u) {
        attrs = (unsigned char)(ENEMY_CUR_SPRITE_ATTR_ROW & 0x03u);
    }
    {
        const unsigned char rsi = (unsigned char)RAM(0x0341u);
        const unsigned char off = k_sprite_offsets[rsi & 0x3Fu];
        gleeok_anim_write_specific_sprite(tile, slot, off, attrs);
    }
}

/* ------------------------------------------------------------------
 * c_gleeok_draw_head_and_check_collisions (NES Z_04.asm:9100)
 *   Entry from UpdateGleeok; starts with slot = 5 (head).
 * ------------------------------------------------------------------ */
static void gleeok_write_head_or_base_sprite_and_check_collisions(
    unsigned char tile, unsigned int slot)
{
    /* Z_04.asm:9073. Sprite offset = neck<<3, +$20 if not head. */
    unsigned char off =
        (unsigned char)((unsigned char)ENEMY_GLEEOK_NECK_INDEX << 3);
    if (slot != 5u) {
        off = (unsigned char)(off + 0x20u);
    }
    gleeok_anim_write_specific_sprite(tile, slot, off, 0x03u);
    enrt_gleeok_check_collisions(slot);
}

void c_gleeok_draw_segment_and_check_collisions(unsigned int slot)
{
    /* Z_04.asm:9102. Tile = $DA; if slot==5 -> $DC; head/base use the
     * write-head-or-base path; middles use Anim_WriteLevelPaletteSprite
     * then Gleeok_CheckCollisions (which loops back to here). */
    unsigned char tile = 0xDAu;
    if (slot == 5u) tile = 0xDCu;

    if (slot == 5u || slot == 1u) {
        gleeok_write_head_or_base_sprite_and_check_collisions(tile, slot);
        return;
    }
    gleeok_anim_write_level_palette_sprite(tile, slot);
    enrt_gleeok_check_collisions(slot);
}

void c_gleeok_draw_head_and_check_collisions(void)
{
    c_gleeok_draw_segment_and_check_collisions(5u);
}

/* ------------------------------------------------------------------
 * c_gleeok_draw_body (NES Z_04.asm:9369)
 *   2-row x 3-col body sprite block at fixed screen pos (x=$74+col*8,
 *   y=$57+row*$10) drawn from tiles0/baseTileOffsets pair. Cycles
 *   the rolling sprite cursor for each of the 6 sprites.
 * ------------------------------------------------------------------ */
void c_gleeok_draw_body(void)
{
    /* Animation-timer gate. */
    if (ENEMY_GLEEOK_ANIM_CNTR != 0u) {
        ENEMY_GLEEOK_ANIM_CNTR = (unsigned char)(ENEMY_GLEEOK_ANIM_CNTR - 1u);
    } else {
        unsigned char timer = 0x10u;
        if (ENEMY_GLEEOK_WRITHE_CNTR != 0u) {
            ENEMY_GLEEOK_WRITHE_CNTR =
                (unsigned char)(ENEMY_GLEEOK_WRITHE_CNTR - 1u);
            timer = 0x06u;
        }
        ENEMY_GLEEOK_ANIM_CNTR = timer;
        /* Cycle body anim frame 0..3. */
        {
            /* NES GleeokBodyAnimationFrame is $04E7; $0512 is
             * LinkParalyzed and must never be used as sprite scratch. */
            unsigned char frame = (unsigned char)ENEMY_GLEEOK_BODY_FRAME;
            frame = (unsigned char)((frame + 1u) & 0x03u);
            ENEMY_GLEEOK_BODY_FRAME = frame;
        }
    }

    /* Write 2 rows x 3 cols using k_body_tiles0 + base tile offset. */
    {
        const unsigned char frame = (unsigned char)ENEMY_GLEEOK_BODY_FRAME & 0x03u;
        unsigned char tile_idx = k_body_base_tile_offsets[frame];
        unsigned char row;
        /* [06] row and [07] column counters live in NES RAM (T-171). */
        for (row = 0u; row < 2u; row++) {
            unsigned char col;
            RAM(0x0006u) = row;
            for (col = 0u; col < 3u; col++) {
                RAM(0x0007u) = col;
                const unsigned char rsi = (unsigned char)RAM(0x0341u);
                const unsigned char off = k_sprite_offsets[rsi & 0x3Fu];

                /* Y = $57 + row * $10. */
                unsigned char gy = (unsigned char)((row << 4) + 0x57u);
                OAM_BYTE((unsigned int)off) = gy;

                /* LDA GleeokBodyTiles0, X (X = base offset + 0..5). */
                unsigned char body_tile = k_body_tiles0[tile_idx];
                OAM_BYTE((unsigned int)(off + 1u)) = body_tile;

                /* ObjInvincibilityTimer+5 & 3 while it runs (cycled
                 * palette rows), else level palette row 7 (3). */
                unsigned char attrs = (unsigned char)ENEMY_HIT_REACTION(5);
                if (attrs == 0u) attrs = 0x03u;
                attrs = (unsigned char)(attrs & 0x03u);
                OAM_BYTE((unsigned int)(off + 2u)) = attrs;

                /* X = $74 + col * 8. */
                unsigned char gx = (unsigned char)((col << 3) + 0x74u);
                OAM_BYTE((unsigned int)(off + 3u)) = gx;

                tile_idx = (unsigned char)(tile_idx + 1u);
                sprite_cycle_cur_sprite_index();
            }
            RAM(0x0007u) = 3u;
        }
        RAM(0x0006u) = 2u;
    }
}

/* ------------------------------------------------------------------
 * c_write_blank_priority_sprites + c_reset_obj_metastate
 *   Forwarders to the native dispatcher entry points. Both are
 *   referenced by enrt_gleeok_check_collisions but not provided by
 *   any other linked TU under the c_* prefix.
 * ------------------------------------------------------------------ */
void c_write_blank_priority_sprites(void)
{
    core_write_blank_priority_sprites();
}

void c_reset_obj_metastate(unsigned int slot)
{
    core_reset_obj_metastate(slot);
}

/* ------------------------------------------------------------------
 * boss_gleeok_init — InitGleeok (NES Z_04.asm:7649)
 *
 * Wired into enemy_loop.c rows $42/$43/$44/$45 INIT.
 * ------------------------------------------------------------------ */
void boss_gleeok_init(unsigned int slot)
{
    (void)slot;
    /* Play Aquamentus/Gleeok/Ganon roar: SampleRequest ($0601) = $10.
     * ($0600 is SongRequest.) */
    RAM(0x0601u) = 0x10u;

    /* Per-segment loop X = 5..0 (each neck has 6 segments).
     * NES uses Gleeok_NeckXs0..3 absolute base addrs. */
    {
        signed char x;
        for (x = 5; x >= 0; --x) {
            const unsigned int u = (unsigned int)x;
            /* X = $7C for all 4 necks at this segment slot. */
            nes_ram[(0x0438u + u)] = 0x7Cu;
            nes_ram[(0x0452u + u)] = 0x7Cu;
            nes_ram[(0x046Cu + u)] = 0x7Cu;
            nes_ram[(0x0395u + u)] = 0x7Cu;
            /* ObjX+1, X (= $0071+x — ENEMY_GLEEOK_BASE_X area). */
            nes_ram[(0x0071u + u)] = 0x7Cu;

            /* Y from GleeokSegmentYs[x] for all 4 necks. */
            {
                const unsigned char y = k_gleeok_segment_ys[u];
                nes_ram[(0x0445u + u)] = y;
                nes_ram[(0x045Fu + u)] = y;
                nes_ram[(0x0479u + u)] = y;
                nes_ram[(0x03BDu + u)] = y;
                nes_ram[(0x0085u + u)] = y;   /* ObjY+1, X */
            }

            /* HP $A0 in ObjHP+1 ($0486+x). */
            nes_ram[(0x0486u + u)] = 0xA0u;

            /* Reset metastate ($0405+1+x) and uninit-flag ($0492+2+x).
             * NES has the +2 quirk noted as UNKNOWN at line 7687. */
            nes_ram[(0x0406u + u)] = 0x00u;
            nes_ram[(0x0494u + u)] = 0x00u;

            /* InvincibilityMask $FE — sword only (ObjInvincibilityMask+1). */
            nes_ram[(0x04B3u + u)] = 0xFEu;
        }
    }

    /* Speed flags: necks 0 X / 1 Y / 2 X / 3 Y to $FF (decel),
     * remaining flags stay 0 (accel). NES STX uses X register at
     * end of loop = $FF (DEX past 0). */
    nes_ram[k_head_info_base[0] + GLEEOK_OFF_SPEEDX] = 0xFFu;
    nes_ram[k_head_info_base[1] + GLEEOK_OFF_SPEEDY] = 0xFFu;
    nes_ram[k_head_info_base[2] + GLEEOK_OFF_SPEEDX] = 0xFFu;
    nes_ram[k_head_info_base[3] + GLEEOK_OFF_SPEEDY] = 0xFFu;

    /* V dir-counter = 3 for all 4 necks. */
    nes_ram[k_head_info_base[0] + GLEEOK_OFF_DIRCNT_V] = 0x03u;
    nes_ram[k_head_info_base[1] + GLEEOK_OFF_DIRCNT_V] = 0x03u;
    nes_ram[k_head_info_base[2] + GLEEOK_OFF_DIRCNT_V] = 0x03u;
    nes_ram[k_head_info_base[3] + GLEEOK_OFF_DIRCNT_V] = 0x03u;

    /* H dir-counter = 6 (= 3 << 1). */
    nes_ram[k_head_info_base[0] + GLEEOK_OFF_DIRCNT_H] = 0x06u;
    nes_ram[k_head_info_base[1] + GLEEOK_OFF_DIRCNT_H] = 0x06u;
    nes_ram[k_head_info_base[2] + GLEEOK_OFF_DIRCNT_H] = 0x06u;
    nes_ram[k_head_info_base[3] + GLEEOK_OFF_DIRCNT_H] = 0x06u;

    /* Per-neck head-delay timers: neck 0 = 0, 1 = 12, 2 = 24, 3 = 36.
     * NES asm derives these via successive ASLs from $03 (= 6 -> 12 -> 24)
     * and ADC for neck 3 (24 + 12 = 36). */
    nes_ram[k_head_info_base[1] + GLEEOK_OFF_DELAY] = 12u;
    nes_ram[k_head_info_base[2] + GLEEOK_OFF_DELAY] = 24u;
    nes_ram[k_head_info_base[3] + GLEEOK_OFF_DELAY] = 36u;
}

/* ------------------------------------------------------------------
 * boss_gleeok_update_head — UpdateGleeokHead (NES Z_04.asm:8527)
 *
 * Wired into enemy_loop.c row $46 UPDATE.
 * ------------------------------------------------------------------ */

/* Mini Flyer JT (Z_04.asm:8559 ControlGleeokHeadFlight). */
extern void enrt_flyer_speed_up(unsigned int slot);
extern void enrt_flyer_gleeok_head_decide_state(unsigned int slot);

/* Flyer_Chase / Flyer_Wander defined static in enemy_flyer_bridge.c.
 * Promote behavior via the wrapper that re-enters the dispatcher. The
 * keese-head JT uses the same routines — re-export via a JT-aware
 * dispatcher. We rely on the flyer subsystem's own state-2/3 handling
 * by piggy-backing on the keese-head dispatch path: the
 * c_control_keese_flight bridge already exposes states 2 (Chase) and
 * 3 (Wander); we reuse it for state >= 2. */
extern void c_control_keese_flight(unsigned int slot);

static void gleeok_control_head_flight(unsigned int slot)
{
    /* Z_04.asm:8559 — JT[FlyingState] over slot $0444. */
    const unsigned char st = (unsigned char)OBJ(0x0444u, slot);
    switch (st & 0x03u) {
    case 0u: enrt_flyer_speed_up(slot);                 break;
    case 1u: enrt_flyer_gleeok_head_decide_state(slot); break;
    case 2u:
    case 3u:
        /* Reuse the keese-head dispatcher for Chase/Wander — same
         * Flyer_Chase / Flyer_Wander routines per NES JT. */
        c_control_keese_flight(slot);
        break;
    default: break;
    }
}

void boss_gleeok_update_head(unsigned int slot)
{
    gleeok_control_head_flight(slot);
    c_move_flyer(slot);

    /* Fireball gate — every odd dist-traveled, RNG_B < $20, slot $B
     * empty -> shoot fireball $56. */
    {
        const unsigned char dist = (unsigned char)OBJ(0x0437u, slot);
        if ((dist & 0x01u) == 0u) {
            const unsigned char rng = (unsigned char)ENEMY_RNG_B(slot);
            if (rng < 0x20u && (unsigned char)OBJ(NES_OBJ_TYPE, 0x0Bu) == 0u) {
                c_shoot_fireball(0x56u, slot);
            }
        }
    }

    /* LDA #$01; JSR Anim_AdvanceAnimCounterAndSetObjPosForSpriteDescriptor;
     * LDA ObjAnimFrame, X; JSR DrawObjectMirrored. T-171: the drain drew
     * frame 0 without advancing the counter (ObjAnimFrame+7 split,
     * t171_gleeok_sword t842). */
    {
        extern void z07_anim_advance_and_fetch(unsigned int val, unsigned int slot);
        extern void draw_object_mirrored(unsigned char frame,
                                         unsigned int slot);
        z07_anim_advance_and_fetch(1u, slot);
        draw_object_mirrored((unsigned char)OBJ(0x03E4u, slot), slot);
    }

    c_check_monster_collisions(slot);

    /* ResetObjMetastate, SetShoveInfoWith0 (A = 0), STA
     * ObjInvincibilityTimer, X: the head can't die nor stay invincible.
     * T-171: the drain cleared $04B2 + slot, not the timer ($04F0), so
     * a hit made the head invincible for 16 frames (t171_gleeok_sword
     * t887: NES HP D0 -> B0 on the next hit, Genesis stayed D0). */
    core_reset_obj_metastate(slot);
    core_set_shove_info_with0(0u, slot);
    OBJ(0x04F0u, slot) = 0u;
}
