/* pause_map.c — see pause_map.h.
 *
 * Screen ownership while open (T-257 census, tools/audit/t257_map_vram_census.py
 * on Debug ad82bca7: Plane A = Plane B = $C000, 64x64 cells; Window $E000
 * 32 wide; H scroll $F000 full-screen mode; SAT $F400):
 *   - Plane A (all 64x64 cells): close view = a ring of grid squares
 *     scrolled by the scroll registers; far view = pause half rows 0..28
 *     shown, its rows 29..63 and the room half hold far tiles.
 *   - Plane A room half: far view tile storage. The room's rows are backed up
 *     to the native-RAM scratch on open and written back on close (unpause
 *     only restores scroll; it never redraws the room).
 *   - Tiles $E700-$EFFF (Window rows 28..31 and past the 32-wide Window),
 *     $F020-$F3FF (H scroll table past entry 0), $F680-$FFFF (past the
 *     80-entry SAT): zero in play and pause, used by no gameplay owner;
 *     zero-filled again on close.
 *   - CRAM line 3 (map colours); lines 0..2 keep the pause colours for the
 *     markers. All 64 colours are saved on open and restored on close; the
 *     screen is black while views are built so no partial frame shows.
 * Pause sheet and HUD/room strip come back through the existing owners
 * (inventory_pause_redraw_sheet, render_pause_scene_recopy).
 *
 * Dungeon (inside levels 1..9): that level's map only. Visited rooms show
 * their art; with the Map item, unvisited Map-item (blue) rooms show an
 * outlined black square; secret rooms (enterable, not blue) stay black until
 * visited; with the Compass the Triforce room gets a blinking dot.
 * Tables: pause_map_dungeon.inc (tools/gen_pause_map_dungeons.py).
 */
#include "pause_map.h"
#include "inventory_pause_internal.h"
#include "platform_abi.h"
#include "render_abi.h"
#include "save_game.h"
#include "../../state/game_ram_reserve.h"
#include "../../../engine/src/engine_state.h"
#include "../../../engine/src/vram_layout.h"
#include "../world/render/sprite_render.h"
#include "../room/room_dispatch.h"
#include "../dungeon/uw_map_builder.h"
#include "../world/trap_dispatch.h"
#include "pause_map_data.inc"
#include "pause_map_dungeon.inc"

#define PAD_UP    0x01u
#define PAD_DOWN  0x02u
#define PAD_LEFT  0x04u
#define PAD_RIGHT 0x08u
#define PAD_B     0x10u
#define PAD_C     0x20u
#define PAD_A     0x40u
#define PAD_START 0x80u

#define PLANE      0xC000u
#define ROW_BYTES  128u           /* 64 cells */
#define SAT_BASE   0xF400u
#define MAP_PAL    3u
#define VDP_DATA   (*(volatile unsigned short *)0xC00000)

/* Grid: 16x8 rooms of 16x11 squares. Close = one 8x8 tile per square. */
#define WORLD_TW   256u
#define WORLD_TH   88u
#define VIEW_ROWS  29u            /* 28 rows + one for the fine offset */
/* Overworld far = one pixel per square: 32x11 cells, shown at plane rows
 * 9..19 with V scroll 4, so the 88-line world sits at screen lines 68..155. */
#define FAR_TOP    9u
#define FAR_VSCROLL 4
#define FAR_ROWS_SHOWN 29u
/* Dungeon far = 4x3 tiles per room (gen_pause_map_dungeons.py), centred. */
#define DG_FAR_ROWS 28u

#define ZOOM_CLOSE 0u
#define ZOOM_FAR   1u

#define PH_BUILD  0u
#define PH_SHOW   1u
#define PH_LIVE   2u
#define PH_CLOSE1 3u
#define PH_CLOSE2 4u

#define BACKUP_ROWS 24u
#define LINE_MAX    160u
#define CLOSE_WORDS (4u + 160u + 8u)   /* black x4, close tiles, outline pieces */

typedef struct {
    unsigned short cram[64];
    unsigned short rows[BACKUP_ROWS][32];
    short cam_x, cam_y;
    short cmin_x, cmax_x, cmin_y, cmax_y;
    unsigned short frame;
    unsigned short menu_col, room_col, room_row;
    unsigned char phase, zoom, buf, quest, uw, cell_x, cell_y, set;
    unsigned char has_map, has_compass, ox, oy;
    unsigned char a_tap;                     /* close view: A down, no pan yet */
    unsigned char pick, sel, elig;           /* T-258 flute picker: on, level index, Triforce bits */
    unsigned short close_word[CLOSE_WORDS];  /* plane word of close slot k */
    unsigned short line_count;               /* far tiles copied for stair lines */
    unsigned short line_tile;                /* tile being drawn, 0 = none */
    unsigned char line_cx, line_cy;          /* its far-view cell */
    unsigned short line_tiles[LINE_MAX];     /* their VRAM tile numbers */
} MapScratch;
_Static_assert(sizeof(MapScratch) <= GR_MAP_SCRATCH_SIZE, "map scratch fits");
#define MS ((MapScratch *)(void *)(nes_ram + GR_MAP_SCRATCH_OFF))

static const unsigned short k_black_cram[64] = {0};

/* T-258 flute picker: 0 idle, 1 requested, 2 map open, 3 chosen. Outside
 * the scratch, which is only owned while the map is open. */
#define FLUTE_IDLE 0u
#define FLUTE_REQ  1u
#define FLUTE_OPEN 2u
#define FLUTE_DONE 3u
static unsigned char s_flute;

void pause_map_flute_request(void) { s_flute = FLUTE_REQ; }
unsigned char pause_map_flute_pending(void) { return (unsigned char)(s_flute == FLUTE_REQ); }
unsigned char pause_map_flute_busy(void) { return s_flute; }
static const unsigned char k_black_tile[32] = {
    0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,
    0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11};

/* Spare tile slots (see header comment): 72 + 31 + 76. */
#define ARCH_SLOTS 179u
static unsigned short arch_tile(unsigned short s)
{
    return s < 72u ? (unsigned short)(1848u + s) :
           s < 103u ? (unsigned short)(1921u + (s - 72u)) :
                      (unsigned short)(1972u + (s - 103u));
}

/* Close view: slots 0..3 black,
 * then the close tiles, then (dungeon) the eight outline pieces. Link's head
 * takes the last two slots: top tiles of the down-facing walk pose, left
 * then right (a 16x8 sprite reads its two tiles in that order). */
#define CLOSE_BLACK 0u
#define CLOSE_FIRST 4u
#define HEAD_SLOT   (ARCH_SLOTS - 2u)
_Static_assert(CLOSE_FIRST + PAUSE_MAP_CLOSE_TILES <= HEAD_SLOT, "overworld close tiles below the head");
/* gen_pause_map_dungeons.py CLOSE_BUDGET 160: 4 + 160 + 8 <= HEAD_SLOT. */
_Static_assert(CLOSE_FIRST + 160u + 8u <= HEAD_SLOT, "dungeon close tiles below the head");
_Static_assert(CLOSE_WORDS <= HEAD_SLOT, "close word table below the head");

static unsigned short word_for(unsigned short tile)
{
    return RENDER_TILE_ATTR_FULL(MAP_PAL, 0, 0, 0, tile);
}

static unsigned char ow_art(unsigned char room)
{
    return pause_map_room[MS->quest][room];
}

/* Dungeon room art index, or PAUSE_DG_NONE. */
static unsigned short dg_art(unsigned char room)
{
    return room < 128u ? pause_dg_room[MS->set][room] : PAUSE_DG_NONE;
}

static unsigned char dg_blue(unsigned char room)
{
    return (unsigned char)((pause_dg_blue[MS->set][room & 15u] >> (room >> 4)) & 1u);
}

/* Outline piece TL,T,TR,L,R,BL,B,BR (0..7) of a w x h room, 8 = inside. */
static unsigned char outline_piece(unsigned char x, unsigned char y, unsigned char w, unsigned char h)
{
    const unsigned char col = (unsigned char)(x == 0u ? 0u : x == w - 1u ? 2u : 1u);
    if (y == 0u) return col;
    if (y == h - 1u) return (unsigned char)(5u + col);
    return (unsigned char)(col == 0u ? 3u : col == 2u ? 4u : 8u);
}

/* ---- tiles ----------------------------------------------------------- */

static void head_upload(void)
{
    unsigned short words[16];
    const unsigned short link = roomrom_sprites_link_walk_tile();
    render_vram_read_run((unsigned short)(link * 32u), words, 16u);         /* TL */
    render_chr_upload((unsigned short)(arch_tile(HEAD_SLOT) * 32u), (const unsigned char *)words, 32u);
    render_vram_read_run((unsigned short)((link + 2u) * 32u), words, 16u);  /* TR */
    render_chr_upload((unsigned short)(arch_tile(HEAD_SLOT + 1u) * 32u), (const unsigned char *)words, 32u);
}

static unsigned short close_count(void)
{
    return MS->uw ? pause_dg_c_count[MS->set] : (unsigned short)PAUSE_MAP_CLOSE_TILES;
}

static void close_upload(void)
{
    const unsigned short n = close_count();
    unsigned short i;
    for (i = 0u; i < CLOSE_FIRST; ++i)
        render_chr_upload((unsigned short)(arch_tile(i) * 32u), k_black_tile, 32u);
    for (i = 0u; i < n; ++i)
        render_chr_upload((unsigned short)(arch_tile((unsigned short)(CLOSE_FIRST + i)) * 32u),
                          MS->uw ? &pause_dg_close_tiles[(pause_dg_c_first[MS->set] + i) * 32u]
                                 : &pause_map_close_tiles[i * 32u], 32u);
    if (MS->uw)
        for (i = 0u; i < 8u; ++i)
            render_chr_upload((unsigned short)(arch_tile((unsigned short)(CLOSE_FIRST + n + i)) * 32u),
                              &pause_dg_outline_close[i * 32u], 32u);
    for (i = 0u; i < CLOSE_WORDS; ++i) MS->close_word[i] = word_for(arch_tile(i));
}

/* ---- close view ------------------------------------------------------ */

/* The close view owns the whole 64x64 Plane A/B nametable as a ring: grid
 * square (wx, wy) sits at plane cell (wx & 63, wy & 63), and the scroll
 * registers carry the camera. Moving the camera across a square writes
 * only the one new column or row (a full redraw took more than a frame
 * and every crossing hitched). The room's rows are backed up on open and
 * restored on close (restore()). */

/* Plane word of grid square (wx, wy); squares outside the grid are black. */
static unsigned short close_square(unsigned short wx, unsigned short wy)
{
    const unsigned short black = MS->close_word[CLOSE_BLACK];
    unsigned char room, lx, ly;
    if (wx >= WORLD_TW || wy >= WORLD_TH) return black;
    room = (unsigned char)((wy / 11u) * 16u + (wx >> 4));
    lx = (unsigned char)(wx & 15u); ly = (unsigned char)(wy % 11u);
    if (!MS->uw)
        return save_game_room_discovered(room)
            ? MS->close_word[CLOSE_FIRST + pause_map_close[ow_art(room)][ly * 16u + lx]] : black;
    {
        const unsigned short id = dg_art(room);
        if (id != PAUSE_DG_NONE && uw_map_room_visited(room))
            return MS->close_word[CLOSE_FIRST + pause_dg_close[id][ly * 16u + lx]];
        if (MS->has_map && dg_blue(room)) {
            const unsigned char k = outline_piece(lx, ly, 16u, 11u);
            if (k != 8u) return MS->close_word[CLOSE_FIRST + close_count() + k];
        }
    }
    return black;
}

static void close_put(unsigned short wx, unsigned short wy)
{
    render_vram_open_write((unsigned short)(PLANE + (wy & 63u) * ROW_BYTES + (wx & 63u) * 2u));
    VDP_DATA = close_square(wx, wy);
}

/* Every square the screen can show: 33 columns x 29 rows (fine scroll).
 * Room row / in-room row once per row, room state once per 16 columns. */
static void close_draw_all(void)
{
    const unsigned short cx0 = (unsigned short)(MS->cam_x >> 3), cy0 = (unsigned short)(MS->cam_y >> 3);
    const unsigned short black = MS->close_word[CLOSE_BLACK];
    const unsigned short outline0 = (unsigned short)(CLOSE_FIRST + close_count());
    unsigned short r, c;
    for (r = 0u; r < VIEW_ROWS; ++r) {
        const unsigned short wy = (unsigned short)(cy0 + r);
        const unsigned short row_addr = (unsigned short)(PLANE + (wy & 63u) * ROW_BYTES);
        const unsigned char rbase = (unsigned char)((wy / 11u) * 16u), ly = (unsigned char)(wy % 11u);
        const unsigned char *art = 0;
        unsigned char room = 0xFFu, outlined = 0u;
        render_vram_open_write((unsigned short)(row_addr + (cx0 & 63u) * 2u));
        for (c = 0u; c <= 32u; ++c) {
            const unsigned short wx = (unsigned short)(cx0 + c);
            const unsigned char lx = (unsigned char)(wx & 15u);
            unsigned short word = black;
            if ((wx & 63u) == 0u && c) render_vram_open_write(row_addr);   /* ring wrap */
            if (wx < WORLD_TW && wy < WORLD_TH) {
                if ((unsigned char)(rbase + (wx >> 4)) != room) {
                    room = (unsigned char)(rbase + (wx >> 4));
                    art = 0; outlined = 0u;
                    if (!MS->uw) {
                        if (save_game_room_discovered(room)) art = &pause_map_close[ow_art(room)][ly * 16u];
                    } else {
                        const unsigned short id = dg_art(room);
                        if (id != PAUSE_DG_NONE && uw_map_room_visited(room)) art = &pause_dg_close[id][ly * 16u];
                        else outlined = (unsigned char)(MS->has_map && dg_blue(room));
                    }
                }
                if (art) word = MS->close_word[CLOSE_FIRST + art[lx]];
                else if (outlined) {
                    const unsigned char k = outline_piece(lx, ly, 16u, 11u);
                    if (k != 8u) word = MS->close_word[outline0 + k];
                }
            }
            VDP_DATA = word;
        }
    }
    MS->cell_x = (unsigned char)cx0;
    MS->cell_y = (unsigned char)cy0;
}

/* After a camera move (at most one square per axis per tick): the new
 * leading column / row. */
static void close_scroll(void)
{
    const unsigned short cx0 = (unsigned short)(MS->cam_x >> 3), cy0 = (unsigned short)(MS->cam_y >> 3);
    unsigned short i;
    if (cx0 != MS->cell_x) {
        const unsigned short col = (unsigned short)(cx0 > MS->cell_x ? cx0 + 32u : cx0);
        for (i = 0u; i < VIEW_ROWS; ++i) close_put(col, (unsigned short)(cy0 + i));
    }
    if (cy0 != MS->cell_y) {
        const unsigned short row = (unsigned short)(cy0 > MS->cell_y ? cy0 + VIEW_ROWS - 1u : cy0);
        for (i = 0u; i <= 32u; ++i) close_put((unsigned short)(cx0 + i), row);
    }
    MS->cell_x = (unsigned char)cx0;
    MS->cell_y = (unsigned char)cy0;
}

/* ---- far views ------------------------------------------------------- */

/* Far tile n: room half tiles (128), menu half rows 29..63 (70), then spare
 * slots after the black one, below the head. */
static unsigned short far_slot(unsigned short n)
{
    if (n < 128u)
        return (unsigned short)((PLANE + (n >> 1) * ROW_BYTES + MS->room_col * 2u) / 32u + (n & 1u));
    n = (unsigned short)(n - 128u);
    if (n < 70u)
        return (unsigned short)((PLANE + (FAR_ROWS_SHOWN + (n >> 1)) * ROW_BYTES +
                                 MS->menu_col * 2u) / 32u + (n & 1u));
    return arch_tile((unsigned short)(1u + n - 70u));            /* slot 0 is black */
}
#define FAR_SLOTS (128u + 70u + HEAD_SLOT - 1u)
_Static_assert(FAR_SLOTS >= 32u * 11u, "overworld far cells fit");
/* gen_pause_map_dungeons.py FAR_BUDGET 360 + 8 outline pieces. */
_Static_assert(FAR_SLOTS >= 196u + 8u + LINE_MAX, "dungeon far tiles (worst level 196) + lines fit");

static void ow_far_build(void)
{
    const unsigned short black_tile = arch_tile(0u);
    const unsigned short black = word_for(black_tile);
    unsigned short words[32];
    unsigned char ty, tx, y;
    render_chr_upload((unsigned short)(black_tile * 32u), k_black_tile, 32u);
    for (y = 0u; y < FAR_ROWS_SHOWN; ++y) {
        if (y >= FAR_TOP && y < FAR_TOP + 11u) continue;
        render_plane_fill((unsigned short)(PLANE + y * ROW_BYTES + MS->menu_col * 2u), black, 32u);
    }
    for (ty = 0u; ty < 11u; ++ty) {
        for (tx = 0u; tx < 32u; ++tx) {
            unsigned short slot = far_slot((unsigned short)(ty * 32u + tx));
            unsigned char shown = 0u;
            for (y = 0u; y < 8u; ++y) {
                unsigned char wy = (unsigned char)(ty * 8u + y);
                unsigned char room = (unsigned char)((wy / 11u) * 16u + (tx >> 1));
                if (save_game_room_discovered(room)) { shown = 1u; break; }
            }
            if (!shown) { words[tx] = black; continue; }
            render_vram_open_write((unsigned short)(slot * 32u));
            for (y = 0u; y < 8u; ++y) {
                unsigned char wy = (unsigned char)(ty * 8u + y);
                unsigned char room = (unsigned char)((wy / 11u) * 16u + (tx >> 1));
                if (save_game_room_discovered(room)) {
                    const unsigned char *p = &pause_map_far[ow_art(room)][(wy % 11u) * 8u + (tx & 1u) * 4u];
                    VDP_DATA = (unsigned short)((p[0] << 8) | p[1]);
                    VDP_DATA = (unsigned short)((p[2] << 8) | p[3]);
                } else {
                    VDP_DATA = 0x1111u; VDP_DATA = 0x1111u;
                }
            }
            words[tx] = word_for(slot);
        }
        render_vram_open_write((unsigned short)(PLANE + (FAR_TOP + ty) * ROW_BYTES + MS->menu_col * 2u));
        for (tx = 0u; tx < 32u; ++tx) VDP_DATA = words[tx];
    }
}

/* Dungeon far: the level's bounding box, 4x3 tiles per room, centred. */
static void dg_far_build(void)
{
    const unsigned char s = MS->set;
    const unsigned short n = pause_dg_f_count[s];
    const unsigned short black = word_for(arch_tile(0u));
    const unsigned char c0 = pause_dg_bbox[s][0], r0 = pause_dg_bbox[s][1];
    const unsigned char w = (unsigned char)((pause_dg_bbox[s][2] - c0 + 1u) * 4u);
    const unsigned char h = (unsigned char)((pause_dg_bbox[s][3] - r0 + 1u) * 3u);
    unsigned short i;
    unsigned char y, x;
    render_chr_upload((unsigned short)(arch_tile(0u) * 32u), k_black_tile, 32u);
    for (i = 0u; i < n; ++i)
        render_chr_upload((unsigned short)(far_slot(i) * 32u),
                          &pause_dg_far_tiles[(pause_dg_f_first[s] + i) * 32u], 32u);
    for (i = 0u; i < 8u; ++i)
        render_chr_upload((unsigned short)(far_slot((unsigned short)(n + i)) * 32u),
                          &pause_dg_outline_far[i * 32u], 32u);
    MS->ox = (unsigned char)((32u - w) / 2u);
    MS->oy = (unsigned char)((DG_FAR_ROWS - h) / 2u);
    for (y = 0u; y < DG_FAR_ROWS; ++y) {
        render_vram_open_write((unsigned short)(PLANE + y * ROW_BYTES + MS->menu_col * 2u));
        for (x = 0u; x < 32u; ++x) {
            unsigned short word = black;
            if (x >= MS->ox && x < MS->ox + w && y >= MS->oy && y < MS->oy + h) {
                const unsigned char rx = (unsigned char)(x - MS->ox), ry = (unsigned char)(y - MS->oy);
                const unsigned char room = (unsigned char)((r0 + ry / 3u) * 16u + c0 + rx / 4u);
                const unsigned char tx = (unsigned char)(rx % 4u), ty = (unsigned char)(ry % 3u);
                const unsigned short art = dg_art(room);
                if (art != PAUSE_DG_NONE && uw_map_room_visited(room)) {
                    word = word_for(far_slot(pause_dg_far[art][ty * 4u + tx]));
                } else if (MS->has_map && dg_blue(room)) {
                    const unsigned char k = outline_piece(tx, ty, 4u, 3u);
                    if (k != 8u) word = word_for(far_slot((unsigned short)(n + k)));
                }
            }
            VDP_DATA = word;
        }
    }
}

/* ---- staircase pairs (dungeon) -------------------------------------- */

/* Pair i is shown once its passage room and both end rooms are visited
 * (the NES marks the passage visited when Link enters it: Z_07.asm
 * RunCrossRoomTasksAndBeginUpdateMode_EnterPlayModes, mode 9). */
static unsigned char stair_known(unsigned char i)
{
    const unsigned char *p = pause_dg_stairs[MS->set][i];
    return (unsigned char)(p[0] < 128u && uw_map_room_visited(p[0]) &&
                           uw_map_room_visited(p[1]) && uw_map_room_visited(p[2]));
}

/* Plane cell word at far-view screen cell (cx, cy). */
static unsigned short far_cell_addr(unsigned char cx, unsigned char cy)
{
    return (unsigned short)(PLANE + cy * ROW_BYTES + (MS->menu_col + cx) * 2u);
}

/* Line drawing keeps one tile in RAM: the cell the line is in. Its tile is
 * a fresh far slot (a copy of the cell's tile the first time a line enters
 * it), so shared baked tiles are not touched. */
static void line_flush(unsigned short *words)
{
    if (MS->line_tile) render_chr_upload((unsigned short)(MS->line_tile * 32u), (const unsigned char *)words, 32u);
    MS->line_tile = 0u;
}

/* Load cell (cx, cy) for drawing; 0 when the fresh slots run out. */
static unsigned char line_enter(unsigned char cx, unsigned char cy, unsigned short *words)
{
    const unsigned short cell = far_cell_addr(cx, cy);
    unsigned short tile = (unsigned short)(render_vram_read_word(cell) & 0x7FFu), i;
    for (i = 0u; i < MS->line_count && MS->line_tiles[i] != tile; ++i) {}
    render_vram_read_run((unsigned short)(tile * 32u), words, 16u);
    if (i == MS->line_count) {
        if (MS->line_count == LINE_MAX) return 0u;
        tile = far_slot((unsigned short)(pause_dg_f_count[MS->set] + 8u + MS->line_count));
        MS->line_tiles[MS->line_count++] = tile;
        render_vram_open_write(cell);
        VDP_DATA = word_for(tile);
    }
    MS->line_cx = cx; MS->line_cy = cy; MS->line_tile = tile;
    return 1u;
}

static unsigned char view_xy(unsigned char room, short px, short py, short *sx, short *sy);

/* Far view: a straight line between the room centres of each known pair,
 * pair i in palette entry PAUSE_DG_LINE_FIRST + i (Bresenham). */
static void stair_lines(void)
{
    unsigned short words[16];
    unsigned char *const bytes = (unsigned char *)words;
    unsigned char i;
    MS->line_count = 0u;
    MS->line_tile = 0u;
    for (i = 0u; i < PAUSE_DG_STAIRS; ++i) {
        short x0, y0, x1, y1, dx, dy, sx, sy, err;
        const unsigned char colour = (unsigned char)(PAUSE_DG_LINE_FIRST + i);   /* one colour per pair */
        if (!stair_known(i)) continue;
        if (!view_xy(pause_dg_stairs[MS->set][i][1], 128, 88, &x0, &y0) ||
            !view_xy(pause_dg_stairs[MS->set][i][2], 128, 88, &x1, &y1)) continue;
        dx = (short)(x1 > x0 ? x1 - x0 : x0 - x1); sx = (short)(x0 < x1 ? 1 : -1);
        dy = (short)(y1 > y0 ? y0 - y1 : y1 - y0); sy = (short)(y0 < y1 ? 1 : -1);
        err = (short)(dx + dy);
        for (;;) {
            const unsigned char cx = (unsigned char)(x0 >> 3), cy = (unsigned char)(y0 >> 3);
            unsigned char k;
            if (!MS->line_tile || cx != MS->line_cx || cy != MS->line_cy) {
                line_flush(words);
                if (!line_enter(cx, cy, words)) return;
            }
            k = (unsigned char)((y0 & 7) * 4 + ((x0 & 7) >> 1));
            bytes[k] = (x0 & 1) ? (unsigned char)((bytes[k] & 0xF0u) | colour)
                                : (unsigned char)((bytes[k] & 0x0Fu) | (colour << 4));
            if (x0 == x1 && y0 == y1) break;
            {
                const short e2 = (short)(2 * err);       /* one test value per step */
                if (e2 >= dy) { err = (short)(err + dy); x0 = (short)(x0 + sx); }
                if (e2 <= dx) { err = (short)(err + dx); y0 = (short)(y0 + sy); }
            }
        }
        line_flush(words);
    }
}

/* ---- markers / publish ----------------------------------------------- */

/* Screen position of room-local NES pixel (px 0..255, py 0..175) of room in
 * the current view; 0 when the room is not on this map. */
static unsigned char view_xy(unsigned char room, short px, short py, short *sx, short *sy)
{
    const short col = (short)(room & 15u), row = (short)((room >> 4) & 7u);
    if (room >= 0x80u || (MS->uw && dg_art(room) == PAUSE_DG_NONE)) return 0u;
    if (MS->zoom == ZOOM_CLOSE) {
        *sx = (short)(col * 128 + px / 2 - MS->cam_x);
        *sy = (short)(row * 88 + py / 2 - MS->cam_y);
    } else if (!MS->uw) {
        *sx = (short)(col * 16 + px / 16);
        *sy = (short)(FAR_TOP * 8 - FAR_VSCROLL + row * 11 + py / 16);
    } else {
        *sx = (short)(MS->ox * 8 + (col - pause_dg_bbox[MS->set][0]) * 32 + px / 8);
        *sy = (short)(MS->oy * 8 + (row - pause_dg_bbox[MS->set][1]) * 24 + py / 8);
    }
    return 1u;
}

/* Link's room-local centre: NES x 0..240, y $3D (top walkable row) .. $DD. */
static unsigned char link_xy(short *sx, short *sy)
{
    short ly = (short)(roomrom_main_current_link_y() - 0x3D);
    if (ly < 0) ly = 0;
    return view_xy(F_ram_00EB_R, (short)(roomrom_main_current_link_x() + 8), (short)(ly + 8), sx, sy);
}

static unsigned short add_sprite(unsigned short slot, short x, short y, unsigned short size, unsigned short attr)
{
    if (x <= -32 || x >= 288 || y <= -32 || y >= 256) return slot;
    render_set_sprite_inline(slot, x, y, size, attr, (unsigned char)(slot + 1u));
    return (unsigned short)(slot + 1u);
}

static void publish(void)
{
    unsigned short slot = 0u;
    short x, y, h, v;
    const unsigned char blink = (unsigned char)((MS->frame & 0x10u) != 0u);
    if (MS->zoom == ZOOM_CLOSE) {
        h = (short)-(short)(MS->cam_x & 511);       /* ring: plane x = world x mod 512 */
        v = (short)(MS->cam_y & 511);
    } else {
        h = (short)-(short)(MS->menu_col * 8u);
        v = MS->uw ? 0 : FAR_VSCROLL;
    }
    /* Link's head first (front-most); Link's colours are PAL1 (indices 1..3
     * unchanged by the pause). */
    if (blink && link_xy(&x, &y))
        slot = add_sprite(slot, (short)(x - 8), (short)(y - 4), RENDER_SPRITE_SIZE(2, 1),
                          RENDER_TILE_ATTR_FULL(RENDER_PAL1, 1, 0, 0, arch_tile(HEAD_SLOT)));
    /* Compass: blinking dot on the Triforce room, visited or not; the dot
     * pixels sit at +2/+3 of its 8x16 cell. */
    if (MS->uw && MS->has_compass && blink &&
        view_xy(uw_map_triforce_room(F_ram_0010_R), 128, 88, &x, &y))
        slot = add_sprite(slot, (short)(x - 2), (short)(y - 2), RENDER_SPRITE_SIZE(1, 2),
                          RENDER_TILE_ATTR_FULL(RENDER_PAL1, 1, 0, 0, ROOMROM_SUBSCREEN_SPRITE_TILE_BASE + 8u));
    /* Staircase pairs: the same letter on both stairs (pause font, PAL0). */
    if (MS->uw) {
        unsigned char i;
        for (i = 0u; i < PAUSE_DG_STAIRS; ++i) {
            const unsigned char *p = pause_dg_stairs[MS->set][i];
            const unsigned short glyph = (unsigned short)(inventory_pause_glyph((char)('A' + i), 0u) & 0x7FFu);
            unsigned char e;
            if (!stair_known(i)) continue;
            for (e = 0u; e < 2u; ++e)
                if (view_xy(p[1u + e], (short)(p[3u + 2u * e] * 16u + 8u), (short)(p[4u + 2u * e] * 16u + 8u), &x, &y))
                    slot = add_sprite(slot, (short)(x - 4), (short)(y - 4), RENDER_SPRITE_SIZE(1, 1),
                                      RENDER_TILE_ATTR_FULL(RENDER_PAL0, 1, 0, 0, glyph));
        }
    }
    /* Flute picker: the level number on each eligible entrance room; the
     * selected one blinks every 8 frames. */
    if (MS->pick) {
        unsigned char i;
        for (i = 0u; i < 8u; ++i) {
            if (!((MS->elig >> i) & 1u)) continue;
            if (i == MS->sel && (MS->frame & 0x08u)) continue;
            if (view_xy(trap_whirlwind_entrance_room(i), 128, 88, &x, &y))
                slot = add_sprite(slot, (short)(x - 4), (short)(y - 4), RENDER_SPRITE_SIZE(1, 1),
                                  RENDER_TILE_ATTR_FULL(RENDER_PAL0, 1, 0, 0,
                                      inventory_pause_glyph((char)('1' + i), 0u) & 0x7FFu));
        }
    }
    render_set_sprite_inline(slot, -32, -32, RENDER_SPRITE_SIZE(1, 1), 0u, 0u);
    render_pause_hscroll_deferred(h);
    render_pause_frame_deferred(SAT_BASE, v, (unsigned short)(slot + 1u));
}

/* ---- lifecycle ------------------------------------------------------- */

static short clamp(short v, short lo, short hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* Close camera centred on Link, or on the map's middle when Link is not on
 * it (dungeon passage rooms), clamped to the map. */
static void center_on_link(void)
{
    short x = (short)((MS->cmin_x + MS->cmax_x) / 2 + 128), y = (short)((MS->cmin_y + MS->cmax_y) / 2 + 112);
    const unsigned char zoom = MS->zoom;
    MS->zoom = ZOOM_CLOSE; MS->cam_x = 0; MS->cam_y = 0;
    (void)link_xy(&x, &y);                       /* close pixels with the camera at 0 */
    MS->zoom = zoom;
    MS->cam_x = clamp((short)(x - 128), MS->cmin_x, MS->cmax_x);
    MS->cam_y = clamp((short)(y - 112), MS->cmin_y, MS->cmax_y);
}

void pause_map_open(void)
{
    const unsigned char level = F_ram_0010_R;
    unsigned char i;
    render_cram_read(MS->cram, 64u);
    render_cram_subrange_upload(0u, k_black_cram, 64u);
    render_pause_scene_cols(&MS->menu_col, &MS->room_col, &MS->room_row);
    for (i = 0u; i < BACKUP_ROWS; ++i)
        render_vram_read_run((unsigned short)(PLANE + ((MS->room_row + 63u + i) & 63u) * ROW_BYTES +
                                              MS->room_col * 2u), MS->rows[i], 32u);
    MS->quest = (unsigned char)(F_ram_062D_X(F_ram_0016) != 0u);
    MS->uw = (unsigned char)(inventory_pause_scene_uw() && level >= 1u && level <= 9u);
    MS->set = (unsigned char)(MS->uw ? MS->quest * 9u + level - 1u : 0u);
    /* The whirlwind only flies on the overworld (SummonWhirlwind). */
    MS->pick = (unsigned char)(s_flute == FLUTE_REQ);
    if (MS->pick) {
        s_flute = FLUTE_OPEN;
        MS->uw = 0u; MS->set = 0u;
        MS->elig = F_ram_0671_R;          /* InvTriforce: bit i = level i+1 */
        MS->sel = trap_whirlwind_level_index();
        if (!MS->elig) MS->elig = (unsigned char)(1u << MS->sel);   /* summon needs a piece */
        while (!((MS->elig >> MS->sel) & 1u)) MS->sel = (unsigned char)((MS->sel + 1u) & 7u);
    }
    MS->has_map = room_has_map();
    MS->has_compass = room_has_compass();
    if (MS->uw) {
        const unsigned char *b = pause_dg_bbox[MS->set];
        MS->cmin_x = (short)(b[0] * 128); MS->cmax_x = (short)(b[2] * 128 + 128 - 256);
        MS->cmin_y = (short)(b[1] * 88);  MS->cmax_y = (short)(b[3] * 88 + 88 - 224);
        if (MS->cmax_x < MS->cmin_x) MS->cmax_x = MS->cmin_x;
        if (MS->cmax_y < MS->cmin_y) MS->cmax_y = MS->cmin_y;
    } else {
        MS->cmin_x = 0; MS->cmax_x = (short)(WORLD_TW * 8u - 256u);
        MS->cmin_y = 0; MS->cmax_y = (short)(WORLD_TH * 8u - 224u);
    }
    MS->zoom = MS->pick ? ZOOM_FAR : ZOOM_CLOSE;
    MS->buf = 0u;
    MS->a_tap = 0u;
    MS->frame = 0u;
    center_on_link();
    MS->phase = PH_BUILD;
    inventory_pause_page_sprites(0u, 0u, 0u);
}

static void build(void)
{
    if (MS->zoom == ZOOM_CLOSE) { close_upload(); close_draw_all(); }
    else if (MS->uw) { dg_far_build(); stair_lines(); }
    else ow_far_build();
    head_upload();
}

static void restore(void)
{
    unsigned char i;
    for (i = 0u; i < 64u; ++i) {
        render_plane_fill((unsigned short)(PLANE + i * ROW_BYTES + MS->room_col * 2u), 0u, 32u);
        render_plane_fill((unsigned short)(PLANE + i * ROW_BYTES + MS->menu_col * 2u), 0u, 32u);
    }
    for (i = 0u; i < BACKUP_ROWS; ++i) {
        unsigned char c;
        render_vram_open_write((unsigned short)(PLANE + ((MS->room_row + 63u + i) & 63u) * ROW_BYTES +
                                                MS->room_col * 2u));
        for (c = 0u; c < 32u; ++c) VDP_DATA = MS->rows[i][c];
    }
    render_plane_fill((unsigned short)(1848u * 32u), 0u, 72u * 16u);
    render_plane_fill((unsigned short)(1921u * 32u), 0u, 31u * 16u);
    render_plane_fill((unsigned short)(1972u * 32u), 0u, 76u * 16u);
    inventory_pause_redraw_sheet();
    render_pause_scene_recopy();
}

static unsigned char close_map(void)
{
    render_cram_subrange_upload(0u, k_black_cram, 64u);
    restore();
    MS->phase = PH_CLOSE1;
    inventory_pause_page_sprites(0u, 0u, 0u);
    return 0u;
}

unsigned char pause_map_flute_take_done(void)
{
    if (s_flute != FLUTE_DONE) return 0u;
    s_flute = FLUTE_IDLE;
    return 1u;
}

unsigned char pause_map_tick(unsigned char joy, unsigned char pressed)
{
    ++MS->frame;
    switch (MS->phase) {
    case PH_BUILD:                       /* black since the last VBlank */
        build();
        MS->phase = PH_SHOW;
        publish();
        return 0u;
    case PH_SHOW:
        render_cram_subrange_upload(0u, MS->cram, 48u);
        render_cram_subrange_upload(48u, MS->uw ? pause_dg_cram[MS->set] : pause_map_cram, 16u);
        MS->phase = PH_LIVE;
        publish();
        return 0u;
    case PH_CLOSE1:                      /* strip copy runs this VBlank */
        MS->phase = PH_CLOSE2;
        inventory_pause_page_sprites(0u, 0u, 0u);
        return 0u;
    case PH_CLOSE2:
        render_cram_subrange_upload(0u, MS->cram, 64u);
        inventory_pause_page_sprites(0u, 0u, 0u);
        return 1u;
    default:
        break;
    }
    if (MS->pick) {
        if (pressed & (PAD_A | PAD_C)) {
            trap_whirlwind_set_destination(MS->sel);
            s_flute = FLUTE_DONE;
            return close_map();
        }
        if (pressed & 0x0Fu) {                    /* next / previous eligible level */
            const unsigned char step = (pressed & (PAD_RIGHT | PAD_DOWN)) ? 1u : 7u;
            unsigned char n = MS->sel;
            do n = (unsigned char)((n + step) & 7u); while (!((MS->elig >> n) & 1u));
            if (n != MS->sel) { MS->sel = n; MS->frame = 0u; F_ram_0602 = 1u; }
        }
        publish();
        return 0u;
    }
    if (pressed & (PAD_B | PAD_START)) return close_map();
    /* Close view: holding A pans fast; a tap (released with no pan) zooms. */
    if (MS->zoom == ZOOM_CLOSE && (pressed & PAD_A)) MS->a_tap = 1u;
    if (joy & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)) MS->a_tap = 0u;
    if ((pressed & PAD_C) || (MS->zoom != ZOOM_CLOSE && (pressed & PAD_A))
        || (MS->a_tap && !(joy & PAD_A))) {
        MS->a_tap = 0u;
        MS->zoom ^= 1u;
        if (MS->zoom == ZOOM_CLOSE) center_on_link();
        MS->buf = 0u;
        render_cram_subrange_upload(0u, k_black_cram, 64u);
        MS->phase = PH_BUILD;
        inventory_pause_page_sprites(0u, 0u, 0u);
        return 0u;
    }
    if (MS->zoom == ZOOM_CLOSE) {
        /* Fast is 8/4: close_scroll draws at most one square per axis per tick. */
        const short fast = (joy & PAD_A) ? 4 : 1;
        short x = MS->cam_x, y = MS->cam_y;
        if (joy & PAD_RIGHT) x = (short)(x + 2 * fast); else if (joy & PAD_LEFT) x = (short)(x - 2 * fast);
        if (joy & PAD_DOWN) y = (short)(y + fast); else if (joy & PAD_UP) y = (short)(y - fast);
        MS->cam_x = clamp(x, MS->cmin_x, MS->cmax_x);
        MS->cam_y = clamp(y, MS->cmin_y, MS->cmax_y);
        close_scroll();
    }
    publish();
    return 0u;
}
