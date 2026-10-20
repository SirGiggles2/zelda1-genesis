/* hud_dispatch.c — native HUD subsystem dispatch (Phase 4).
 *
 * Drain MATCH (verified-by-use). Pure C; calls native cave price
 * formatter (cave_format_decimal_byte) and native core helpers.
 */

#include "hud_dispatch.h"
#include "../../state/game_profile.h"
#include <stdint.h>
#include "platform_abi.h"
#include "world_state.h"       /* TRANSFER_BUF_BYTE, FRAME_COUNTER */
#include "combat_state.h"      /* LINK_HEARTS, LINK_PARTIAL_HEART */
#include "item_state.h"        /* LINK_BOMB_COUNT, INVENTORY_VALUE */
#include "cave_state.h"        /* LINK_RUPEES, CAVE_DOOR_REPAIR_RUPEE_DELTA */
#include "room_state.h"        /* ROOM_TRANSFER_BUF_SELECT */
#include "cave/cave_dispatch.h"   /* cave_format_decimal_byte */
#include "core/core_dispatch.h"   /* core_format_char_doublet */

/* NES Z_01.asm StatusBarTransferBufTemplate (line 2804) — 41 bytes.
 * src/data/ui_layout.inc:18. */
static const unsigned char k_status_bar_template[41] __attribute__((aligned(4))) = {
    0x20u, 0xB6u, 0x08u, 0x24u, 0x24u, 0x24u, 0x24u, 0x24u,
    0x24u, 0x24u, 0x24u, 0x20u, 0xD6u, 0x08u, 0x24u, 0x24u,
    0x24u, 0x24u, 0x24u, 0x24u, 0x24u, 0x24u, 0x20u, 0x6Cu,
    0x03u, 0x21u, 0x00u, 0x24u, 0x20u, 0xACu, 0x03u, 0x21u,
    0x00u, 0x24u, 0x20u, 0xCCu, 0x03u, 0x21u, 0x00u, 0x24u,
    0xFFu
};

/* NES source: pinned Redux ROM $16C80 / rupee.asm:UpdateRupee.
 * Drained C: hud_format_status_bar_text below.
 * Coverage: FULL 62-byte preset; Stance: EXTEND native transfer owner.
 * The installed ROM relocates hearts and swaps keys/arrows relative to
 * the source file's earlier preset. ROM bytes are the authority. */
static const unsigned char k_redux_status_bar_template[62] __attribute__((aligned(4))) = {
    0x20,0xB8,0x08,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,
    0x20,0xD8,0x08,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,
    0x20,0x6C,0x03,0x62,0x00,0x24,
    0x20,0x8C,0x03,0x62,0x64,0x24,
    0x20,0xCC,0x03,0x62,0x03,0x00,
    0x20,0xAC,0x03,0x62,0x00,0x24,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
};

/* NES source: Z_01.asm:FormatHeartsInTextBuf; Redux automap.asm:PartialHeartRoutine.
 * Drained C: hud_format_hearts_in_text_buf below.
 * Coverage: FULL heart tile choice; Stance: EXTEND (shared pure helper).
 * HeartValues encodes capacity minus one and whole hearts; the fractional
 * Original displays full at $80, half below it, empty at zero.
 * Redux's pinned routine uses $50..$52 below $C0, full at $C0. */
/* T-172 status-bar memo (see hud_format_status_bar_text). */
#define FMT_INPUTS 9u
#define FMT_SCRATCH 9u
static unsigned char s_fmt_cleared_529;
static unsigned char s_fmt_len;                 /* 0 = no memo */
static unsigned char s_fmt_gen;
static unsigned char s_fmt_in[FMT_INPUTS];
static unsigned char s_fmt_scratch[FMT_SCRATCH];
static unsigned char s_fmt_clear;
static unsigned short s_fmt_out[31];            /* 62 bytes, word aligned */
/* T-172: ascending indices of the bytes that differ between output
 * generation s_fmt_delta_from and the next one; FMT_DELTA_NONE when the
 * lengths differ or more than FMT_DELTA_MAX bytes changed. */
#define FMT_DELTA_MAX 12u
#define FMT_DELTA_NONE 0xFFu
static unsigned char s_fmt_delta[FMT_DELTA_MAX];
static unsigned char s_fmt_delta_count = FMT_DELTA_NONE;
static unsigned char s_fmt_delta_from;
static const unsigned short k_fmt_scratch_addr[FMT_SCRATCH] = {
    0x0000u, 0x0001u, 0x0002u, 0x0003u, 0x000Bu, 0x000Cu, 0x000Du, 0x000Eu, 0x000Fu
};

unsigned char hud_heart_tile(unsigned char hearts, unsigned char partial,
                             unsigned char index)
{
    unsigned char whole = (unsigned char)(hearts & 15u);
    if (!hearts || index > (hearts >> 4) || index >= 16u) return 0x24u;
    if (index < whole) return 0xF2u;
    if (index > whole || !partial) return 0x66u;
    if (game_profile_is_remix())
        return partial >= 0xC0u ? 0xF2u : (unsigned char)(0x50u + (partial >> 6));
    return partial >= 0x80u ? 0xF2u : 0x65u;
}

void hud_format_hearts_in_text_buf(unsigned char start_off)
{
    /* drain at hud_runtime.c:6-50. NES FormatHeartsInTextBuf. */
    RAM(0x000Du) = start_off;
    const unsigned char hearts = (unsigned char)RAM(0x000Eu);
    /* Pinned Redux FormatHeartsInTextBuf exchanges the two row offsets so
     * occupied hearts fill the top row. Original retains its order. */
    const unsigned char remix = game_profile_is_remix();
    RAM(0x000Bu) = (uint8_t)(start_off + (remix ? 18u : 7u));
    unsigned char row_pos = 7u;
    for (unsigned char slot = 0u; slot < 16u; ++slot) {
        unsigned char tile;
        if (row_pos == 0xFFu) {
            RAM(0x000Bu) = (uint8_t)(RAM(0x000Du) + (remix ? 7u : 18u));
            row_pos = 18u;
        }
        tile = hud_heart_tile(hearts, (unsigned char)RAM(0x000Fu),
                              (unsigned char)(15u - slot));
        /* NES @CheckPartial: a partial glyph clears $0529 (the sword-shot
         * allowance MakeSwordShot reads). T-171 asm_equiv: the drain
         * cleared CurRoomHistoryIndex $0620 instead. */
        if (tile == 0x65u || (game_profile_is_remix() && tile >= 0x50u && tile <= 0x52u)) {
            RAM(0x0529u) = 0u;
            s_fmt_cleared_529 = 1u;
        }
        RAM(0x000Cu) = row_pos;
        TRANSFER_BUF_BYTE(RAM(0x000Bu)) = tile;
        RAM(0x000Bu) = (uint8_t)(RAM(0x000Bu) - 1u);
        row_pos = (uint8_t)(RAM(0x000Cu) - 1u);
    }
}

void hud_copy_triplet_to_text_buf(void)
{
    /* drain at hud_runtime.c:52-57. */
    const unsigned char base_off = (unsigned char)RAM(0x0000u);
    TRANSFER_BUF_BYTE(base_off) = (uint8_t)RAM(0x0003u);
    TRANSFER_BUF_BYTE((unsigned char)(base_off - 1u)) = (uint8_t)RAM(0x0002u);
    TRANSFER_BUF_BYTE((unsigned char)(base_off - 2u)) = (uint8_t)RAM(0x0001u);
}

void hud_format_decimal_count_byte(unsigned char val)
{
    /* drain at hud_runtime.c:59-68. NES FormatDecimalCountByte. */
    cave_format_decimal_byte(val);
    unsigned char hundreds = (unsigned char)RAM(0x0001u);
    if (hundreds == 0x24u) {
        hundreds = game_profile_is_remix() ? 0x62u : 33u;
    }
    RAM(0x0001u) = hundreds;
    if (RAM(0x0002u) == 0x24u) {
        core_format_char_doublet(RAM(0x0003u));
    }
}

void hud_format_decimal_count_byte_in_text_buf(unsigned char val,
                                                unsigned char buf_offset)
{
    /* drain at hud_runtime.c:70-74. */
    RAM(0x0000u) = buf_offset;
    hud_format_decimal_count_byte(val);
    hud_copy_triplet_to_text_buf();
}

/* NES source: Z_01.asm:FormatHeartsInTextBuf (Redux row order).
 * Drained C: hud_format_hearts_in_text_buf above (kept for other callers).
 * Coverage: FULL; Stance: EXTEND (same buffer bytes, same final $0B/$0C/$0D,
 * same $0529 clear; intermediate scratch values are never observed).
 * T-172: the per-slot RAM round trips and profile calls cost ~1/3 of the
 * formatter on heart-change ticks. Host test: tools/build/test_t172_hud_format.c. */
static void format_hearts_fast(unsigned char start_off, unsigned char remix)
{
    const unsigned char hearts = (unsigned char)RAM(0x000Eu);
    const unsigned char partial = (unsigned char)RAM(0x000Fu);
    const unsigned char whole = (unsigned char)(hearts & 15u);
    const unsigned char cap = (unsigned char)(hearts >> 4);
    unsigned char b = (unsigned char)(start_off + (remix ? 18u : 7u));
    unsigned char row_pos = 7u, last_row = 7u, cleared = 0u, slot;
    for (slot = 0u; slot < 16u; ++slot) {
        const unsigned char index = (unsigned char)(15u - slot);
        unsigned char tile;
        if (row_pos == 0xFFu) {
            b = (unsigned char)(start_off + (remix ? 7u : 18u));
            row_pos = 18u;
        }
        if (!hearts || index > cap) tile = 0x24u;
        else if (index < whole) tile = 0xF2u;
        else if (index > whole || !partial) tile = 0x66u;
        else if (remix) {
            tile = partial >= 0xC0u ? 0xF2u : (unsigned char)(0x50u + (partial >> 6));
            if (tile != 0xF2u) cleared = 1u;
        } else {
            tile = partial >= 0x80u ? 0xF2u : 0x65u;
            if (tile == 0x65u) cleared = 1u;
        }
        TRANSFER_BUF_BYTE(b) = tile;
        last_row = row_pos;
        --b;
        --row_pos;
    }
    RAM(0x000Du) = start_off;
    RAM(0x000Bu) = b;
    RAM(0x000Cu) = last_row;
    if (cleared) {
        RAM(0x0529u) = 0u;
        s_fmt_cleared_529 = 1u;
    }
}

/* FormatStatusBarText in two independent halves (T-172 split memo):
 * hearts: reads LINK_HEARTS/LINK_PARTIAL_HEART, writes the hearts payload,
 * $0B-$0F and (partial glyph) $0529; counts: reads $0664/$066E/$0658 and the
 * rupee cells, writes the rupee/key/bomb payload and $00-$03. Neither half
 * reads what the other writes, and their buffer bytes are disjoint. */
static void format_hearts_part(unsigned char remix)
{
    RAM(0x000Eu) = (uint8_t)LINK_HEARTS;
    RAM(0x000Fu) = (uint8_t)LINK_PARTIAL_HEART;
    format_hearts_fast(3u, remix);
}

static void format_counts_part(unsigned char remix)
{
    if (remix) {
        /* UpdateRupee publishes the authoritative decimal currency. */
        TRANSFER_BUF_BYTE(25u) = RAM(0x067Bu) ? RAM(0x067Bu) : 0x62u;
        TRANSFER_BUF_BYTE(26u) = RAM(0x067Au) >> 4;
        TRANSFER_BUF_BYTE(27u) = (RAM(0x067Au) || RAM(0x067Bu))
            ? RAM(0x067Au) & 15u : 0x24u;
    }
    hud_format_decimal_count_byte_in_text_buf(
        remix ? RAM(0x0677u) : LINK_RUPEES, remix ? 45u : 27u);
    if (RAM(0x0664u) != 0u) {
        RAM(0x0000u) = 33u;
        RAM(0x0001u) = remix ? 0x62u : 33u;
        core_format_char_doublet(remix ? 0x64u : 10u);
        hud_copy_triplet_to_text_buf();
    } else {
        hud_format_decimal_count_byte_in_text_buf((unsigned char)RAM(0x066Eu), 33u);
    }
    hud_format_decimal_count_byte_in_text_buf(LINK_BOMB_COUNT, 39u);
}

static void format_status_bar_full(void)
{
    /* drain at hud_runtime.c:76-93. NES FormatStatusBarText. */
    const unsigned char remix = game_profile_is_remix();
    const unsigned char length = remix ? 62u : 41u;
    {
        /* Templates and DynTileBuf ($FF8302) are word aligned. */
        const unsigned long *src = (const unsigned long *)(remix ? k_redux_status_bar_template
                                                                 : k_status_bar_template);
        unsigned long *dst = (unsigned long *)&TRANSFER_BUF_BYTE(0);
        unsigned char n = (unsigned char)(length >> 2), i;
        while (n--) *dst++ = *src++;
        for (i = (unsigned char)(length & ~3u); i < length; ++i)
            TRANSFER_BUF_BYTE(i) = remix ? k_redux_status_bar_template[i]
                                         : k_status_bar_template[i];
    }
    format_hearts_part(remix);
    format_counts_part(remix);
}

/* NES source: Z_01.asm:FormatStatusBarText (Redux rupee.asm preset).
 * Drained C: format_status_bar_full above (unchanged formatter).
 * Coverage: FULL; Stance: EXTEND (exact memo of a pure function).
 * The formatter reads only the nine cells below plus the profile and writes
 * only DynTileBuf[0..len), scratch $00-$03/$0B-$0F and (partial heart)
 * $0529 = 0. NES reformats every even frame although the text rarely
 * changes (T-172 room $38: ~62k master clocks per call). Equal inputs
 * replay the recorded writes byte for byte, including scratch finals. */
void hud_format_status_bar_text(void)
{
    const unsigned char remix = game_profile_is_remix();
    unsigned char in[FMT_INPUTS];
    unsigned char i;
    /* Words of the output that can differ from the memo: all for a full
     * format; only the recomputed half's words after a partial replay
     * (hearts payload bytes 3-21, counts payload bytes 25-45; both layouts). */
    unsigned char w_lo = 0u, w_hi = (unsigned char)((remix ? 62u : 41u) >> 1), tail = 1u;
    in[0] = remix;
    in[1] = (unsigned char)LINK_HEARTS;
    in[2] = (unsigned char)LINK_PARTIAL_HEART;
    in[3] = (unsigned char)RAM(0x0664u);
    in[4] = (unsigned char)RAM(0x066Eu);
    in[5] = (unsigned char)LINK_BOMB_COUNT;
    in[6] = remix ? (unsigned char)RAM(0x0677u) : (unsigned char)LINK_RUPEES;
    in[7] = remix ? (unsigned char)RAM(0x067Au) : 0u;
    in[8] = remix ? (unsigned char)RAM(0x067Bu) : 0u;
    if (s_fmt_len != 0u && in[0] == s_fmt_in[0]) {
        const unsigned char hearts_same = (unsigned char)(in[1] == s_fmt_in[1] && in[2] == s_fmt_in[2]);
        unsigned char counts_same = 1u;
        for (i = 3u; i < FMT_INPUTS; ++i) if (in[i] != s_fmt_in[i]) counts_same = 0u;
        if (hearts_same || counts_same) {
            /* Replay the memo (DynTileBuf $FF8302 and the memo are word
             * aligned; 62/41 bytes copy as longs plus a tail), then rerun
             * only the half whose inputs changed. */
            const unsigned long *src = (const unsigned long *)s_fmt_out;
            unsigned long *dst = (unsigned long *)&TRANSFER_BUF_BYTE(0);
            unsigned char n = (unsigned char)(s_fmt_len >> 2);
            while (n--) *dst++ = *src++;
            for (i = (unsigned char)(s_fmt_len & ~3u); i < s_fmt_len; ++i)
                TRANSFER_BUF_BYTE(i) = ((const unsigned char *)s_fmt_out)[i];
            for (i = 0u; i < FMT_SCRATCH; ++i) RAM(k_fmt_scratch_addr[i]) = s_fmt_scratch[i];
            if (hearts_same && s_fmt_clear) RAM(0x0529u) = 0u;   /* hearts would clear it again */
            if (hearts_same && counts_same) return;
            if (!hearts_same) {
                s_fmt_cleared_529 = 0u;
                format_hearts_part(remix);
                w_lo = 1u; w_hi = 11u;                       /* bytes 2..21 */
            } else {
                s_fmt_cleared_529 = s_fmt_clear;
                format_counts_part(remix);
                w_lo = 12u; if (w_hi > 23u) w_hi = 23u;      /* bytes 24..45 */
            }
            tail = 0u;
            goto record;
        }
    }
    s_fmt_cleared_529 = 0u;
    format_status_bar_full();
record:
    {
        unsigned char *out = (unsigned char *)s_fmt_out;
        const unsigned char len = remix ? 62u : 41u;
        const unsigned short *nw = (const unsigned short *)&TRANSFER_BUF_BYTE(0) + w_lo;
        unsigned short *ow = s_fmt_out + w_lo;
        unsigned char count = (s_fmt_len == len) ? 0u : FMT_DELTA_NONE;
        s_fmt_delta_from = s_fmt_gen;
        /* Word compare (both word aligned); bytes only where a word differs. */
        for (i = w_lo; i < w_hi; ++i, ++nw, ++ow) {
            const unsigned short v = *nw;
            if (*ow == v) continue;
            if (count != FMT_DELTA_NONE) {
                /* Byte compare inside the differing word (byte order neutral). */
                const unsigned char k = (unsigned char)(i << 1);
                const unsigned char *ob = (const unsigned char *)ow;
                const unsigned char *nb = (const unsigned char *)nw;
                if (ob[0] != nb[0]) {
                    if (count < FMT_DELTA_MAX) s_fmt_delta[count++] = k;
                    else count = FMT_DELTA_NONE;
                }
                if (count != FMT_DELTA_NONE && ob[1] != nb[1]) {
                    if (count < FMT_DELTA_MAX) s_fmt_delta[count++] = (unsigned char)(k + 1u);
                    else count = FMT_DELTA_NONE;
                }
            }
            *ow = v;
        }
        if ((len & 1u) && tail) {
            const unsigned char v = (unsigned char)TRANSFER_BUF_BYTE(len - 1u);
            if (out[len - 1u] != v && count != FMT_DELTA_NONE) {
                if (count < FMT_DELTA_MAX) s_fmt_delta[count++] = (unsigned char)(len - 1u);
                else count = FMT_DELTA_NONE;
            }
            out[len - 1u] = v;
        }
        s_fmt_delta_count = count;
        s_fmt_len = len;
        for (i = 0u; i < FMT_SCRATCH; ++i) s_fmt_scratch[i] = (unsigned char)RAM(k_fmt_scratch_addr[i]);
        for (i = 0u; i < FMT_INPUTS; ++i) s_fmt_in[i] = in[i];
        s_fmt_clear = s_fmt_cleared_529;
        ++s_fmt_gen;
    }
}

const unsigned char *hud_status_bar_delta(unsigned char *from_gen, unsigned char *count)
{
    *from_gen = s_fmt_delta_from;
    *count = s_fmt_delta_count;
    return s_fmt_delta_count != FMT_DELTA_NONE ? s_fmt_delta : 0;
}

const unsigned char *hud_status_bar_output(unsigned char *gen)
{
    *gen = s_fmt_gen;
    return s_fmt_len ? (const unsigned char *)s_fmt_out : 0;
}

void hud_world_change_rupees(void)
{
    /* drain at hud_runtime.c:95-122. NES WorldChangeRupees. */
    if (ROOM_TRANSFER_BUF_SELECT != 0u) {
        return;
    }
    if (!(TRANSFER_BUF_BYTE(0) & 0x80u)) {
        return;
    }
    hud_tick_native_rupees(FRAME_COUNTER);
    if (!(FRAME_COUNTER & 1u)) hud_format_status_bar_text();
}

/* NES source: Z_01.asm:World_ChangeRupees.
 * Drained C: hud_world_change_rupees. Coverage: FULL currency mutation.
 * Stance: EXTEND; share state logic without NES PPU transfer ownership.
 * The native renderer owns its own display transfers. */
void hud_tick_native_rupees(unsigned char frame_counter)
{
    /* NES source: Redux rupee.asm:AddingRupee / Substrrupee / GenerateHexValue.
     * Drained C: hud_tick_native_rupees (Original state owner below).
     * Coverage: PARTIAL (currency mutation; extension migration remains T-291).
     * Stance: EXTEND the same pending-transaction owner per saved profile. */
    if (game_profile_is_remix()) {
        const unsigned char decimal = RAM(0x067Au);
        const unsigned char tens = (unsigned char)(decimal >> 4);
        const unsigned char ones = (unsigned char)(decimal & 0x0Fu);
        const unsigned char hundreds = (unsigned char)RAM(0x067Bu);
        /* No pending credit/debit: the full path would convert the BCD
         * cells to binary, divide by 100/10, write the same cells back and
         * project the same LINK_RUPEES. Project directly from valid BCD and
         * skip two 68K divides per play tick. Invalid cells still take the
         * normalising path below. */
        if (RAM(0x067Du) == 0u && RAM(0x067Eu) == 0u &&
            hundreds <= 9u && tens <= 9u && ones <= 9u) {
            const unsigned short projection = (unsigned short)
                (hundreds * 160u + tens * 10u + ones);
            LINK_RUPEES = (unsigned char)(projection > 255u ? 255u : projection);
            return;
        }
        unsigned short value = (unsigned short)(hundreds * 100u +
            tens * 10u + ones);
        /* Both branches precede FrameCheck in the pinned source. */
        if (RAM(0x067Du)) {
            RAM(0x067Du)--;
            if (value < 999u) ++value;
            RAM(0x0604u) = 16u;
        }
        if (RAM(0x067Eu)) {
            RAM(0x067Eu)--;
            if (value) --value;
            RAM(0x0604u) = 16u;
        }
        RAM(0x067Bu) = (unsigned char)(value / 100u);
        value %= 100u;
        RAM(0x067Au) = (unsigned char)((value / 10u) * 16u + value % 10u);
        /* GenerateHexValue indexes TrueTableHex10 with (hundreds<<4)|tens.
         * The pinned table advances ten per index, then saturates at $FF.
         * Preserve this compatibility projection (100 maps to160), while
         * the decimal cells above remain the actual saved currency. */
        value = (unsigned short)(RAM(0x067Bu) * 160u + value);
        LINK_RUPEES = (unsigned char)(value > 255u ? 255u : value);
        return;
    }
    const unsigned char rupees = (unsigned char)LINK_RUPEES;
    if (rupees == 0u) {
        INVENTORY_VALUE(39) = 0u;
    } else if (rupees == 0xFFu) {
        INVENTORY_VALUE(38) = 0u;
    }
    if (frame_counter & 1u) {
        return;
    }
    if (RAM(0x067Du) != 0u) {
        RAM(0x067Du) = (uint8_t)(RAM(0x067Du) - 1u);
        LINK_RUPEES = (uint8_t)(LINK_RUPEES + 1u);
        RAM(0x0604u) = 16u;  /* ROOM_SFX_MAIN */
    }
    if (CAVE_DOOR_REPAIR_RUPEE_DELTA == 0) {
        return;
    }
    CAVE_DOOR_REPAIR_RUPEE_DELTA = (int8_t)(CAVE_DOOR_REPAIR_RUPEE_DELTA - 1);
    LINK_RUPEES = (uint8_t)(LINK_RUPEES - 1u);
    RAM(0x0604u) = 16u;  /* ROOM_SFX_MAIN */
}

/* NES World_FillHearts (Z_05.asm), run every play frame from
 * UpdateHeartsAndRupees before World_ChangeRupees. Potions, fairies and
 * the pond fairy set World_IsFillingHearts ($63). */
void hud_world_fill_hearts(void)
{
    unsigned char hv;
    if (RAM(0x0063u) == 0u) return;                /* World_IsFillingHearts */
    RAM(0x0604u) = 0x10u;                          /* Tune0Request: heart */
    if ((unsigned char)RAM(0x0670u) < 0xF8u) {     /* HeartPartial */
        RAM(0x0670u) = (uint8_t)((unsigned char)RAM(0x0670u) + 6u);
        return;
    }
    RAM(0x0670u) = 0u;
    /* CompareHeartsToContainers: [00] = hearts, compare containers. */
    hv = (unsigned char)RAM(0x066Fu);              /* HeartValues */
    RAM(0x0000u) = (uint8_t)(hv & 0x0Fu);
    if ((unsigned char)(hv >> 4) != (unsigned char)(hv & 0x0Fu)) {
        RAM(0x066Fu) = (uint8_t)(hv + 1u);         /* INC HeartValues */
        return;
    }
    RAM(0x0670u) = 0xFFu;                          /* DEC from 0 */
    RAM(0x052Eu) = 0u;                             /* SwordBlocked */
    RAM(0x0063u) = 0u;
    RAM(0x00E0u) = 0u;                             /* Paused */
}
