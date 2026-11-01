/* T-261 boss overlay model (Remix only).
 *
 * NES source: docs/audit/2026-10-29_T261_boss_sources.md
 * Drained C:  reads ENEMY_TYPE/HP/METASTATE/ALIVE_FLAG (src/state/enemy_state.h)
 * Coverage:   all boss ObjTypes; tools/audit/t261_boss_overlay_check.py
 * Stance:     GREENFIELD presentation, read-only on game state.
 *
 * Called once per Mode 5 play update after the object loop, so HP is
 * post-damage. Clears itself when RoomId or CurLevel changes. */
#include "boss_overlay.h"
#include "enemy_state.h"
#include "game_profile.h"

boss_overlay_state_t g_boss_overlay;
unsigned char g_boss_overlay_gen;     /* bumped whenever the model is rebuilt */

#define FIRST_SLOT    1u
#define LAST_SLOT     11u

static unsigned char s_room = 0xFFu;
static unsigned char s_level = 0xFFu;
static unsigned char s_gleeok_alive;  /* necks alive when the current neck started */
static unsigned char s_gleeok_full;   /* ObjHP[5] when the current neck started */
static unsigned char s_bar_of_anchor[BOSS_OVERLAY_MAX_BARS]; /* anchor slot -> bar + 1 */
/* Last inputs the model was built from, per boss-type slot: type, HP,
 * metastate, init flag (4 bytes x slots 0..11; type 0 = not a boss part,
 * so fireballs and other objects never trigger a rebuild), plus Gleeok's
 * shared neck HP (slot 5 is type 0) and dead-neck mask. Slots are checked
 * in thirds on successive ticks: a change is seen within two ticks (33 ms),
 * and a steady fight costs a few compares (T-261 lag gate: Patra and
 * Gleeok rooms run at ~99% of the frame before the overlay). */
static unsigned char s_snap[4u * 12u];
static unsigned char s_snap_neck_hp;
static unsigned char s_snap_mask;
static unsigned char s_phase;         /* which snapshot half this tick */
static unsigned char s_snap_valid;

void boss_overlay_clear(void)
{
    unsigned char i;
    unsigned char *p = (unsigned char *)&g_boss_overlay;
    for (i = 0; i < sizeof(g_boss_overlay); ++i) p[i] = 0u;
    for (i = 0; i < BOSS_OVERLAY_MAX_BARS; ++i) s_bar_of_anchor[i] = 0u;
    s_gleeok_alive = 0u;
    s_snap_valid = 0u;
}

void boss_overlay_room_enter(void)
{
    boss_overlay_clear();
}

/* 68000 divu.w: 32/16 -> 16-bit quotient in one instruction. Quotients
 * here are at most BOSS_BAR_WIDTH * 4, so it never overflows. GCC emits a
 * __udivsi3 call for the plain C division (T-261 lag gate: ~700 cycles
 * per bar per tick in Patra rooms). */
static unsigned char divu16(unsigned long n, unsigned short d)
{
    __asm__("divu.w %1,%0" : "+d"(n) : "d"(d));
    return (unsigned char)n;
}

static unsigned char fill_of(unsigned short value, unsigned short full)
{
    if (value == 0u || full == 0u) return 0u;
    if (value >= full) return BOSS_BAR_WIDTH;
    return divu16((unsigned long)value * BOSS_BAR_WIDTH + full - 1u, full);
}

static unsigned char slot_live(unsigned char s)
{
    return (unsigned char)(ENEMY_METASTATE(s) == 0u && ENEMY_ALIVE_FLAG(s) == 0u);
}

static unsigned char find_bar(unsigned char anchor)
{
    return (unsigned char)(s_bar_of_anchor[anchor] - 1u);    /* 0xFF if none */
}

/* Returns the bar index for anchor, appending a new one when absent. */
static unsigned char take_bar(unsigned char anchor, unsigned char type,
                              unsigned char full, unsigned char *seen)
{
    unsigned char i = find_bar(anchor);
    if (i != 0xFFu && g_boss_overlay.obj_type[i] != type) {
        g_boss_overlay.obj_type[i] = type;          /* slot reused: fresh bar */
        g_boss_overlay.spawn_hp[i] = full;
        g_boss_overlay.hp_full_sum = (unsigned short)(g_boss_overlay.hp_full_sum + full);
        g_boss_overlay.fill_px[i] = BOSS_BAR_WIDTH;
    }
    if (i == 0xFFu) {
        i = g_boss_overlay.bar_count;
        if (i >= BOSS_OVERLAY_MAX_BARS) return 0xFFu;
        g_boss_overlay.slot[i] = anchor;
        g_boss_overlay.obj_type[i] = type;
        g_boss_overlay.spawn_hp[i] = full;
        g_boss_overlay.fill_px[i] = BOSS_BAR_WIDTH;
        g_boss_overlay.bar_count = (unsigned char)(i + 1u);
        g_boss_overlay.hp_full_sum = (unsigned short)(g_boss_overlay.hp_full_sum + full);
        s_bar_of_anchor[anchor] = (unsigned char)(i + 1u);
    }
    seen[i] = 1u;
    return i;
}

static void update_chain(unsigned char s, unsigned char type, unsigned char *seen)
{
    /* one bar per chain, anchored on the head slot (5 or 10); the sum is
     * at most 5 x $20, so it fits the byte-wide spawn_hp */
    unsigned char head = (unsigned char)(s <= 5u ? 5u : 10u);
    unsigned char total = 0u;
    unsigned char x, i = find_bar(head);
    if (i != 0xFFu && seen[i]) return;
    for (x = (unsigned char)(head - 4u); x <= head; ++x)
        if (ENEMY_TYPE(x) == type && ENEMY_METASTATE(x) == 0u)
            total = (unsigned char)(total + ENEMY_HP(x));
    i = take_bar(head, type, total, seen);
    if (i == 0xFFu) return;
    x = fill_of(total, g_boss_overlay.spawn_hp[i]);
    if (x < g_boss_overlay.fill_px[i]) g_boss_overlay.fill_px[i] = x;
}

static void update_gleeok(unsigned char type, unsigned char *seen)
{
    unsigned char necks = (unsigned char)(type - 0x42u + 1u);
    unsigned char mask = (unsigned char)(F_ENEMY_GLEEOK_DEAD_NECK_MASK_R & ((1u << necks) - 1u));
    unsigned char alive = necks, i, f;
    unsigned short v;
    while (mask) { alive = (unsigned char)(alive - (mask & 1u)); mask >>= 1; }
    i = find_bar(1u);
    if (i == 0xFFu) { s_gleeok_alive = alive; s_gleeok_full = ENEMY_HP(5u); }
    i = take_bar(1u, type, ENEMY_HP(5u), seen);
    if (i == 0xFFu) return;
    if (alive != s_gleeok_alive) {                  /* new neck: its own full HP */
        s_gleeok_alive = alive;
        s_gleeok_full = ENEMY_HP(5u);
    }
    v = alive ? (unsigned short)((alive - 1u) * s_gleeok_full + ENEMY_HP(5u)) : 0u;
    f = fill_of(v, (unsigned short)(necks * s_gleeok_full));
    if (f < g_boss_overlay.fill_px[i]) g_boss_overlay.fill_px[i] = f;
}

/* noinline: one call per tick; keeps a stable entry PC for the T-261
 * check, which reads the NES cells exactly when this function does. */
__attribute__((noinline)) void boss_overlay_update(void)
{
    unsigned char seen[BOSS_OVERLAY_MAX_BARS];
    unsigned char s, i, n;
    if (!game_profile_is_remix()) return;
    if (F_ram_00EB_R3 != s_room || F_ram_0010_R != s_level) {
        s_room = F_ram_00EB_R3;
        s_level = F_ram_0010_R;
        boss_overlay_clear();
    }
    {
        unsigned char changed = (unsigned char)!s_snap_valid, v;
        static const unsigned char k_first[3] = { 1u, 5u, 9u };
        static const unsigned char k_last[3]  = { 4u, 8u, 11u };
        const unsigned char third = s_phase = (unsigned char)(s_phase >= 2u ? 0u : s_phase + 1u);
        const unsigned char half = (unsigned char)(third == 2u);   /* extras with slots 9..11 */
        const unsigned char first = k_first[third];
        const unsigned char last = k_last[third];
        unsigned char *q = &s_snap[first * 4u];
        for (s = first; s <= last; ++s, q += 4) {
            v = ENEMY_TYPE(s);
            if (!boss_overlay_rule_for(v)) {
                if (q[0]) { q[0] = 0u; changed = 1u; }  /* a boss part left the slot */
                continue;
            }
            if (q[0] != v) { q[0] = v; changed = 1u; }
            v = ENEMY_HP(s);          if (q[1] != v) { q[1] = v; changed = 1u; }
            v = ENEMY_METASTATE(s);   if (q[2] != v) { q[2] = v; changed = 1u; }
            v = ENEMY_ALIVE_FLAG(s);  if (q[3] != v) { q[3] = v; changed = 1u; }
        }
        if (half) {
            v = ENEMY_HP(5u);
            if (s_snap_neck_hp != v) { s_snap_neck_hp = v; changed = 1u; }
            v = F_ENEMY_GLEEOK_DEAD_NECK_MASK_R;
            if (s_snap_mask != v) { s_snap_mask = v; changed = 1u; }
        }
        s_snap_valid = 1u;
        if (!changed) return;
        ++g_boss_overlay_gen;
    }
    for (i = 0; i < BOSS_OVERLAY_MAX_BARS; ++i) seen[i] = 0u;
    for (s = FIRST_SLOT; s <= LAST_SLOT; ++s) {
        const unsigned char type = ENEMY_TYPE(s);
        const boss_part_rule_t *r;
        if (type == 0u) continue;
        r = boss_overlay_rule_for(type);
        if (!r || !slot_live(s)) continue;
        if (!g_boss_overlay.active) {
            g_boss_overlay.active = 1u;
            g_boss_overlay.name_id = r->name_id;
            g_boss_overlay.banner_timer = BOSS_BANNER_FRAMES;
            g_boss_overlay.hud_mode = 1u;
        }
        if (r->flags & BOSS_RULE_CHAIN) {
            update_chain(s, type, seen);
        } else if (r->flags & BOSS_RULE_GLEEOK) {
            update_gleeok(type, seen);
        } else if ((r->flags & BOSS_RULE_BAR) &&
                   !((r->flags & BOSS_RULE_NO_SLOT5) && s == 5u)) {
            i = take_bar(s, type, ENEMY_HP(s), seen);
            if (i != 0xFFu)
                g_boss_overlay.fill_px[i] = fill_of(ENEMY_HP(s), g_boss_overlay.spawn_hp[i]);
        }
    }
    /* drop bars whose part is gone (keep order) */
    for (i = 0, n = 0; i < g_boss_overlay.bar_count; ++i) {
        s_bar_of_anchor[g_boss_overlay.slot[i]] = 0u;
        if (!seen[i]) continue;
        s_bar_of_anchor[g_boss_overlay.slot[i]] = (unsigned char)(n + 1u);
        g_boss_overlay.slot[n] = g_boss_overlay.slot[i];
        g_boss_overlay.obj_type[n] = g_boss_overlay.obj_type[i];
        g_boss_overlay.spawn_hp[n] = g_boss_overlay.spawn_hp[i];
        g_boss_overlay.fill_px[n] = g_boss_overlay.fill_px[i];
        ++n;
    }
    g_boss_overlay.bar_count = n;
    if (n > g_boss_overlay.parts_seen) g_boss_overlay.parts_seen = n;
}
