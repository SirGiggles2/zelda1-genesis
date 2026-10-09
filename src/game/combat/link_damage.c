#include "link_damage.h"
#include "../../state/inventory.h"

extern void audio_sfx_play(unsigned char sfx);

/* ObjInvincibilityTimer mirror — Link is slot 0. NES Z_07.asm:5757 reads
 * `ObjInvincibilityTimer, X` with X=0; we hold a single byte. */
static unsigned char s_invincibility_timer = 0u;

/* Task 6.11.4 BeginShove state — Link slot. NES ObjShoveDir / ObjShoveDistance
 * each have 16 entries; we keep the slot-0 cells. */
static unsigned char s_shove_dir      = 0u;  /* NES bits + $80 first-frame */
static unsigned char s_shove_distance = 0u;  /* pixels remaining (0..$20) */

#define ROOMROM_INVINCIBILITY_INITIAL  0x10u  /* damage_apply scaffold */
#define ROOMROM_SHOVE_INVINCIBILITY    0x18u  /* NES Z_01.asm:6568 */
#define ROOMROM_SHOVE_DISTANCE         0x20u  /* NES Z_01.asm:6570 */

#define DIR_RIGHT 0x01u
#define DIR_LEFT  0x02u
#define DIR_DOWN  0x04u
#define DIR_UP    0x08u
#define DIR_FIRST_FRAME 0x80u

void roomrom_link_damage_init(void)
{
    s_invincibility_timer = 0u;
    s_shove_dir           = 0u;
    s_shove_distance      = 0u;
}

unsigned char roomrom_link_damage_invincible(void)
{
    return (unsigned char)(s_invincibility_timer != 0u);
}

unsigned char roomrom_link_damage_dead(void)
{
    return (unsigned char)((g_inventory.heart_values & 0x0Fu) == 0u
                            && g_inventory.heart_partial == 0u);
}

/* NES Z_07.asm:5756 DecrementInvincibilityTimer:
 *   if timer == 0: return
 *   if (FrameCounter LSR) carry: return   ; only decrement on even frames
 *   DEC ObjInvincibilityTimer
 */
void roomrom_link_damage_tick(unsigned char frame_counter)
{
    if (s_invincibility_timer == 0u) return;
    if ((frame_counter & 1u) != 0u) return;  /* odd frame, skip */
    s_invincibility_timer--;
}

/* NES Z_01.asm:5691 Link_BeHarmed — entry receives [0D] hi / [0E] lo
 * damage; loops `LSR $0D / ROR $0E` per ring level (InvRing 0/1/2). */
static void apply_ring_divide(unsigned char *dmg_hi, unsigned char *dmg_lo)
{
    unsigned char rings = g_inventory.ring;
    while (rings != 0u) {
        unsigned char carry = (unsigned char)(*dmg_hi & 1u);
        *dmg_hi = (unsigned char)(*dmg_hi >> 1);
        *dmg_lo = (unsigned char)((*dmg_lo >> 1) | (carry ? 0x80u : 0u));
        rings--;
    }
}

unsigned char roomrom_link_damage_apply(unsigned char dmg_hi,
                                        unsigned char dmg_lo)
{
    if (s_invincibility_timer != 0u) return 0u;     /* already invincible */
    if (g_inventory.clock != 0u) return 0u;         /* clock freezes harm */

    /* NES Z_01.asm:5695-5696 Link_BeHarmed:
     *   LDA #$08 / JSR PlaySample = hurt sfx (bit 3 -> DMC sample 4). */
    audio_sfx_play(4u);

    apply_ring_divide(&dmg_hi, &dmg_lo);

    /* NES Z_01.asm:5718-5754 — subtract low byte from HeartPartial; if
     * HeartPartial < dmg_lo, borrow one full heart and recompute. */
    if (g_inventory.heart_partial >= dmg_lo) {
        g_inventory.heart_partial = (unsigned char)(g_inventory.heart_partial - dmg_lo);
    } else {
        unsigned char need = (unsigned char)(dmg_lo - g_inventory.heart_partial);
        unsigned char full = (unsigned char)(g_inventory.heart_values & 0x0Fu);
        if (full == 0u) {
            /* Can't borrow — Link dies. */
            g_inventory.heart_values =
                (unsigned char)(g_inventory.heart_values & 0xF0u);
            g_inventory.heart_partial = 0u;
            inventory_hud_mark_dirty();
            return 1u;
        }
        full--;
        g_inventory.heart_values =
            (unsigned char)((g_inventory.heart_values & 0xF0u) | full);
        /* NES quirk Z_01.asm:5742-5743: partial wraps to $FF, not $100. */
        g_inventory.heart_partial = (unsigned char)(0xFFu - need);
        dmg_hi = (unsigned char)0;  /* low-byte borrow consumed; high byte stays */
    }

    /* Now subtract dmg_hi full hearts. */
    {
        unsigned char full = (unsigned char)(g_inventory.heart_values & 0x0Fu);
        if (full < dmg_hi) {
            g_inventory.heart_values =
                (unsigned char)(g_inventory.heart_values & 0xF0u);
            g_inventory.heart_partial = 0u;
            inventory_hud_mark_dirty();
            return 1u;
        }
        full = (unsigned char)(full - dmg_hi);
        g_inventory.heart_values =
            (unsigned char)((g_inventory.heart_values & 0xF0u) | full);
    }

    s_invincibility_timer = ROOMROM_INVINCIBILITY_INITIAL;
    inventory_hud_mark_dirty();
    return 0u;
}

/* Task 6.11.4 BeginShove — NES Z_01.asm:6470 Link-defender path.
 *
 * Direction polarity rule (NES @CheckVertical):
 *   if monster_axis >= defender_axis: keep base_dir (UP or LEFT)
 *   else                            : LSR base_dir   (turns UP->DOWN, LEFT->RIGHT)
 *
 * "Push Link AWAY from the monster": if the monster is below Link
 * (monster_y >= link_y, ignoring sign for position), shove direction
 * is UP. If the monster is above Link, LSR'd base = DOWN.  Same for
 * horizontal axis.
 *
 * Coverage detail: NES dispatches axis selection via $0B (the attacker's
 * direction or weapon direction). For the Link-defender unblocked path,
 * the dispatcher uses Link's own ObjDir & 0x03 to pick horizontal vs
 * vertical when his grid offset != 0; otherwise falls back to $0B (which
 * for monsters is normally the monster's facing direction). We accept
 * defender_dir + a "use_horizontal_fallback" derived from grid_offset and
 * pass through. The monster-direction fallback path lands when 6.11.2
 * enemy state lights up. */
unsigned char roomrom_link_shove_begin(unsigned char defender_dir,
                                       unsigned char defender_grid_offset,
                                       signed short monster_x,
                                       signed short monster_y,
                                       signed short link_x,
                                       signed short link_y)
{
    unsigned char horizontal;
    unsigned char base_dir;
    signed short  m_axis, d_axis;

    if (s_invincibility_timer != 0u) return 0u;

    /* NES Z_01.asm:6501-6508:
     *   if defender is Link (Y == 0) and grid_offset != 0:
     *       use defender_dir & $03 != 0 -> horizontal
     *       use defender_dir & $03 == 0 -> vertical
     *   else (grid_offset == 0):
     *       fall back to $0B-direction code (we approximate via monster
     *       axis-distance compare; horizontal if |dx| > |dy|). */
    if (defender_grid_offset != 0u) {
        horizontal = (unsigned char)((defender_dir & 0x03u) != 0u);
    } else {
        signed short dx = monster_x - link_x;
        signed short dy = monster_y - link_y;
        signed short adx = (dx < 0) ? (signed short)(-dx) : dx;
        signed short ady = (dy < 0) ? (signed short)(-dy) : dy;
        horizontal = (unsigned char)(adx > ady);
    }

    if (horizontal) {
        base_dir = DIR_LEFT;     /* NES base for horizontal axis */
        m_axis   = monster_x;
        d_axis   = link_x;
    } else {
        base_dir = DIR_UP;       /* NES base for vertical axis */
        m_axis   = monster_y;
        d_axis   = link_y;
    }

    /* NES @CheckVertical: BCS keeps base, else LSR base.
     * BCS = unsigned >=. Our axes are signed but the compare is the
     * same as `if (m_axis >= d_axis) keep else LSR`. */
    if (!(m_axis >= d_axis)) {
        base_dir = (unsigned char)(base_dir >> 1);  /* UP->DOWN, LEFT->RIGHT */
    }

    s_shove_dir      = (unsigned char)(base_dir | DIR_FIRST_FRAME);
    s_shove_distance = ROOMROM_SHOVE_DISTANCE;
    s_invincibility_timer = ROOMROM_SHOVE_INVINCIBILITY;
    return 1u;
}

/* Per-frame applier — moves Link 1 pixel along ObjShoveDir until distance
 * reaches 0. Caller adds the returned dx/dy to Link's position. NES
 * Obj_Shove (Z_01.asm:2274) tests collision per-pixel; collision testing
 * lands with 6.11.x rooming-physics work. */
void roomrom_link_shove_tick(signed char *out_dx, signed char *out_dy)
{
    unsigned char dir;

    *out_dx = 0;
    *out_dy = 0;

    if (s_shove_distance == 0u) {
        s_shove_dir = 0u;
        return;
    }

    dir = (unsigned char)(s_shove_dir & 0x0Fu);  /* strip first-frame bit */
    if      (dir == DIR_RIGHT) *out_dx =  1;
    else if (dir == DIR_LEFT)  *out_dx = -1;
    else if (dir == DIR_DOWN)  *out_dy =  1;
    else if (dir == DIR_UP)    *out_dy = -1;
    else { s_shove_distance = 0u; return; }

    s_shove_distance--;
    s_shove_dir = (unsigned char)(s_shove_dir & 0x7Fu);  /* clear first-frame bit */
}

unsigned char roomrom_link_shove_active(void)
{
    return (unsigned char)(s_shove_distance != 0u);
}

unsigned char roomrom_link_shove_dir(void)      { return s_shove_dir; }
unsigned char roomrom_link_shove_distance(void) { return s_shove_distance; }
