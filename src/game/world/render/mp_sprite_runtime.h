#ifndef MP_SPRITE_RUNTIME_H
#define MP_SPRITE_RUNTIME_H
#include "render_abi.h"

/* Derived presentation records for item APIs and owned beam pieces.
 * Tokens identify owner/family; they are never hardware SAT addresses. */
void mp_sprite_submit(unsigned short token, short x, short y,
                       unsigned short size, unsigned short attr);
void mp_sprite_clear_from(unsigned char player);
void mp_sprite_clear_beam(unsigned char player);
unsigned short mp_sprite_emit(unsigned short first, unsigned short last,
                               unsigned char players);
extern unsigned char g_mp_sprite_dropped;
/* Rebuild canonical links before composing, then budget the completed chain.
 * Fixed bodies/masks and the captured hand keep their relative priority. */
void mp_sprite_restore_links(unsigned char players);
void mp_sprite_limit_lines(unsigned char players, unsigned char phase,
                           unsigned char captured_hand);
extern unsigned char g_mp_sprite_line_dropped;
/* T-398 sprite-overflow policy for mp_sprite_limit_lines (values match
 * OPTIONS_SPRITES_*). SMART: untouched chain unless an 8-line band can
 * exceed 16 sprites/32 cells, then NES. WHOLE: exact whole-sprite admission.
 * NES: rotate the dynamic chain by phase; the VDP drops per line. Captured
 * hands and non-contiguous chains always take WHOLE. */
#define MP_SPRITE_MODE_SMART 0u
#define MP_SPRITE_MODE_WHOLE 1u
#define MP_SPRITE_MODE_NES   2u
extern unsigned char g_mp_sprite_overflow_mode;
/* T-397: SMART/NES band test without a SAT walk. A sweep that built the
 * canonical chain (every link slot + 1 up to a link-0 terminator) names its
 * length, terminator included; the next limiter call consumes it and charges
 * the dynamic slots in one linear pass while the fixed prefix keeps its own
 * ledger. A frame without it (ring overlay, other writers) walks the chain. */
#define MP_SPRITE_BANDS 28u
void mp_sprite_chain_built(unsigned char length);
/* Ring snapshots are presentation only. Publish after a world sweep;
 * the first visible compose captures its SAT;
 * repeated compose calls animate frozen1P without touching object state. */
void mp_sprite_overlay_capture(unsigned char hand);
void mp_sprite_overlay_retire(void);
void mp_sprite_overlay_compose(const render_sprite_entry_t *entries, unsigned char count,
                                unsigned char phase);
extern volatile unsigned char g_ring_sprite_requested, g_ring_sprite_admitted,
    g_ring_sprite_dropped, g_ring_gameplay_dropped;
#endif
