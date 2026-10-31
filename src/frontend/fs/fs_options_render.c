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
#include "audio_abi.h"

#define PLANE_A_BASE 0xC000u
#define SAT_VRAM 0xF800u
#define HEART_CHR_BASE 0x104u
#define TILE_SPACE 0x24u
#define BORDER_LEFT 3u
#define BORDER_RIGHT 28u
#define TOP_BORDER_ROW 2u
#define TITLE_ROW 3u
#define BODY_ROW 4u  /* ten rows (4..22) above the help border at 23 */
#define ROW_STRIDE 2u
#define HELP_ROW 23u

static const char *const LABELS[FS_OPTIONS_ROW_COUNT] = {
    "CORE REDUX", "BUTTON SWAP", "REDUCE FLASH", "LOW HP BEEP",
    "SWORD BEAMS", "SHIELD LOSS", "MUSIC", "MORE", "SAVE CHANGES"
};
/* Each line fits inside the 24-column description box. */
static const char *const HELP[FS_OPTIONS_ROW_COUNT][3] = {
    {"REDUX IS THE BASE GAME", "THESE SETTINGS ARE EXTRA", "NO CORE SWITCHES NEEDED"},
    {"SWAP THE SWORD AND ITEM", "BUTTONS DURING GAMEPLAY", "OFF USES NORMAL BUTTONS"},
    {"STEADY ITEM COLORS", "AND HIT FLASH COLORS", "OPTIONAL VISUAL SETTING"},
    {"BEEP WHEN YOUR HEARTS", "ARE NEARLY EMPTY", "OFF KEEPS IT QUIET"},
    {"STANDARD USES HEALTH", "NEVER DISABLES BEAMS", "ALWAYS IGNORES HEALTH"},
    {"NORMAL ALLOWS LIKE LIKES", "TO EAT YOUR MAGIC SHIELD", "PROTECT KEEPS THE SHIELD"},
    {"A OPENS MUSIC OPTIONS", "PICK EACH DUNGEON SONG", "OR SET ALL DUNGEONS"},
    {"A OPENS PAGE TWO", "ROOM SCROLL AND SPEED", "AND SPRITE FLICKER"},
    {"A OR START SAVES CHANGES", "B OR C DISCARDS CHANGES", "SETTINGS BELONG TO FILE"}
};

/* T-409 MORE OPTIONS page (fs_more_id_for_row). */
static const char *const MORE_LABELS[FS_MORE_ROW_COUNT] = {
    "ROOM SCROLL", "SCROLL SPEED", "SPRITES", "BACK"
};
static const char *const MORE_HELP[FS_MORE_ROW_COUNT][3] = {
    {"SMOOTH GLIDES BETWEEN", "ROOMS CLASSIC USES STEPS", "GAMEPLAY TIMING IS SAME"},
    {"NORMAL IS THE NES SPEED", "FAST SLIDES ROOMS 2X", "FASTER SLIDES ROOMS 4X"},
    {"SMART FLICKERS CROWDED", "ROWS NES ALWAYS ROTATES", "WHOLE HIDES FULL SPRITES"},
    {"A RETURNS TO PAGE ONE", "START SAVES CHANGES", "B OR C ALSO RETURNS"}
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
static const char *value_for_id(unsigned int id)
{
    unsigned char v;
    if (id>=OPTION_ID_COUNT) return "";
    v=options_get(id);
    switch (id) {
    case OPTION_ID_AB_SWAP:
    case OPTION_ID_NO_REDUCED_FLASHING:
    case OPTION_ID_LOW_HEALTH_WARNING: return v ? "ON" : "OFF";
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
    case OPTION_ID_SPRITE_OVERFLOW:
        if (v==OPTIONS_SPRITES_WHOLE) return "WHOLE";
        if (v==OPTIONS_SPRITES_NES) return "NES";
        return "SMART";
    case OPTION_ID_SCROLL_SPEED:
        if (v==OPTIONS_SCROLL_SPEED_FAST) return "FAST";
        if (v==OPTIONS_SCROLL_SPEED_FASTER) return "FASTER";
        return "NORMAL";
    default: return "";
    }
}
static const char *value(uint8_t row)
{
    if (row==FS_OPTIONS_CORE_ROW) return "AUTOMATIC";
    if (row==FS_OPTIONS_MUSIC_ROW || row==FS_OPTIONS_MORE_ROW) return "OPEN";
    return value_for_id(fs_options_id_for_row(row));
}
void fs_options_render_row_value(uint8_t row)
{
    unsigned short cells[32];
    clear_row(cells);
    text(cells,5u,LABELS[row]);
    text(cells,(row==FS_OPTIONS_MUSIC_ROW || row==FS_OPTIONS_MORE_ROW) ? 15u : 19u,value(row));
    render_plane_write_row(PLANE_A_BASE,BODY_ROW+row*ROW_STRIDE,cells,32u);
}
static void page_cursor(uint8_t row, const char *const help[3])
{
    unsigned short cells[32], line;
    /* Entry0 remains the SAT list head; entries1..3 are hidden. */
    render_sat_write(SAT_VRAM,0u,(BODY_ROW+row*ROW_STRIDE)*8u+128u,
                     0u,(1u<<13)|HEART_CHR_BASE,152u);
    border(HELP_ROW,0x6Cu,0x6Cu);
    for (line=0u; line<3u; ++line) {
        clear_row(cells);
        text(cells,4u,help[line]);
        render_plane_write_row(PLANE_A_BASE,HELP_ROW+1u+line,cells,32u);
    }
}
void fs_options_render_cursor(uint8_t row)
{
    page_cursor(row,HELP[row]);
}
/* Clear the screen and draw an options page frame titled `title`. */
static void page_frame(const char *title)
{
    unsigned short cells[32], i;
    for (i=0u; i<32u; ++i) cells[i]=TILE_SPACE;
    for (i=0u; i<30u; ++i) render_plane_write_row(PLANE_A_BASE,i,cells,32u);
    text(cells,3u,"A EDIT START SAVE B BACK");
    render_plane_write_row(PLANE_A_BASE,1u,cells,32u);
    border(TOP_BORDER_ROW,0x69u,0x6Bu);
    for (i=TITLE_ROW; i<27u; ++i) {
        clear_row(cells);
        if (i==TITLE_ROW) text(cells,8u,title);
        render_plane_write_row(PLANE_A_BASE,i,cells,32u);
    }
    border(27u,0x6Eu,0x6Du);
    for (i=0u; i<4u; ++i) render_sat_write(SAT_VRAM,(uint8_t)i,0u,0u,0u,0u);
}
void fs_options_render_full(uint8_t row)
{
    uint8_t i;
    page_frame("MD REMIX OPTIONS");
    for (i=0u; i<FS_OPTIONS_ROW_COUNT; ++i) fs_options_render_row_value(i);
    fs_options_render_cursor(row);
}
void fs_more_render_row(uint8_t row)
{
    unsigned short cells[32];
    clear_row(cells);
    text(cells,5u,MORE_LABELS[row]);
    text(cells,19u,value_for_id(fs_more_id_for_row(row)));
    render_plane_write_row(PLANE_A_BASE,BODY_ROW+row*ROW_STRIDE,cells,32u);
}
void fs_more_render_cursor(uint8_t row)
{
    page_cursor(row,MORE_HELP[row]);
}
void fs_more_render_full(uint8_t row)
{
    uint8_t i;
    page_frame("MORE OPTIONS");
    for (i=0u; i<FS_MORE_ROW_COUNT; ++i) fs_more_render_row(i);
    fs_more_render_cursor(row);
}
void fs_options_render_leave(uint8_t cursor)
{
    render_sat_write(SAT_VRAM,0u,0u,0u,0u,0u);
    fs_render_clear_screen();
    fs_render_static_layout();
    fs_render_all_slots();
    fs_render_cursor(cursor);
}

/* Only meaningful alternatives appear. Navigation and editing preserve the
 * existing page; only entry/exit performs a complete redraw. */
static const char *music_value(uint8_t row)
{
    /* Indexed by OPTIONS_DUNGEON_MUSIC_*; each fits columns 18..27. */
    static const char *const modes[OPTIONS_DUNGEON_MUSIC_REDUX+1u]={
        "DEFAULT","INGLEBARD","CYBERDEOUS","ZELDA","NEW LIGHT",
        "PERILS","TRIFORCE","LEVEL NINE","REDUX"};
    if (row==0u) return options_music_get(1u)==4u ? "REDUX" : "INGLEBARD";
    if (row==1u) return modes[options_dungeon_music_preset()];
    if (row<FS_MUSIC_BACK_ROW) return modes[options_dungeon_music_get((uint8_t)(row-1u))];
    return "";
}
void fs_music_render_row(uint8_t row)
{
    unsigned short cells[32];
    char label[]="DUNGEON 1";
    clear_row(cells);
    if (row==0u) text(cells,5u,"OVERWORLD");
    else if (row==1u) text(cells,5u,"ALL DUNGEONS");
    else if (row==FS_MUSIC_BACK_ROW) text(cells,5u,"BACK");
    else { label[8]=(char)('0'+row-1u); text(cells,5u,label); }
    text(cells,18u,music_value(row));
    render_plane_write_row(PLANE_A_BASE,6u+row,cells,32u);
}
void fs_music_render_cursor(uint8_t cursor)
{
    unsigned short cells[32], i;
    const char *description="LEFT RIGHT SELECT SONG";
    if (cursor==1u) description="SET ALL NINE DUNGEONS";
    else if (cursor==FS_MUSIC_BACK_ROW) description="RETURN TO OPTIONS";
    for (i=0u; i<3u; ++i) {
        clear_row(cells);
        if (i==0u && cursor<FS_MUSIC_BACK_ROW) {
            unsigned char track=cursor==0u ? (options_music_get(1u)==4u ? 4u : 1u) :
                cursor==1u ? 0u : options_dungeon_music_track((uint8_t)(cursor-1u));
            if (cursor==1u) text(cells,4u,music_value(cursor));
            else text(cells,4u,audio_music_choice_name(track));
        } else if (i==1u) text(cells,4u,description);
        else if (i==2u) text(cells,4u,"DEFAULT USES EACH CHOICE");
        render_plane_write_row(PLANE_A_BASE,21u+i,cells,32u);
    }
    render_sat_write(SAT_VRAM,0u,(6u+cursor)*8u+128u,0u,(1u<<13)|HEART_CHR_BASE,152u);
}
void fs_music_render_full(uint8_t cursor)
{
    unsigned short cells[32], i;
    for (i=0u; i<32u; ++i) cells[i]=TILE_SPACE;
    for (i=0u; i<30u; ++i) render_plane_write_row(PLANE_A_BASE,i,cells,32u);
    text(cells,3u,"A NEXT START SAVE B BACK");
    render_plane_write_row(PLANE_A_BASE,1u,cells,32u);
    border(3u,0x69u,0x6Bu);
    clear_row(cells); text(cells,8u,"REMIX MUSIC");
    render_plane_write_row(PLANE_A_BASE,4u,cells,32u);
    for (i=5u; i<27u; ++i) {
        clear_row(cells);
        render_plane_write_row(PLANE_A_BASE,i,cells,32u);
    }
    for (i=0u; i<FS_MUSIC_ROW_COUNT; ++i) fs_music_render_row((uint8_t)i);
    border(20u,0x6Cu,0x6Cu);
    border(27u,0x6Eu,0x6Du);
    fs_music_render_cursor(cursor);
}
