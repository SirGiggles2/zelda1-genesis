/* Phase 9 Task 9.5 — HUD format probe (in-ROM tests). */

#include "hud_format_probe.h"
#include "../hud_dispatch.h"
#include "platform_abi.h"
#include "world_state.h"     /* TRANSFER_BUF_BYTE */
#include "combat_state.h"    /* LINK_HEARTS, LINK_PARTIAL_HEART */
#include "cave_state.h"      /* LINK_RUPEES, CAVE_DOOR_REPAIR_RUPEE_DELTA */
#include "item_state.h"      /* LINK_BOMB_COUNT */
#include "room_state.h"      /* ROOM_TRANSFER_BUF_SELECT, ROOM_SFX_MAIN */
#include "progress_state.h"  /* FRAME_COUNTER */

#define PROBE  ((volatile unsigned char *)HUD_FORMAT_PROBE_BASE)

#define TILE_SPC   0x24u
#define TILE_FULL  0xF2u
#define TILE_EMP   0x66u   /* 102 */
#define TILE_HALF  0x65u   /* 101 */

static void stamp_magic(void)
{
    unsigned int i;
    PROBE[0] = 'H';
    PROBE[1] = 'F';
    PROBE[2] = 0x04u;  /* v4 — adds inventory-edge writes */
    for (i = 3u; i < 16u; ++i) PROBE[i] = 0u;
}

static unsigned char buf_eq(unsigned char start, const unsigned char *expect,
                            unsigned char count)
{
    unsigned char i;
    for (i = 0u; i < count; ++i) {
        if ((unsigned char)TRANSFER_BUF_BYTE((unsigned char)(start + i))
            != expect[i]) {
            return 0u;
        }
    }
    return 1u;
}

static void format_with(unsigned char hearts, unsigned char partial)
{
    LINK_HEARTS = hearts;
    LINK_PARTIAL_HEART = partial;
    /* hud_format_status_bar_text re-reads LINK_HEARTS / LINK_PARTIAL_HEART
     * into RAM(0x000E/000F) before formatting. */
    hud_format_status_bar_text();
}

/* Format the full status bar with controlled rupee/bomb/key/master_key
 * cells. heart cells held at $33/0 so they don't dominate failures. */
static void format_with_counters(unsigned char rupees, unsigned char bombs,
                                  unsigned char keys, unsigned char master_key)
{
    LINK_HEARTS = 0x33u;
    LINK_PARTIAL_HEART = 0u;
    LINK_RUPEES = rupees;
    LINK_BOMB_COUNT = bombs;
    RAM(0x066Eu) = keys;
    RAM(0x0664u) = master_key;
    hud_format_status_bar_text();
}

/* ---- T0: template integrity ---------------------------------------- */
static unsigned char test_template_loaded(void)
{
    format_with(0x33u, 0u);
    return ((unsigned char)TRANSFER_BUF_BYTE(0u)  == 0x20u
         && (unsigned char)TRANSFER_BUF_BYTE(1u)  == 0xB6u
         && (unsigned char)TRANSFER_BUF_BYTE(2u)  == 0x08u
         && (unsigned char)TRANSFER_BUF_BYTE(11u) == 0x20u
         && (unsigned char)TRANSFER_BUF_BYTE(12u) == 0xD6u
         && (unsigned char)TRANSFER_BUF_BYTE(13u) == 0x08u
         && (unsigned char)TRANSFER_BUF_BYTE(40u) == 0xFFu) ? 1u : 0u;
}

/* ---- T1: hearts=$33 partial=0 -------------------------------------- *
 * full=3, containers=3, ts=12, te=12.
 * row1 (slots 0..7): all < 12 → SPC.        buf[3..10] = SPC×8
 * row2 (slots 8..15): 8..11 SPC; 12 partial=0 → EMP; 13..15 → FULL.
 *   slot N → buf[29-N]. slot 8→21, 15→14.
 *   buf[14..21] = [FULL, FULL, FULL, EMP, SPC, SPC, SPC, SPC]
 */
static unsigned char test_hearts_3_full(void)
{
    static const unsigned char row1[8] = {
        TILE_SPC, TILE_SPC, TILE_SPC, TILE_SPC,
        TILE_SPC, TILE_SPC, TILE_SPC, TILE_SPC
    };
    static const unsigned char row2[8] = {
        TILE_FULL, TILE_FULL, TILE_FULL, TILE_EMP,
        TILE_SPC,  TILE_SPC,  TILE_SPC,  TILE_SPC
    };
    format_with(0x33u, 0u);
    return (buf_eq(3u, row1, 8u) && buf_eq(14u, row2, 8u)) ? 1u : 0u;
}

/* ---- T2: hearts=$31 partial=0 -------------------------------------- *
 * full=1, containers=3, ts=12, te=14.
 * row1: all SPC.
 * row2: slot 8..11 SPC (<12); slot 12,13 EMP (<14); slot 14 EMP (==te,
 *       partial=0); slot 15 FULL (>14).
 *   buf[14..21] = [FULL, EMP, EMP, EMP, SPC, SPC, SPC, SPC]
 */
static unsigned char test_hearts_3_max_1_cur(void)
{
    static const unsigned char row1[8] = {
        TILE_SPC, TILE_SPC, TILE_SPC, TILE_SPC,
        TILE_SPC, TILE_SPC, TILE_SPC, TILE_SPC
    };
    static const unsigned char row2[8] = {
        TILE_FULL, TILE_EMP, TILE_EMP, TILE_EMP,
        TILE_SPC,  TILE_SPC, TILE_SPC, TILE_SPC
    };
    format_with(0x31u, 0u);
    return (buf_eq(3u, row1, 8u) && buf_eq(14u, row2, 8u)) ? 1u : 0u;
}

/* ---- T3: hearts=$83 partial=$80 ------------------------------------ *
 * full=3, containers=8, ts=7, te=12.
 * row1 (slots 0..7): 0..6 SPC (<7); slot 7 EMP (<12).
 *   slot N → buf[10-N]. slot 0→10, 7→3.
 *   buf[3..10] = [EMP, SPC, SPC, SPC, SPC, SPC, SPC, SPC]
 * row2 (slots 8..15): 8..11 EMP (<12); 12 FULL (==te, partial>=$80);
 *   13..15 FULL (>12).
 *   buf[14..21] = [FULL, FULL, FULL, FULL, EMP, EMP, EMP, EMP]
 */
static unsigned char test_hearts_8_max_3_cur_high_partial(void)
{
    static const unsigned char row1[8] = {
        TILE_EMP, TILE_SPC, TILE_SPC, TILE_SPC,
        TILE_SPC, TILE_SPC, TILE_SPC, TILE_SPC
    };
    static const unsigned char row2[8] = {
        TILE_FULL, TILE_FULL, TILE_FULL, TILE_FULL,
        TILE_EMP,  TILE_EMP,  TILE_EMP,  TILE_EMP
    };
    format_with(0x83u, 0x80u);
    return (buf_eq(3u, row1, 8u) && buf_eq(14u, row2, 8u)) ? 1u : 0u;
}

/* ---- T4: hearts=$83 partial=$40 ------------------------------------ *
 * Identical to T3 except slot 12 partial<$80 && !=0 → HALF (101).
 * Slot 12 → buf[17]. With buf[14..21] left-to-right that is row2[3].
 *   buf[14..21] = [FULL, FULL, FULL, HALF, EMP, EMP, EMP, EMP]
 */
static unsigned char test_hearts_8_max_3_cur_low_partial(void)
{
    static const unsigned char row1[8] = {
        TILE_EMP, TILE_SPC, TILE_SPC, TILE_SPC,
        TILE_SPC, TILE_SPC, TILE_SPC, TILE_SPC
    };
    static const unsigned char row2[8] = {
        TILE_FULL, TILE_FULL, TILE_FULL, TILE_HALF,
        TILE_EMP,  TILE_EMP,  TILE_EMP,  TILE_EMP
    };
    format_with(0x83u, 0x40u);
    return (buf_eq(3u, row1, 8u) && buf_eq(14u, row2, 8u)) ? 1u : 0u;
}

/* ---- T5: hearts=$FF partial=0 -------------------------------------- *
 * full=15, containers=15, ts=0, te=0.
 * slot 0: ==te, partial=0 → EMP.
 * slot 1..15: > 0 → FULL.
 * row1: slot 0 EMP, 1..7 FULL.
 *   buf[10]=EMP (slot 0), buf[9..3]=FULL.
 *   buf[3..10] = [FULL, FULL, FULL, FULL, FULL, FULL, FULL, EMP]
 * row2: slots 8..15 FULL.
 *   buf[14..21] = [FULL×8]
 */
static unsigned char test_hearts_15_full(void)
{
    static const unsigned char row1[8] = {
        TILE_FULL, TILE_FULL, TILE_FULL, TILE_FULL,
        TILE_FULL, TILE_FULL, TILE_FULL, TILE_EMP
    };
    static const unsigned char row2[8] = {
        TILE_FULL, TILE_FULL, TILE_FULL, TILE_FULL,
        TILE_FULL, TILE_FULL, TILE_FULL, TILE_FULL
    };
    format_with(0xFFu, 0u);
    return (buf_eq(3u, row1, 8u) && buf_eq(14u, row2, 8u)) ? 1u : 0u;
}

/* ---- T6: hearts=$00 partial=0 -------------------------------------- *
 * Degenerate: hearts==0 → all 16 slots SPC.
 *   buf[3..10] = SPC×8, buf[14..21] = SPC×8.
 */
static unsigned char test_hearts_zero(void)
{
    static const unsigned char all_spc[8] = {
        TILE_SPC, TILE_SPC, TILE_SPC, TILE_SPC,
        TILE_SPC, TILE_SPC, TILE_SPC, TILE_SPC
    };
    format_with(0u, 0u);
    return (buf_eq(3u, all_spc, 8u) && buf_eq(14u, all_spc, 8u)) ? 1u : 0u;
}

/* ---- T7: hearts=$77 partial=0 -------------------------------------- *
 * full=7, containers=7, ts=8, te=8.
 * row1 (slots 0..7): 0..7 < 8 → SPC.
 * row2: slot 8 ==te partial=0 → EMP; slots 9..15 > 8 → FULL.
 *   buf[14..21] = [FULL, FULL, FULL, FULL, FULL, FULL, FULL, EMP]
 */
static unsigned char test_hearts_7_full(void)
{
    static const unsigned char row1[8] = {
        TILE_SPC, TILE_SPC, TILE_SPC, TILE_SPC,
        TILE_SPC, TILE_SPC, TILE_SPC, TILE_SPC
    };
    static const unsigned char row2[8] = {
        TILE_FULL, TILE_FULL, TILE_FULL, TILE_FULL,
        TILE_FULL, TILE_FULL, TILE_FULL, TILE_EMP
    };
    format_with(0x77u, 0u);
    return (buf_eq(3u, row1, 8u) && buf_eq(14u, row2, 8u)) ? 1u : 0u;
}

/* ---- Group B: decimal counter formatters --------------------------- *
 * Rupee/bomb/key cells live at template offsets 25..27, 37..39, 31..33.
 * cave_format_decimal_byte produces 3 BCD digits in RAM(1..3) (hi..lo).
 * format_decimal_count_byte then:
 *   - replaces $24 (space) hundreds digit with $21 (placeholder tile);
 *   - if tens is space, calls core_format_char_doublet(units) which
 *     writes RAM(2) = units, RAM(3) = $24.
 * copy_triplet_to_text_buf writes buf[off]=RAM(3), buf[off-1]=RAM(2),
 * buf[off-2]=RAM(1).
 */

#define TILE_PLACE 0x21u    /* leading-zero placeholder */
#define TILE_DASH  0x0Au    /* master-key indicator (NES tile $0A) */

static unsigned char buf_at(unsigned char off)
{
    return (unsigned char)TRANSFER_BUF_BYTE(off);
}

static unsigned char test_rupees_42(void)
{
    /* val=42: hundreds=0->$21, tens=4, units=2.
     * buf[25..27] = [$21, 4, 2]. */
    format_with_counters(42u, 0u, 0u, 0u);
    return (buf_at(25u) == TILE_PLACE
         && buf_at(26u) == 4u
         && buf_at(27u) == 2u) ? 1u : 0u;
}

static unsigned char test_rupees_0(void)
{
    /* val=0: hundreds=0->$21, tens=0->$24 (doublet), units=0->$24.
     * After doublet RAM(2)=units(0), RAM(3)=$24.
     * buf[25..27] = [$21, 0, $24]. */
    format_with_counters(0u, 0u, 0u, 0u);
    return (buf_at(25u) == TILE_PLACE
         && buf_at(26u) == 0u
         && buf_at(27u) == TILE_SPC) ? 1u : 0u;
}

static unsigned char test_rupees_255(void)
{
    /* val=255: hundreds=2 (no replace), tens=5, units=5.
     * buf[25..27] = [2, 5, 5]. */
    format_with_counters(255u, 0u, 0u, 0u);
    return (buf_at(25u) == 2u
         && buf_at(26u) == 5u
         && buf_at(27u) == 5u) ? 1u : 0u;
}

static unsigned char test_bombs_8(void)
{
    /* val=8: hundreds=0->$21, tens=0->doublet, units=8.
     * After doublet RAM(2)=8, RAM(3)=$24.
     * buf[37..39] = [$21, 8, $24]. */
    format_with_counters(0u, 8u, 0u, 0u);
    return (buf_at(37u) == TILE_PLACE
         && buf_at(38u) == 8u
         && buf_at(39u) == TILE_SPC) ? 1u : 0u;
}

static unsigned char test_bombs_99(void)
{
    /* val=99: hundreds=0->$21, tens=9, units=9.
     * buf[37..39] = [$21, 9, 9]. */
    format_with_counters(0u, 99u, 0u, 0u);
    return (buf_at(37u) == TILE_PLACE
         && buf_at(38u) == 9u
         && buf_at(39u) == 9u) ? 1u : 0u;
}

static unsigned char test_keys_5_no_mkey(void)
{
    /* master_key=0 path: format keys=5 like a normal decimal.
     * val=5: hundreds=$21, tens=$24->doublet -> RAM(2)=5, RAM(3)=$24.
     * buf[31..33] = [$21, 5, $24]. */
    format_with_counters(0u, 0u, 5u, 0u);
    return (buf_at(31u) == TILE_PLACE
         && buf_at(32u) == 5u
         && buf_at(33u) == TILE_SPC) ? 1u : 0u;
}

static unsigned char test_master_key_dash(void)
{
    /* master_key!=0: explicit triplet.
     *   RAM(0) = 33; RAM(1) = 33 (=$21);
     *   core_format_char_doublet(10) -> RAM(2)=10, RAM(3)=$24.
     *   copy_triplet writes buf[33]=$24, buf[32]=10, buf[31]=$21.
     * keys cell ignored when master_key set. */
    format_with_counters(0u, 0u, 0u, 1u);
    return (buf_at(31u) == TILE_PLACE
         && buf_at(32u) == TILE_DASH
         && buf_at(33u) == TILE_SPC) ? 1u : 0u;
}

/* ---- Group C: animated rupee tick ---------------------------------- *
 * hud_world_change_rupees() drains hud_runtime.c:95-122. Five gates:
 *   (1) ROOM_TRANSFER_BUF_SELECT != 0 → return.
 *   (2) TRANSFER_BUF_BYTE(0) bit 7 clear → return.
 *   (3) FRAME_COUNTER & 1 → return after inventory edge writes.
 *   (4) RAM($067D) != 0 → decrement, ++LINK_RUPEES, sfx=16.
 *   (5) CAVE_DOOR_REPAIR_RUPEE_DELTA != 0 → decrement, --LINK_RUPEES, sfx=16.
 * Final hud_format_status_bar_text() rewrites buf, so tests check state
 * cells (RAM) rather than buf contents.
 */

static void anim_setup(unsigned char rupees_to_add, signed char delta,
                       unsigned char rupees, unsigned char fc,
                       unsigned char buf_select, unsigned char buf0_bit7)
{
    LINK_RUPEES = rupees;
    LINK_HEARTS = 0x33u;            /* avoid heart-tile traps */
    LINK_PARTIAL_HEART = 0u;
    LINK_BOMB_COUNT = 0u;
    RAM(0x066Eu) = 0u;              /* keys */
    RAM(0x0664u) = 0u;              /* master_key */
    RAM(0x067Du) = rupees_to_add;
    CAVE_DOOR_REPAIR_RUPEE_DELTA = delta;
    FRAME_COUNTER = fc;
    ROOM_TRANSFER_BUF_SELECT = buf_select;
    ROOM_SFX_MAIN = 0u;
    if (buf0_bit7) {
        TRANSFER_BUF_BYTE(0) = 0xA0u;  /* arbitrary, bit 7 set */
    } else {
        TRANSFER_BUF_BYTE(0) = 0x20u;  /* clear */
    }
}

static unsigned char test_anim_buf_select_skip(void)
{
    /* BUF_SELECT=1: function returns immediately; $067D unchanged. */
    anim_setup(5u, 0, 10u, 0u, /*buf_select=*/1u, /*bit7=*/1u);
    hud_world_change_rupees();
    const unsigned char ok = (RAM(0x067Du) == 5u
                           && (unsigned char)LINK_RUPEES == 10u
                           && ROOM_SFX_MAIN == 0u) ? 1u : 0u;
    ROOM_TRANSFER_BUF_SELECT = 0u;
    return ok;
}

static unsigned char test_anim_high_bit_clear_skip(void)
{
    /* TRANSFER_BUF_BYTE(0) bit 7 clear: return; $067D unchanged. */
    anim_setup(5u, 0, 10u, 0u, /*buf_select=*/0u, /*bit7=*/0u);
    hud_world_change_rupees();
    return (RAM(0x067Du) == 5u
         && (unsigned char)LINK_RUPEES == 10u
         && ROOM_SFX_MAIN == 0u) ? 1u : 0u;
}

static unsigned char test_anim_odd_frame_skip(void)
{
    /* FRAME_COUNTER odd → return after inventory edge checks. */
    anim_setup(5u, 0, 10u, 1u, /*buf_select=*/0u, /*bit7=*/1u);
    hud_world_change_rupees();
    return (RAM(0x067Du) == 5u
         && (unsigned char)LINK_RUPEES == 10u
         && ROOM_SFX_MAIN == 0u) ? 1u : 0u;
}

static unsigned char test_anim_credit_tick(void)
{
    /* $067D=5, RUPEES=10, delta=0 → $067D=4, RUPEES=11, sfx=16. */
    anim_setup(5u, 0, 10u, 0u, /*buf_select=*/0u, /*bit7=*/1u);
    hud_world_change_rupees();
    return (RAM(0x067Du) == 4u
         && (unsigned char)LINK_RUPEES == 11u
         && ROOM_SFX_MAIN == 16u) ? 1u : 0u;
}

static unsigned char test_anim_debit_tick(void)
{
    /* $067D=0, delta=3, RUPEES=10 → delta=2, RUPEES=9, sfx=16. */
    anim_setup(0u, 3, 10u, 0u, /*buf_select=*/0u, /*bit7=*/1u);
    hud_world_change_rupees();
    return ((signed char)CAVE_DOOR_REPAIR_RUPEE_DELTA == 2
         && (unsigned char)LINK_RUPEES == 9u
         && ROOM_SFX_MAIN == 16u) ? 1u : 0u;
}

/* ---- Group D: rupee inventory-edge writes -------------------------- *
 * INVENTORY_VALUE(slot)=RAM(0x0657+slot). slot 38=$067D (=rta),
 * slot 39=$067E (=delta). FC odd gates out the credit/debit branches
 * so only the edge clears fire.
 */

static unsigned char test_anim_zero_clears_delta(void)
{
    /* RUPEES=0, delta=5: edge clears delta. FC odd → return after edge. */
    anim_setup(/*rta=*/0u, /*delta=*/5, /*rupees=*/0u, /*fc=*/1u,
               /*buf_select=*/0u, /*bit7=*/1u);
    hud_world_change_rupees();
    return ((signed char)CAVE_DOOR_REPAIR_RUPEE_DELTA == 0
         && (unsigned char)LINK_RUPEES == 0u) ? 1u : 0u;
}

static unsigned char test_anim_max_clears_rta(void)
{
    /* RUPEES=$FF, $067D=5: edge clears $067D. FC odd → return after edge. */
    anim_setup(/*rta=*/5u, /*delta=*/0, /*rupees=*/0xFFu, /*fc=*/1u,
               /*buf_select=*/0u, /*bit7=*/1u);
    hud_world_change_rupees();
    return (RAM(0x067Du) == 0u
         && (unsigned char)LINK_RUPEES == 0xFFu) ? 1u : 0u;
}

/* ---- Driver -------------------------------------------------------- */

static void mark(unsigned int bit_idx, unsigned char *passes_io,
                 unsigned char bits[3])
{
    const unsigned int byte_idx = bit_idx >> 3;
    const unsigned int bit_in_byte = bit_idx & 7u;
    bits[byte_idx] |= (unsigned char)(1u << bit_in_byte);
    ++(*passes_io);
}

void hud_format_probe_run(void)
{
    /* Snapshot mutable cells — tests overwrite them. */
    const unsigned char saved_hearts     = (unsigned char)LINK_HEARTS;
    const unsigned char saved_partial    = (unsigned char)LINK_PARTIAL_HEART;
    const unsigned char saved_rupees     = (unsigned char)LINK_RUPEES;
    const unsigned char saved_bombs      = (unsigned char)LINK_BOMB_COUNT;
    const unsigned char saved_keys       = (unsigned char)RAM(0x066Eu);
    const unsigned char saved_master_key = (unsigned char)RAM(0x0664u);
    const unsigned char saved_rta        = (unsigned char)RAM(0x067Du);
    const signed char   saved_delta      = (signed char)CAVE_DOOR_REPAIR_RUPEE_DELTA;
    const unsigned char saved_buf_sel    = (unsigned char)ROOM_TRANSFER_BUF_SELECT;
    const unsigned char saved_fc         = (unsigned char)FRAME_COUNTER;
    const unsigned char saved_sfx        = (unsigned char)ROOM_SFX_MAIN;

    unsigned char bits[3] = { 0u, 0u, 0u };
    unsigned char passes = 0u;
    const unsigned char total = 22u;

    stamp_magic();

    /* Group A — heart row */
    if (test_template_loaded())                    mark(0u, &passes, bits);
    if (test_hearts_3_full())                      mark(1u, &passes, bits);
    if (test_hearts_3_max_1_cur())                 mark(2u, &passes, bits);
    if (test_hearts_8_max_3_cur_high_partial())    mark(3u, &passes, bits);
    if (test_hearts_8_max_3_cur_low_partial())     mark(4u, &passes, bits);
    if (test_hearts_15_full())                     mark(5u, &passes, bits);
    if (test_hearts_zero())                        mark(6u, &passes, bits);
    if (test_hearts_7_full())                      mark(7u, &passes, bits);

    /* Group B — decimal counters */
    if (test_rupees_42())                          mark(8u, &passes, bits);
    if (test_rupees_0())                           mark(9u, &passes, bits);
    if (test_rupees_255())                         mark(10u, &passes, bits);
    if (test_bombs_8())                            mark(11u, &passes, bits);
    if (test_bombs_99())                           mark(12u, &passes, bits);
    if (test_keys_5_no_mkey())                     mark(13u, &passes, bits);
    if (test_master_key_dash())                    mark(14u, &passes, bits);

    /* Group C — animated rupee tick */
    if (test_anim_buf_select_skip())               mark(15u, &passes, bits);
    if (test_anim_high_bit_clear_skip())           mark(16u, &passes, bits);
    if (test_anim_odd_frame_skip())                mark(17u, &passes, bits);
    if (test_anim_credit_tick())                   mark(18u, &passes, bits);
    if (test_anim_debit_tick())                    mark(19u, &passes, bits);

    /* Group D — rupee inventory-edge writes */
    if (test_anim_zero_clears_delta())             mark(20u, &passes, bits);
    if (test_anim_max_clears_rta())                mark(21u, &passes, bits);

    PROBE[3] = total;
    PROBE[4] = passes;
    PROBE[5] = bits[0];
    PROBE[6] = bits[1];
    PROBE[7] = bits[2];

    /* Restore live cells so subsequent gameplay sees the real state. */
    LINK_HEARTS = saved_hearts;
    LINK_PARTIAL_HEART = saved_partial;
    LINK_RUPEES = saved_rupees;
    LINK_BOMB_COUNT = saved_bombs;
    RAM(0x066Eu) = saved_keys;
    RAM(0x0664u) = saved_master_key;
    RAM(0x067Du) = saved_rta;
    CAVE_DOOR_REPAIR_RUPEE_DELTA = saved_delta;
    ROOM_TRANSFER_BUF_SELECT = saved_buf_sel;
    FRAME_COUNTER = saved_fc;
    ROOM_SFX_MAIN = saved_sfx;
}
