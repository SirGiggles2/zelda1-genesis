/*
 * Render adapter implementation (S1 Phase D/F).
 *
 * Phase D: forwards render_set_plane_a_word / render_load_palette /
 *   render_chr_upload to existing vdp_ helpers in intro_common.c.
 * Phase F3: adds raw VDP streaming helpers used by intro_title.c after
 *   its local VDP macros are removed.  These inline the same MMIO writes
 *   the title file used to do directly; the adapter is the IO primitive
 *   layer per spec Section 4.
 * Phase F4: consolidates all vdp_* IO primitives from intro_common.c into
 *   this file.  Renames them to render_* per the long-term clean naming
 *   convention.  extern vdp_dma_to_vram / vdp_load_cram forwarders removed.
 *
 * Phase S11+ swap: replace the VDP_CTRL_LONG / VDP_DATA_WORD lines with
 * SGDK VDP_*, DMA_*, PAL_* calls.
 */

#include "render_adapter.h"

/* Phase 12.2 SGDK-1 cleanup: adapter MAY include <genesis.h>; only
 * src/game/ + src/frontend/ are forbidden from direct SGDK. Used by
 * the Window-plane HUD wrappers below. */
#include <genesis.h>

/* VDP MMIO addresses used by all helpers below. */
#define VDP_DATA_WORD (*(volatile unsigned short *)0x00C00000)
#define VDP_CTRL_WORD (*(volatile unsigned short *)0x00C00004)
#define VDP_CTRL_LONG (*(volatile unsigned long  *)0x00C00004)

/* Z80 bus control registers. */
#define Z80_BUSREQ_WORD (*(volatile unsigned short *)0x00A11100)

/* VDP plane A nametable base for the native title / RoomRom layouts. */
#define PLANE_A_BASE 0xC000u

/* VDP plane B nametable base — used by PR-2 V scroll staging.
 * RoomRom 64x32 layout post PR-2: BGA @ $C000, Window @ $D000,
 * BGB @ $E000. Title H32 layout sets BGB via reg 4 = $07 ($E000) too. */
#define PLANE_B_BASE 0xE000u

/* Phase Q v2 (2026-05-19): DMA byte-count telemetry storage. Defined
 * up here so the static inline accumulator helper is visible to every
 * upload primitive below. Public getters at end of file. */
static unsigned long s_dma_current_frame_bytes = 0UL;
static unsigned long s_dma_peak_frame_bytes = 0UL;
static unsigned long s_dma_total_uploads = 0UL;

static inline void dma_stats_record(unsigned long bytes)
{
    s_dma_current_frame_bytes += bytes;
    s_dma_total_uploads++;
}

/* CRAM color words per palette. */
#define CRAM_COLORS_PER_PAL 16u

/* Active nametable row stride in bytes. Title runs H32/V32 (32 tiles per
 * row = 64 bytes); RoomRom runs 64x64 (64 tiles per row = 128 bytes).
 * CombinedDebug links one render ABI, so the stride has to follow the mode. */
static unsigned short s_plane_row_stride_bytes = 64u;

static void render_set_autoinc_word(void)
{
    VDP_CTRL_WORD = 0x8F02;
}

/* ---- IO primitives (moved in from intro_common.c, F4) ---- */

void render_display_enable(unsigned char on)
{
    /* Reg 1: $8134 = display OFF (VBlank IRQ, DMA, M5); $8174 = display ON. */
    VDP_CTRL_WORD = on ? (unsigned short)0x8174 : (unsigned short)0x8134;
}

void render_vscroll_set(unsigned short value)
{
    /* VSRAM write to slot 0 (plane A vscroll). */
    VDP_CTRL_LONG = 0x40000010UL;
    VDP_DATA_WORD = value;
}

void render_vscroll_reset(void)
{
    /* Both planes' full-screen vscroll words (VSRAM slots 0 = A, 1 = B),
     * with interrupts off so a VBlank handler cannot move the VDP address
     * between the control and data writes. */
    SYS_disableInts();
    render_set_autoinc_word();
    VDP_CTRL_LONG = 0x40000010UL;
    VDP_DATA_WORD = 0u;
    VDP_DATA_WORD = 0u;
    SYS_enableInts();
}

void render_scene_scroll_set(short horizontal, short vertical)
{
    VDP_setHorizontalScroll(BG_A, horizontal);
    VDP_setHorizontalScroll(BG_B, horizontal);
    VDP_setVerticalScroll(BG_A, vertical);
    VDP_setVerticalScroll(BG_B, vertical);
}

void render_plane_write_row(unsigned short plane_base, unsigned short row,
                            const unsigned short *cells, unsigned short count)
{
    /* plane_base = $C000 (A) or $E000 (B). */
    unsigned short addr = (unsigned short)(plane_base + row * s_plane_row_stride_bytes);
    VDP_CTRL_LONG = 0x40000000UL
                  | ((unsigned long)(addr & 0x3FFFu) << 16)
                  | ((addr >> 14) & 0x0003u);
    while (count--) VDP_DATA_WORD = *cells++;
}

void render_mode_set_v32(void)
{
    /* VDP Reg 16 = $9000 (H32 x V32). Row stride = 64 bytes. */
    s_plane_row_stride_bytes = 64u;
    VDP_CTRL_WORD = 0x9000;
}

void render_mode_set_v64(void)
{
    /* VDP Reg 16 = $9011 (H64 x V64). Matches gameplay default. */
    s_plane_row_stride_bytes = 128u;
    VDP_CTRL_WORD = 0x9011;
}

void render_mode_set_h64v32(void)
{
    /* VDP Reg 16 = $9001 (H64 x V32). RoomRom PR-2 64x32 plane mode:
     * 64-wide stride (128 B/row) but 4 KB plane size, freeing 192 tiles
     * vs V64. */
    s_plane_row_stride_bytes = 128u;
    VDP_CTRL_WORD = 0x9001;
}

void render_z80_bus_grab(void)
{
    /* Hold Z80 bus so 68K has uncontended VRAM access. */
    Z80_BUSREQ_WORD = 0x0100;
    while ((Z80_BUSREQ_WORD & 0x0100) != 0) { /* wait BUSACK */ }
}

void render_z80_bus_release(void)
{
    Z80_BUSREQ_WORD = 0x0000;
}

void render_irq_mask(void)
{
    __asm__ volatile ("ori.w #0x0700,%sr");
}

void render_irq_unmask(void)
{
    __asm__ volatile ("andi.w #0xF8FF,%sr");
}

void render_wait_vblank(void)
{
    /* Wait for VBlank rising edge: drain through any in-progress vblank,
     * then spin until status bit 3 is set again. Used by frontends that
     * do not run from VBlankISR. */
    while ( (VDP_CTRL_WORD & 0x0008));
    while (!(VDP_CTRL_WORD & 0x0008));
}

void render_window_v_set(unsigned char value)
{
    /* VDP Reg 18: window vertical position byte. value=0 disables the
     * Window plane vertically; non-zero sets row count (high bit chooses
     * up vs down direction). genesis_shell.asm uses 0x9208 for 8-row top
     * window during transpiled gameplay HUD; intro_main resets to 0x9200. */
    VDP_CTRL_WORD = (unsigned short)(0x9200u | value);
}

void render_sat_clear(void)
{
    /* SAT lives at VRAM $FC00 in our genesis_shell.asm boot (VDP reg 5 =
     * $7E for H32 mode, 80 sprite slots * 8 bytes each = 640 bytes).
     * Zero every slot. Slot 0 with y=0 link=0 attr=0 x=0 is the standard
     * "end of sprite list" terminator and produces an off-screen sprite. */
    render_vram_open_write(0xFC00u);
    /* 640 bytes = 320 words. */
    unsigned short i;
    for (i = 0; i < 320u; i++) {
        VDP_DATA_WORD = 0;
    }
}

/* CRAM read access:
 *   VDP control word $00000020 = CD bits for CRAM read at byte addr 0.
 *   Auto-increment (VDP reg 15 = 2) drives sequential reads. After
 *   genesis_shell.asm boot, auto-inc is locked at 2.
 * CRAM write access:
 *   $C0000000 base for CRAM write at offset 0 (existing render_cram_open
 *   formula). */

static unsigned short s_fade_snapshot[64];

/* CRAM shadow + PPUMASK grayscale (T-114/T-080).
 *
 * Every gameplay CRAM write goes through this adapter, so the shadow holds
 * the intended colors. While a grayscale table is set, each write (and a
 * full re-upload on the edge) goes to CRAM through the table: grayscale is
 * a display post-process on the NES, palette writes during it still land.
 * Replaces a 64-word VDP read-back per edge (~35 scanlines in a busy UW
 * room, which pushed bomb-flash frames over budget). */
static unsigned short s_cram_shadow[64];

static const unsigned short *s_cram_gray = (const unsigned short *)0;

static unsigned short cram_out(unsigned short w)
{
    if (s_cram_gray == (const unsigned short *)0) return w;
    return s_cram_gray[((w >> 1) & 0x7u) | ((w >> 2) & 0x38u) | ((w >> 3) & 0x1C0u)];
}

/* T-168: in gameplay CRAM goes out in VBlank, as the NES only writes its
 * palette in the NMI. A CRAM write while the VDP draws changes the colors
 * from that line down for one frame (and shows a CRAM dot streak): room
 * transitions wrote palettes at lines 17-178 (probe $01FA). Deferred mode:
 * writes update the shadow and s_cram_dma, and one CRAM DMA per frame is
 * queued for SGDK's VBlank process (the buffer is read then, so later
 * writes in the same frame are included). The front ends (title, File
 * Select) wait for VBlank themselves and never run that process: they
 * keep immediate writes (render_cram_defer(0)). */
static unsigned short s_cram_dma[64];
static unsigned char s_cram_defer = 0u;
static unsigned long s_cram_queued_vt = 0xFFFFFFFFul;

static void cram_queue(void)
{
    if (s_cram_queued_vt == vtimer) return;
    DMA_queueDma(DMA_CRAM, s_cram_dma, 0u, 64u, 2u);
    s_cram_queued_vt = vtimer;
}

/* Store count colors from start_slot: shadow always; the VDP now
 * (immediate mode) or at the next VBlank (deferred). */
static void cram_store(unsigned short start_slot, const unsigned short *src,
                       unsigned short count)
{
    unsigned short slot = start_slot;
    if (s_cram_defer) {
        while (count--) {
            s_cram_shadow[slot & 0x3Fu] = *src;
            s_cram_dma[slot++ & 0x3Fu] = cram_out(*src++);
        }
        cram_queue();
        return;
    }
    render_set_autoinc_word();
    VDP_CTRL_LONG = 0xC0000000UL
                  | (((unsigned long)(start_slot * 2u) & 0x3FFFu) << 16);
    while (count--) {
        s_cram_shadow[slot++ & 0x3Fu] = *src;
        VDP_DATA_WORD = cram_out(*src++);
    }
}

/* Re-send the whole shadow through the grayscale table (deferred or now). */
static void cram_resend_all(void)
{
    unsigned char i;
    if (s_cram_defer) {
        for (i = 0u; i < 64u; i++) s_cram_dma[i] = cram_out(s_cram_shadow[i]);
        cram_queue();
        return;
    }
    render_set_autoinc_word();
    VDP_CTRL_LONG = 0xC0000000UL;
    for (i = 0u; i < 64u; i++) VDP_DATA_WORD = cram_out(s_cram_shadow[i]);
}

void render_cram_defer(unsigned char on)
{
    if (s_cram_defer && !on) {
        s_cram_defer = 0u;
        cram_resend_all();          /* the front end draws at once */
        return;
    }
    if (!s_cram_defer && on) {
        unsigned char i;
        for (i = 0u; i < 64u; i++) s_cram_dma[i] = cram_out(s_cram_shadow[i]);
        s_cram_queued_vt = 0xFFFFFFFFul;
    }
    s_cram_defer = on;
}

void render_cram_set_grayscale(const unsigned short *word_to_gray)
{
    s_cram_gray = word_to_gray;
    cram_resend_all();
}

void render_cram_fade_capture(void)
{
    unsigned char i;
    if (s_cram_defer) {             /* the VDP may still hold last frame's */
        for (i = 0; i < 64u; i++) s_fade_snapshot[i] = s_cram_shadow[i];
        return;
    }
    VDP_CTRL_LONG = 0x00000020UL;
    for (i = 0; i < 64u; i++) {
        s_fade_snapshot[i] = VDP_DATA_WORD;
    }
}

void render_cram_fade_apply(unsigned char step, unsigned char total)
{
    if (total == 0u) return;
    if (step > total) step = total;
    unsigned short remaining = (unsigned short)(total - step);
    unsigned char i;
    unsigned short faded[64];
    for (i = 0; i < 64u; i++) {
        unsigned short c = s_fade_snapshot[i];
        /* Each channel: 4-bit value in low 4 of nibble, even-step encoded
         * (0, 2, 4, 6, 8, 10, 12, 14). Mask off the high bit of each nibble
         * since CRAM ignores it on read but the snapshot may carry junk. */
        unsigned short b = (c >> 8) & 0x000Eu;
        unsigned short g = (c >> 4) & 0x000Eu;
        unsigned short r = (c >> 0) & 0x000Eu;
        b = (unsigned short)((b * remaining) / total) & 0x000Eu;
        g = (unsigned short)((g * remaining) / total) & 0x000Eu;
        r = (unsigned short)((r * remaining) / total) & 0x000Eu;
        faded[i] = (unsigned short)((b << 8) | (g << 4) | r);
    }
    cram_store(0u, faded, 64u);
}

void render_cram_read(unsigned short *dst, unsigned short count)
{
    unsigned short i = 0u;
    if (s_cram_defer) {             /* intended colors (maybe not sent yet) */
        while (count--) *dst++ = s_cram_shadow[i++ & 0x3Fu];
        return;
    }
    render_set_autoinc_word();
    VDP_CTRL_LONG = 0x00000020UL;
    while (count--) *dst++ = VDP_DATA_WORD;
}

/* ---- Internal DMA / CRAM helpers (were extern'd from intro_common.c) ---- */

/* CPU-based VRAM upload. Writes len bytes from src to VRAM[dst..dst+len-1].
 * Slower than DMA. Interrupts MUST be masked during the loop — VBlank/NMI
 * handler doing its own VDP_CTRL writes mid-stream would clobber our
 * auto-increment address register, scattering ~2 KB of bytes to wrong
 * VRAM slots. Observed via atlas byte-audit: bank-swap of 17024-byte BG
 * sparse blob left a 2304-byte hole at slots 436..507 every first swap
 * because the VBlank handler interrupted us. Mask IPL to 7 for the loop. */
static void vram_dma_upload(const unsigned char *bytes, unsigned short dst,
                            unsigned short len)
{
    SYS_disableInts();

    /* T-118: word-aligned sources (nearly all CHR blobs) go through a
     * real 68k->VRAM DMA (the 68000 is halted for the transfer; SGDK
     * splits at the 128 KB source bank boundary). The byte loop below was
     * ~7 instructions per word and, with interrupts masked, stretched a
     * dungeon-entry CHR swap over dozens of frames. Odd-address sources
     * keep the byte path (word reads there would address-error). */
    if ((((unsigned long)bytes) & 1UL) == 0UL && len >= 2u) {
        DMA_doDma(DMA_VRAM, (void *)bytes, dst, (unsigned short)(len >> 1), 2);
        if ((len & 1u) != 0u) {
            render_set_autoinc_word();
            VDP_CTRL_LONG = 0x40000000UL
                          | ((unsigned long)((dst + len - 1u) & 0x3FFFu) << 16)
                          | (((dst + len - 1u) >> 14) & 0x0003u);
            VDP_DATA_WORD = (unsigned short)((unsigned short)bytes[len - 1u] << 8);
        }
        SYS_enableInts();
        return;
    }

    render_set_autoinc_word();

    /* Open VRAM write at dst. */
    unsigned long cmd = 0x40000000UL | ((unsigned long)(dst & 0x3FFFu) << 16)
                                     | ((dst >> 14) & 0x0003u);
    VDP_CTRL_LONG = cmd;

    for (unsigned short i = 0; i + 1u < len; i = (unsigned short)(i + 2u)) {
        unsigned short hi = bytes[i];
        unsigned short lo = bytes[i + 1u];
        VDP_DATA_WORD = (unsigned short)((hi << 8) | lo);
    }

    if ((len & 1u) != 0u) {
        VDP_DATA_WORD = (unsigned short)((unsigned short)bytes[len - 1u] << 8);
    }

    SYS_enableInts();
}

/* ---- Phase D public API ---- */

/* T-172: NES name-table transfers reach VRAM in the NMI that starts the
 * next frame; the transfer-buffer drain runs at the end of a Genesis tick,
 * so its cells showed one frame early (t166_l1_oldman t836 text, t057_grumble
 * t791 blank lines). While deferring, single-cell plane writes queue here
 * and render_plane_defer_flush (next tick, in VBlank) writes them. A full
 * queue flushes early rather than dropping a cell. */
#define PLANE_DEFER_MAX 192u
static unsigned short s_pd_addr[PLANE_DEFER_MAX];
static unsigned short s_pd_word[PLANE_DEFER_MAX];
static unsigned char s_pd_count;
static unsigned char s_pd_on;

static void plane_word_now(unsigned short addr, unsigned short word)
{
    VDP_CTRL_LONG = 0x40000000UL
                  | ((unsigned long)(addr & 0x3FFFu) << 16)
                  | ((addr >> 14) & 0x0003u);
    VDP_DATA_WORD = word;
}

static void window_move_flush(void);
static unsigned char s_wm_pending;
static void pause_scene_flush(void);
static unsigned char s_ps_pending;
static unsigned char s_menu_restore_pending;
static unsigned short s_menu_restore_rows;
static unsigned short s_ps_col, s_ps_room_col, s_ps_room_row;

void render_plane_defer_flush(void)
{
    unsigned char i;
    if (s_wm_pending) window_move_flush();
    if (s_ps_pending) pause_scene_flush();
    if (s_menu_restore_pending) {
        s_menu_restore_pending = 0u;
        VDP_setWindowOnTop(s_menu_restore_rows);
    }
    for (i = 0u; i < s_pd_count; ++i) plane_word_now(s_pd_addr[i], s_pd_word[i]);
    s_pd_count = 0u;
}

void render_plane_defer(unsigned char on)
{
    s_pd_on = on;
}

static void plane_word(unsigned short addr, unsigned short word)
{
    if (s_pd_on) {
        if (s_pd_count >= PLANE_DEFER_MAX) render_plane_defer_flush();
        s_pd_addr[s_pd_count] = addr;
        s_pd_word[s_pd_count] = word;
        s_pd_count = (unsigned char)(s_pd_count + 1u);
        return;
    }
    plane_word_now(addr, word);
}

void render_set_plane_a_word(unsigned short col, unsigned short row,
                             unsigned short word)
{
    plane_word((unsigned short)(PLANE_A_BASE + row * s_plane_row_stride_bytes + col * 2u),
               word);
}

void render_set_plane_b_word(unsigned short col, unsigned short row,
                             unsigned short word)
{
    plane_word((unsigned short)(PLANE_B_BASE + row * s_plane_row_stride_bytes + col * 2u),
               word);
}

void render_load_palette(unsigned short idx, const unsigned short *src)
{
    cram_store((unsigned short)((idx * 16u) & 0x3Fu), src, CRAM_COLORS_PER_PAL);
}

void render_chr_upload(unsigned short vram_addr,
                       const unsigned char *src,
                       unsigned short byte_count)
{
    /* vram_dma_upload takes BYTE count and byte-reads the source. Genesis
     * ROM byte assets can legally link at odd addresses; word-reading them
     * would address-error on 68000. */
    vram_dma_upload(src, vram_addr, byte_count);
    dma_stats_record((unsigned long)byte_count);
}

/* ---- Phase F3 raw streaming helpers ---- */

/* Open a VRAM write port at the given byte address.
 * Control word format: 0x40000000 | (addr[13:0] << 16) | addr[15:14]
 * (same formula intro_title.c used in its local vram_write_open). */
void render_vram_open_write(unsigned short vram_addr)
{
    render_set_autoinc_word();
    VDP_CTRL_LONG = 0x40000000UL
                  | ((unsigned long)(vram_addr & 0x3FFFu) << 16)
                  | ((vram_addr >> 14) & 0x0003u);
}

/* Stream one word to the currently open VRAM target. */
void render_vram_write_word(unsigned short word)
{
    VDP_DATA_WORD = word;
}

/* Stream count words from src[] to the currently open VRAM target. */
void render_vram_write_words(const unsigned short *src, unsigned short count)
{
    while (count--) VDP_DATA_WORD = *src++;
}

/* Open CRAM write cursor at byte-offset slot*2.
 * Control word format: 0xC0000000 | (byteaddr[13:0] << 16) | byteaddr[15:14].
 * slot is the palette-color index (0..63). */
void render_cram_open_write(unsigned short slot)
{
    unsigned long addr = (unsigned long)slot * 2u;
    render_set_autoinc_word();
    VDP_CTRL_LONG = 0xC0000000UL
                  | ((addr & 0x3FFFu) << 16)
                  | ((addr >> 14) & 0x0003u);
}

/* Open CRAM at slot and write one color word. */
void render_cram_write_color(unsigned short slot, unsigned short value)
{
    cram_store((unsigned short)(slot & 0x3Fu), &value, 1u);
}

/* Open CRAM at offset 0 and stream count color words. */
void render_cram_upload(const unsigned short *src, unsigned short count)
{
    unsigned long bytes = (unsigned long)count * 2UL;
    cram_store(0u, src, count);
    dma_stats_record(bytes);
}

/* Open CRAM at start_slot and stream count color words.
 * Phase 12.2 SGDK-1 cleanup: subrange palette load wrapper used by
 * combat sword-beam color-cycle (PAL2[0..3] swap per frame). */
void render_cram_subrange_upload(unsigned short start_slot,
                                 const unsigned short *src,
                                 unsigned short count)
{
    unsigned long bytes = (unsigned long)count * 2UL;
    cram_store((unsigned short)(start_slot & 0x3Fu), src, count);
    dma_stats_record(bytes);
}

/* Phase 12.2 SGDK-1 cleanup: VRAM word read at vram_addr.
 * Control word format for VRAM read: 0x00000000 | (addr[13:0] << 16) |
 * addr[15:14]. Used by uw_render plane_read_live_word for autoinc-2
 * VRAM scanning. */
unsigned short render_vram_read_word(unsigned short vram_addr)
{
    render_set_autoinc_word();
    VDP_CTRL_LONG = 0x00000000UL
                  | ((unsigned long)(vram_addr & 0x3FFFu) << 16)
                  | ((unsigned long)(vram_addr >> 14) & 0x0003u);
    return VDP_DATA_WORD;
}

/* Phase 12.2 SGDK-1 cleanup: sprite SAT slot wrapper. */
void render_set_sprite_full(unsigned short slot,
                            signed short   x,
                            signed short   y,
                            unsigned short size,
                            unsigned short attr,
                            unsigned short link)
{
    VDP_setSpriteFull((u16)slot, (s16)x, (s16)y,
                      (u8)size, (u16)attr, (u8)link);
}

/* 2026-05-15 perf: expose SGDK's CPU-side SAT cache as a render_abi
 * typed pointer. src/game/ can write SAT entries inline via
 * render_set_sprite_inline() (declared in render_abi.h) without
 * pulling <genesis.h>. SGDK's VDPSprite struct (sgdk/inc/vdp_spr.h)
 * is 8 bytes — matches render_sprite_entry_t exactly: s16 y,
 * u8 size, u8 link, u16 attribut, s16 x. */
render_sprite_entry_t *const g_render_sat_cache =
    (render_sprite_entry_t *)vdpSpriteCache;

/* Phase 12.2 SGDK-1 cleanup: SAT upload for `count` first slots,
 * via SGDK DMA queue. Wraps VDP_updateSprites(count, DMA_QUEUE). */
void render_update_sprites(unsigned short count)
{
    VDP_updateSprites((u16)count, DMA_QUEUE);
}

/* Phase 12.2 SGDK-1 cleanup: Window plane HUD wrappers.
 *
 * RoomRom HUD lives on the Window plane (NES status-bar parity at top
 * of screen). Underlying SGDK call: VDP_setTileMapXY(WINDOW, ...).
 * Adapter routes here so src/game/hud/ can drop <genesis.h>. */
void render_set_window_word(unsigned short col, unsigned short row,
                            unsigned short word)
{
    /* T-172: same deferral as the plane cells (status-bar changes reach
     * the NES screen in the next NMI). */
    if (s_pd_on) {
        /* = VDP_getPlaneAddress(WINDOW, col, row), inline. */
        plane_word((unsigned short)(VDP_WINDOW + (((col & (windowWidth - 1u)) +
                   ((row & 31u) << windowWidthSft)) << 1)), word);
        return;
    }
    VDP_setTileMapXY(WINDOW, word, col, row);
}

void render_clear_window_rect(unsigned short col, unsigned short row,
                              unsigned short w, unsigned short h)
{
    VDP_clearTileMapRect(WINDOW, col, row, w, h);
}

/* V2.4k (2026-05-26): Window plane position toggle for per-pause HUD
 * placement. NES Z1 gameplay HUD top; inventory subscreen HUD bottom. */
void render_set_window_on_top(unsigned short rows)
{
    VDP_setWindowOnTop(rows);
}

void render_set_window_on_bottom(unsigned short rows)
{
    VDP_setWindowOnBottom(rows);
}

/* Keep scroll and fixed-window restoration at the same VBlank boundary. */
void render_menu_restore_deferred(short horizontal, short vertical, unsigned short window_rows)
{
    VDP_setHorizontalScrollVSync(BG_A, horizontal);
    VDP_setHorizontalScrollVSync(BG_B, horizontal);
    VDP_setVerticalScrollVSync(BG_A, vertical);
    VDP_setVerticalScrollVSync(BG_B, vertical);
    s_menu_restore_rows = window_rows;
    s_menu_restore_pending = 1u;
}

void render_pause_scene_deferred(unsigned short menu_col,
                                 unsigned short room_col, unsigned short room_row)
{
    s_ps_col = menu_col;
    s_ps_room_col = room_col;
    s_ps_room_row = room_row;
    s_ps_pending = 1u;
}

/* NES Z_05 UpdateMenuScrollDown/Up move one continuous nametable strip:
 * menu, status bar, then the frozen room. Copy the existing rendered cells
 * rather than reconstructing the room (which would lose cave text/doors).
 * Both background planes share this strip; Window must not cover it. */
static void pause_scene_flush(void)
{
    unsigned short row;
    const unsigned short dst = (unsigned short)(PLANE_A_BASE + s_ps_col * 2u);
    SYS_disableInts();
    for (row = 0u; row < 28u; ++row) {
        const unsigned short src = row < 7u
            ? (unsigned short)(VDP_getWindowAddress() + row * windowWidth * 2u)
            : (unsigned short)(PLANE_A_BASE +
                ((s_ps_room_row + row - 7u) & 63u) * 128u + s_ps_room_col * 2u);
        DMA_doVRamCopy(src, (unsigned short)(dst + (22u + row) * 128u), 64u, 1);
        DMA_waitCompletion();
    }
    render_set_autoinc_word();
    VDP_setWindowOnTop(0u);
    VDP_setHorizontalScroll(BG_A, s_ps_col ? -256 : 0);
    VDP_setHorizontalScroll(BG_B, s_ps_col ? -256 : 0);
    VDP_CTRL_LONG = 0x40000010UL;
    VDP_DATA_WORD = 174u;
    VDP_DATA_WORD = 174u;
    s_ps_pending = 0u;
    SYS_enableInts();
}

static unsigned char s_wm_pending;
static unsigned short s_wm_src, s_wm_dst, s_wm_rows, s_wm_win_rows;
static unsigned char s_wm_bottom;

void render_window_move_deferred(unsigned short src_row, unsigned short dst_row,
                                 unsigned short rows, unsigned char bottom,
                                 unsigned short win_rows)
{
    s_wm_src = src_row;
    s_wm_dst = dst_row;
    s_wm_rows = rows;
    s_wm_bottom = bottom;
    s_wm_win_rows = win_rows;
    s_wm_pending = 1u;
}

static void window_move_flush(void)
{
    const unsigned short row_bytes = (unsigned short)(windowWidth * 2u);
    const unsigned short base = VDP_getWindowAddress();
    s_wm_pending = 0u;
    if (s_wm_rows) {
        SYS_disableInts();
        DMA_doVRamCopy((unsigned short)(base + s_wm_src * row_bytes),
                       (unsigned short)(base + s_wm_dst * row_bytes),
                       (unsigned short)(s_wm_rows * row_bytes), 1);
        DMA_waitCompletion();
        render_set_autoinc_word();
        SYS_enableInts();
    }
    if (s_wm_bottom) VDP_setWindowOnBottom(s_wm_win_rows);
    else             VDP_setWindowOnTop(s_wm_win_rows);
}

/* Open VSRAM write cursor at byte-offset slot*2.
 * Control word: 0x40000010 for slot 0; general form uses the same
 * slot*2 formula as CRAM but with VSRAM CD bits (0x40000010 base).
 * slot is the VSRAM word index (0..39). */
void render_vsram_open_write(unsigned short slot)
{
    unsigned long addr = (unsigned long)slot * 2u;
    render_set_autoinc_word();
    VDP_CTRL_LONG = 0x40000010UL
                  | ((addr & 0x3FFFu) << 16)
                  | ((addr >> 14) & 0x0003u);
}

/* Write one word at the open VSRAM cursor. */
void render_vsram_write_word(unsigned short value)
{
    VDP_DATA_WORD = value;
}

/* Fill tile_count nametable words at plane_base with fill_word.
 * Opens VRAM at plane_base then streams fill_word tile_count times. */
void render_plane_fill(unsigned short plane_base, unsigned short fill_word,
                       unsigned short tile_count)
{
    /* T-125: 16 cells per step as 8 long writes (a long data-port write
     * is two word writes at the auto-increment); a full 64x64 plane was
     * ~12k 68000 instructions, most of the pause-open frame freeze. */
    const unsigned long fill2 = ((unsigned long)fill_word << 16) | fill_word;
    volatile unsigned long *const port = (volatile unsigned long *)0xC00000;
    render_vram_open_write(plane_base);
    while (tile_count >= 16u) {
        *port = fill2; *port = fill2; *port = fill2; *port = fill2;
        *port = fill2; *port = fill2; *port = fill2; *port = fill2;
        tile_count = (unsigned short)(tile_count - 16u);
    }
    while (tile_count--) VDP_DATA_WORD = fill_word;
}

/* Write count cells into row of Plane A using the active mode stride. */
void render_plane_a_write_row(unsigned short row, const unsigned short *cells,
                              unsigned short count)
{
    unsigned short addr = (unsigned short)(PLANE_A_BASE + row * s_plane_row_stride_bytes);
    render_vram_open_write(addr);
    while (count--) VDP_DATA_WORD = *cells++;
}

/* T-118: write count cells down Plane A column col starting at row,
 * wrapping at plane_rows. One VDP address set per contiguous run with
 * auto-increment = plane row stride (room column fills were setting the
 * address and multiplying the row stride for every tile). Interrupts are
 * masked per run: the VBlank handler's DMA queue rewrites the VDP address
 * and auto-increment. */
void render_plane_a_write_col(unsigned short col, unsigned short row,
                              const unsigned short *cells, unsigned short count,
                              unsigned short plane_rows)
{
    const unsigned short stride = s_plane_row_stride_bytes;
    while (count) {
        unsigned short run = (unsigned short)(plane_rows - row);
        unsigned short addr;
        if (run > count) run = count;
        addr = (unsigned short)(PLANE_A_BASE + row * stride + col * 2u);
        count = (unsigned short)(count - run);
        SYS_disableInts();
        VDP_CTRL_WORD = (unsigned short)(0x8F00u | (stride & 0xFFu));
        VDP_CTRL_LONG = 0x40000000UL
                      | ((unsigned long)(addr & 0x3FFFu) << 16)
                      | ((addr >> 14) & 0x0003u);
        /* T-172: unrolled (room loads stream ~700 cells this way). */
        while (run >= 4u) {
            VDP_DATA_WORD = cells[0]; VDP_DATA_WORD = cells[1];
            VDP_DATA_WORD = cells[2]; VDP_DATA_WORD = cells[3];
            cells += 4;
            run = (unsigned short)(run - 4u);
        }
        while (run--) VDP_DATA_WORD = *cells++;
        render_set_autoinc_word();
        SYS_enableInts();
        row = 0u;
    }
}

void render_plane_a_write_run(unsigned short col, unsigned short row,
                              const unsigned short *cells, unsigned short count)
{
    const unsigned short addr = (unsigned short)(PLANE_A_BASE +
        row * s_plane_row_stride_bytes + col * 2u);
    volatile unsigned long *const port = (volatile unsigned long *)0xC00000;
    const unsigned long *src = (const unsigned long *)(const void *)cells;
    SYS_disableInts();
    render_set_autoinc_word();
    VDP_CTRL_LONG = 0x40000000UL
                  | ((unsigned long)(addr & 0x3FFFu) << 16)
                  | ((addr >> 14) & 0x0003u);
    while (count >= 8u) {
        port[0] = src[0]; port[0] = src[1]; port[0] = src[2]; port[0] = src[3];
        src += 4;
        count = (unsigned short)(count - 8u);
    }
    while (count >= 2u) {
        port[0] = *src++;
        count = (unsigned short)(count - 2u);
    }
    if (count) VDP_DATA_WORD = *(const unsigned short *)(const void *)src;
    SYS_enableInts();
}

void render_plane_a_write_col_strided(unsigned short col, unsigned short row,
                                      const unsigned short *cells,
                                      unsigned short count,
                                      unsigned short plane_rows,
                                      unsigned short src_stride)
{
    const unsigned short stride = s_plane_row_stride_bytes;
    while (count) {
        unsigned short run = (unsigned short)(plane_rows - row);
        unsigned short addr;
        if (run > count) run = count;
        addr = (unsigned short)(PLANE_A_BASE + row * stride + col * 2u);
        count = (unsigned short)(count - run);
        SYS_disableInts();
        VDP_CTRL_WORD = (unsigned short)(0x8F00u | (stride & 0xFFu));
        VDP_CTRL_LONG = 0x40000000UL
                      | ((unsigned long)(addr & 0x3FFFu) << 16)
                      | ((addr >> 14) & 0x0003u);
        while (run--) {
            VDP_DATA_WORD = *cells;
            cells += src_stride;
        }
        render_set_autoinc_word();
        SYS_enableInts();
        row = 0u;
    }
}

void render_plane_clear_full_rows(unsigned char plane_b, unsigned short row,
                                  unsigned short rows, unsigned short width)
{
    const unsigned short addr = (unsigned short)(
        (plane_b ? PLANE_B_BASE : PLANE_A_BASE) + row * s_plane_row_stride_bytes);
    const unsigned long len = (unsigned long)rows * s_plane_row_stride_bytes;
    if (width * 2u != s_plane_row_stride_bytes || len == 0u || len > 0xFFFFu) {
        while (rows--) render_plane_fill_row(plane_b, 0u, row++, width, 0u);
        return;
    }
    /* T-172: one VDP fill instead of a CPU loop per row (room loads clear
     * ~40 gutter rows). Fill byte 0 = cells 0. */
    SYS_disableInts();
    DMA_doVRamFill(addr, (unsigned short)len, 0u, 1);
    DMA_waitCompletion();
    render_set_autoinc_word();
    SYS_enableInts();
}

void render_plane_fill_row(unsigned char plane_b, unsigned short col,
                           unsigned short row, unsigned short count,
                           unsigned short word)
{
    const unsigned short addr = (unsigned short)(
        (plane_b ? PLANE_B_BASE : PLANE_A_BASE) +
        row * s_plane_row_stride_bytes + col * 2u);
    SYS_disableInts();
    render_set_autoinc_word();
    VDP_CTRL_LONG = 0x40000000UL
                  | ((unsigned long)(addr & 0x3FFFu) << 16)
                  | ((addr >> 14) & 0x0003u);
    {
        /* Long writes, 8 cells a step (room loads clear ~3.4k cells). */
        const unsigned long word2 = ((unsigned long)word << 16) | word;
        volatile unsigned long *const port = (volatile unsigned long *)0xC00000;
        while (count >= 8u) {
            *port = word2; *port = word2; *port = word2; *port = word2;
            count = (unsigned short)(count - 8u);
        }
    }
    while (count--) VDP_DATA_WORD = word;
    SYS_enableInts();
}

unsigned char render_plane_a_queue_row(unsigned short row, unsigned short col,
                                       const unsigned short *cells,
                                       unsigned short count)
{
    const unsigned short addr = (unsigned short)(PLANE_A_BASE +
        row * s_plane_row_stride_bytes + col * 2u);
    return DMA_queueDmaFast(DMA_VRAM, (void *)cells, addr, count, 2) ? 1u : 0u;
}

unsigned char render_vram_queue_words(unsigned short vram_addr,
                                      const unsigned short *src,
                                      unsigned short word_count)
{
    dma_stats_record((unsigned long)word_count * 2u);
    return DMA_queueDmaFast(DMA_VRAM, (void *)src, vram_addr, word_count, 2) ? 1u : 0u;
}

void render_vram_read_run(unsigned short vram_addr, unsigned short *dst,
                          unsigned short count)
{
    SYS_disableInts();
    render_set_autoinc_word();
    VDP_CTRL_LONG = 0x00000000UL
                  | ((unsigned long)(vram_addr & 0x3FFFu) << 16)
                  | ((unsigned long)(vram_addr >> 14) & 0x0003u);
    /* T-172: two words per long read of the data port (68000 long
     * accesses need only word alignment). */
    {
        volatile unsigned long *const port = (volatile unsigned long *)0xC00000;
        unsigned long *d = (unsigned long *)(void *)dst;
        while (count >= 8u) {
            d[0] = *port; d[1] = *port; d[2] = *port; d[3] = *port;
            d += 4;
            count = (unsigned short)(count - 8u);
        }
        while (count >= 2u) {
            *d++ = *port;
            count = (unsigned short)(count - 2u);
        }
        dst = (unsigned short *)(void *)d;
    }
    if (count) *dst = VDP_DATA_WORD;
    SYS_enableInts();
}

/* ---- Phase F5 FS frontend cutover primitives ---- */

/* Open CRAM write cursor at a raw byte address.
 * Genesis CRAM is byte-addressed; palette N starts at byte N*32.
 * Caller passes byte_addr directly (0, 32, 64, 96 for palettes 0..3).
 * Same CD encoding as render_cram_open_write but skips the *2 step. */
void render_cram_open_write_byte(unsigned short byte_addr)
{
    unsigned long addr = (unsigned long)byte_addr;
    render_set_autoinc_word();
    VDP_CTRL_LONG = 0xC0000000UL
                  | ((addr & 0x3FFFu) << 16)
                  | ((addr >> 14) & 0x0003u);
}

/* Write one complete SAT entry at slot entry.
 * SAT VRAM address = sat_base + entry * 8.
 * Streams 4 words: y, size_link, tile_attr, x (Genesis SAT layout). */
void render_sat_write(unsigned short sat_base, unsigned char entry,
                      unsigned short y, unsigned short size_link,
                      unsigned short tile_attr, unsigned short x)
{
    unsigned short addr = (unsigned short)(sat_base + (unsigned short)entry * 8u);
    VDP_CTRL_LONG = 0x40000000UL
                  | ((unsigned long)(addr & 0x3FFFu) << 16)
                  | ((addr >> 14) & 0x0003u);
    VDP_DATA_WORD = y;
    VDP_DATA_WORD = size_link;
    VDP_DATA_WORD = tile_attr;
    VDP_DATA_WORD = x;
}

/* Zero 16 words (one 4bpp tile = 32 bytes) at vram_addr.
 * Used to blank VRAM tile 0 so cells referencing it render transparent. */
void render_vram_write_zero_tile(unsigned short vram_addr)
{
    render_vram_open_write(vram_addr);
    unsigned short i;
    for (i = 0; i < 16u; i++) VDP_DATA_WORD = 0;
}

/* Genesis ROM bank-window load. Wraps the asm primitive
 * `c_copy_bank_to_window` (which itself wraps `_copy_bank_to_window`
 * with the C calling convention).
 *
 * Why this exists: src/game/ native code can't call c_-prefixed
 * shims per CLAUDE.md rule "no transpile-bridge shims (z01_/z07_/c_/
 * ...) in src/game/". This wrapper is in src/sgdk_adapter/ — the
 * Genesis platform layer — so callers in src/game/ get a clean
 * `render_bank_window_load(bank)` API without naming a c_ shim.
 *
 * Per debate 007 synthesis option D resolution: bank-window cache
 * is Genesis-native ROM access (NOT NES MMC1 emulation). The c_
 * prefix on c_copy_bank_to_window is misleading nomenclature —
 * functional reality is "Genesis cartridge ROM bank reader." */
extern void c_copy_bank_to_window(unsigned int bank);

void render_bank_window_load(unsigned char bank)
{
    c_copy_bank_to_window((unsigned int)bank);
}

/* Phase Q v2 public getters (storage + dma_stats_record helper at top
 * of file so every upload primitive sees the inline accumulator). */
void render_dma_stats_get(render_dma_stats_t *out)
{
    if (!out) return;
    out->current_frame_bytes = s_dma_current_frame_bytes;
    out->peak_frame_bytes    = s_dma_peak_frame_bytes;
    out->total_uploads       = s_dma_total_uploads;
}

void render_dma_stats_reset(void)
{
    s_dma_current_frame_bytes = 0UL;
    s_dma_peak_frame_bytes    = 0UL;
    s_dma_total_uploads       = 0UL;
}

void render_dma_stats_frame_end(void)
{
    if (s_dma_current_frame_bytes > s_dma_peak_frame_bytes) {
        s_dma_peak_frame_bytes = s_dma_current_frame_bytes;
    }
    s_dma_current_frame_bytes = 0UL;
}
