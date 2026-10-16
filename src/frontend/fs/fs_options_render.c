/* NES source: NONE (user-approved custom MD Remix menu).
 * Drained C: fs_options_render.c existing adapter-backed menu renderer.
 * Coverage: PARTIAL(selected-option help and clearer labels).
 * Stance: EXTEND. Reuse FS font/heart tiles; no new VRAM patterns. */
#include "fs_options_render.h"
#include "fs_options.h"
#include "fs_render.h"
#include "../../game/options/options_runtime.h"
#include "../../game/options/options_state.h"
#include "render_abi.h"

#define PLANE_A_BASE 0xC000u
#define SAT_VRAM 0xF800u
#define HEART_CHR_BASE 0x104u
#define TILE_SPACE 0x24u
#define BORDER_LEFT 3u
#define BORDER_RIGHT 28u
#define BODY_ROW 6u
#define ROW_STRIDE 2u
#define HELP_ROW 22u

static const char *const LABELS[FS_OPTIONS_ROW_COUNT] = {
    "CORE REDUX", "BUTTON SWAP", "REDUCE FLASH", "SWORD BEAMS",
    "SHIELD LOSS", "ROOM SCROLL", "MUSIC", "SAVE CHANGES"
};
/* Each line fits inside the 24-column description box. */
static const char *const HELP[FS_OPTIONS_ROW_COUNT][3] = {
    {"REDUX IS THE BASE GAME", "THESE SETTINGS ARE EXTRA", "NO CORE SWITCHES NEEDED"},
    {"SWAP THE SWORD AND ITEM", "BUTTONS DURING GAMEPLAY", "OFF USES NORMAL BUTTONS"},
    {"STEADY ITEM COLORS", "AND HIT FLASH COLORS", "OPTIONAL VISUAL SETTING"},
    {"STANDARD USES HEALTH", "NEVER DISABLES BEAMS", "ALWAYS IGNORES HEALTH"},
    {"NORMAL ALLOWS LIKE LIKES", "TO EAT YOUR MAGIC SHIELD", "PROTECT KEEPS THE SHIELD"},
    {"SMOOTH GLIDES BETWEEN", "ROOMS CLASSIC USES STEPS", "GAMEPLAY TIMING IS SAME"},
    {"CHOOSE THE DUNGEON THEME", "INGLEBARD IS THE DEFAULT", "REMIX IS ANOTHER CHOICE"},
    {"A OR START SAVES CHANGES", "B OR C DISCARDS CHANGES", "SETTINGS BELONG TO FILE"}
};

static unsigned short glyph(char c)
{
    if (c >= '0' && c <= '9') return (unsigned short)(c - '0');
    if (c >= 'A' && c <= 'Z') return (unsigned short)(0x0A + c - 'A');
    return TILE_SPACE;
}
static void clear_row(unsigned short *cells)
{
    unsigned short i;
    for (i=0u; i<32u; ++i) cells[i]=TILE_SPACE;
    cells[BORDER_LEFT]=0x6Cu;
    cells[BORDER_RIGHT]=0x6Cu;
}
static void text(unsigned short *cells, unsigned short col, const char *s)
{
    while (*s && col < BORDER_RIGHT) cells[col++]=glyph(*s++);
}
static void border(unsigned short row, unsigned short left, unsigned short right)
{
    unsigned short cells[32], i;
    clear_row(cells);
    cells[BORDER_LEFT]=left;
    cells[BORDER_RIGHT]=right;
    for (i=BORDER_LEFT+1u; i<BORDER_RIGHT; ++i) cells[i]=0x6Au;
    render_plane_write_row(PLANE_A_BASE,row,cells,32u);
}
static const char *value(uint8_t row)
{
    unsigned int id=fs_options_id_for_row(row);
    unsigned char v;
    if (row==FS_OPTIONS_CORE_ROW) return "AUTOMATIC";
    if (id>=OPTION_ID_COUNT) return "";
    v=options_get(id);
    switch (id) {
    case OPTION_ID_AB_SWAP:
    case OPTION_ID_NO_REDUCED_FLASHING: return v ? "ON" : "OFF";
    case OPTION_ID_SWORD_STYLE:
        if (v==OPTIONS_SWORD_STAB_ONLY) return "NEVER";
        if (v==OPTIONS_SWORD_BEAM_ALWAYS) return "ALWAYS";
        return "STANDARD";
    case OPTION_ID_LIKE_LIKE_BEHAVIOR:
        return v==OPTIONS_LIKELIKE_NO_EAT ? "PROTECT" : "NORMAL";
    case OPTION_ID_ROOM_SCROLL:
        return v==OPTIONS_SCROLL_CLASSIC ? "CLASSIC" : "SMOOTH";
    case OPTION_ID_UW_MUSIC:
        return v==OPTIONS_UWMUSIC_CYBERDEOUS ? "CYBERDEOUS" : "INGLEBARD";
    default: return "";
    }
}
void fs_options_render_row_value(uint8_t row)
{
    unsigned short cells[32];
    clear_row(cells);
    text(cells,5u,LABELS[row]);
    text(cells,row==6u ? 15u : 19u,value(row));
    render_plane_write_row(PLANE_A_BASE,BODY_ROW+row*ROW_STRIDE,cells,32u);
}
void fs_options_render_cursor(uint8_t row)
{
    unsigned short cells[32], line;
    /* Entry0 remains the SAT list head; entries1..3 are hidden. */
    render_sat_write(SAT_VRAM,0u,(BODY_ROW+row*ROW_STRIDE)*8u+128u,
                     0u,(1u<<13)|HEART_CHR_BASE,152u);
    border(HELP_ROW,0x6Cu,0x6Cu);
    for (line=0u; line<3u; ++line) {
        clear_row(cells);
        text(cells,4u,HELP[row][line]);
        render_plane_write_row(PLANE_A_BASE,HELP_ROW+1u+line,cells,32u);
    }
}
void fs_options_render_full(uint8_t row)
{
    unsigned short cells[32], i;
    for (i=0u; i<32u; ++i) cells[i]=TILE_SPACE;
    for (i=0u; i<30u; ++i) render_plane_write_row(PLANE_A_BASE,i,cells,32u);
    text(cells,3u,"A EDIT START SAVE B BACK");
    render_plane_write_row(PLANE_A_BASE,1u,cells,32u);
    border(3u,0x69u,0x6Bu);
    for (i=4u; i<27u; ++i) {
        clear_row(cells);
        if (i==4u) text(cells,8u,"MD REMIX OPTIONS");
        render_plane_write_row(PLANE_A_BASE,i,cells,32u);
    }
    for (i=0u; i<FS_OPTIONS_ROW_COUNT; ++i) fs_options_render_row_value((uint8_t)i);
    border(27u,0x6Eu,0x6Du);
    for (i=0u; i<4u; ++i) render_sat_write(SAT_VRAM,(uint8_t)i,0u,0u,0u,0u);
    fs_options_render_cursor(row);
}
void fs_options_render_leave(uint8_t cursor)
{
    render_sat_write(SAT_VRAM,0u,0u,0u,0u,0u);
    fs_render_clear_screen();
    fs_render_static_layout();
    fs_render_all_slots();
    fs_render_cursor(cursor);
}
