#ifndef ROOMROM_INVENTORY_H
#define ROOMROM_INVENTORY_H

/* ---------------------------------------------------------------------------
 * Phase 6 Task 6.10.4 — RoomRom inventory_t struct.
 *
 * Shape mirrors NES Z1 Variables.inc cells one-for-one. Field names follow
 * the NES "Inv*" prefix to keep cross-references between drained C, NES
 * disasm, and parity oracle traces obvious. Storage is byte-wide except
 * for `rupees`, which the NES treats as a single byte but RoomRom widens
 * to 16-bit so the `rupees_to_add` / `rupees_to_sub` tick animation can
 * run without per-frame wrap.
 *
 * NES anchor table (reference/aldonunez/Variables.inc:236-267):
 *   Items            $657  native sword tier (not the compatibility bitmask)
 *   items bitmask below is derived from individual native ownership cells.
 *   InvBombs         $658  current bomb count (0..MaxBombs)
 *   InvArrow         $659  arrow type ($00 none, $01 wood, $02 silver)
 *   Bow              $65A  bow ownership ($01 = owned)
 *   InvCandle        $65B  candle tier ($00 none, $01 blue, $02 red)
 *   InvFood          $65D  bait count (0..1)
 *   Potion           $65E  potion tier ($00 none, $01 blue/life, $02 red/2nd)
 *   InvRaft          $660  raft ownership ($01 = owned)
 *   InvBook          $661  book of magic ownership
 *   InvRing          $662  ring tier ($00 none, $01 blue/2, $02 red/4)
 *   InvLadder        $663  step ladder ownership
 *   InvMagicKey      $664  master key ownership
 *   InvBracelet      $665  power bracelet ownership
 *   InvLetter        $666  letter ($00 none, $01 unread, $02 read)
 *   InvCompass       $667  compass-Q1 dungeon bitfield (bit0=L1 .. bit7=L8)
 *   InvMap           $668  map-Q1 bitfield
 *   InvCompass9      $669  compass-L9 / Q2 bitfield
 *   InvMap9          $66A  map-L9 / Q2 bitfield
 *   InvClock         $66C  clock pickup flag (timed enemy freeze)
 *   InvRupees        $66D  current rupees (0..255)
 *   InvKeys          $66E  current keys (0..255)
 *   HeartValues      $66F  high nibble = max containers, low = current full
 *   HeartPartial     $670  partial heart fraction ($00..$FF, 4-px granularity)
 *   InvTriforce      $671  triforce-piece bitfield (bit0=L1 .. bit7=L8)
 *   InvBoomerang     $674  wood-boomerang ownership
 *   InvMagicBoomerang$675  magic-boomerang ownership
 *   InvMagicShield   $676  magic shield ownership ($00 wood, $01 magic)
 *   MaxBombs         $67C  bomb capacity (8 default, 12 upgraded, 16 cap)
 *   RupeesToAdd      $67D  pending +1/+5 rupee tick countdown
 *   RupeesToSubtract $67E  pending -1 rupee tick countdown
 *   WorldFlags       $67F  per-room secret-revealed bitfield (Q1/Q2)
 *
 * The `selected_b_item` field aliases NES $656 (SelectedItemSlot). RoomRom
 * already owns this in its `b_item_t s_b_item` token; the inventory_t copy
 * is the canonical save-side cell.
 *
 * Sizing rationale: byte-wide where NES is byte-wide. `rupees` widened to
 * 16-bit per Genesis-native rupee tick (master plan Task 6.10.10). Total
 * struct size kept under PLAYER_STATE_SRAM_HEADROOM_BYTES so the multiplayer
 * SRAM static_assert in src/state/player_state.c continues to hold once
 * inventory is folded into PlayerState.
 *
 * Phase 6 Task 6.10 sub-tasks consume this header:
 *   6.10.4 — declare struct (THIS file)
 *   6.10.5 — Items bitfield writers/readers (item-pickup paths)
 *   6.10.6 — status-bar transfer buffer reads InvRupees/InvBombs/InvKeys/HeartValues
 *   6.10.10 — RupeesToAdd / RupeesToSubtract tick path
 *   6.11.x — HeartValues / HeartPartial damage subtract
 * ------------------------------------------------------------------------ */

/* ---- Derived compatibility bits; NOT NES $657 (sword tier) ---- */
#define ITEMS_BIT_BOW         0x01u
#define ITEMS_BIT_WAND        0x02u
#define ITEMS_BIT_BOOMERANG   0x04u
#define ITEMS_BIT_FLUTE       0x08u
#define ITEMS_BIT_BAIT        0x10u
#define ITEMS_BIT_LETTER      0x20u
#define ITEMS_BIT_POTION_T1   0x40u
#define ITEMS_BIT_POTION_T2   0x80u

/* ---- Tier constants ---- */
#define INV_CANDLE_NONE       0u
#define INV_CANDLE_BLUE       1u
#define INV_CANDLE_RED        2u

#define INV_RING_NONE         0u
#define INV_RING_BLUE         1u  /* damage / 2 */
#define INV_RING_RED          2u  /* damage / 4 */

#define INV_ARROW_NONE        0u
#define INV_ARROW_WOOD        1u
#define INV_ARROW_SILVER      2u

#define INV_LETTER_NONE       0u
#define INV_LETTER_UNREAD     1u
#define INV_LETTER_READ       2u

#define INV_SHIELD_WOOD       0u
#define INV_SHIELD_MAGIC      1u

#define MAX_BOMBS_DEFAULT     8u
#define MAX_BOMBS_UPGRADE_1   12u
#define MAX_BOMBS_UPGRADE_2   16u

#define HEART_CONTAINERS_MIN  3u
#define HEART_CONTAINERS_MAX  16u

/* `inventory_t` — Genesis-native shape mirroring NES Variables.inc cells.
 * Plain C primitives only (avoids stdint/SGDK types.h clash; same widths). */
typedef struct inventory_t {
    /* Derived compatibility equipment bitfield. */
    unsigned char items;

    /* Active counts. */
    unsigned char bombs;             /* $658 InvBombs */
    unsigned char arrow;             /* $659 InvArrow tier */
    unsigned char bow;               /* $65A Bow ownership */
    unsigned char candle;            /* $65B InvCandle tier */
    unsigned char food;              /* $65D InvFood (bait) count */
    unsigned char potion;            /* $65E Potion tier */

    /* Single-bit ownership flags (kept as bytes for direct NES parity). */
    unsigned char raft;              /* $660 */
    unsigned char book;              /* $661 */
    unsigned char ring;              /* $662 tier */
    unsigned char ladder;            /* $663 */
    unsigned char magic_key;         /* $664 */
    unsigned char bracelet;          /* $665 */
    unsigned char letter;            /* $666 tier */

    /* Per-quest dungeon collectibles (bitfield: bit0=L1 .. bit7=L8). */
    unsigned char compass_q1;        /* $667 InvCompass */
    unsigned char map_q1;            /* $668 InvMap */
    unsigned char compass_l9;        /* $669 InvCompass9 */
    unsigned char map_l9;            /* $66A InvMap9 */

    unsigned char clock;             /* $66C clock pickup */

    /* Currency / keys — rupees widened to 16-bit per master plan 6.10.10. */
    unsigned short rupees;           /* $66D InvRupees (NES is byte; widened) */
    unsigned char  keys;             /* $66E InvKeys */

    /* Hearts. */
    unsigned char heart_values;      /* $66F: hi=max, lo=current */
    unsigned char heart_partial;     /* $670: 0..$FF */

    /* Triforce + boomerangs + shield. */
    unsigned char triforce;          /* $671 bitfield */
    unsigned char boomerang_wood;    /* $674 */
    unsigned char boomerang_magic;   /* $675 */
    unsigned char magic_shield;      /* $676 */

    /* Capacity + tick counters. */
    unsigned char max_bombs;         /* $67C MaxBombs */
    unsigned char rupees_to_add;     /* $67D RupeesToAdd */
    unsigned char rupees_to_sub;     /* $67E RupeesToSubtract */

    /* Per-room scripted-secret revealed bitfield (Q1/Q2 select via WorldFlags hi/lo).
     * NES is a 16-bit cell; RoomRom keeps it 16-bit too. */
    unsigned short world_flags;      /* $67F */

    /* B-item selection (NES $656 SelectedItemSlot). RoomRom mirrors here so
     * pause-screen UI and per-frame dispatch share one source of truth. */
    unsigned char selected_b_item;
} inventory_t;

/* ---- HeartValues encoding helpers ---- */
static inline unsigned char heart_values_max(unsigned char hv) {
    return (unsigned char)(hv >> 4);
}
static inline unsigned char heart_values_cur(unsigned char hv) {
    return (unsigned char)(hv & 0x0Fu);
}
static inline unsigned char heart_values_pack(unsigned char max_h, unsigned char cur_h) {
    return (unsigned char)(((max_h & 0x0Fu) << 4) | (cur_h & 0x0Fu));
}

/* Single global inventory instance — Phase 13 will fold this into
 * PlayerState. Until then, RoomRom keeps a singleton for the 1-player
 * boot path. */
extern inventory_t g_inventory;
void inventory_hud_mark_dirty(void);
/* Pull native ownership/counts for legacy readers; preserve selection/layout. */
void inventory_sync_from_native(void);
unsigned char inventory_hud_consume_dirty(void);

/* Native NES currency owns count/queues; this tick synchronizes the
 * compatibility mirror. Two-frame cadence, 255 cap, native tune request. */
#define INV_RUPEE_CAP 255u

void inventory_rupee_tick(unsigned char frame_counter);

/* Convenience: queue rupees to credit/debit through the rolling tick. */
void inventory_rupee_credit(unsigned char count);
void inventory_rupee_debit(unsigned char count);

/* Plan v5b T2.7 — heart-container pickup. Mirrors NES Z_01.asm:4538
 * @TakeHeartContainer: max += 1 + cur += 1, capped at $0F (16). Kicks
 * the 3-frame scale-up anim on the newly visible slot and marks HUD
 * dirty so the next refresh paints it. Returns 1 if a heart container
 * was actually added (max wasn't already at cap), 0 otherwise. */
unsigned char inventory_add_heart_container(void);

#endif /* ROOMROM_INVENTORY_H */
