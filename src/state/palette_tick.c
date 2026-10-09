/* palette_tick.c — frame-cadence palette toggle runtime.
 *
 * Substrate per debate 004 Rule WT-1 (main worktree single-writer).
 * Pure C; no SGDK API calls. Both RoomRom gameplay (low-health,
 * boss-flash, hit-invuln) and Debug.md frontend (intro item flash)
 * register toggles and call palette_tick() from their per-frame
 * update.
 *
 * Per master plan Task 2.6.5 + docs/audit/palette_toggle_inventory.md.
 */

#include "palette_state.h"

void palette_tick_init(palette_tick_state_t *ts)
{
    if (ts == 0) return;
    for (unsigned i = 0; i < PALETTE_TOGGLE_MAX; ++i) {
        ts->toggles[i] = 0;
    }
    ts->toggle_count = 0;
    ts->last_frame_seen = 0;
}

int palette_register_toggle(palette_tick_state_t *ts,
                            const palette_toggle_t *t)
{
    if (ts == 0 || t == 0) return -1;
    if (ts->toggle_count >= PALETTE_TOGGLE_MAX) return -1;
    ts->toggles[ts->toggle_count++] = t;
    return 0;
}

/* Apply one toggle's state to the PaletteState mirror. The renderer
 * picks up the change on the next CRAM upload (palette_invalidate_cache
 * bumps the generation). */
static void apply_toggle(const palette_toggle_t *t,
                         PaletteState *p,
                         uint16_t frame_counter)
{
    if (t == 0 || p == 0 || !t->enabled) return;

    /* Bit-set picks value_b; bit-clear picks value_a. */
    uint8_t bit_mask = (uint8_t)(1u << t->frame_bit);
    uint8_t state_b  = (frame_counter & bit_mask) != 0u;
    uint8_t value    = state_b ? t->value_b : t->value_a;

    switch (t->kind) {
    case PALETTE_TOGGLE_BG_SUBPAL:
        /* target_index = sub-pal slot 0..3; value = NES color index applied
         * to the FIRST color of that sub-pal. Caller picks which slot in
         * the sub-pal gets toggled by choosing target_index appropriately
         * (encoding: target_index = sub_pal * 4 + color_in_subpal). */
        if (t->target_index < PALETTE_BG_HALF_BYTES) {
            p->nes_palram[t->target_index] = value;
        }
        break;

    case PALETTE_TOGGLE_SPR_SUBPAL:
        /* target_index encodes sub-pal slot relative to sprite half:
         * 0..15 = slot in sprite half. Stored at PALRAM offset
         * BG_HALF_BYTES + target_index. */
        if (t->target_index < PALETTE_SPR_HALF_BYTES) {
            p->nes_palram[PALETTE_BG_HALF_BYTES + t->target_index] = value;
        }
        break;

    case PALETTE_TOGGLE_CRAM_SLOT:
        /* target_index = full CRAM slot 0..63; value goes into the cached
         * Genesis CRAM word's low byte. This is for direct overlays
         * (PAL2/PAL3). Not commonly used by NES toggles; reserved. */
        if (t->target_index < PALETTE_CRAM_TOTAL_COLORS) {
            p->cram_cache[t->target_index] =
                (uint16_t)((p->cram_cache[t->target_index] & 0xFF00u) | value);
        }
        break;
    }
}

void palette_tick(palette_tick_state_t *ts,
                  PaletteState *p,
                  uint16_t frame_counter)
{
    if (ts == 0 || p == 0) return;
    if (frame_counter == ts->last_frame_seen) return;  /* idempotent */
    ts->last_frame_seen = frame_counter;

    int any_change = 0;
    for (unsigned i = 0; i < ts->toggle_count; ++i) {
        const palette_toggle_t *t = ts->toggles[i];
        if (t == 0 || !t->enabled) continue;
        apply_toggle(t, p, frame_counter);
        any_change = 1;
    }

    if (any_change) {
        palette_invalidate_cache(p);
    }
}
