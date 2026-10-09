/* Phase 8 Task 8.8 — Patra drain (NES Z_04.asm:9552 InitPatra,
 *                                  Z_04.asm:10070 UpdatePatra,
 *                                  Z_04.asm:10164 UpdatePatraChild,
 *                                  Z_04.asm:11898 PatraSines,
 *                                  Z_04.asm:11911 RotateObjectLocation,
 *                                  Z_04.asm:12025 ShiftMultiply,
 *                                  Z_04.asm:12055 DecreaseObjectAngle,
 *                                  Z_01.asm:5337  Anim_SetSpriteDescriptorRedPaletteRow).
 *
 * Drained C:  original InitPatra/UpdatePatraChild/math bodies here.
 * Coverage:   PARTIAL — Q1 red/blue fights and shared pond-fairy math;
 *             blue orbit/angle/maneuver changes byte-verified through
 *             death/drop. Re-entry/SRAM remain open. UpdatePatra parent
 *             orchestration lives in boss_patra.c.
 * Stance:     EXTEND — native word math preserves the drained byte
 *             products and carry/borrow. Pond-fairy wrappers below
 *             share the same rotation helpers.
 */

#include "enemy_runtime_private.h"
#include "legacy_bridge.h"

extern void c_check_link_collision(unsigned int slot);

/* PatraSines (NES Z_04.asm:11898). Two quarter-cycles of sin*$80
 * sampled at 16 angle steps. */
static const unsigned char kPatraSines[16] = {
    0x00, 0x18, 0x30, 0x47, 0x5A, 0x6A, 0x76, 0x7D,
    0x80, 0x7D, 0x76, 0x6A, 0x5A, 0x47, 0x30, 0x18
};

/* PatraChildStartAngles (NES Z_04.asm:10152). 7 entries; used to
 * stagger child appearance during State 0. */
static const unsigned char kPatraChildStartAngles[7] = {
    0x14, 0x10, 0x0C, 0x08, 0x04, 0x00, 0x1C
};

/* PatraChild1RotationCosineBits (NES Z_04.asm:10155):
 *   maneuver-index 0: cosine bits = 6
 *   maneuver-index 1: cosine bits = 6 (PatraChild1RotationSineBits[0])
 * PatraChild1RotationSineBits   (NES Z_04.asm:10161):
 *   maneuver-index 0: sine bits = 6
 *   maneuver-index 1: sine bits = 6 (PatraChild1RotationSineBits[1])
 *
 * NES asm reads PatraChild1RotationCosineBits, Y for maneuver Y in {0,1};
 * Y=1 falls into PatraChild2RotationBits ($05) per the .BYTE layout. So
 * the effective Child1 table is { (cos=6, sin=6), (cos=5, sin=6) }. */
static const unsigned char kPatraChild1CosineBits[2] = { 0x06, 0x05 };
static const unsigned char kPatraChild1SineBits[2]   = { 0x06, 0x06 };

/* PatraChild2RotationBits (NES Z_04.asm:10158):
 *   maneuver-index 0: bits = 5
 *   maneuver-index 1: bits = 6 (PatraChild1RotationSineBits[0]) */
static const unsigned char kPatraChild2Bits[2] = { 0x05, 0x06 };

/* ShiftMultiply (NES Z_04.asm:12025).
 *   A = multiplicand, Y = num-high-bits-to-use, multiplier = high-Y bits
 *   of [00]. Returns product in [02:03] (lo:hi).
 *
 * Algorithm = repeated-shift multiply where each iteration shifts the
 * accumulator left and conditionally adds the multiplicand based on the
 * MSB of the multiplier register. The `INC $03` at the unknown block
 * handles the carry-out from the lo-byte add.
 */
static unsigned short patra_shift_multiply(unsigned char a_in,
                                           unsigned char y_bits,
                                           unsigned char mult_in)
{
    /* NES Z_04.asm ShiftMultiply: A * (the high y_bits of [00]). Leaves
     * [00] = multiplier shifted left y_bits, [01] = A, [02:03] = product
     * as the NES does (T-171). */
    const unsigned short product = (unsigned short)((unsigned short)a_in *
                                   ((unsigned short)mult_in >> (8u - y_bits)));
    RAM(0x0000u) = (unsigned char)(mult_in << y_bits);
    RAM(0x0001u) = a_in;
    RAM(0x0002u) = (unsigned char)product;
    RAM(0x0003u) = (unsigned char)(product >> 8);
    return product;
}

static void patra_decrease_object_angle(unsigned char low,
                                        unsigned char high,
                                        unsigned int slot)
{
    unsigned short angle = (unsigned short)
        (((unsigned short)ENEMY_OBJ_ANGLE_WHOLE(slot) << 8) |
         ENEMY_OBJ_ANGLE_FRAC(slot));
    RAM(0x000Au) = low;                 /* NES [0A]/[0B] amount */
    RAM(0x000Bu) = high;
    angle -= (unsigned short)(((unsigned short)high << 8) | low);
    ENEMY_OBJ_ANGLE_FRAC(slot) = (unsigned char)angle;
    ENEMY_OBJ_ANGLE_WHOLE(slot) = (unsigned char)((angle >> 8) & 0x1Fu);
}

/* RotateObjectLocation (NES Z_04.asm:11911). Updates ObjX/ObjXFrac
 * directly; returns the new ObjY (caller stores). */
/* T-172: keep child rotation in the caller; avoid register save/reload. */
__attribute__((always_inline)) static inline unsigned char patra_rotate_object_location(unsigned char cosine_bits,
                                                  unsigned char sine_bits,
                                                  unsigned int slot)
{
    unsigned char angle = ENEMY_OBJ_ANGLE_WHOLE(slot);
    unsigned char speed = ENEMY_OBJ_QSPEED_FRAC(slot);
    unsigned short product;
    unsigned short position;

    RAM(0x0006u) = cosine_bits;         /* NES STA $06 / STY $05 */
    RAM(0x0005u) = sine_bits;

    /* NES ASL/ROL product + ADC/SBC fractions are 16-bit wrap arithmetic.
     * Pack each axis once; a native word add preserves carry/borrow. */
    product = patra_shift_multiply(speed, sine_bits,
                                   kPatraSines[angle & 0x0Fu]);
    position = (unsigned short)(((unsigned short)ENEMY_X(slot) << 8) |
                                ENEMY_OBJ_X_FRAC(slot));
    if ((angle & 0x18u) >= 0x10u) position -= product;
    else position += product;
    ENEMY_OBJ_X_FRAC(slot) = (unsigned char)position;
    ENEMY_X(slot) = (unsigned char)(position >> 8);

    product = patra_shift_multiply(speed, cosine_bits,
                                   kPatraSines[(angle + 8u) & 0x0Fu]);
    position = (unsigned short)(((unsigned short)ENEMY_Y(slot) << 8) |
                                ENEMY_OBJ_Y_FRAC(slot));
    if (((unsigned char)(angle - 8u) & 0x18u) >= 0x10u) position -= product;
    else position += product;
    ENEMY_OBJ_Y_FRAC(slot) = (unsigned char)position;
    return (unsigned char)(position >> 8);
}

/* PatraChild_Draw (NES Z_04.asm:10312).
 *   JSR Anim_SetSpriteDescriptorRedPaletteRow  ; A=2, [04]=[05]=$02
 *   JSR Anim_AdvanceAnimCounterAndSetObjPosForSpriteDescriptor (val=2)
 *   LDA ObjAnimFrame, X
 *   JMP DrawObjectNotMirrored
 */
static void patra_child_draw(unsigned int slot)
{
    z01_anim_set_sprite_desc_attrs(0x02u);
    c_anim_advance_and_fetch(2u, slot);
    {
        unsigned char frame = ENEMY_DRAW_FRAME(slot);
        c_draw_object_not_mirrored_with_frame(frame, slot);
    }
}

/* InitPatra (NES Z_04.asm:9552).
 *   ObjInvincibilityMask, X = $FE  (sword-only)
 *   ObjX = $80, ObjY = $70, ObjDir = $08 (up)
 *   Flyer_ObjSpeed, X = $1F, FlyingMaxSpeedFrac = $40
 *   SampleRequest = $40 (Digdogger/Manhandla/Patra roar)
 *   ObjTimer+1, X = $FF
 *   Patra type $47 -> child type $25; type $48 -> child type $26.
 *   Loop slots 2..9 setting ObjType[Y] = child_type and
 *   ObjInvincibilityMask[Y] = $FE.
 */
void enrt_init_patra(unsigned int slot)
{
    unsigned char child_type;
    unsigned int  child_slot;

    ENEMY_INVINCIBILITY(slot) = 0xFEu;
    ENEMY_X(slot) = 0x80u;
    ENEMY_Y(slot) = 0x70u;
    ENEMY_DIR(slot) = 0x08u;
    ENEMY_AIR_SPEED(slot) = 0x1Fu;
    ENEMY_MAX_AIR_SPEED   = 0x40u;
    ENEMY_SFX_BOSS_CRY    = 0x40u;
    ENEMY_OBJ_TIMER_HI(slot) = 0xFFu;

    /* LDY ObjType+1: Patra $47 -> child $25, else $26. */
    child_type = (ENEMY_TYPE(1u) == 0x47u) ? 0x25u : 0x26u;
    for (child_slot = 2u; child_slot < 10u; ++child_slot) {
        ENEMY_TYPE(child_slot) = child_type;
        ENEMY_INVINCIBILITY(child_slot) = 0xFEu;
    }
}

/* UpdatePatraChild (NES Z_04.asm:10164). State 0 stages each child's
 * appearance off the slot-2 child's angle; State 1 orbits Patra. */
void enrt_update_patra_child(unsigned int slot)
{
    /* ObjState, X (= NES $AC) drives the State 0/1 toggle. */
    if (ENEMY_STATE_TIMER(slot) != 0u) {
        /* PatraChild_State1 (Z_04.asm:10244). */
        unsigned char maneuver_idx;
        unsigned char cos_bits;
        unsigned char sin_bits;
        unsigned char new_y;

        /* Add Patra's distance traveled (Flyer_ObjOffsetX/Y at slot 1). */
        ENEMY_X(slot) = (unsigned char)(ENEMY_X(slot) + ENEMY_FLYER_OFFSET_X(1));
        ENEMY_Y(slot) = (unsigned char)(ENEMY_Y(slot) + ENEMY_FLYER_OFFSET_Y(1));

        /* Decrease angle by $70 (Child1) or $60 (Child2). */
        {
            unsigned char low = (ENEMY_TYPE(slot) == 0x25u) ? 0x70u : 0x60u;
            patra_decrease_object_angle(low, 0x00u, slot);
        }

        maneuver_idx = (unsigned char)(ENEMY_PATRA_MANEUVER_INDEX(1) & 0x01u);
        if (ENEMY_TYPE(slot) == 0x25u) {
            cos_bits = kPatraChild1CosineBits[maneuver_idx];
            sin_bits = kPatraChild1SineBits[maneuver_idx];
        } else {
            unsigned char bits = kPatraChild2Bits[maneuver_idx];
            cos_bits = bits;
            sin_bits = bits;
        }
        new_y = patra_rotate_object_location(cos_bits, sin_bits, slot);
        ENEMY_Y(slot) = new_y;

        patra_child_draw(slot);

        /* ObjState+1 == 0 → Patra still in spawn animation, skip
         * collision until last child has appeared. */
        if (ENEMY_STATE_TIMER(1) == 0u)
            return;

        c_check_monster_collisions(slot);
        /* Post-collision: if monster's metastate cleared (= still alive),
         * exit. NES `LDA ObjMetastate, X / BEQ @Exit`. */
        if (ENEMY_METASTATE(slot) == 0u)
            return;
        enrt_set_dead_dummy_obj_type(slot);
        return;
    }

    /* State 0 (PatraChild_State0). */
    if (slot != 2u) {
        unsigned int probe_y;
        /* If slot 2 is still in State 0, no later child can advance. */
        if (ENEMY_STATE_TIMER(2) == 0u)
            return;
        /* Y = slot - 3 → index into PatraChildStartAngles. */
        probe_y = slot - 3u;
        if (ENEMY_OBJ_ANGLE_WHOLE(2) != kPatraChildStartAngles[probe_y])
            return;
    }
    /* @Ready: when slot 9 reaches here, advance Patra parent's state. */
    if (slot == 9u) {
        ENEMY_STATE_TIMER(1)++;
    }
    ENEMY_STATE_TIMER(slot)++;
    ENEMY_DIR(slot) = 0x80u;            /* TODO?: NES quirk */
    ENEMY_OBJ_ANGLE_WHOLE(slot) = 0x18u;
    ENEMY_X(slot) = ENEMY_X(1);
    {
        unsigned char radius = (ENEMY_TYPE(slot) == 0x25u) ? 0x2Cu : 0x18u;
        RAM(0x0000u) = radius;          /* NES STA $00 (T-171) */
        ENEMY_Y(slot) = (unsigned char)(ENEMY_Y(1) - radius);
    }
}

/* T-050: shared with the pond fairy hearts (Z_04.asm PondFairy_MoveHearts
 * uses the same DecreaseObjectAngle / RotateObjectLocation). */
void enrt_decrease_object_angle(unsigned char low, unsigned char high,
                                unsigned int slot)
{
    patra_decrease_object_angle(low, high, slot);
}

unsigned char enrt_rotate_object_location(unsigned char cosine_bits,
                                          unsigned char sine_bits,
                                          unsigned int slot)
{
    return patra_rotate_object_location(cosine_bits, sine_bits, slot);
}
