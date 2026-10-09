/* state_dump.c - A+B+C+Start: freeze the game and show all internal data.
 *
 * See state_dump.h and docs/debug/state_dump.md. The freeze never returns:
 * interrupts off, Z80 halted on its bus (sound off), and the dump runs out of
 * the VDP ports and the pad port directly, without SGDK calls that keep
 * state. It owns no RAM: everything it keeps is on the stack below the
 * frozen frames, so the 64 KB of 68k RAM it shows is the RAM at the
 * freeze (only the dead stack below the recorded SP changes).
 *
 * VRAM during the dump: the low half ($0000-$7FFF) is never written. The
 * high half holds the pages (name table, font, image tiles), so it is
 * copied first: $8000-$DFFF to cart SRAM 0x2000-0x7FFF (the 32 KB the ROM
 * header declares; saves use 0x0000-0x1FFF), $E000-$FFFF to Z80 RAM.
 * Image pages read the high half back from those copies.
 *
 * Image stream (all big-endian), shown on the image pages 768 tiles at a
 * time, each page = 16-byte page header + 24560 stream bytes:
 *   0      header (64): "ZDMP", version, context, length, SP, ROM checksum,
 *          image page count, vtimer, scene/room/Link, region offsets,
 *          scratch check
 *   64     CRAM (64 words, as read back)
 *   192    VSRAM (40 words)
 *   272    VDP register shadow (SGDK's, 24 used of 32)
 *   304    68k RAM $FF0000-$FFFFFF
 *   65840  VRAM $0000-$FFFF
 *   131376 end
 */
#include <genesis.h>
#include "state_dump.h"
#include "roomrom_debug_runtime.h"

extern const unsigned char g_state_dump_font[64 * 8];

/* ---- hardware ---- */
#define VDP_DATA_W   (*(volatile unsigned short *)0x00C00000ul)
#define VDP_CTRL_W   (*(volatile unsigned short *)0x00C00004ul)
#define VDP_CTRL_L   (*(volatile unsigned long  *)0x00C00004ul)
#define DUMP_PSG_PORT     (*(volatile unsigned char  *)0x00C00011ul)
#define DUMP_Z80_RAM      ((volatile unsigned char *)0x00A00000ul)
#define DUMP_Z80_BUSREQ   (*(volatile unsigned short *)0x00A11100ul)
#define DUMP_Z80_RESET    (*(volatile unsigned short *)0x00A11200ul)
#define YM_ADDR0     (*(volatile unsigned char *)0x00A04000ul)
#define YM_DATA0     (*(volatile unsigned char *)0x00A04001ul)
#define PAD1_DATA    (*(volatile unsigned char *)0x00A10003ul)
#define PAD1_CTRL    (*(volatile unsigned char *)0x00A10009ul)
#define SRAM_CTRL    (*(volatile unsigned char *)0x00A130F1ul)
#define SRAM_B(o)    (*(volatile unsigned char *)(0x00200001ul + ((unsigned long)(o) << 1)))
#define RAM68K       ((const volatile unsigned char *)0x00FF0000ul)
#define NES_RAM      ((const volatile unsigned char *)0x00FF8000ul)
#define ROM_CHECKSUM (*(const volatile unsigned short *)0x0000018Eul)

#define CMD_VRAM_W(a)  (0x40000000ul | (((unsigned long)(a) & 0x3FFFul) << 16) | ((unsigned long)(a) >> 14))
#define CMD_VRAM_R(a)  ((((unsigned long)(a) & 0x3FFFul) << 16) | ((unsigned long)(a) >> 14))
#define CMD_CRAM_W(a)  (0xC0000000ul | ((unsigned long)(a) << 16))
#define CMD_CRAM_R(a)  (0x00000020ul | ((unsigned long)(a) << 16))
#define CMD_VSRAM_W(a) (0x40000010ul | ((unsigned long)(a) << 16))
#define CMD_VSRAM_R(a) (0x00000010ul | ((unsigned long)(a) << 16))

/* ---- pad (3-button read, active high) ---- */
#define P_UP    0x01u
#define P_DOWN  0x02u
#define P_LEFT  0x04u
#define P_RIGHT 0x08u
#define P_B     0x10u
#define P_C     0x20u
#define P_A     0x40u
#define P_START 0x80u
#define P_CHORD (P_A | P_B | P_C | P_START)
#define JOY_CHORD (BUTTON_A | BUTTON_B | BUTTON_C | BUTTON_START)

/* VInt frames without a main-loop poll before the VInt reads the pad. */
#define HANG_FRAMES 60u

/* ---- dump VRAM layout (high half only) ---- */
#define NT_BASE      0xE000u   /* planes A and B, 64x32 cells */
#define FONT_BASE    0xF000u   /* 64 glyphs, ASCII $20-$5F, ink = colour 1 */
#define FONT_TILE    (FONT_BASE / 32u)
#define HSCROLL_BASE 0xF800u
#define CAL_BASE     0xF820u   /* 16 solid tiles, palette 0 colours 0-15 */
#define CAL_TILE     (CAL_BASE / 32u)
#define SAT_BASE     0xFC00u
#define WIN_BASE     0x8000u   /* image page tiles */
#define WIN_TILE     (WIN_BASE / 32u)
#define WIN_TILES    768u
#define WIN_COLS     40u
#define SCRATCH_SRAM 0x2000u   /* VRAM $8000-$DFFF -> SRAM 0x2000-0x7FFF */
#define SCRATCH_HI   0xE000u   /* VRAM $E000-$FFFF -> Z80 RAM $0000-$1FFF */

/* ---- image stream ---- */
#define S_CRAM    64ul
#define S_VSRAM   192ul
#define S_REGS    272ul
#define S_RAM     304ul
#define S_VRAM    (S_RAM + 0x10000ul)
#define S_LEN     (S_VRAM + 0x10000ul)
#define PAGE_HDR  16u
#define PAGE_DATA ((unsigned long)WIN_TILES * 32ul - PAGE_HDR)
#define IMG_PAGES ((unsigned short)((S_LEN + PAGE_DATA - 1ul) / PAGE_DATA))
#define VERSION   1u

/* ---- pages ---- */
#define PG_SUMMARY  0u
#define PG_OBJECTS  1u
#define PG_RAM0     2u
#define RAM_PAGES   5u            /* 26 lines of 16 bytes per page */
#define PG_VDP      (PG_RAM0 + RAM_PAGES)
#define PG_IMG0     (PG_VDP + 1u)
#define PAGES       (PG_IMG0 + IMG_PAGES)

#define PAL_WHITE  1u
#define PAL_YELLOW 2u
#define PAL_CYAN   3u

/* Header bytes 0-303 of the stream, in stream order (no padding: every
 * member is an even number of bytes). Lives on the freeze's stack. */
typedef struct {
    unsigned char  hdr[64];
    unsigned short cram[64];
    unsigned short vsram[40];
    unsigned char  regs[32];
} dump_t;

static volatile unsigned short s_beat;

static const unsigned long k_crc_nibble[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
    0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
    0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu,
};

/* Palette 0 for the image pages: 16 colours far apart in every channel
 * (levels 0/7 and 2/5), so the decoder's nearest match is unambiguous. */
#define GEN_RGB(r, g, b) ((unsigned short)(((b) << 9) | ((g) << 5) | ((r) << 1)))
static const unsigned short k_cal_pal[16] = {
    GEN_RGB(0, 0, 0), GEN_RGB(7, 0, 0), GEN_RGB(0, 7, 0), GEN_RGB(7, 7, 0),
    GEN_RGB(0, 0, 7), GEN_RGB(7, 0, 7), GEN_RGB(0, 7, 7), GEN_RGB(7, 7, 7),
    GEN_RGB(2, 2, 2), GEN_RGB(5, 2, 2), GEN_RGB(2, 5, 2), GEN_RGB(5, 5, 2),
    GEN_RGB(2, 2, 5), GEN_RGB(5, 2, 5), GEN_RGB(2, 5, 5), GEN_RGB(5, 5, 5),
};

static const char k_hex[16] = "0123456789ABCDEF";

/* ------------------------------------------------------------------ */

static void settle(void)
{
    volatile unsigned char i;
    for (i = 0u; i < 8u; i++) { }
}

static unsigned char pad_read(void)
{
    unsigned char hi, lo;
    PAD1_CTRL = 0x40u;
    PAD1_DATA = 0x40u; settle();
    hi = (unsigned char)~PAD1_DATA;            /* C B R L D U */
    PAD1_DATA = 0x00u; settle();
    lo = (unsigned char)~PAD1_DATA;            /* Start A 0 0 D U */
    PAD1_DATA = 0x40u;
    return (unsigned char)((hi & 0x3Fu) | ((lo & 0x30u) << 2));
}

static void wait_vblank(void)
{
    while (VDP_CTRL_W & 0x0008u) { }
    while (!(VDP_CTRL_W & 0x0008u)) { }
}

static void ym_wait(void)
{
    unsigned short n = 1000u;                  /* bounded: never hang here */
    while ((YM_ADDR0 & 0x80u) && --n) { }
}

static void ym_write(unsigned char reg, unsigned char val)
{
    ym_wait();
    YM_ADDR0 = reg;
    ym_wait();
    YM_DATA0 = val;
}

static void put16(unsigned char *p, unsigned short v)
{
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)v;
}

static void put32(unsigned char *p, unsigned long v)
{
    put16(p, (unsigned short)(v >> 16));
    put16(p + 2, (unsigned short)v);
}

/* ---- text ---- */

static unsigned short glyph(char c, unsigned char pal)
{
    unsigned char u = (unsigned char)c;
    if (u >= 'a' && u <= 'z') u = (unsigned char)(u - 32u);
    if (u < 0x20u || u > 0x5Fu) u = '?';
    return (unsigned short)(((unsigned short)pal << 13) | (FONT_TILE + (u - 0x20u)));
}

static unsigned char text(unsigned char col, unsigned char row, unsigned char pal,
                          const char *s)
{
    VDP_CTRL_L = CMD_VRAM_W(NT_BASE + (((unsigned short)row * 64u + col) << 1));
    while (*s && col < 40u) {
        VDP_DATA_W = glyph(*s++, pal);
        col++;
    }
    return col;
}

static unsigned char hexv(unsigned char col, unsigned char row, unsigned char pal,
                          unsigned long v, unsigned char digits)
{
    char b[9];
    unsigned char i;
    for (i = 0u; i < digits; i++)
        b[i] = k_hex[(v >> ((digits - 1u - i) * 4u)) & 15u];
    b[digits] = 0;
    return text(col, row, pal, b);
}

static unsigned char dec2(unsigned char col, unsigned char row, unsigned char pal,
                          unsigned char v)
{
    char b[3];
    b[0] = (char)('0' + v / 10u);
    b[1] = (char)('0' + v % 10u);
    b[2] = 0;
    return text(col, row, pal, b);
}

/* "LABEL vv " at col; returns the next column. */
static unsigned char kv(unsigned char col, unsigned char row, const char *label,
                        unsigned long v, unsigned char digits)
{
    col = text(col, row, PAL_WHITE, label);
    col = hexv((unsigned char)(col + 1u), row, PAL_YELLOW, v, digits);
    return (unsigned char)(col + 2u);
}

/* "AAA 0011223344556677 8899AABBCCDDEEFF SS": address, 16 bytes in
 * alternating colours, additive checksum of the 16 bytes. */
static void hex_line(unsigned char row, unsigned short addr,
                     const volatile unsigned char *p, unsigned char n)
{
    unsigned char i, col, sum = 0u;
    hexv(0u, row, PAL_YELLOW, addr, 3u);
    col = 4u;
    for (i = 0u; i < n; i++) {
        unsigned char v = p[i];
        sum = (unsigned char)(sum + v);
        if (i == 8u) col++;
        col = hexv(col, row, (i & 1u) ? PAL_CYAN : PAL_WHITE, v, 2u);
    }
    hexv(38u, row, PAL_YELLOW, sum, 2u);
}

static void page_title(unsigned char page, const char *title)
{
    text(0u, 0u, PAL_YELLOW, title);
    text(29u, 0u, PAL_WHITE, "PAGE");
    dec2(34u, 0u, PAL_WHITE, (unsigned char)(page + 1u));
    text(36u, 0u, PAL_WHITE, "/");
    dec2(37u, 0u, PAL_WHITE, PAGES);
}

static void nt_clear(void)
{
    unsigned short i;
    VDP_CTRL_L = CMD_VRAM_W(NT_BASE);
    for (i = 0u; i < 64u * 32u; i++) VDP_DATA_W = FONT_TILE;   /* ' ', palette 0 */
}

/* ---- image stream ---- */

/* n stream bytes from off into dst (zero past the end). */
static void stream_fill(const dump_t *d, unsigned long off, unsigned char *dst,
                        unsigned short n)
{
    while (n) {
        unsigned short run = 1u;
        if (off >= S_LEN) {
            *dst = 0u;
        } else if (off < S_RAM) {
            *dst = ((const unsigned char *)d)[off];
        } else if (off < S_VRAM) {
            *dst = RAM68K[off - S_RAM];
        } else {
            unsigned long a = off - S_VRAM;
            if (a < WIN_BASE) {                 /* low half: untouched VRAM */
                unsigned short w;
                if (!(a & 1ul) && n >= 2u) {
                    unsigned short k;
                    run = n & ~1u;
                    if ((unsigned long)run > WIN_BASE - a) run = (unsigned short)(WIN_BASE - a);
                    VDP_CTRL_L = CMD_VRAM_R(a);
                    for (k = 0u; k < run; k += 2u) {
                        w = VDP_DATA_W;
                        dst[k] = (unsigned char)(w >> 8);
                        dst[k + 1u] = (unsigned char)w;
                    }
                } else {
                    VDP_CTRL_L = CMD_VRAM_R(a & ~1ul);
                    w = VDP_DATA_W;
                    *dst = (a & 1ul) ? (unsigned char)w : (unsigned char)(w >> 8);
                }
            } else if (a < SCRATCH_HI) {
                *dst = SRAM_B(SCRATCH_SRAM + (a - WIN_BASE));
            } else {
                *dst = DUMP_Z80_RAM[a - SCRATCH_HI];
            }
        }
        dst += run;
        off += run;
        n = (unsigned short)(n - run);
    }
}

static unsigned long crc_update(unsigned long crc, const unsigned char *p, unsigned short n)
{
    while (n--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ k_crc_nibble[crc & 15u];
        crc = (crc >> 4) ^ k_crc_nibble[crc & 15u];
    }
    return crc;
}

static void write_tile(unsigned short tile, const unsigned char *b)
{
    unsigned char i;
    VDP_CTRL_L = CMD_VRAM_W((unsigned short)(tile * 32u));
    for (i = 0u; i < 32u; i += 2u)
        VDP_DATA_W = (unsigned short)(((unsigned short)b[i] << 8) | b[i + 1u]);
}

static void draw_image_page(const dump_t *d, unsigned char page)
{
    unsigned char img = (unsigned char)(page - PG_IMG0);
    unsigned long base = (unsigned long)img * PAGE_DATA;
    unsigned long len = S_LEN - base;
    unsigned long crc = 0xFFFFFFFFul;
    unsigned char buf[32];
    unsigned short t, pos = 0u;      /* stream bytes uploaded */
    if (len > PAGE_DATA) len = PAGE_DATA;

    for (t = 0u; t < WIN_TILES; t++) {
        unsigned char skip = (t == 0u) ? PAGE_HDR : 0u;
        unsigned char k;
        for (k = 0u; k < skip; k++) buf[k] = 0u;
        stream_fill(d, base + pos, buf + skip, (unsigned short)(32u - skip));
        {
            unsigned short live = (unsigned short)(32u - skip);
            if ((unsigned long)pos + live > len)
                live = ((unsigned long)pos < len) ? (unsigned short)(len - pos) : 0u;
            crc = crc_update(crc, buf + skip, live);
        }
        pos = (unsigned short)(pos + 32u - skip);
        write_tile((unsigned short)(WIN_TILE + t), buf);   /* tile 0: header below */
    }
    crc = ~crc;

    /* Page header: "ZD", version, image index, image count, 0 x3,
     * stream offset, CRC-32 of this page's stream bytes. */
    {
        unsigned char h[PAGE_HDR];
        unsigned char i;
        h[0] = 'Z'; h[1] = 'D'; h[2] = VERSION; h[3] = img;
        h[4] = (unsigned char)IMG_PAGES; h[5] = 0u; h[6] = 0u; h[7] = 0u;
        put32(h + 8, base);
        put32(h + 12, crc);
        VDP_CTRL_L = CMD_VRAM_W(WIN_BASE);
        for (i = 0u; i < PAGE_HDR; i += 2u)
            VDP_DATA_W = (unsigned short)(((unsigned short)h[i] << 8) | h[i + 1u]);
    }

    /* Row 0: calibration cells 0-15. Rows 1+: the 768 tiles, 40 a row. */
    {
        unsigned char i;
        VDP_CTRL_L = CMD_VRAM_W(NT_BASE);
        for (i = 0u; i < 16u; i++) VDP_DATA_W = (unsigned short)(CAL_TILE + i);
    }
    for (t = 0u; t < WIN_TILES; t++) {
        unsigned char row = (unsigned char)(1u + t / WIN_COLS);
        unsigned char col = (unsigned char)(t % WIN_COLS);
        if (col == 0u)
            VDP_CTRL_L = CMD_VRAM_W(NT_BASE + ((unsigned short)row * 64u << 1));
        VDP_DATA_W = (unsigned short)(WIN_TILE + t);
    }
    text(17u, 0u, PAL_WHITE, "IMAGE");
    hexv(23u, 0u, PAL_WHITE, img + 1u, 1u);
    text(24u, 0u, PAL_WHITE, "/");
    hexv(25u, 0u, PAL_WHITE, IMG_PAGES, 1u);
    text(29u, 0u, PAL_WHITE, "PAGE");
    dec2(34u, 0u, PAL_WHITE, (unsigned char)(page + 1u));
    text(36u, 0u, PAL_WHITE, "/");
    dec2(37u, 0u, PAL_WHITE, PAGES);
    text(0u, 24u, PAL_WHITE, "LOSSLESS DATA PAGE. SAVE A PNG");
    text(0u, 25u, PAL_WHITE, "SCREENSHOT OF EACH IMAGE PAGE AND RUN");
    text(0u, 26u, PAL_WHITE, "TOOLS/DEBUG/DECODE_DUMP.PY ON THEM.");
}

/* ---- readable pages ---- */

static const char *ctx_name(unsigned char ctx)
{
    switch (ctx) {
        case STATE_DUMP_CTX_TITLE: return "TITLE";
        case STATE_DUMP_CTX_FS:    return "FILE SELECT";
        case STATE_DUMP_CTX_GAME:  return "GAME";
        case STATE_DUMP_CTX_HANG:  return "HANG (VINT)";
        default:                   return "?";
    }
}

static void draw_summary(const dump_t *d)
{
    const volatile unsigned char *r = NES_RAM;
    unsigned char c, slot = r[0x16u];
    page_title(PG_SUMMARY, "ZELDA STATE DUMP");
    text(0u, 1u, PAL_WHITE, "LEFT/RIGHT: TURN PAGES");

    c = text(0u, 3u, PAL_WHITE, "FROZEN IN");
    text((unsigned char)(c + 1u), 3u, PAL_YELLOW, ctx_name(d->hdr[7]));
    c = kv(22u, 3u, "ROM SUM", ROM_CHECKSUM, 4u);

    c = kv(0u, 5u, "MODE", r[0x12u], 2u);
    c = kv(c, 5u, "SUB", r[0x13u], 2u);
    c = kv(c, 5u, "UPDATING", r[0x11u], 2u);
    c = kv(c, 5u, "PAUSED", r[0xE0u], 2u);
    c = kv(0u, 6u, "FRAME", r[0x15u], 2u);
    c = kv(c, 6u, "RANDOM", r[0x18u], 2u);
    c = kv(c, 6u, "LEVEL", r[0x10u], 2u);
    c = kv(c, 6u, "ROOM", r[0xEBu], 2u);
    c = kv(0u, 7u, "NEXT ROOM", r[0xECu], 2u);
    c = kv(c, 7u, "SAVE SLOT", slot, 2u);
    c = kv(c, 7u, "QUEST", (slot < 3u) ? r[0x62Du + slot] : 0xFFu, 2u);

    c = kv(0u, 9u, "LINK X", r[0x70u], 2u);
    c = kv(c, 9u, "Y", r[0x84u], 2u);
    c = kv(c, 9u, "DIR", r[0x98u], 2u);
    c = kv(c, 9u, "STATE", r[0xACu], 2u);
    c = kv(0u, 10u, "HEARTS", r[0x66Fu], 2u);
    c = kv(c, 10u, "PARTIAL", r[0x670u], 2u);
    c = kv(c, 10u, "RUPEES", r[0x66Du], 2u);
    c = kv(0u, 11u, "KEYS", r[0x66Eu], 2u);
    c = kv(c, 11u, "BOMBS", r[0x658u], 2u);
    c = kv(c, 11u, "ITEM SLOT", r[0x656u], 2u);

    text(0u, 13u, PAL_WHITE, "INVENTORY / PROFILE $650-$67F");
    hex_line(14u, 0x650u, r + 0x650u, 16u);
    hex_line(15u, 0x660u, r + 0x660u, 16u);
    hex_line(16u, 0x670u, r + 0x670u, 16u);

    c = kv(0u, 18u, "GENESIS SCENE", d->hdr[24], 2u);
    c = kv(c, 18u, "ROOM", d->hdr[25], 2u);
    c = kv(0u, 19u, "LINK PX X", ((unsigned short)d->hdr[26] << 8) | d->hdr[27], 4u);
    c = kv(c, 19u, "Y", ((unsigned short)d->hdr[28] << 8) | d->hdr[29], 4u);
    c = kv(0u, 20u, "VTIMER", ((unsigned long)d->hdr[20] << 24) | ((unsigned long)d->hdr[21] << 16)
                               | ((unsigned long)d->hdr[22] << 8) | d->hdr[23], 8u);
    c = kv(c, 20u, "SP", ((unsigned long)d->hdr[12] << 24) | ((unsigned long)d->hdr[13] << 16)
                          | ((unsigned long)d->hdr[14] << 8) | d->hdr[15], 8u);
    c = text(0u, 21u, PAL_WHITE, "VRAM COPY (SRAM/Z80)");
    text((unsigned char)(c + 1u), 21u, d->hdr[30] ? PAL_YELLOW : PAL_CYAN,
         d->hdr[30] ? "OK" : "FAILED");

    text(0u, 23u, PAL_WHITE, "2 OBJECTS  3-7 NES RAM  8 VDP");
    c = dec2(0u, 24u, PAL_WHITE, (unsigned char)(PG_IMG0 + 1u));
    c = text(c, 24u, PAL_WHITE, "-");
    c = dec2(c, 24u, PAL_WHITE, PAGES);
    text(c, 24u, PAL_WHITE, " IMAGE (ALL RAM+VRAM, PNG)");
    text(0u, 26u, PAL_CYAN, "SEND SCREENSHOTS OF PAGES 1-8,");
    text(0u, 27u, PAL_CYAN, "OR PNG FILES OF THE IMAGE PAGES.");
}

static void draw_objects(void)
{
    static const unsigned short k_obj_col[11] = {
        0x34Fu, 0x070u, 0x084u, 0x098u, 0x0ACu, 0x485u,
        0x028u, 0x03Du, 0x405u, 0x4F0u, 0x394u,
    };
    const volatile unsigned char *r = NES_RAM;
    unsigned char s, i;
    page_title(PG_OBJECTS, "OBJECT SLOTS $00-$13");
    text(0u, 2u, PAL_YELLOW, "SL TY X  Y  DR ST HP TM SN MS IV GO");
    for (s = 0u; s < 0x14u; s++) {
        unsigned char row = (unsigned char)(3u + s);
        hexv(0u, row, PAL_YELLOW, s, 2u);
        for (i = 0u; i < 11u; i++)
            hexv((unsigned char)(3u + i * 3u), row, (i & 1u) ? PAL_CYAN : PAL_WHITE,
                 r[k_obj_col[i] + s], 2u);
    }
    text(0u, 24u, PAL_WHITE, "TY $34F X $70 Y $84 DR $98 ST $AC");
    text(0u, 25u, PAL_WHITE, "HP $485 TM $28 SN $3D MS $405");
    text(0u, 26u, PAL_WHITE, "IV $4F0 GO $394  (NES RAM + SLOT)");
}

static void draw_ram(unsigned char page)
{
    unsigned short start = (unsigned short)((page - PG_RAM0) * 26u * 16u);
    unsigned short end = (unsigned short)(start + 26u * 16u);
    unsigned char row = 2u;
    unsigned short a;
    if (end > 0x800u) end = 0x800u;
    page_title(page, "NES RAM $");
    hexv(9u, 0u, PAL_YELLOW, start, 3u);
    text(12u, 0u, PAL_YELLOW, "-");
    hexv(13u, 0u, PAL_YELLOW, end - 1u, 3u);
    text(0u, 1u, PAL_WHITE, "ADR +0               +8               CK");
    for (a = start; a < end; a += 16u)
        hex_line(row++, a, NES_RAM + a, 16u);
}

static void draw_vdp(const dump_t *d)
{
    unsigned char i;
    page_title(PG_VDP, "VDP AT FREEZE");
    text(0u, 2u, PAL_WHITE, "CRAM (PALETTES 0-3, 16 WORDS EACH)");
    for (i = 0u; i < 64u; i++)
        hexv((unsigned char)((i & 7u) * 5u), (unsigned char)(3u + (i >> 3)),
             (i & 1u) ? PAL_CYAN : PAL_WHITE, d->cram[i], 4u);
    text(0u, 12u, PAL_WHITE, "VSRAM (40 WORDS)");
    for (i = 0u; i < 40u; i++)
        hexv((unsigned char)((i & 7u) * 5u), (unsigned char)(13u + (i >> 3)),
             (i & 1u) ? PAL_CYAN : PAL_WHITE, d->vsram[i], 4u);
    text(0u, 19u, PAL_WHITE, "REGS $00-$17 (SGDK SHADOW)");
    for (i = 0u; i < 24u; i++)
        hexv((unsigned char)((i % 12u) * 3u), (unsigned char)(20u + i / 12u),
             (i & 1u) ? PAL_CYAN : PAL_WHITE, d->regs[i], 2u);
    text(0u, 23u, PAL_WHITE, "VDP REGISTERS ARE WRITE-ONLY. THE GAME");
    text(0u, 24u, PAL_WHITE, "ALSO WRITES SOME DIRECTLY, SO THE SHADOW");
    text(0u, 25u, PAL_WHITE, "CAN BE STALE. CRAM/VSRAM ARE READ BACK.");
}

static void draw_page(const dump_t *d, unsigned char page)
{
    VDP_CTRL_W = 0x8104u;                      /* display off while drawing */
    nt_clear();
    if (page == PG_SUMMARY)      draw_summary(d);
    else if (page == PG_OBJECTS) draw_objects();
    else if (page < PG_VDP)      draw_ram(page);
    else if (page == PG_VDP)     draw_vdp(d);
    else                         draw_image_page(d, page);
    VDP_CTRL_W = 0x8144u;                      /* display on, no VInt, no DMA */
}

/* ---- freeze ---- */

static void __attribute__((noreturn)) freeze(unsigned char ctx)
{
    dump_t d;
    unsigned long sp;
    unsigned short i;
    unsigned char ok = 1u;

    __asm__ volatile ("move.w #0x2700,%%sr" ::: "memory");
    __asm__ volatile ("move.l %%sp,%0" : "=d"(sp));

    (void)VDP_CTRL_W;                          /* drop a half-written command */
    while (VDP_CTRL_W & 0x0002u) { }           /* fill/copy DMA */
    VDP_CTRL_W = 0x8104u;                      /* display off: frozen, and fast VDP access */
    VDP_CTRL_W = 0x8F02u;

    for (i = 0u; i < 32u; i++) d.regs[i] = (i < 24u) ? VDP_getReg(i) : 0u;
    VDP_CTRL_L = CMD_CRAM_R(0u);
    for (i = 0u; i < 64u; i++) d.cram[i] = (unsigned short)(VDP_DATA_W & 0x0EEEu);
    VDP_CTRL_L = CMD_VSRAM_R(0u);
    for (i = 0u; i < 40u; i++) d.vsram[i] = (unsigned short)(VDP_DATA_W & 0x07FFu);

    /* Sound off: Z80 (XGM) halted on a held bus request (out of reset:
     * a Z80 in reset does not grant its bus, and Z80 RAM is scratch
     * below), every FM key off, DAC off, PSG mute. */
    DUMP_Z80_RESET = 0x0100u;
    DUMP_Z80_BUSREQ = 0x0100u;
    while (DUMP_Z80_BUSREQ & 0x0100u) { }
    for (i = 0u; i < 7u; i++)
        if (i != 3u) ym_write(0x28u, (unsigned char)i);
    ym_write(0x2Bu, 0x00u);
    DUMP_PSG_PORT = 0x9Fu; DUMP_PSG_PORT = 0xBFu; DUMP_PSG_PORT = 0xDFu; DUMP_PSG_PORT = 0xFFu;

    /* High half of VRAM to the scratch copies, then check them. */
    SRAM_CTRL = 1u;
    {
        volatile unsigned char *sr = &SRAM_B(SCRATCH_SRAM);   /* 2 bytes apart */
        volatile unsigned char *zr = DUMP_Z80_RAM;
        VDP_CTRL_L = CMD_VRAM_R(WIN_BASE);
        for (i = 0u; i < (SCRATCH_HI - WIN_BASE) / 2u; i++, sr += 4) {
            unsigned short w = VDP_DATA_W;
            sr[0] = (unsigned char)(w >> 8);
            sr[2] = (unsigned char)w;
        }
        for (i = 0u; i < (0x10000ul - SCRATCH_HI) / 2u; i++, zr += 2) {
            unsigned short w = VDP_DATA_W;
            zr[0] = (unsigned char)(w >> 8);
            zr[1] = (unsigned char)w;
        }
        sr = &SRAM_B(SCRATCH_SRAM);
        zr = DUMP_Z80_RAM;
        VDP_CTRL_L = CMD_VRAM_R(WIN_BASE);
        for (i = 0u; i < (SCRATCH_HI - WIN_BASE) / 2u; i++, sr += 4)
            if (VDP_DATA_W != (unsigned short)(((unsigned short)sr[0] << 8) | sr[2])) ok = 0u;
        for (i = 0u; i < (0x10000ul - SCRATCH_HI) / 2u; i++, zr += 2)
            if (VDP_DATA_W != (unsigned short)(((unsigned short)zr[0] << 8) | zr[1])) ok = 0u;
    }

    for (i = 0u; i < 64u; i++) d.hdr[i] = 0u;
    d.hdr[0] = 'Z'; d.hdr[1] = 'D'; d.hdr[2] = 'M'; d.hdr[3] = 'P';
    put16(d.hdr + 4, VERSION);
    put16(d.hdr + 6, ctx);
    put32(d.hdr + 8, S_LEN);
    put32(d.hdr + 12, sp);
    put16(d.hdr + 16, ROM_CHECKSUM);
    put16(d.hdr + 18, IMG_PAGES);
    put32(d.hdr + 20, vtimer);
    d.hdr[24] = roomrom_debug_get_scene();
    d.hdr[25] = roomrom_debug_get_room_id();
    put16(d.hdr + 26, (unsigned short)roomrom_debug_get_link_x());
    put16(d.hdr + 28, (unsigned short)roomrom_debug_get_link_y());
    d.hdr[30] = ok;
    put32(d.hdr + 32, S_CRAM);
    put32(d.hdr + 36, S_VSRAM);
    put32(d.hdr + 40, S_REGS);
    put32(d.hdr + 44, S_RAM);
    put32(d.hdr + 48, S_VRAM);
    put16(d.hdr + 52, PAGE_HDR);
    put16(d.hdr + 54, WIN_TILES);

    /* Dump video setup: H40 V28, planes A=B at $E000 (64x32), no window,
     * one off-screen sprite, no scroll. Writes only the high half. */
    {
        static const unsigned short k_regs[] = {
            0x8004u, 0x8104u, 0x8238u, 0x833Cu, 0x8407u, 0x857Eu, 0x8600u,
            0x8700u, 0x8800u, 0x8900u, 0x8AFFu, 0x8B00u, 0x8C81u, 0x8D3Eu,
            0x8E00u, 0x8F02u, 0x9001u, 0x9100u, 0x9200u,
        };
        for (i = 0u; i < sizeof(k_regs) / sizeof(k_regs[0]); i++) VDP_CTRL_W = k_regs[i];
    }
    VDP_CTRL_L = CMD_CRAM_W(0u);
    for (i = 0u; i < 64u; i++) {
        unsigned short c = 0u;
        if (i < 16u) c = k_cal_pal[i];
        else if (i == 16u + 1u) c = GEN_RGB(7, 7, 7);
        else if (i == 32u + 1u) c = GEN_RGB(7, 7, 0);
        else if (i == 48u + 1u) c = GEN_RGB(0, 7, 7);
        VDP_DATA_W = c;
    }
    VDP_CTRL_L = CMD_VSRAM_W(0u);
    for (i = 0u; i < 40u; i++) VDP_DATA_W = 0u;
    VDP_CTRL_L = CMD_VRAM_W(HSCROLL_BASE);
    VDP_DATA_W = 0u; VDP_DATA_W = 0u;
    VDP_CTRL_L = CMD_VRAM_W(SAT_BASE);
    for (i = 0u; i < 4u; i++) VDP_DATA_W = 0u;
    VDP_CTRL_L = CMD_VRAM_W(FONT_BASE);
    for (i = 0u; i < 64u * 8u; i++) {
        unsigned char b = g_state_dump_font[i];
        unsigned short hi = 0u, lo = 0u;
        unsigned char x;
        for (x = 0u; x < 4u; x++) {
            if (b & (0x80u >> x))      hi |= (unsigned short)(1u << ((3u - x) * 4u));
            if (b & (0x08u >> x))      lo |= (unsigned short)(1u << ((3u - x) * 4u));
        }
        VDP_DATA_W = hi;
        VDP_DATA_W = lo;
    }
    VDP_CTRL_L = CMD_VRAM_W(CAL_BASE);
    for (i = 0u; i < 16u * 16u; i++)
        VDP_DATA_W = (unsigned short)((i >> 4) * 0x1111u);

    {
        unsigned char page = PG_SUMMARY;
        unsigned char prev;
        draw_page(&d, page);
        prev = pad_read();
        for (;;) {
            unsigned char cur, edge;
            wait_vblank();
            cur = pad_read();
            edge = (unsigned char)(cur & ~prev);
            prev = cur;
            if (edge & P_RIGHT)     page = (unsigned char)((page + 1u) % PAGES);
            else if (edge & P_LEFT) page = (unsigned char)(page ? page - 1u : PAGES - 1u);
            else continue;
            draw_page(&d, page);
        }
    }
}

void state_dump_poll(unsigned char ctx)
{
    s_beat = 0u;
    if ((JOY_readJoypad(JOY_1) & JOY_CHORD) == JOY_CHORD)
        freeze(ctx);
}

void state_dump_pad(unsigned char ctx, unsigned char held)
{
    s_beat = 0u;
    if ((held & P_CHORD) == P_CHORD)
        freeze(ctx);
}

void state_dump_vint(void)
{
    if (s_beat < 0xFFFFu) s_beat++;
    if (s_beat > HANG_FRAMES && (pad_read() & P_CHORD) == P_CHORD)
        freeze(STATE_DUMP_CTX_HANG);
}
