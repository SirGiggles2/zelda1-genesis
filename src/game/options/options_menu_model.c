/* options_menu_model.c — see header. Tables and edit rules moved verbatim
 * from fs_options.c / fs_options_render.c (T-256). */
#include "options_menu_model.h"
#include "options_runtime.h"
#include "options_state.h"
#include "audio_abi.h"

static const char *const LABELS[OPTIONS_MENU_ROW_COUNT] = {
    "CORE REDUX", "BUTTON SWAP", "REDUCE FLASH", "LOW HP BEEP",
    "SWORD BEAMS", "SHIELD LOSS", "ROOM SCROLL", "SPRITES", "MUSIC",
    "SAVE CHANGES"
};
/* Each line fits inside the 24-column description box. */
static const char *const HELP[OPTIONS_MENU_ROW_COUNT][3] = {
    {"REDUX IS THE BASE GAME", "THESE SETTINGS ARE EXTRA", "NO CORE SWITCHES NEEDED"},
    {"SWAP THE SWORD AND ITEM", "BUTTONS DURING GAMEPLAY", "OFF USES NORMAL BUTTONS"},
    {"STEADY ITEM COLORS", "AND HIT FLASH COLORS", "OPTIONAL VISUAL SETTING"},
    {"BEEP WHEN YOUR HEARTS", "ARE NEARLY EMPTY", "OFF KEEPS IT QUIET"},
    {"STANDARD USES HEALTH", "NEVER DISABLES BEAMS", "ALWAYS IGNORES HEALTH"},
    {"NORMAL ALLOWS LIKE LIKES", "TO EAT YOUR MAGIC SHIELD", "PROTECT KEEPS THE SHIELD"},
    {"SMOOTH GLIDES BETWEEN", "ROOMS CLASSIC USES STEPS", "GAMEPLAY TIMING IS SAME"},
    {"SMART FLICKERS CROWDED", "ROWS NES ALWAYS ROTATES", "WHOLE HIDES FULL SPRITES"},
    {"A OPENS MUSIC OPTIONS", "PICK EACH DUNGEON SONG", "OR SET ALL DUNGEONS"},
    {"A OR START SAVES CHANGES", "B OR C DISCARDS CHANGES", "SETTINGS BELONG TO FILE"}
};

/* User-approved MD menu; preserve OptionId and all saved byte meanings.
 * No placeholder/core switch is offered as an optional modifier. */
unsigned int options_menu_id_for_row(unsigned char row)
{
    static const unsigned char ids[OPTIONS_MENU_ROW_COUNT] = {
        OPTION_ID_COUNT, OPTION_ID_AB_SWAP, OPTION_ID_NO_REDUCED_FLASHING,
        OPTION_ID_LOW_HEALTH_WARNING, OPTION_ID_SWORD_STYLE, OPTION_ID_LIKE_LIKE_BEHAVIOR,
        OPTION_ID_ROOM_SCROLL, OPTION_ID_SPRITE_OVERFLOW, OPTION_ID_COUNT, OPTION_ID_COUNT
    };
    return row < OPTIONS_MENU_ROW_COUNT ? ids[row] : OPTION_ID_COUNT;
}

const char *options_menu_label(unsigned char row)
{
    return row < OPTIONS_MENU_ROW_COUNT ? LABELS[row] : "";
}

const char *options_menu_help(unsigned char row, unsigned char line)
{
    return (row < OPTIONS_MENU_ROW_COUNT && line < 3u) ? HELP[row][line] : "";
}

const char *options_menu_value(unsigned char row)
{
    unsigned int id=options_menu_id_for_row(row);
    unsigned char v;
    if (row==OPTIONS_MENU_CORE_ROW) return "AUTOMATIC";
    if (row==OPTIONS_MENU_MUSIC_ROW) return "OPEN";
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
    default: return "";
    }
}

static unsigned char enum_count_for_row(unsigned char row)
{
    switch (row) {
    case OPTION_ID_SWORD_STYLE:        return OPTIONS_SWORD_COUNT;
    case OPTION_ID_LIKE_LIKE_BEHAVIOR: return OPTIONS_LIKELIKE_COUNT;
    case OPTION_ID_BOMB_UPGRADE:       return OPTIONS_BOMBUPG_COUNT;
    case OPTION_ID_LOST_WOODS:         return OPTIONS_LWOODS_COUNT;
    case OPTION_ID_DARK_ROOM_LIGHT:    return OPTIONS_DARK_COUNT;
    case OPTION_ID_ROOM_SCROLL:        return OPTIONS_SCROLL_COUNT;
    case OPTION_ID_UW_MUSIC:           return OPTIONS_UWMUSIC_COUNT;
    case OPTION_ID_SPRITE_OVERFLOW:    return OPTIONS_SPRITES_COUNT;
    default: return 0u;
    }
}

void options_menu_step(unsigned char row, unsigned char right)
{
    unsigned int id = options_menu_id_for_row(row);
    unsigned char v, count, nv;
    if (id >= OPTION_ID_COUNT) return;
    v = options_get(id);
    if (id <= OPTION_ID_AUTO_COLLECT_DROPS) {
        options_set(id, (v != 0u) ? 0u : 1u);
        return;
    }
    if (id == OPTION_ID_START_HEARTS) {
        if (right ? (v < OPTIONS_START_HEARTS_MAX) : (v > OPTIONS_START_HEARTS_MIN))
            options_set(id, (unsigned char)(right ? v + 1u : v - 1u));
        return;
    }
    count = enum_count_for_row((unsigned char)id);
    if (count == 0u) return;
    if (right) nv = (unsigned char)((v + 1u) % count);
    else nv = (v == 0u) ? (unsigned char)(count - 1u) : (unsigned char)(v - 1u);
    options_set(id, nv);
}

/* Only meaningful alternatives appear. */
const char *options_music_value(unsigned char row)
{
    /* Indexed by OPTIONS_DUNGEON_MUSIC_* (T-408). */
    static const char *const modes[OPTIONS_DUNGEON_MUSIC_REDUX+1u]={
        "DEFAULT","INGLEBARD","CYBERDEOUS","ZELDA","NEW LIGHT",
        "PERILS","TRIFORCE","LEVEL NINE","REDUX"};
    if (row==0u) return options_music_get(1u)==4u ? "REDUX" : "INGLEBARD";
    if (row==1u) return modes[options_dungeon_music_preset()];
    if (row<OPTIONS_MUSIC_BACK_ROW) return modes[options_dungeon_music_get((unsigned char)(row-1u))];
    return "";
}

const char *options_music_label(unsigned char row, char *buf)
{
    if (row==0u) return "OVERWORLD";
    if (row==1u) return "ALL DUNGEONS";
    if (row>=OPTIONS_MUSIC_BACK_ROW) return "BACK";
    buf[0]='D';buf[1]='U';buf[2]='N';buf[3]='G';buf[4]='E';buf[5]='O';buf[6]='N';
    buf[7]=' ';buf[8]=(char)('0'+row-1u);buf[9]='\0';
    return buf;
}

unsigned char options_music_step(unsigned char row, unsigned char left)
{
    unsigned char choice, last;
    if (row>=OPTIONS_MUSIC_BACK_ROW) return 0u;
    if (row==0u) {
        options_music_set(1u,options_music_get(1u)==4u ? 1u : 4u);
        return 0u;
    }
    choice=row==1u ? options_dungeon_music_preset() :
        options_dungeon_music_get((unsigned char)(row-1u));
    /* T-408: every dungeon cycles all dungeon songs; ALL DUNGEONS adds REDUX. */
    last=row==1u ? OPTIONS_DUNGEON_MUSIC_REDUX : OPTIONS_DUNGEON_MUSIC_SONGS;
    choice=left ? (choice<=1u ? last : choice-1u) : (choice>=last ? 1u : choice+1u);
    if (row==1u) { options_dungeon_music_set_all(choice); return 1u; }
    options_dungeon_music_set((unsigned char)(row-1u),choice);
    return 0u;
}

const char *options_music_help(unsigned char cursor, unsigned char line)
{
    if (line==0u) {
        unsigned char track;
        if (cursor>=OPTIONS_MUSIC_BACK_ROW) return "";
        if (cursor==1u) return options_music_value(cursor);
        track=cursor==0u ? (options_music_get(1u)==4u ? 4u : 1u) :
            options_dungeon_music_track((unsigned char)(cursor-1u));
        return audio_music_choice_name(track);
    }
    if (line==1u) {
        if (cursor==1u) return "SET ALL NINE DUNGEONS";
        if (cursor==OPTIONS_MUSIC_BACK_ROW) return "RETURN TO OPTIONS";
        return "LEFT RIGHT SELECT SONG";
    }
    return "DEFAULT USES EACH CHOICE";
}
