#ifndef CAVE_STATE_H
#define CAVE_STATE_H

#include <stdint.h>
#include "platform_abi.h"
#include "scratch_state.h"

/* Cave subsystem state.
 *
 * Phase 3 (Caves) typed-struct promotion target per
 * docs/audit/state_contract.md migration order.
 *
 * Two views coexist during promotion (state_contract.md migration step
 * 4: deprecate then remove macros once consumers migrate):
 *
 *   1. CaveState typed struct + inline accessors below — used by promoted
 *      owned C cave runtime code (Phase 12 promotion target).
 *
 *   2. Legacy CAVE_* RAM() macros below the struct — used by transpiled
 *      z_01 co-resident code until those callsites migrate.
 *
 * Cross-subsystem aliasing:
 *   CAVE_TMP0..4 ($0000..$0004) are NES zero-page scratch shared with
 *   every subsystem (combat, world, targeting, progress). They appear in
 *   the cross-subsystem alias-collision report from
 *   tools/state/verify_no_alias_collisions.py and are intentionally NOT
 *   modeled as fields of CaveState — scratch ownership belongs in a
 *   future scratch_state.h (decision deferred per state_contract.md
 *   "do not bulk-resolve" guidance).
 */

/* Cave subsystem persistent state. Fields cite their NES RAM offset to
 * preserve transpiler co-residency. Add a _Static_assert against
 * offsetof when field-specific layout is required by a consumer. */
typedef struct CaveState {
    /* RNG state mirrored from $0019/$001A (shared with NES Z_Rand). */
    uint8_t random_a;       /* NES $0019 */
    uint8_t random_b;       /* NES $001A */

    uint8_t  delay_timer;            /* NES $0029 */
    uint8_t  link_action_timer;      /* NES $00AC */
    uint8_t  person_state;           /* NES $00AD */
    uint8_t  link_input_flags;       /* NES $00F8 */
    uint8_t  room_type;              /* NES $0350 */
    uint8_t  flags;                  /* NES $0413 */
    uint8_t  text_selector;          /* NES $0415 */
    uint8_t  text_char_index;        /* NES $0416 */

    /* Shop ware state. */
    uint8_t  active_ware_index;      /* NES $0421 */
    uint8_t  ware_items[3];          /* NES $0422..$0424 (CAVE_WARES_PER_ROOM) */
    uint8_t  transfer_price_count;   /* NES $042E */
    uint8_t  transfer_price_offset;  /* NES $042F */
    uint8_t  selected_ware_index;    /* NES $0438 */

    /* Money game permanent + amount state (3-slot). */
    uint8_t  money_game_perm[3];     /* NES $046C..$046E */
    uint8_t  money_game_amount[3];   /* NES $046F..$0471 */

    uint8_t  text_line_addr_lo;      /* NES $045F */
    uint8_t  text_tick_sfx;          /* NES $0604 */
    uint8_t  room_script_state;      /* NES $0666 */
    int8_t   door_repair_rupee_delta;/* NES $067E (signed delta) */
} CaveState;

/* Compile-time constants (kept for both views). */
#define CAVE_WARE_DRAW_SLOT              19u
#define CAVE_WARES_PER_ROOM              3u
#define CAVE_TRANSFER_BUF_CHAR_BASE      0x0302

/* ----------------------------------------------------------------------
 * Inline accessors — read/write through nes_ram[] at the NES offsets so
 * promoted owned C and transpiled co-resident code see the same byte.
 * These belong to the bridge layer per docs/audit/state_contract.md.
 * Not yet wired into consumers; consumers migrate per-callsite.
 * -------------------------------------------------------------------- */

static inline uint8_t  cave_random_a_get(void)            { return RAM(0x0019); }
static inline void     cave_random_a_set(uint8_t v)       { RAM(0x0019) = v; }
static inline uint8_t  cave_random_b_get(void)            { return RAM(0x001A); }
static inline void     cave_random_b_set(uint8_t v)       { RAM(0x001A) = v; }
static inline uint8_t  cave_room_type_get(void)           { return RAM(0x0350); }
static inline void     cave_room_type_set(uint8_t v)      { RAM(0x0350) = v; }
static inline uint8_t  cave_flags_get(void)               { return RAM(0x0413); }
static inline void     cave_flags_set(uint8_t v)          { RAM(0x0413) = v; }
static inline uint8_t  cave_active_ware_index_get(void)   { return RAM(0x0421); }
static inline void     cave_active_ware_index_set(uint8_t v) { RAM(0x0421) = v; }

/* ----------------------------------------------------------------------
 * Legacy macro view — kept until Phase 3 close gate confirms all
 * cave-runtime callers have migrated to the inline accessors above.
 * -------------------------------------------------------------------- */
/* Zero-page scratch + RNG aliases through scratch_state.h canonical names. */
#define CAVE_TMP0                        ZP_TMP0
#define CAVE_TMP1                        ZP_TMP1
#define CAVE_TMP2                        ZP_TMP2
#define CAVE_TMP3                        ZP_TMP3
#define CAVE_TMP4                        ZP_TMP4
#define CAVE_RANDOM_A                    ZP_RNG_A
#define CAVE_RANDOM_B                    ZP_RNG_B
#define CAVE_DELAY_TIMER                 RAM(0x0029)
#define CAVE_LINK_ACTION_TIMER           RAM(0x00AC)
#define CAVE_PERSON_STATE                RAM(0x00AD)
#define CAVE_LINK_INPUT_FLAGS            RAM(0x00F8)
#define CAVE_ROOM_TYPE                   RAM(0x0350)
#define CAVE_FLAGS                       RAM(0x0413)
#define CAVE_TEXT_SELECTOR               RAM(0x0415)
#define CAVE_TEXT_CHAR_INDEX             RAM(0x0416)
#define CAVE_ACTIVE_WARE_INDEX           RAM(0x0421)
#define CAVE_WARE_ITEM(idx)              RAM(0x0422 + (idx))
#define CAVE_TRANSFER_PRICE_COUNT        RAM(0x042E)
#define CAVE_TRANSFER_PRICE_OFFSET       RAM(0x042F)
#define CAVE_PRICE(idx)                  RAM(0x0430 + (idx))
#define CAVE_SELECTED_WARE_INDEX         RAM(0x0438)
#define CAVE_PRIZE_ORDER(idx)            RAM(0x0448 + (idx))
#define CAVE_TEXT_LINE_ADDR_LO           RAM(0x045F)
#define CAVE_MONEY_GAME_PERM(idx)        RAM(0x046C + (idx))
#define CAVE_MONEY_GAME_AMOUNT(idx)      RAM(0x046F + (idx))
#define CAVE_TEXT_TICK_SFX               RAM(0x0604)
#define LINK_RUPEES                      RAM(0x066D)
/* NES name: InvLetter (Variables.inc). $0666 holds inventory letter
 * state (used / unused / received) globally; cave runtime aliases it
 * as CAVE_ROOM_SCRIPT_STATE because medicine-shop script logic reads
 * + bumps it on letter-use (Z_01.asm:UpdateCavePerson @UseLetter).
 * Both names refer to the SAME byte. Per Gate 1 finding 3_2b. */
#define CAVE_ROOM_SCRIPT_STATE           RAM(0x0666)
#define INV_LETTER                       RAM(0x0666)  /* canonical NES name */
#define CAVE_DOOR_REPAIR_RUPEE_DELTA     RAM(0x067E)

/* CAVE_TRANSFER_BUF_CHAR_BASE moved above with the typed view's constants. */
#define CAVE_TRANSFER_BUF_PRICE_SIGN(off) RAM(0x0306 + (off))
#define CAVE_TRANSFER_BUF_PRICE_HUNDREDS(off) RAM(0x0307 + (off))
#define CAVE_TRANSFER_BUF_PRICE_TENS(off) RAM(0x0308 + (off))
#define CAVE_TRANSFER_BUF_PRICE_UNITS(off) RAM(0x0309 + (off))

/* CAVE_WARE_DRAW_SLOT and CAVE_WARES_PER_ROOM moved above. */

#endif
