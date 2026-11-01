/* NES source: existing item sprite producers/Anim_WriteItemSprites.
 * Drained C: sprite_render.c:wslot/set_full_cached and native SAT sweep.
 * Coverage: existing single-sprite item APIs and four native beam pieces per
 *           extra owner; multi-piece clouds/full H32 policy remain separate.
 * Stance: EXTEND presentation ownership; no gameplay state or SGDK IO. */
#include "mp_sprite_runtime.h"
#include <stdint.h>
#include "sprite_slots.h"
#include "render_abi.h"
#include "../../../state/render_budget.h"

_Static_assert(ROOMROM_MP_WEAPON_TOKEN_BASE >= RENDER_BUDGET_VDP_TOTAL_SPRITES,
               "Virtual item tokens must never name hardware slots");
_Static_assert(ROOMROM_MP_WEAPON_TOKEN_COUNT <= RENDER_BUDGET_DYNAMIC_SPRITES &&
               ROOMROM_MP_WEAPON_TOKEN_COUNT < 32u,
               "Bounded presentation records must fit the H32 pool and mask");

static render_sprite_entry_t s_mp_weapon_sprites[ROOMROM_MP_WEAPON_TOKEN_COUNT];
static unsigned long s_mp_weapon_visible;
unsigned char g_mp_sprite_dropped;
unsigned char g_mp_sprite_line_dropped;

/* T390 explicit Redux divergence: optional ring and gameplay parts share the
 *35 dynamic entries by rotating admission. Fixed actors and a carrying hand
 *are retained. Only this512-byte snapshot persists; candidates live on stack. */
static render_sprite_entry_t s_overlay_base[64];
static unsigned char s_overlay_valid, s_overlay_live, s_overlay_hand;
volatile unsigned char g_ring_sprite_requested, g_ring_sprite_admitted,
    g_ring_sprite_dropped, g_ring_gameplay_dropped;

void mp_sprite_overlay_retire(void)
{
    unsigned char i;
    if (!s_overlay_live) return;
    /* Actor producers have already updated attributes before a sweep. Restore
     *only links here; dynamic entries are rebuilt by their ordinary owners. */
    for (i=0u;i<64u;++i) g_render_sat_cache[i].link=s_overlay_base[i].link;
    s_overlay_live=0u;
}

void mp_sprite_overlay_capture(unsigned char hand)
{
    /* T392: a world sweep publishes a new baseline, but a closed ring does
     * not need its512-byte copy. The first visible composition captures it;
     * subsequent frozen1P draws continue using that same baseline. */
    s_overlay_valid=0u; s_overlay_live=0u; s_overlay_hand=hand;
}

void mp_sprite_overlay_compose(const render_sprite_entry_t *entries,
                               unsigned char count,unsigned char phase)
{
    g_ring_sprite_requested=count; g_ring_sprite_admitted=0u;
    g_ring_sprite_dropped=count; g_ring_gameplay_dropped=0u;
    /* T392: test the idle case before initializing the compositor's128
     * scratch bytes. GCC emits both memsets even when the later validity
     * check returns without a visible ring. Closing still restores its SAT. */
    if(count==0u && !s_overlay_live)return;
    unsigned char order[64], base[35], chosen[64], seen[64]={0}, ring_at[64]={0};
    unsigned char i,n=0u,b=0u,slot=0u,out=0u,previous=0u;
    unsigned short total,start,k;
    if (!s_overlay_valid) {
        if(count==0u)return;
        for(i=0u;i<64u;++i)s_overlay_base[i]=g_render_sat_cache[i];
        s_overlay_valid=1u;
    }
    if (count==0u && !s_overlay_live) return;
    for(i=0u;i<64u;++i) {
        if(i<ROOMROM_SPRITE_SLOT_ENEMY_FIRST) {
            /* The frozen UI may update a HUD descriptor. Keep live fixed
             * attributes, but restore its ordinary chain before recomposing. */
            unsigned char link=s_overlay_base[i].link;
            s_overlay_base[i]=g_render_sat_cache[i];
            s_overlay_base[i].link=link;
        }
        g_render_sat_cache[i]=s_overlay_base[i];
    }
    if (count==0u) { s_overlay_live=0u; return; }
    do {
        if(slot>=64u || seen[slot]) break;
        seen[slot]=1u; order[n++]=slot;
        if(slot>=ROOMROM_SPRITE_SLOT_ENEMY_FIRST &&
           !(s_overlay_hand>=ROOMROM_SPRITE_SLOT_ENEMY_FIRST &&
             (slot==s_overlay_hand || slot==s_overlay_hand+1u)) &&
           /* Keep sprites whose lower rows intersect the visible screen. */
           ((s_overlay_base[slot].y & 511u) +
            ((s_overlay_base[slot].size & 3u)+1u)*8u)>128u &&
           (s_overlay_base[slot].y & 511u)<352u) base[b++]=slot;
        slot=s_overlay_base[slot].link;
    } while(slot && n<64u);
    /* Preserve original fixed/captured ordering, including Wallmaster's
     *hand-before-Link splice. Reuse the other physical dynamic slots. */
    for(i=0u;i<n;++i) {
        slot=order[i];
        if(slot<ROOMROM_SPRITE_SLOT_ENEMY_FIRST ||
           (s_overlay_hand>=ROOMROM_SPRITE_SLOT_ENEMY_FIRST &&
            (slot==s_overlay_hand || slot==s_overlay_hand+1u))) chosen[out++]=slot;
    }
    total=(unsigned short)(b+count); start=phase%total;
    slot=ROOMROM_SPRITE_SLOT_ENEMY_FIRST;
    for(k=0u;k<total;++k) {
        unsigned short candidate=(unsigned short)((start+k)%total);
        while(slot<64u && s_overlay_hand>=ROOMROM_SPRITE_SLOT_ENEMY_FIRST &&
              (slot==s_overlay_hand || slot==s_overlay_hand+1u)) ++slot;
        if(slot>=64u) break;
        g_render_sat_cache[slot]=candidate<b ? s_overlay_base[base[candidate]] : entries[candidate-b];
        ring_at[slot]=(unsigned char)(candidate>=b);
        if(candidate>=b) ++g_ring_sprite_admitted;
        else ++previous;
        chosen[out++]=slot++;
    }
    g_ring_gameplay_dropped=(unsigned char)(b-previous);
    g_ring_sprite_dropped=(unsigned char)(count-g_ring_sprite_admitted);
    for(i=0u;i<out;++i)
        g_render_sat_cache[chosen[i]].link=i+1u<out?chosen[i+1u]:0u;
    /* Force the existing exact H32 policy in frozen1P too. Actor priority
     *and a carrying hand stay protected; optional parts rotate per UI tick. */
    {
        /* T-398: the ring keeps exact WHOLE admission in every SPRITES mode. */
        const unsigned char mode=g_mp_sprite_overflow_mode;
        g_mp_sprite_overflow_mode=MP_SPRITE_MODE_WHOLE;
        mp_sprite_limit_lines(2u,phase,s_overlay_hand);
        g_mp_sprite_overflow_mode=mode;
    }
    g_ring_sprite_admitted=0u; previous=0u; slot=0u; i=0u;
    do {
        if(ring_at[slot]) ++g_ring_sprite_admitted;
        else if(slot>=ROOMROM_SPRITE_SLOT_ENEMY_FIRST &&
                !(s_overlay_hand>=ROOMROM_SPRITE_SLOT_ENEMY_FIRST &&
                  (slot==s_overlay_hand || slot==s_overlay_hand+1u))) ++previous;
        slot=g_render_sat_cache[slot].link;
    } while(slot && ++i<64u);
    g_ring_sprite_dropped=(unsigned char)(count-g_ring_sprite_admitted);
    g_ring_gameplay_dropped=(unsigned char)(b-previous);
    s_overlay_live=1u;
}

/* NES source: sprite priority / bounded hardware sprite evaluation;
 * Genesis H32 contract:64 entries,16 sprites/line,256 pixels/line.
 * Drained C: existing bounded SAT composition and render-budget constants.
 * Coverage: PARTIAL multiplayer presentation; every gameplay update remains.
 * Stance: EXTEND composition, keeping bodies ahead of budgeted effects. */
void mp_sprite_restore_links(unsigned char players)
{
    unsigned char slot;
    if (players <= 1u) return;
    /* T-395: fixed count; unrolled into immediate byte stores. */
#pragma GCC unroll 29
    for (slot = 0u; slot < ROOMROM_SPRITE_SLOT_ENEMY_FIRST; ++slot)
        g_render_sat_cache[slot].link = (unsigned char)(slot + 1u);
}

/* NES source: existing exact H32 sprite/pixel admission below.
 * Drained C: reserve_lines; policy/order is unchanged.
 * Coverage: exact counter lanes and a sufficient all-fit broad phase.
 * Stance: EXTEND CPU efficiency only; never skip simulation or quantize output. */
typedef struct {
    unsigned char begin[64], end[64];
    uint16_t remaining[64];
} LineUsage;
/* Walk the hardware chain and decode each Y range once. The conservative
 * bands prove an all-fit result; exact fallback reuses these same bounds. */
static unsigned char collect_lines(unsigned char *order, unsigned char *length,
                                    LineUsage *u, unsigned char prove)
{
    uint16_t remaining[28];
    unsigned char seen[64] = {0};
    unsigned char init;
    for (init = 0u; init < 28u; ++init) remaining[init] = 0x2010u;
    unsigned char slot = 0u, n = 0u, fits = prove;
    do {
        const render_sprite_entry_t *e;
        short first, last;
        if (slot >= 64u || seen[slot]) break;
        seen[slot] = 1u;
        order[n++] = slot;
        e = &g_render_sat_cache[slot];
        first = (short)(((unsigned short)e->y & 511u) - 128);
        last = (short)(first + ((e->size & 3u) + 1u) * 8u);
        if (last <= 0 || first >= 224) {
            u->begin[slot] = u->end[slot] = 0u;
        } else {
            unsigned char width, band, end;
            if (first < 0) first = 0;
            if (last > 224) last = 224;
            u->begin[slot] = (unsigned char)first;
            u->end[slot] = (unsigned char)last;
            if (fits) {
                width = (unsigned char)(((e->size >> 2) & 3u) + 1u);
                band = (unsigned char)(first >> 3);
                end = (unsigned char)((last - 1) >> 3);
                do {
                    const uint16_t value = (uint16_t)(remaining[band] -
                        (uint16_t)(1u + ((uint16_t)width << 8)));
                    remaining[band] = value;
                    if (value & 0x8080u) {
                        fits = 0u;
                        break;
                    }
                } while (++band <= end);
            }
        }
        slot = e->link;
    } while (slot != 0u && n < 64u);
    *length = n;
    return (unsigned char)(slot == 0u && fits);
}

/* Occupancy changes only at a sprite's first/end scanline. Compress those
 * boundaries once, so a stack sharing Y uses one interval instead of16
 * identical line counters. At most64 sprites produce64 maximal active sets.
 * Each exact interval stores remaining tiles/sprites in separate bytes;
 * subtracting a charge sets a high bit precisely on capacity underflow. */
_Static_assert(RENDER_BUDGET_VDP_PER_LINE == 16u &&
               RENDER_BUDGET_VDP_PIXELS_PER_LINE == 256u,
               "Packed line capacities require the exact H32 contract");

static void prepare_lines(const unsigned char *order, unsigned char n, LineUsage *u)
{
    unsigned char boundaries[225] = {0};
    unsigned short line, count = 0u, first_line = 224u, last_line = 0u;
    unsigned char pending_start = 0u;
    unsigned char i;
    for (i = 0u; i < n; ++i) {
        const unsigned char slot = order[i];
        const unsigned char first = u->begin[slot], last = u->end[slot];
        if (first == last) continue;
        boundaries[first] |= 1u;
        boundaries[last] |= 2u;
        if (first < first_line) first_line = first;
        if (last > last_line) last_line = last;
    }
    /* Starts only add sprites and ends only remove them. Between a run of
     * starts and the next end, the full active set dominates every smaller
     * set. Its two nonnegative capacities therefore suffice for every admitted
     * subset. END before START preserves disjoint touching intervals. */
    for (line = first_line; line <= last_line; ++line) {
        const unsigned char event = boundaries[line];
        if (event == 0u) continue;
        if ((event & 2u) && pending_start) {
            ++count;
            pending_start = 0u;
        }
        boundaries[line] = (unsigned char)count;
        if (event & 1u) pending_start = 1u;
    }
    for (i = 0u; i < n; ++i) {
        const unsigned char slot = order[i];
        if (u->begin[slot] != u->end[slot]) {
            u->begin[slot] = boundaries[u->begin[slot]];
            u->end[slot] = boundaries[u->end[slot]];
        }
    }
    for (line = 0u; line < count; ++line) u->remaining[line] = 0x2010u;
}

static unsigned char reserve_lines(unsigned char slot, LineUsage *u)
{
    const render_sprite_entry_t *e = &g_render_sat_cache[slot];
    const unsigned char begin = u->begin[slot], end = u->end[slot];
    const uint16_t charge = (uint16_t)(0x0101u + ((e->size & 12u) << 6));
    unsigned char i;
    for (i = begin; i < end; ++i) {
        const uint16_t value = (uint16_t)(u->remaining[i] - charge);
        if (value & 0x8080u) {
            while (i > begin) u->remaining[--i] += charge;
            return 0u;
        }
        u->remaining[i] = value;
    }
    return 1u;
}

static unsigned char protected_sprite(unsigned char slot, unsigned char hand)
{
    return (unsigned char)(slot <= ROOMROM_SPRITE_SLOT_LINK_R ||
        (slot >= ROOMROM_SPRITE_SLOT_MP_LINK2 && slot <= ROOMROM_SPRITE_SLOT_MP_LINK4_R) ||
        (hand >= ROOMROM_SPRITE_SLOT_ENEMY_FIRST && (slot == hand || slot == hand + 1u)));
}

/* Byte L of the counter array is scanline L's remaining sprite capacity.
 * With widths at most two tiles the16-sprite constraint implies the32-tile
 * constraint. T-395: a16-line interval is read as longs from the even byte
 * at or before its first line (the68000 needs only word alignment): four
 * full longs for an even start, five with partial ends for an odd one. The
 * partial masks follow host byte order, so host tests run the same layout.
 * Tentative values stay in registers, so rejection never needs to undo a
 * prefix. A borrow between lanes occurs only after an actual capacity
 * underflow; its high bit rejects the whole reservation before any store. */
typedef uint32_t __attribute__((may_alias)) LineLanes;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define LANES_AFTER_FIRST 0xFFFFFF00UL /* bytes1..3 of the leading long */
#define LANES_FIRST_ONLY 0x000000FFUL  /* byte0 of the trailing long */
#else
#define LANES_AFTER_FIRST 0x00FFFFFFUL
#define LANES_FIRST_ONLY 0xFF000000UL
#endif
static inline __attribute__((always_inline)) LineLanes *
line_lanes(unsigned char first, uint32_t *remaining)
{
    return (LineLanes *)((unsigned char *)remaining + (first & 0xFEu));
}

static inline __attribute__((always_inline)) unsigned char
reserve_sixteen(unsigned char first, uint32_t *remaining)
{
    LineLanes *const p = line_lanes(first, remaining);
    const uint32_t b = p[1] - 0x01010101UL;
    const uint32_t c = p[2] - 0x01010101UL;
    const uint32_t d = p[3] - 0x01010101UL;
    if (!(first & 1u)) {
        const uint32_t a = p[0] - 0x01010101UL;
        if ((a | b | c | d) & 0x80808080UL) return 0u;
        p[0] = a; p[1] = b; p[2] = c; p[3] = d;
    } else {
        const uint32_t a = p[0] - (0x01010101UL & LANES_AFTER_FIRST);
        const uint32_t e = p[4] - (0x01010101UL & LANES_FIRST_ONLY);
        if ((a | b | c | d | e) & 0x80808080UL) return 0u;
        p[0] = a; p[1] = b; p[2] = c; p[3] = d; p[4] = e;
    }
    return 1u;
}

/* NES source: existing protected sprite admission. Drained C: reserve_sixteen.
 * Coverage: direct narrow path, no captured hand. Stance: EXTEND arithmetic.
 * At most twelve protected slots precede every optional reservation, so their
 * total charge always fits16 sprites/32 tiles. Equal-Y reservations commute;
 * aggregate consecutive visible bodies without changing any admission result. */
_Static_assert(ROOMROM_SPRITE_SLOT_LINK_R + 1u +
    ROOMROM_SPRITE_SLOT_MP_LINK4_R - ROOMROM_SPRITE_SLOT_MP_LINK2 + 1u <= 16u,
    "Protected direct reservations must fit the hardware capacity");
static void charge_protected_run(unsigned char first, unsigned char count,
                                  uint32_t *remaining)
{
    LineLanes *const p = line_lanes(first, remaining);
    uint32_t charge = count;
    charge |= charge << 8;
    charge |= charge << 16;
    p[1] -= charge; p[2] -= charge; p[3] -= charge;
    if (!(first & 1u)) p[0] -= charge;
    else {
        p[0] -= charge & LANES_AFTER_FIRST;
        p[4] -= charge & LANES_FIRST_ONLY;
    }
}

/* T-395: the input chain is 0->1->...->n-1->0, so relinking kept sprites in
 * traversal order changes only the kept slot ahead of each dropped run: it
 * now names the next kept slot (or ends the chain). Every other kept link
 * is already its successor; dropped links stay untouched, as before.
 * Slots0..5 are protected, so a run's predecessor always exists. */
static void relink_dropped_run(unsigned char first, unsigned char last,
                               unsigned char n)
{
    g_render_sat_cache[first - 1u].link =
        (unsigned char)(last + 1u < n ? last + 1u : 0u);
}

/* NES source: the existing bounded sprite-priority policy below.
 * Drained C: mp_sprite_limit_lines/reserve_sixteen.
 * Coverage: contiguous SAT prefixes, sixteen visible lines, width<=two,
 * no captured hand. Stance: EXTEND the same policy without list building.
 * Hidden dynamic entries still participate in the rotation denominator. */
static __attribute__((noinline)) void limit_direct(unsigned char n,
    unsigned char phase, const LineUsage *usage)
{
    uint32_t remaining[57];
    unsigned char drops[64];
    unsigned char i, slot, nd = 0u, fixed_drops, wrap;
    unsigned char run_y = 0u, run_count = 0u;
    unsigned char fixed_end = n < ROOMROM_SPRITE_SLOT_ENEMY_FIRST ?
        n : ROOMROM_SPRITE_SLOT_ENEMY_FIRST;
    /* Sixteen sprites of capacity per scanline byte (unrolled library fill). */
    __builtin_memset(remaining, 0x10, sizeof remaining);
    /* protected_sprite(slot,0) is exactly these two ranges; the fixed
     * slots between and after them keep ascending order. */
    for (i = 0u; i < fixed_end; ++i) {
        if (i == ROOMROM_SPRITE_SLOT_LINK_R + 1u) i = ROOMROM_SPRITE_SLOT_MP_LINK2;
        if (i > ROOMROM_SPRITE_SLOT_MP_LINK4_R || i >= fixed_end) break;
        if (usage->begin[i] != usage->end[i]) {
            if (run_count && run_y != usage->begin[i]) {
                charge_protected_run(run_y, run_count, remaining);
                run_count = 0u;
            }
            run_y = usage->begin[i];
            ++run_count;
        }
    }
    if (run_count) charge_protected_run(run_y, run_count, remaining);
    for (i = ROOMROM_SPRITE_SLOT_LINK_R + 1u; i < fixed_end; ++i) {
        if (i == ROOMROM_SPRITE_SLOT_MP_LINK2) {
            i = ROOMROM_SPRITE_SLOT_MP_LINK4_R + 1u;
            if (i >= fixed_end) break;
        }
        if (usage->begin[i] != usage->end[i] &&
            !reserve_sixteen(usage->begin[i], remaining)) drops[nd++] = i;
    }
    fixed_drops = wrap = nd;
    if (n > ROOMROM_SPRITE_SLOT_ENEMY_FIRST) {
        const unsigned char count = n - ROOMROM_SPRITE_SLOT_ENEMY_FIRST;
        slot = ROOMROM_SPRITE_SLOT_ENEMY_FIRST + phase % count;
        for (i = 0u; i < count; ++i) {
            if (usage->begin[slot] != usage->end[slot] &&
                !reserve_sixteen(usage->begin[slot], remaining)) drops[nd++] = slot;
            if (++slot == n) {
                slot = ROOMROM_SPRITE_SLOT_ENEMY_FIRST;
                wrap = nd;
            }
        }
    }
    g_mp_sprite_line_dropped = nd;
    if (nd == 0u) return;
    /* Fixed drops ascend; the rotation visits [start,n) then [first,start),
     * each ascending. Walk them in slot order and close every maximal run. */
    {
        unsigned char run_first = 0xFFu, run_last = 0u, k, segment;
        for (segment = 0u; segment < 3u; ++segment) {
            unsigned char from, to;
            if (segment == 0u) { from = 0u; to = fixed_drops; }
            else if (segment == 1u) { from = wrap; to = nd; }
            else { from = fixed_drops; to = wrap; }
            for (k = from; k < to; ++k) {
                slot = drops[k];
                if (run_first != 0xFFu && slot != run_last + 1u) {
                    relink_dropped_run(run_first, run_last, n);
                    run_first = 0xFFu;
                }
                if (run_first == 0xFFu) run_first = slot;
                run_last = slot;
            }
        }
        relink_dropped_run(run_first, run_last, n);
    }
}

/* NES source: hardware sprite evaluation (existing bounded traversal).
 * Drained C: collect_lines/coarse_lines_fit. Coverage: the ordinary sweep
 * chain0->1->...->n-1 with link0 at its end; anything else (captured hand,
 * ring composition, repaired links, a64-entry chain without a terminator)
 * returns0 and takes the general traversal. Stance: EXTEND CPU efficiency.
 * One linear pass decodes each Y range, folds in the64-line all-fit proof,
 * and classifies the narrow sixteen-line shape; no visited/order arrays. */
static unsigned char walk_contiguous(LineUsage *u, unsigned char *length,
                                     unsigned char *narrow, unsigned char prove)
{
    uint16_t band[4] = {0x2010u,0x2010u,0x2010u,0x2010u};
    const render_sprite_entry_t *e = g_render_sat_cache;
    unsigned char slot = 0u, fits = prove, sixteen = 1u;
    for (;;) {
        const unsigned short first16 = (unsigned short)(((unsigned short)e->y & 511u) - 128u);
        const unsigned char size = e->size;
        unsigned char link, begin, end;
        /* Common case first: a sixteen-line (height2), width1..2 sprite
         * wholly inside lines0..223 needs no clipping or shape update. */
        if (first16 <= 208u && (size & 0x0Bu) == 0x01u) {
            begin = (unsigned char)first16;
            end = (unsigned char)(first16 + 16u);
        } else {
            short first = (short)first16;
            short last = (short)(first + ((size & 3u) + 1u) * 8u);
            if (last <= 0 || first >= 224) {
                u->begin[slot] = u->end[slot] = 0u;
                goto next;
            }
            if (first < 0) first = 0;
            if (last > 224) last = 224;
            begin = (unsigned char)first;
            end = (unsigned char)last;
            if ((unsigned char)(end - begin) != 16u || (size & 12u) > 4u)
                sixteen = 0u;
        }
        u->begin[slot] = begin;
        u->end[slot] = end;
        if (fits) {
            const uint16_t charge = (uint16_t)(0x0101u + ((size & 12u) << 6));
            unsigned char first_band = begin >> 6;
            const unsigned char last_band = (unsigned char)(end - 1u) >> 6;
            do {
                const uint16_t value = (uint16_t)(band[first_band] - charge);
                if (value & 0x8080u) { fits = 0u; break; }
                band[first_band] = value;
            } while (++first_band <= last_band);
        }
next:
        link = e->link;
        if (link != slot + 1u) {
            if (link == 0u) break;
            return 0u;
        }
        if (slot == 63u) return 0u;
        ++slot; ++e;
    }
    *length = (unsigned char)(slot + 1u);
    *narrow = sixteen;
    return (unsigned char)(fits ? 1u : 2u);
}

/* NES source: hardware sprite evaluation; approved H32 party policy.
 * Drained C: collect_lines, exact admission below. Coverage: sufficient proof
 * only. Stance: EXTEND CPU efficiency with identical SAT output.
 * A64-line band charges every intersecting sprite for the whole band. If
 * these upper bounds fit, every actual scanline fits. A failed proof leaves
 * all output untouched. Bounded traversal rejects invalid links and cycles;
 * no order/bounds/visited arrays are needed for this common all-fit result. */
static unsigned char s_proof_skip;

static unsigned char coarse_lines_fit(void)
{
    uint16_t remaining[4] = {0x2010u,0x2010u,0x2010u,0x2010u};
    unsigned short count = 64u;
    unsigned char slot = 0u;
    do {
        const render_sprite_entry_t *e;
        short first, last;
        if (slot >= 64u) return 0u;
        e = &g_render_sat_cache[slot];
        first = (short)(((unsigned short)e->y & 511u) - 128);
        last = (short)(first + ((e->size & 3u) + 1u) * 8u);
        if (last > 0 && first < 224) {
            unsigned short band, end;
            const uint16_t charge = (uint16_t)(0x0101u + ((e->size & 12u) << 6));
            if (first < 0) first = 0;
            if (last > 224) last = 224;
            band = (unsigned short)first >> 6;
            end = (unsigned short)(last - 1) >> 6;
            do {
                const uint16_t value = (uint16_t)(remaining[band] - charge);
                if (value & 0x8080u) return 0u;
                remaining[band] = value;
            } while (++band <= end);
        }
        slot = e->link;
        if (slot == 0u) return 1u;
    } while (--count);
    return 0u;
}

static __attribute__((noinline)) void limit_exact(const unsigned char *order,
    unsigned char n, LineUsage *usage, unsigned char phase,
    unsigned char captured_hand)
{
    unsigned char visible[64], priority[14];
    unsigned char fixed[ROOMROM_SPRITE_SLOT_ENEMY_FIRST];
    unsigned char dynamic[RENDER_BUDGET_DYNAMIC_SPRITES];
    unsigned char ordinal[RENDER_BUDGET_DYNAMIC_SPRITES];
    unsigned char v = 0u, p = 0u, f = 0u, d = 0u, total = 0u;
    unsigned char slot, i, cursor, previous = 0xFFu, narrow_sixteen = 1u;
    uint32_t narrow_remaining[57];
    unsigned char kept[64] = {0};
    /* Hidden sprites consume no lines, but retain their original links and
     * their place in the dynamic phase rotation. Classify once; only visible
     * candidates need the exact interval and priority passes below. */
    for (i = 0u; i < n; ++i) {
        unsigned char shown;
        slot = order[i];
        shown = (unsigned char)(usage->begin[slot] != usage->end[slot]);
        if (shown) {
            visible[v++] = slot;
            if ((unsigned char)(usage->end[slot] - usage->begin[slot]) != 16u ||
                (g_render_sat_cache[slot].size & 12u) > 4u)
                narrow_sixteen = 0u;
        }
        else kept[slot] = 1u;
        if (protected_sprite(slot, captured_hand)) {
            if (shown) priority[p++] = slot;
        } else if (slot >= ROOMROM_SPRITE_SLOT_ENEMY_FIRST) {
            if (shown) { dynamic[d] = slot; ordinal[d++] = total; }
            ++total;
        } else if (shown) fixed[f++] = slot;
    }
    if (narrow_sixteen)
        __builtin_memset(narrow_remaining, 0x10, sizeof narrow_remaining);
    else prepare_lines(visible, v, usage);
    for (i = 0u; i < p; ++i) {
        slot = priority[i];
        if (narrow_sixteen)
            (void)reserve_sixteen(usage->begin[slot], narrow_remaining);
        else (void)reserve_lines(slot, usage);
        kept[slot] = 1u;
    }
    for (i = 0u; i < f; ++i) {
        slot = fixed[i];
        if (narrow_sixteen ? reserve_sixteen(usage->begin[slot], narrow_remaining) :
                             reserve_lines(slot, usage)) kept[slot] = 1u;
        else ++g_mp_sprite_line_dropped;
    }
    cursor = 0u;
    if (d != 0u) {
        const unsigned char start = (unsigned char)(phase % total);
        while (cursor < d && ordinal[cursor] < start) ++cursor;
        if (cursor == d) cursor = 0u;
    }
    for (i = 0u; i < d; ++i) {
        slot = dynamic[cursor];
        if (narrow_sixteen ? reserve_sixteen(usage->begin[slot], narrow_remaining) :
                             reserve_lines(slot, usage)) kept[slot] = 1u;
        else ++g_mp_sprite_line_dropped;
        if (++cursor == d) cursor = 0u;
    }
    for (i = 0u; i < n; ++i) {
        slot = order[i];
        if (!kept[slot]) continue;
        if (previous != 0xFFu) g_render_sat_cache[previous].link = slot;
        previous = slot;
    }
    if (previous != 0xFFu) g_render_sat_cache[previous].link = 0u;
}

/* NES source: hardware sprite evaluation (per-line drop) with the accepted
 * T-251 phase rotation. Drained C: none (presentation policy).
 * Coverage: canonical sweep chain 0->1->...->n-1->0 only.
 * Stance: EXTEND (T-398 user-selected SPRITES option). */
unsigned char g_mp_sprite_overflow_mode;

/* One walk of the canonical chain 0->1->...->n-1->0 with conservative 8-line
 * band charges (same packed lanes as the exact path: 16 sprites low byte, 32
 * cells high byte; a set high bit = the band could overflow). Returns
 * 0 = every band fits, 1 = only a dynamic sprite (slot >= ENEMY_FIRST) can
 * overflow, 2 = the fixed prefix can overflow or the chain is not canonical
 * (caller keeps WHOLE). *length = chain length when the result is 0 or 1.
 * Only slots below `charged` are charged (NES needs the prefix only).
 * Common case first, as walk_contiguous: a 16-line sprite fully on screen. */
static unsigned char overflow_walk(unsigned char *length, unsigned char charged)
{
    uint16_t band[28];
    const render_sprite_entry_t *e = g_render_sat_cache;
    unsigned char slot = 0u, state = 0u, i;
    for (i = 0u; i < 28u; ++i) band[i] = 0x2010u;
    for (;;) {
        unsigned char link;
        if (state == 0u && slot < charged) {
            const unsigned short first16 = (unsigned short)(((unsigned short)e->y & 511u) - 128u);
            const unsigned char size = e->size;
            unsigned char b, end;
            if (first16 <= 208u && (size & 3u) == 1u) {
                b = (unsigned char)(first16 >> 3);
                end = (unsigned char)((first16 + 15u) >> 3);
            } else {
                short first = (short)first16;
                short last = (short)(first + ((size & 3u) + 1u) * 8u);
                if (last <= 0 || first >= 224) goto next;
                if (first < 0) first = 0;
                if (last > 224) last = 224;
                b = (unsigned char)(first >> 3);
                end = (unsigned char)((last - 1) >> 3);
            }
            {
                const uint16_t charge = (uint16_t)(0x0101u + ((size & 12u) << 6));
                do {
                    const uint16_t value = (uint16_t)(band[b] - charge);
                    if (value & 0x8080u) {
                        if (slot < ROOMROM_SPRITE_SLOT_ENEMY_FIRST) return 2u;
                        state = 1u;
                        break;
                    }
                    band[b] = value;
                } while (++b <= end);
            }
        }
next:
        link = e->link;
        if (link != (unsigned char)(slot + 1u)) {
            if (link == 0u) break;
            return 2u;
        }
        if (slot == 63u) return 2u;
        ++slot; ++e;
    }
    *length = (unsigned char)(slot + 1u);
    return state;
}

/* Fixed slots keep their order; dynamic slots [FIRST,n) start at
 * FIRST + phase % count and wrap. Three links change. */
static void rotate_dynamic(unsigned char n, unsigned char phase)
{
    unsigned char count, start;
    if (n <= ROOMROM_SPRITE_SLOT_ENEMY_FIRST) return;
    count = (unsigned char)(n - ROOMROM_SPRITE_SLOT_ENEMY_FIRST);
    start = (unsigned char)(ROOMROM_SPRITE_SLOT_ENEMY_FIRST + phase % count);
    if (start == ROOMROM_SPRITE_SLOT_ENEMY_FIRST) return;
    g_render_sat_cache[ROOMROM_SPRITE_SLOT_ENEMY_FIRST - 1u].link = start;
    g_render_sat_cache[n - 1u].link = ROOMROM_SPRITE_SLOT_ENEMY_FIRST;
    g_render_sat_cache[start - 1u].link = 0u;
}

/* T-397 band ledger. A sweep that built the canonical chain (links slot + 1
 * up to a terminator) names its length; the limiter then charges the dynamic
 * slots in one linear pass, and the fixed prefix keeps a ledger compared
 * against the live SAT heads (y/size/link) every call, so only changed slots
 * are re-charged and writers outside the render adapter (pause restore,
 * relinks) are still seen. Per band the sprite byte and the cell byte only
 * fall, and stay negative once negative (<= 64 sprites), so "charge all,
 * test 0x8080" equals the walk's first-underflow test; prefix charges
 * precede dynamic ones in chain order (walk result 2). */
/* First/last 8-line band of a sprite (raw SAT y, size); 0 = no visible line. */
static inline unsigned char band_range(short ysat, unsigned char size,
                                       unsigned char *first_band, unsigned char *last_band)
{
    const unsigned short first16 = (unsigned short)(((unsigned short)ysat & 511u) - 128u);
    if (first16 <= 208u && (size & 3u) == 1u) {
        *first_band = (unsigned char)(first16 >> 3);
        *last_band = (unsigned char)((first16 + 15u) >> 3);
    } else {
        short first = (short)first16;
        short last = (short)(first + ((size & 3u) + 1u) * 8u);
        if (last <= 0 || first >= 224) return 0u;
        if (first < 0) first = 0;
        if (last > 224) last = 224;
        *first_band = (unsigned char)(first >> 3);
        *last_band = (unsigned char)((last - 1) >> 3);
    }
    return 1u;
}
static unsigned char s_chain_length;           /* 0 = no chain named this frame */
typedef struct { signed short y; unsigned char size, link; } PrefixHead;
_Static_assert(sizeof(PrefixHead) == 4u, "PrefixHead mirrors a SAT entry head");
/* y/size/link as one long (m68k: 2-byte aligned like the SAT entry). */
typedef uint32_t __attribute__((may_alias)) SatHeadWord;
static PrefixHead s_pre_head[ROOMROM_SPRITE_SLOT_ENEMY_FIRST];
/* Per band: 0x2010 (16 sprites, 32 cells) minus the prefix charges. */
static unsigned short s_pre_room[MP_SPRITE_BANDS];
static unsigned char s_pre_over;               /* bands whose room has 0x8080 */
static unsigned char s_pre_bad;                /* prefix links != slot + 1 */
static unsigned char s_pre_ready;

void mp_sprite_chain_built(unsigned char length)
{
    s_chain_length = length;
}

/* Add (or remove) one prefix head's links/bands to the ledger. */
static void prefix_charge(const PrefixHead *e, unsigned char slot, unsigned char add)
{
    unsigned char b, end;
    if (e->link != (unsigned char)(slot + 1u))
        s_pre_bad = (unsigned char)(add ? s_pre_bad + 1u : s_pre_bad - 1u);
    if (band_range(e->y, e->size, &b, &end)) {
        unsigned short charge = (unsigned short)(0x0101u + ((e->size & 12u) << 6));
        if (!add) charge = (unsigned short)(0u - charge);
        do {
            const unsigned short was = s_pre_room[b];
            const unsigned short now = (unsigned short)(was - charge);
            s_pre_room[b] = now;
            s_pre_over = (unsigned char)(s_pre_over + ((now & 0x8080u) != 0u) -
                                         ((was & 0x8080u) != 0u));
        } while (++b <= end);
    }
}

static void __attribute__((noinline)) prefix_swap(const render_sprite_entry_t *e,
                                                  unsigned char slot)
{
    PrefixHead *old = &s_pre_head[slot];
    prefix_charge(old, slot, 0u);
    old->y = e->y; old->size = e->size; old->link = e->link;
    prefix_charge(old, slot, 1u);
}

/* 0 = ledger valid (prefix canonical), else the caller walks. */
static unsigned char prefix_update(void)
{
    const render_sprite_entry_t *e = g_render_sat_cache;
    const SatHeadWord *h = (const SatHeadWord *)(const void *)s_pre_head;
    unsigned short left;
    if (!s_pre_ready) {
        unsigned char slot;
        for (slot = 0u; slot < MP_SPRITE_BANDS; ++slot) s_pre_room[slot] = 0x2010u;
        s_pre_over = 0u;
        s_pre_bad = 0u;
        for (slot = 0u; slot < ROOMROM_SPRITE_SLOT_ENEMY_FIRST; ++slot) {
            s_pre_head[slot].y = g_render_sat_cache[slot].y;
            s_pre_head[slot].size = g_render_sat_cache[slot].size;
            s_pre_head[slot].link = g_render_sat_cache[slot].link;
            prefix_charge(&s_pre_head[slot], slot, 1u);
        }
        s_pre_ready = 1u;
        return s_pre_bad;
    }
    /* Unrolled: one long compare per slot (displacement addressing). */
#pragma GCC unroll 29
    for (left = 0u; left < ROOMROM_SPRITE_SLOT_ENEMY_FIRST; ++left) {
        if (*(const SatHeadWord *)(const void *)&e[left] != h[left])
            prefix_swap(&e[left], (unsigned char)left);
    }
    return s_pre_bad;
}

/* The walk's result from the ledger plus the dynamic slots [FIRST, length):
 * 2 prefix, 1 dynamic, 0 fits; 3 = a dynamic link is not slot + 1 (the
 * last one 0), so the caller walks. Neighbours with the same y and size (enemy
 * pairs) charge as one group; each band's new room folds into one OR, so no
 * band pass follows (the sums are the walk's, modulo 2^16, and <= 64 sprites
 * keep a negative byte negative). */
static unsigned char bands_overflow(unsigned char length, unsigned char dynamic)
{
    unsigned short room[MP_SPRITE_BANDS];
    unsigned short all = 0u;
    if (s_pre_over) return 2u;
    if (!dynamic) {                     /* NES: links only (the walk checks them) */
        const render_sprite_entry_t *e = &g_render_sat_cache[ROOMROM_SPRITE_SLOT_ENEMY_FIRST];
        unsigned char next;
        for (next = ROOMROM_SPRITE_SLOT_ENEMY_FIRST + 1u; next <= length; ++next, ++e)
            if (e->link != (next < length ? next : 0u)) return 3u;
        return 0u;
    }
    __builtin_memcpy(room, s_pre_room, sizeof room);
    {
        const render_sprite_entry_t *e = &g_render_sat_cache[ROOMROM_SPRITE_SLOT_ENEMY_FIRST];
        const render_sprite_entry_t *const end = &g_render_sat_cache[length];
        unsigned char next = ROOMROM_SPRITE_SLOT_ENEMY_FIRST + 1u;
        while (e < end) {
            const signed short y = e->y;
            const unsigned char size = e->size;
            const unsigned short one = (unsigned short)(0x0101u + ((size & 12u) << 6));
            const unsigned short first16 = (unsigned short)(((unsigned short)y & 511u) - 128u);
            unsigned short charge = one;
            for (;;) {
                /* Named chains are canonical by contract; verify it. */
                if (e->link != (next < length ? next : 0u)) return 3u;
                ++e; ++next;
                if (e >= end || e->y != y || e->size != size) break;
                charge = (unsigned short)(charge + one);
            }
            if (size == 1u && first16 <= 208u) {
                /* Unclipped 8x16: two bands, three when not aligned. */
                unsigned short *u = &room[first16 >> 3];
                all |= (u[0] = (unsigned short)(u[0] - charge));
                all |= (u[1] = (unsigned short)(u[1] - charge));
                if (first16 & 7u) all |= (u[2] = (unsigned short)(u[2] - charge));
            } else {
                unsigned char band, last;
                if (band_range(y, size, &band, &last)) {
                    do all |= (room[band] = (unsigned short)(room[band] - charge));
                    while (++band <= last);
                }
            }
        }
    }
    return (unsigned char)((all & 0x8080u) ? 1u : 0u);
}

void mp_sprite_limit_lines(unsigned char players, unsigned char phase,
                           unsigned char captured_hand)
{
    unsigned char order[64], n = 0u, narrow, i;
    const unsigned char chain = s_chain_length;
    LineUsage usage;
    s_chain_length = 0u;
    g_mp_sprite_line_dropped = 0u;
    if (players <= 1u) return;
    if (g_mp_sprite_overflow_mode != MP_SPRITE_MODE_WHOLE &&
        captured_hand < ROOMROM_SPRITE_SLOT_ENEMY_FIRST) {
        unsigned char length, overflow;
        overflow = 3u;
        if (chain > ROOMROM_SPRITE_SLOT_ENEMY_FIRST && prefix_update() == 0u) {
            length = chain;
            overflow = bands_overflow(chain,
                (unsigned char)(g_mp_sprite_overflow_mode == MP_SPRITE_MODE_SMART));
        }
        if (overflow == 3u) {
            overflow = overflow_walk(&length,
                g_mp_sprite_overflow_mode == MP_SPRITE_MODE_SMART ? 64u : ROOMROM_SPRITE_SLOT_ENEMY_FIRST);
        }
        if (overflow != 2u) {
            if (g_mp_sprite_overflow_mode == MP_SPRITE_MODE_SMART && overflow == 0u) return;
            /* Rotation only reorders dynamic slots behind the fixed prefix,
             * whose bands fit: protected bodies keep every line. */
            rotate_dynamic(length, phase);
            return;
        }
    }
    /* T-394: the all-fit proofs only shortcut a result the exact pass also
     * produces (every sprite kept, chain rebuilt in traversal order). While
     * the previous exact pass dropped a sprite they fail, so skip them until
     * an exact pass keeps everything. Output is identical either way. */
    if (captured_hand < ROOMROM_SPRITE_SLOT_ENEMY_FIRST) {
        const unsigned char shape =
            walk_contiguous(&usage, &n, &narrow, (unsigned char)!s_proof_skip);
        if (shape == 1u) return;
        if (shape == 2u) {
            if (narrow) limit_direct(n, phase, &usage);
            else {
                for (i = 0u; i < n; ++i) order[i] = i;
                limit_exact(order, n, &usage, phase, captured_hand);
            }
            s_proof_skip = (unsigned char)(g_mp_sprite_line_dropped != 0u);
            return;
        }
    }
    if (!s_proof_skip && coarse_lines_fit()) return;
    if (collect_lines(order, &n, &usage, (unsigned char)!s_proof_skip)) return;
    limit_exact(order, n, &usage, phase, captured_hand);
    s_proof_skip = (unsigned char)(g_mp_sprite_line_dropped != 0u);
}

void mp_sprite_submit(unsigned short token, short x, short y,
                       unsigned short size, unsigned short attr)
{
    unsigned short index;
    render_sprite_entry_t *entry;
    unsigned long bit;
    if (token < ROOMROM_MP_WEAPON_TOKEN_BASE ||
        token >= ROOMROM_MP_WEAPON_TOKEN_BASE + ROOMROM_MP_WEAPON_TOKEN_COUNT) return;
    index = (unsigned short)(token - ROOMROM_MP_WEAPON_TOKEN_BASE);
    bit = 1ul << index;
    if (x == -32 && y == -32) {
        s_mp_weapon_visible &= ~bit;
        return;
    }
    entry = &s_mp_weapon_sprites[index];
    entry->x = (short)(x + 128);
    entry->y = (short)(y + ROOMROM_PLAY_SPRITE_DY + 128);
    entry->size = (unsigned char)size;
    entry->attribut = attr;
    entry->link = 0u; /* Hardware chain is assigned only during composition. */
    s_mp_weapon_visible |= bit;
}

void mp_sprite_clear_from(unsigned char player)
{
    unsigned char i;
    if (s_mp_weapon_visible == 0ul) return;
    if (player < 1u) player = 1u;
    for (i = player; i < 4u; ++i) {
        const unsigned char shift = (unsigned char)((i - 1u) * ROOMROM_MP_WEAPON_FAMILIES);
        s_mp_weapon_visible &= ~(0x3FFul << shift);
    }
}

void mp_sprite_clear_beam(unsigned char player)
{
    if (player < 1u || player >= 4u) return;
    s_mp_weapon_visible &= ~(0xFul << ((player - 1u) * ROOMROM_MP_WEAPON_FAMILIES));
}

unsigned short mp_sprite_emit(unsigned short first, unsigned short last,
                               unsigned char players)
{
    const render_sprite_entry_t *source = s_mp_weapon_sprites;
    unsigned long pending = s_mp_weapon_visible;
    g_mp_sprite_dropped = 0u;
    if (pending == 0ul) return first;
    if (last > ROOMROM_SPRITE_SLOT_LAST_H32) last = ROOMROM_SPRITE_SLOT_LAST_H32;
    if (players > 4u) players = 4u;
    if (players <= 1u) return first;
    /* T-395: records are owner-major, family-minor, so ascending bit order
     * is the original player/family order. Scan right with constant shifts
     * (the68000 pays per bit for variable long shifts) and skip empty
     * bytes; the mask keeps only owners1..players-1. */
    pending &= (1ul << ((players - 1u) * ROOMROM_MP_WEAPON_FAMILIES)) - 1ul;
    while (pending != 0ul) {
        if ((pending & 0xFFul) == 0ul) {
            pending >>= 8;
            source += 8;
            continue;
        }
        if (pending & 1ul) {
            if (first > last) ++g_mp_sprite_dropped;
            else {
                render_sprite_entry_t *target = &g_render_sat_cache[first];
                *target = *source;
                target->link = (unsigned char)(first < last ? first + 1u : 0u);
                ++first;
            }
        }
        pending >>= 1;
        ++source;
    }
    return first;
}
