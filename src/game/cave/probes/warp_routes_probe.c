/* Phase E warp-routes probe — see warp_routes_probe.h header.
 *
 * NES authority: reference/aldonunez/Z_05.asm:7313 HandleWarpOW
 * dispatch. Oracle: tools/parity/warp_routes_expected.json.
 */

#include "warp_routes_probe.h"
#include "../cave_entrance.h"
#include "../../world/ow_meta.h"

/* Diagnostic: direct read of the blob to bypass ow_meta. If both reads
 * agree, ow_meta is correct; if they disagree, ow_meta has a bug.
 *
 * Phase E investigation 2026-05-24: 22-room cutoff observed (rooms
 * $00..$15 OK, $16+ all zero). Comparing both reads pinpoints the bug
 * location. */
extern const unsigned char rooms_overworld[];

void warp_routes_probe_run(void)
{
    volatile unsigned char *block =
        (volatile unsigned char *)WARP_ROUTES_PROBE_BASE;
    unsigned int room;
    unsigned char no_warp_count = 0u;
    unsigned char dungeon_count = 0u;
    unsigned char cave_count    = 0u;

    /* Phase E fix 2026-05-24: write magic AT THE END so Lua poll
     * loop can't break mid-iteration. Pre-magic init clears the
     * magic bytes to ensure Lua sees old/stale magic as not-yet-
     * published (block stays at boot-zero state until probe completes). */
    block[0] = 0u;
    block[1] = 0u;
    block[2] = 0u;
    block[3] = 0u;
    block[4] = 0u;
    block[5] = 0u;
    block[6] = 0u;
    block[7] = 0u;

    for (room = 0u; room < WARP_ROUTES_PROBE_ROOMS; ++room) {
        /* Diagnostic: direct blob read at hardcoded offset 0x80 (= NES
         * LevelBlockAttrsB sub-table base). Compare against
         * ow_meta_attr_b to isolate the bug. */
        unsigned char attr_b_direct = rooms_overworld[0x80u + room];
        unsigned char attr_b =
            roomrom_ow_meta_attr_b((unsigned char)room);
        unsigned char selector = (unsigned char)(attr_b & 0xFCu);
        unsigned char cid =
            (unsigned char)cave_entrance_check(0x24u, (unsigned char)room);
        block[WARP_ROUTES_PROBE_RESULTS_OFFSET + room] = cid;
        /* Pack diagnostic into one byte by XOR (or-of-equal-bytes ==
         * 0). If both reads return the same byte, this slot mirrors
         * attr_b. If they differ, the slot stores attr_b_direct ^
         * attr_b (non-zero diff). */
        block[WARP_ROUTES_PROBE_ATTR_B_OFFSET + room] =
            (unsigned char)(attr_b ^ attr_b_direct);
        /* Stash attr_b_direct in a third 128-byte page so the differ
         * can read both: layout becomes
         *   [8..135]   cid
         *   [136..263] XOR(direct, ow_meta)  (0 = agree)
         *   [264..391] direct (raw blob byte) */
        block[264u + room] = attr_b_direct;
        if (cid != 0u) {
            ++cave_count;
        } else if (selector != 0u && selector < 0x40u) {
            ++dungeon_count;
        } else {
            ++no_warp_count;
        }
    }

    block[3] = no_warp_count;
    block[4] = dungeon_count;
    block[5] = cave_count;
    block[6] = 0u;
    block[7] = 0u;

    /* Publish magic LAST — Lua waits for this. */
    block[2] = WARP_ROUTES_PROBE_VERSION;
    block[1] = 0x52u;                       /* 'R' */
    block[0] = 0x57u;                       /* 'W' */
}
