/* palette_state.h — NES PALRAM mirror + Genesis CRAM cache.
 *
 * Phase 2 (Graphics Registry) per docs/audit/state_contract.md migration
 * order. Typed-struct promotion target for palette state.
 *
 * NES PALRAM is 32 bytes at PPU $3F00–$3F1F (not CPU RAM):
 *   $3F00–$3F0F : 4 BG sub-palettes (4 colors each)
 *   $3F10–$3F1F : 4 sprite sub-palettes (4 colors each)
 * Color 0 of every sub-palette mirrors the universal background color
 * at $3F00 (handled by NES PPU; we replicate via PALETTE_UBG_INDEX).
 *
 * Genesis CRAM is 64 word entries (4 PAL banks x 16 colors). The cached
 * Genesis colors are produced by nes_to_cram (RoomRom roomrom_bg_palette
 * already defines the canonical conversion).
 *
 * NOT yet wired to consumers — this header establishes the typed shape.
 * Wiring lands as RoomRom palette modules promote (Phase 12).
 */

#ifndef PALETTE_STATE_H
#define PALETTE_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PALETTE_PALRAM_BYTES         32u  /* NES PALRAM size */
#define PALETTE_BG_HALF_BYTES        16u  /* $3F00–$3F0F */
#define PALETTE_SPR_HALF_BYTES       16u  /* $3F10–$3F1F */
#define PALETTE_SUBPAL_COUNT         4u   /* 4 BG + 4 sprite sub-palettes */
#define PALETTE_COLORS_PER_SUBPAL    4u
#define PALETTE_UBG_INDEX            0u   /* universal BG color slot */

/* Genesis CRAM: 4 banks (PAL0..PAL3) x 16 colors per bank. */
#define PALETTE_CRAM_BANK_COUNT      4u
#define PALETTE_CRAM_BANK_COLORS    16u
#define PALETTE_CRAM_TOTAL_COLORS   (PALETTE_CRAM_BANK_COUNT * PALETTE_CRAM_BANK_COLORS)

typedef struct PaletteState {
    /* NES PALRAM mirror — 32 bytes at $3F00–$3F1F.
     * Caller writes raw NES color indices ($00..$3F). */
    uint8_t  nes_palram[PALETTE_PALRAM_BYTES];

    /* Genesis CRAM cache — 64 words from the most recent conversion of
     * nes_palram. Layout follows the canonical RoomRom mapping
     * (roomrom_bg_palette_load_palram_full). */
    uint16_t cram_cache[PALETTE_CRAM_TOTAL_COLORS];

    /* Bumped each time nes_palram is rewritten. Lets adapter code skip
     * redundant CRAM uploads when the palette has not changed. */
    uint16_t generation;

    /* Set non-zero when cram_cache is valid for the current nes_palram.
     * Cleared on nes_palram write; set after conversion. */
    uint8_t  cram_cache_valid;
} PaletteState;

#define PALETTE_STATE_INITIALIZER  { \
    .nes_palram       = { 0 }, \
    .cram_cache       = { 0 }, \
    .generation       = 0u, \
    .cram_cache_valid = 0u, \
}

/* Sub-palette accessors. subpal_idx is 0..3 across BG (sub) or sprite
 * (sub + PALETTE_SUBPAL_COUNT). color_idx is 0..3 within the sub-pal. */
static inline uint8_t palette_bg_color(const PaletteState *p,
                                       uint8_t subpal, uint8_t color) {
    return p->nes_palram[(uint16_t)subpal * PALETTE_COLORS_PER_SUBPAL + color];
}
static inline uint8_t palette_spr_color(const PaletteState *p,
                                        uint8_t subpal, uint8_t color) {
    return p->nes_palram[
        PALETTE_BG_HALF_BYTES +
        (uint16_t)subpal * PALETTE_COLORS_PER_SUBPAL + color];
}
static inline uint8_t palette_ubg_color(const PaletteState *p) {
    return p->nes_palram[PALETTE_UBG_INDEX];
}

static inline void palette_invalidate_cache(PaletteState *p) {
    p->cram_cache_valid = 0u;
    p->generation++;
}

/* ----------------------------------------------------------------------
 * Frame-cadence palette toggle (Phase 2.6.5)
 *
 * NES Zelda 1 animates several palette flashes by toggling a sub-palette
 * index every N frames against FrameCounter bits. Phase 2.6 cuts the
 * Genesis renderer to per-tile-index sub-palette selection; without a
 * frame-cadence preservation layer, those NES animations die.
 *
 * Each palette_toggle_t describes one such toggle: which palette slot
 * (BG sub-palette or sprite sub-palette) gets flipped, what the two
 * states are, and what frame-counter bit triggers the flip. The runtime
 * iterates a registered table once per frame and applies state.
 *
 * See docs/audit/palette_toggle_inventory.md for the full inventory of
 * NES toggles we preserve. Memory rule: project_intro_item_flash.
 * -------------------------------------------------------------------- */

typedef enum {
    PALETTE_TOGGLE_BG_SUBPAL  = 0,   /* operates on a BG sub-palette index */
    PALETTE_TOGGLE_SPR_SUBPAL = 1,   /* operates on a sprite sub-palette index */
    PALETTE_TOGGLE_CRAM_SLOT  = 2,   /* operates on a single CRAM word slot */
} palette_toggle_kind_t;

typedef struct palette_toggle_t {
    /* Identification. Stable string for logging + parity-oracle diff. */
    const char *id;

    /* Which kind of palette write fires. */
    palette_toggle_kind_t kind;

    /* Index this toggle targets (sub-pal index 0..3 OR CRAM slot 0..63). */
    uint8_t target_index;

    /* Two NES color values to flip between (kind=BG_SUBPAL/SPR_SUBPAL:
     * indices into PALETTE_COLORS_PER_SUBPAL; kind=CRAM_SLOT: raw NES
     * color bytes). */
    uint8_t value_a;
    uint8_t value_b;

    /* FrameCounter bit that selects state. Bit set -> value_b; clear ->
     * value_a. NES Zelda 1 most often uses bit 3 (8-frame cadence). */
    uint8_t frame_bit;

    /* Per-toggle enable flag — runtime can mute without removing entry. */
    uint8_t enabled;
} palette_toggle_t;

/* Runtime API.
 *
 * palette_tick_init(state) — call once at scene-enter; clears registered
 *   toggle table.
 * palette_register_toggle(state, t) — append a toggle. Caller owns the
 *   palette_toggle_t storage (typically a static const). Returns 0 on
 *   success, -1 if table full.
 * palette_tick(state, frame_counter) — call once per frame from the
 *   per-frame update. Iterates registered toggles, applies state to
 *   nes_palram[], and bumps generation. Caller flushes CRAM cache via
 *   the existing palette path on next render.
 *
 * Implementation lives in `src/state/palette_tick.c` (substrate, main
 * worktree per Rule WT-1). Both RoomRom (gameplay toggles: low-health,
 * boss flash, hit-invuln) and Debug.md frontend (intro item flash) call
 * palette_tick from their per-frame update; the runtime is pure C with
 * no SGDK dependencies, so it lands in substrate rather than gameplay-
 * side.
 */

#define PALETTE_TOGGLE_MAX 16u

typedef struct palette_tick_state_t {
    const palette_toggle_t *toggles[PALETTE_TOGGLE_MAX];
    uint8_t  toggle_count;
    uint16_t last_frame_seen;
} palette_tick_state_t;

void palette_tick_init(palette_tick_state_t *ts);
int  palette_register_toggle(palette_tick_state_t *ts,
                             const palette_toggle_t *t);
void palette_tick(palette_tick_state_t *ts, PaletteState *p,
                  uint16_t frame_counter);

/* Compile-time sanity. */
_Static_assert(PALETTE_BG_HALF_BYTES + PALETTE_SPR_HALF_BYTES ==
               PALETTE_PALRAM_BYTES,
               "BG + sprite halves must equal full PALRAM");
_Static_assert(PALETTE_SUBPAL_COUNT * PALETTE_COLORS_PER_SUBPAL ==
               PALETTE_BG_HALF_BYTES,
               "sub-pal count x colors-per-sub-pal must equal BG half");
_Static_assert(PALETTE_CRAM_BANK_COUNT * PALETTE_CRAM_BANK_COLORS == 64u,
               "Genesis CRAM is 64 words");

#ifdef __cplusplus
}
#endif

#endif /* PALETTE_STATE_H */
