/*
 * Render API public surface (S1 Phase D, Task D1).
 *
 * Owned C in src/frontend/ and src/game/ calls render_* functions
 * here instead of writing raw VDP control words. The implementation
 * (src/sgdk_adapter/render_adapter.c) initially forwards to existing
 * vdp_* helpers in src/frontend/intro/intro_common.c; Phase F migrates
 * those forwarders onto SGDK API (VDP_*, DMA_*, PAL_*) per call site.
 *
 * Spec ref: 2026-04-27-native-genesis-rewrite-design.md Section 4.1, 4.5
 */

#ifndef RENDER_ABI_H
#define RENDER_ABI_H

/* ---- Plane A tilemap write ----
 * (col, row) in tile units. Plane A is 64x32 in current H32 mode;
 *   col in [0, 63], row in [0, 31].
 * word is the VDP nametable entry: priority<<15 | palette<<13 |
 *   vflip<<12 | hflip<<11 | tile_index. */
void render_set_plane_a_word(unsigned short col, unsigned short row,
                             unsigned short word);

/* ---- Plane B tilemap write (PR-2: V scroll staging) ----
 * Same word format as plane A. Plane B occupies VRAM $E000-$EFFF in
 * RoomRom 64x32 layout (post PR-2). Used by V scroll transitions to
 * stage the incoming room while plane A still shows the old room. */
/* T-172: queue single-cell plane writes (render_set_plane_a/b_word) while
 * on; render_plane_defer_flush writes them (NES NMI timing). */
void render_plane_defer(unsigned char on);
void render_plane_defer_flush(void);
void render_set_plane_b_word(unsigned short col, unsigned short row,
                             unsigned short word);

/* ---- Palette load (full 16-color palette) ----
 * idx in [0, 3] selects which CRAM palette slot.
 * src points to 16 little-endian VDP color words (BGR-444 in CRAM order). */
void render_load_palette(unsigned short idx, const unsigned short *src);

/* ---- CHR (tile pattern) upload to VRAM ----
 * vram_addr is the byte address within VRAM (0..0xFFFF).
 * src points to packed 4bpp tile data; byte_count is the source size. */
void render_chr_upload(unsigned short vram_addr,
                       const unsigned char *src,
                       unsigned short byte_count);
/* Queue a VRAM word run for the next VBlank DMA; src (even, RAM) must
 * stay unchanged until then. Returns 0 when the queue is full. */
unsigned char render_vram_queue_words(unsigned short vram_addr,
                                      const unsigned short *src,
                                      unsigned short word_count);

/* ---- Raw VRAM streaming (F3: title cutover) ----
 *
 * These calls give callers a write cursor into VRAM without touching
 * VDP registers themselves.  The adapter owns the MMIO.
 *
 * render_vram_open_write  -- set VRAM write address; leaves port open.
 * render_vram_write_word  -- stream one word at the current VRAM cursor.
 * render_vram_write_words -- stream count words from src[] at the cursor.
 */
void render_vram_open_write(unsigned short vram_addr);
void render_vram_write_word(unsigned short word);
void render_vram_write_words(const unsigned short *src, unsigned short count);

/* ---- CRAM single-slot write (F3: title cutover) ----
 *
 * render_cram_open_write   -- set CRAM write cursor to byte-offset slot*2.
 * render_cram_write_color  -- open slot and write one color word (combined).
 * render_cram_upload       -- open CRAM at offset 0 and stream count words.
 */
void render_cram_open_write(unsigned short slot);
void render_cram_write_color(unsigned short slot, unsigned short value);
void render_cram_upload(const unsigned short *src, unsigned short count);
/* T-168: 1 = CRAM writes go out in the next VBlank (SGDK DMA queue, one
 * transfer per frame; gameplay), 0 = at once (front ends that wait for
 * VBlank themselves). Switching to 0 sends the current colors at once. */
void render_cram_defer(unsigned char on);
void render_cram_defer(unsigned char on);

/* Phase 12.2 SGDK-1 cleanup: load count colors starting at CRAM
 * start_slot (palette-color index 0..63). Used for sub-palette swaps
 * without rewriting the full 16-color palette. */
void render_cram_subrange_upload(unsigned short start_slot,
                                 const unsigned short *src,
                                 unsigned short count);

/* Phase 12.2 SGDK-1 cleanup: Window-plane HUD writes. Wraps SGDK
 * VDP_setTileMapXY(WINDOW, ...) / VDP_clearTileMapRect(WINDOW, ...).
 * Used by src/game/hud/ which is SGDK-1-forbidden from direct calls. */
void render_set_window_word(unsigned short col, unsigned short row,
                            unsigned short word);
void render_clear_window_rect(unsigned short col, unsigned short row,
                              unsigned short w, unsigned short h);

/* V2.4k Window-plane position toggle. NES Z1 gameplay HUD top;
 * inventory subscreen HUD bottom. */
void render_set_window_on_top(unsigned short rows);
/* At the next render_plane_defer_flush (VBlank): copy `rows` whole
 * Window-plane rows from src_row to dst_row (VDP DMA copy; rows must not
 * overlap), then show the window on the top (bottom = 0) or bottom
 * `win_rows` rows. */
void render_window_move_deferred(unsigned short src_row, unsigned short dst_row,
                                 unsigned short rows, unsigned char bottom,
                                 unsigned short win_rows);
void render_set_window_on_bottom(unsigned short rows);
/* Stage the paused HUD and visible room beneath the menu at next VBlank. */
void render_pause_scene_deferred(unsigned short menu_col,
                                 unsigned short room_col, unsigned short room_row);

/* Phase 12.2 SGDK-1 cleanup: VRAM word read. Used by src/game/dungeon/
 * uw_render for plane-readback during scroll diffs. Wraps the
 * VDP_CTRL_LONG read-control formula + VDP_DATA_WORD fetch. */
unsigned short render_vram_read_word(unsigned short vram_addr);

/* Phase 12.2 SGDK-1 cleanup: sprite SAT slot write. Wraps SGDK
 * VDP_setSpriteFull. slot 0..79; x/y are screen-space coords;
 * size is SPRITE_SIZE-encoded width/height nibble; attr is the
 * VDP sprite attribute word (priority<<15 | palette<<13 |
 * vflip<<12 | hflip<<11 | tile_index); link is the SAT next-slot
 * link byte. */
void render_set_sprite_full(unsigned short slot,
                            signed short   x,
                            signed short   y,
                            unsigned short size,
                            unsigned short attr,
                            unsigned short link);

/* Phase 12.2 SGDK-1 cleanup: trigger SAT DMA upload for first `count`
 * sprite slots. Wraps VDP_updateSprites(count, DMA_QUEUE). */
void render_update_sprites(unsigned short count);

/* 2026-05-15 perf: inline SAT cache writer. PC histogram showed ~30%
 * of frame in the render_set_sprite_full + VDP_setSpriteFull function
 * chain when 11 enemies were alive (54 SAT writes/frame). Inlining the
 * 5 struct-field writes directly bypasses two layers of function call
 * overhead per write. SGDK's vdpSpriteCache[] layout is 8 bytes per
 * entry, exposed here without pulling <genesis.h> into src/game/. */
typedef struct {
    signed short   y;          /* offset 0; on-VDP y = y + 0x80         */
    unsigned char  size;       /* offset 2; SPRITE_SIZE-encoded nibble  */
    unsigned char  link;       /* offset 3; next-SAT-slot link byte     */
    unsigned short attribut;   /* offset 4; prio/pal/flip/tile word     */
    signed short   x;          /* offset 6; on-VDP x = x + 0x80         */
} render_sprite_entry_t;
extern render_sprite_entry_t *const g_render_sat_cache;

static inline void render_set_sprite_inline(unsigned short slot,
                                            signed short  x,
                                            signed short  y,
                                            unsigned short size,
                                            unsigned short attr,
                                            unsigned char link)
{
    render_sprite_entry_t *e = &g_render_sat_cache[slot];
    e->y        = (signed short)(y + 0x80);
    e->size     = (unsigned char)size;
    e->link     = link;
    e->attribut = attr;
    e->x        = (signed short)(x + 0x80);
}

/* Phase 12.2 SGDK-1 cleanup: portable replacements for SGDK
 * SPRITE_SIZE / TILE_ATTR_FULL / PAL0..PAL3 macros so src/game/
 * TUs do not need <genesis.h>. Bit-math identical to SGDK. */
#define RENDER_PAL0 0u
#define RENDER_PAL1 1u
#define RENDER_PAL2 2u
#define RENDER_PAL3 3u
#define RENDER_SPRITE_SIZE(w, h) \
    ((unsigned short)((((w)-1) << 2) | ((h)-1)))
#define RENDER_TILE_ATTR_FULL(pal, prio, vflip, hflip, tile) \
    ((unsigned short)( \
        ((prio) ? 0x8000u : 0u) | \
        (((unsigned short)(pal) & 0x3u) << 13) | \
        ((vflip) ? 0x1000u : 0u) | \
        ((hflip) ? 0x0800u : 0u) | \
        ((unsigned short)(tile) & 0x07FFu) \
    ))

/* ---- VSRAM write (F3: title cutover) ----
 *
 * render_vsram_open_write -- set VSRAM write cursor to byte-offset slot*2.
 * render_vsram_write_word -- write one word at the open VSRAM cursor.
 */
void render_vsram_open_write(unsigned short slot);
void render_vsram_write_word(unsigned short value);

/* ---- Plane bulk helpers (F3: title cutover) ----
 *
 * render_plane_fill          -- fill tile_count nametable words starting at
 *                               plane_base with fill_word.
 * render_plane_a_write_row   -- write count cells to row of Plane A.
 *                               Plane A nametable base is fixed at $C000.
 */
void render_plane_fill(unsigned short plane_base, unsigned short fill_word,
                       unsigned short tile_count);
/* T-118: count cells down a Plane A column, wrapping at plane_rows. */
/* T-125: count cells of one plane row (A, or B when plane_b) from col set
 * to word, one VDP address per call. */
void render_plane_fill_row(unsigned char plane_b, unsigned short col,
                           unsigned short row, unsigned short count,
                           unsigned short word);
/* T-134: queue count cells of Plane A row `row` from col for the VBlank
 * DMA (cells must stay valid until then). Returns 0 if the queue is full. */
unsigned char render_plane_a_queue_row(unsigned short row, unsigned short col,
                                       const unsigned short *cells,
                                       unsigned short count);
/* T-125: count words of VRAM from vram_addr into dst (one address set). */
void render_vram_read_run(unsigned short vram_addr, unsigned short *dst,
                          unsigned short count);
void render_plane_a_write_col(unsigned short col, unsigned short row,
                              const unsigned short *cells, unsigned short count,
                              unsigned short plane_rows);
/* Clear `rows` whole plane rows (all 64 cells) from `row` to word 0 with
 * one VDP DMA fill (waits for it). Same cells as render_plane_fill_row per
 * row with col 0 and `width` cells (CPU rows unless width fills the row). */
void render_plane_clear_full_rows(unsigned char plane_b, unsigned short row,
                                  unsigned short rows, unsigned short width);
/* Write count (even) cells along Plane A row `row` from column col
 * (no wrap), two cells per long write, interrupts masked. */
void render_plane_a_write_run(unsigned short col, unsigned short row,
                              const unsigned short *cells, unsigned short count);
/* As above, cells read src_stride words apart. */
void render_plane_a_write_col_strided(unsigned short col, unsigned short row,
                                      const unsigned short *cells,
                                      unsigned short count,
                                      unsigned short plane_rows,
                                      unsigned short src_stride);
void render_plane_a_write_row(unsigned short row, const unsigned short *cells,
                              unsigned short count);

/* ---- F4 additions: IO primitives consolidated from intro_common.c ----
 *
 * render_display_enable    -- write VDP Reg 1: 1=display on, 0=display off.
 * render_vscroll_set       -- write value to VSRAM slot 0 (plane A vscroll).
 * render_plane_write_row   -- write 32 cells to row of any plane (A or B).
 *                             plane_base = $C000 (A) or $E000 (B).
 * render_mode_set_v32      -- set VDP Reg 16 to H32xV32 ($9000).
 * render_mode_set_v64      -- set VDP Reg 16 to H64xV64 ($9011).
 * render_mode_set_h64v32   -- set VDP Reg 16 to H64xV32 ($9001).
 *                             RoomRom PR-2 layout: 64-wide tilemap (128 B
 *                             stride) but only 32 rows tall (4 KB plane,
 *                             half of V64). Frees $A800-$BFFF for CHR.
 * render_z80_bus_grab      -- assert Z80 bus request; spin until ACKed.
 * render_z80_bus_release   -- release Z80 bus request.
 * render_irq_mask          -- raise SR IPL to 7 (mask all interrupts).
 * render_irq_unmask        -- lower SR IPL to 0 (unmask all interrupts).
 */
void render_display_enable(unsigned char on);
void render_vscroll_set(unsigned short value);
/* Zero the vertical scroll of planes A and B (VSRAM slots 0-1); IRQ-safe. */
void render_vscroll_reset(void);
/* SGDK scroll for both aliased gameplay planes; immediate VBlank use. */
void render_scene_scroll_set(short horizontal, short vertical);
void render_plane_write_row(unsigned short plane_base, unsigned short row,
                            const unsigned short *cells, unsigned short count);
void render_mode_set_v32(void);
void render_mode_set_v64(void);
void render_mode_set_h64v32(void);
void render_z80_bus_grab(void);
void render_z80_bus_release(void);
void render_irq_mask(void);
void render_irq_unmask(void);

/* ---- F5 additions: FS frontend cutover primitives ----
 *
 * render_cram_open_write_byte -- open CRAM write cursor at a raw byte address
 *                                (0, 32, 64, 96 for palettes 0..3).  Honest
 *                                about CRAM's byte-addressable nature; avoids
 *                                the /2 conversion at call sites that already
 *                                work in byte space.
 * render_sat_write            -- write one complete SAT entry (8 bytes = 4 words)
 *                                at SAT slot entry.  Computes the VRAM address
 *                                from sat_base + entry*8 and streams the four
 *                                words: y, size_link, tile_attr, x.
 *                                sat_base is the VRAM byte address of the SAT
 *                                (typically $F800 in our boot config).
 * render_vram_write_zero_tile -- zero 16 words (32 bytes = one tile) at
 *                                VRAM address vram_addr.  Used to blank tile 0
 *                                so default nametable cells render transparent.
 */
void render_cram_open_write_byte(unsigned short byte_addr);
void render_sat_write(unsigned short sat_base, unsigned char entry,
                      unsigned short y, unsigned short size_link,
                      unsigned short tile_attr, unsigned short x);
void render_vram_write_zero_tile(unsigned short vram_addr);

/* ---- Frame sync + register pokes (F6: handoff cleanup) ----
 *
 * render_wait_vblank   -- spin on VDP status until VBlank (bit 3) cycles
 *                         from set to clear (rising-edge trigger). Used
 *                         by frontends that don't run from VBlankISR.
 * render_window_v_set  -- set VDP register 18 (window vertical offset)
 *                         to value. value=0 turns Window plane off; non-
 *                         zero sets the row count (top of screen).
 */
void render_wait_vblank(void);
void render_window_v_set(unsigned char value);

/* ---- Sprite attribute table clear ----
 *
 * render_sat_clear -- zero all 80 sprite slots (VRAM $FC00..$FE7F, 640 bytes).
 * Used by frontend handoffs to drop the previous scene's sprites cleanly
 * before the next scene rebuilds its SAT. Without this, title-screen
 * sprite-list waterfall + ZELDA fragments persist on top of File Select
 * for the duration of fs_init's CHR upload + palette load.
 */
void render_sat_clear(void);

/* ---- CRAM fade-to-black ----
 *
 * render_cram_fade_capture  -- snapshot the current 64 CRAM color words
 *                              into an internal shadow buffer.
 * render_cram_fade_apply    -- write a dimmed CRAM frame: each color is
 *                              snapshot[i] * (total-step)/total per channel,
 *                              snapped back to Genesis even-step quantization
 *                              (0,2,4,...,E). step=0 -> full color, step=total
 *                              -> all black.
 *
 * Usage (caller drives N-frame fade in a wait_vblank loop):
 *
 *     render_cram_fade_capture();
 *     for (s = 1; s <= STEPS; s++) {
 *         render_wait_vblank();
 *         render_cram_fade_apply(s, STEPS);
 *     }
 *
 * Works from ANY palette state because the snapshot reads live CRAM via
 * VDP CRAM-read mode. Used by intro_start_pressed to dim whatever the
 * current scene is showing (title / fade / black / story / items) before
 * the FS handoff blanks the planes.
 */
void render_cram_fade_capture(void);
void render_cram_fade_apply(unsigned char step, unsigned char total);

/* Read `count` CRAM color words starting at slot 0 (VDP CRAM-read mode).
 * Used by the PPUMASK grayscale consumer to snapshot live colors. */
void render_cram_read(unsigned short *dst, unsigned short count);

/* PPUMASK grayscale: route every CRAM write (and re-upload all 64 colors
 * now) through a 512-entry table indexed by the 9 color bits
 * (BBB GGG RRR); NULL turns it off. The adapter keeps a CRAM shadow. */
void render_cram_set_grayscale(const unsigned short *word_to_gray);

/* ---- Genesis ROM bank-window cache ----
 *
 * render_bank_window_load(bank) -- ensure the M68K bank-window at
 *   $FF8000 mirrors PRG bank `bank` (0-7). No-op if already cached;
 *   else copies bank ROM bytes into RAM window. Bank 7 is exempt
 *   (always direct-mapped per genesis_shell.asm).
 *
 *   Genesis-native ROM access primitive — mirrors what the M68K
 *   boot path uses for bank-windowed asset reads. This is NOT NES
 *   MMC1 emulation; the bank window is a Genesis-cartridge feature
 *   that pre-stages ROM bytes into addressable RAM for code that
 *   reads via $FF8000+offset.
 *
 *   Wraps the asm primitive `_copy_bank_to_window` (nes_io.asm:1719)
 *   with a clean C-callable interface. Native code in src/game/
 *   calls this; no c_-prefixed shim. */
void render_bank_window_load(unsigned char bank);

/* ---- Phase Q v2 (2026-05-19): DMA byte-count telemetry ----
 *
 * Per-frame accumulator for VRAM + CRAM upload bytes. Set by every
 * render_chr_upload / render_cram_upload / render_cram_subrange_upload
 * call (and the raw stream helpers if used in a loop). Probe code
 * calls render_dma_stats_frame_end() at the per-frame boundary
 * (immediately after SYS_doVBlankProcess) to roll the current-frame
 * count into the peak tracker and reset to 0.
 *
 * total_uploads = lifetime call count across all upload primitives.
 * peak_frame_bytes records the max-bytes-in-one-frame since reset.
 * current_frame_bytes is the running tally for the in-progress frame.
 *
 * VBlank ceiling per SGDK dma.h ≈ 7790 B (Z80-pause-aware budget).
 * peak_frame_bytes > 7790 indicates VBlank overflow risk. */
typedef struct {
    unsigned long current_frame_bytes;
    unsigned long peak_frame_bytes;
    unsigned long total_uploads;
} render_dma_stats_t;

void render_dma_stats_get(render_dma_stats_t *out);
void render_dma_stats_reset(void);   /* zero peak + total + current */
void render_dma_stats_frame_end(void); /* roll current into peak, reset current */

#endif /* RENDER_ABI_H */

/* Restore both gameplay planes and the fixed HUD together at VBlank. */
void render_menu_restore_deferred(short horizontal, short vertical, unsigned short window_rows);
