/* GR_CHECK builds only (ZELDA_GR_CHECK=1). Slow path of GR_CHK (game_ram_check.h): called when an
 * indexed access leaves the Variables.inc extent of its macro base. Logs each DISTINCT
 * (macro base, extent_lo of the table the address reached) pair, so a loop sweeping a table is one
 * entry. Layout: [0] overflow flag, [1] count, then GR_CHECK_LOG_ENTRIES x {u16 base, u16 table_lo}. A full log sets the
 * flag; the scan (gr_check_scan.py) rejects a run that overflowed. */
#include "game_ram_check.h"
#include "game_ram_check_extents.h"

volatile unsigned char g_gr_check_log[2 + GR_CHECK_LOG_ENTRIES * 4];

static unsigned short c_base = 0xFFFFu, c_lo, c_hi;     /* last out-of-extent hit: sweeping one table costs 3 compares */

static unsigned short region_hi(unsigned short x)       /* extents.py REGIONS */
{
    if (x <= 0x07FFu) return 0x07FFu;
    if (x >= 0x6000u && x <= 0x7FFFu) return 0x7FFFu;
    return x;
}

static unsigned short first_label_gt(unsigned short x)   /* index of the first Variables.inc label > x */
{
    unsigned short lo = 0u, hi = CHK_NLABELS;
    while (lo < hi) {
        unsigned short mid = (unsigned short)((lo + hi) >> 1);
        if (k_chk_labels[mid] <= x) lo = (unsigned short)(mid + 1u); else hi = mid;
    }
    return lo;
}

unsigned short gr_chk(unsigned short base, unsigned short a)
{
    unsigned short lo, hi, i, tlo, thi, nxt;
    unsigned char n, k;
    if (base == c_base && a >= c_lo && a <= c_hi) return a;
    lo = 0u; hi = CHK_N;                                  /* k_chk_base[] sorted ascending (extents.py) */
    while (lo < hi) {
        unsigned short mid = (unsigned short)((lo + hi) >> 1);
        if (k_chk_base[mid] < base) lo = (unsigned short)(mid + 1u); else hi = mid;
    }
    i = lo;
    if (i < CHK_N && k_chk_base[i] == base && a >= k_chk_lo[i] && a <= k_chk_hi[i]) return a;
    i = first_label_gt(a);                                /* extents.extent(a) */
    tlo = i ? k_chk_labels[i - 1u] : a;
    if (region_hi(tlo) != region_hi(a)) tlo = a;
    nxt = first_label_gt(tlo);
    thi = region_hi(tlo);
    if (nxt < CHK_NLABELS && (unsigned short)(k_chk_labels[nxt] - 1u) < thi) thi = (unsigned short)(k_chk_labels[nxt] - 1u);
    if (thi < a) thi = a;
    c_base = base; c_lo = tlo; c_hi = thi;
    n = g_gr_check_log[1];
    for (k = 0u; k < n; ++k) {
        volatile unsigned char *e = &g_gr_check_log[2u + k * 4u];
        if ((unsigned short)((e[0] << 8) | e[1]) == base && (unsigned short)((e[2] << 8) | e[3]) == tlo) return a;   /* dedupe */
    }
    if (n >= GR_CHECK_LOG_ENTRIES) { g_gr_check_log[0] = 1u; return a; }                                                              /* overflow */
    {
        volatile unsigned char *e = &g_gr_check_log[2u + n * 4u];
        e[0] = (unsigned char)(base >> 8); e[1] = (unsigned char)base;
        e[2] = (unsigned char)(tlo >> 8);  e[3] = (unsigned char)tlo;
    }
    g_gr_check_log[1] = (unsigned char)(n + 1u);
    return a;
}
