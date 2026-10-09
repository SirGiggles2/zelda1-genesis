/* mode_continue_question.c — GameMode 8 (CONTINUE / SAVE / RETRY).
 *
 * NES source: reference/aldonunez/Z_05.asm InitMode8 (1374),
 * UpdateMode8ContinueQuestion_Full (2207) with Mode8BaseSpriteValues /
 * Mode8SpriteYs / Mode8SelectionToMode / Mode8FlashTransferRecord /
 * Mode8FlashAttrsAddrLo (2192-2205); Z_06.asm Mode8TextTileBuffer (745);
 * Z_07.asm ResetPlayerState (1447), EndGameMode (1683),
 * PatchAndCueLevelPalettesTransferAndAdvanceSubmode (1423),
 * ClearRoomHistory (1387); Z_01.asm SilenceAllSound (3259).
 * Reached from UpdateMenuActive (pad 2 Up+A, RoomRom/src/main.c) and from
 * mode $11 death.
 * Drained C: room_patch_and_cue_level_palettes_transfer and
 * room_clear_room_history (src/game/room/room_dispatch.c).
 * Coverage: FULL. Stance: REPLACE (T-013 P2.6). The previous scaffold used
 * wrong cells (ButtonsPressed $F7, ObjTimer+1 $30, Tune0Request $89,
 * DynTileBuf $0500), had no InitMode8 and empty ResetPlayerState /
 * EndGameMode / SilenceAllSound stubs. Evidence: t013_save NES rows
 * t131-t253 (tools/lockstep/presets/t013_save.json).
 *
 * The Genesis screen (blank, text, cursor, flash) is drawn by
 * RoomRom/src/main.c hooks; this file only touches NES RAM and calls them.
 */

#include "platform_abi.h"
#include "transfer_buf_drain.h"
#include "render_abi.h"                /* render_plane_defer */
#include "mode_continue_question.h"
#include "../core/core_dispatch.h"
#include "../room/room_dispatch.h"   /* room_patch_and_cue_level_palettes_transfer,
                                        room_clear_room_history */

/* NES Variables.inc. */
#define M8_IS_UPDATING_MODE   RAM(0x0011u)
#define M8_GAME_MODE          RAM(0x0012u)
#define M8_GAME_SUBMODE       RAM(0x0013u)
#define M8_TILE_BUF_SELECTOR  RAM(0x0014u)
#define M8_OBJ_TIMER_1        RAM(0x0029u)   /* ObjTimer+1 */
#define M8_UNDERGROUND_EXIT   RAM(0x005Au)
#define M8_OBJ_STATE          RAM(0x00ACu)   /* ObjState (Link) */
#define M8_BUTTONS_PRESSED    RAM(0x00F8u)
#define M8_TUNE0_REQUEST      RAM(0x0604u)
#define M8_INV_CLOCK          RAM(0x066Cu)
#define M8_HEART_VALUES       RAM(0x066Fu)
#define M8_HEART_PARTIAL      RAM(0x0670u)
#define M8_SPRITES            0x0200u        /* Sprites (OAM shadow) */
#define M8_DYN_TILE_BUF       0x0302u        /* DynTileBuf */

#define M8_BTN_SELECT 0x20u
#define M8_BTN_START  0x10u

static const unsigned char k_base_sprite_values[3] = { 0xF3u, 0x02u, 0x40u };
static const unsigned char k_sprite_ys[3]          = { 0x4Fu, 0x67u, 0x7Fu };
static const unsigned char k_selection_to_mode[3]  = { 0x03u, 0x0Du, 0x00u };
static const unsigned char k_flash_record[5]       = { 0x23u, 0xD2u, 0x43u, 0x00u, 0xFFu };
static const unsigned char k_flash_attrs_lo[3]     = { 0xD2u, 0xDAu, 0xE2u };

/* Mode8TextTileBuffer: attributes $23C0-$23FF = 0, then CONTINUE at NT
 * $214A (row 10, col 10), SAVE at $21AA (row 13), RETRY at $220A (row 16). */
static const unsigned char k_text_continue[8] = { 0x0Cu, 0x18u, 0x17u, 0x1Du, 0x12u, 0x17u, 0x1Eu, 0x0Eu };
static const unsigned char k_text_save[4]     = { 0x1Cu, 0x0Au, 0x1Fu, 0x0Eu };
static const unsigned char k_text_retry[5]    = { 0x1Bu, 0x0Eu, 0x1Du, 0x1Bu, 0x22u };
static const unsigned char *const k_text[3]   = { k_text_continue, k_text_save, k_text_retry };
static const unsigned char k_text_len[3]      = { 8u, 4u, 5u };
static const unsigned char k_text_row[3]      = { 10u, 13u, 16u };
#define M8_TEXT_COL 10u

/* Genesis presentation (RoomRom/src/main.c). */
extern void roomrom_mode8_blank(void);    /* TurnOffVideoAndClearArtifacts */
extern void roomrom_mode8_text(unsigned char nes_row, unsigned char nes_col,
                               const unsigned char *tiles, unsigned char n,
                               unsigned char subpal);
extern void roomrom_mode8_cursor(void);   /* draws Sprites+0..3 */
extern void roomrom_mode8_show(void);     /* display on */

/* The selection's text drawn with BG palette row 0 or 1 (the NES flashes
 * the attribute bytes of its 4x4-tile blocks between $00 and $55). */
static void draw_selection(unsigned char sel, unsigned char subpal)
{
    roomrom_mode8_text(k_text_row[sel], M8_TEXT_COL, k_text[sel], k_text_len[sel], subpal);
}

static void init_mode8(void)
{
    unsigned char i;
    if (M8_GAME_SUBMODE == 0u) {
        M8_UNDERGROUND_EXIT = 0u;
        roomrom_mode8_blank();
        transfer_buf_nt_cleared();
        room_patch_and_cue_level_palettes_transfer();   /* $14 = $18, sub 1 */
        room_clear_room_history();
        return;
    }
    /* Sub1: cue Mode8TextTileBuffer, BeginUpdateMode. */
    M8_TILE_BUF_SELECTOR = 0x04u;
    for (i = 0u; i < 3u; ++i) draw_selection(i, 0u);
    roomrom_mode8_show();                        /* video back on */
    M8_GAME_SUBMODE = 0u;
    M8_IS_UPDATING_MODE = (unsigned char)(M8_IS_UPDATING_MODE + 1u);
}

static void draw_cursor(void)
{
    RAM(M8_SPRITES + 1u) = k_base_sprite_values[0];
    RAM(M8_SPRITES + 2u) = k_base_sprite_values[1];
    RAM(M8_SPRITES + 3u) = k_base_sprite_values[2];
    RAM(M8_SPRITES + 0u) = k_sprite_ys[M8_GAME_SUBMODE];
    roomrom_mode8_cursor();
}

static void handle_activated(void)
{
    unsigned char sel = (unsigned char)(M8_GAME_SUBMODE & 0x03u);
    M8_GAME_SUBMODE = sel;
    M8_OBJ_STATE = 0u;                           /* ResetPlayerState */
    M8_INV_CLOCK = 0u;
    M8_GAME_MODE = k_selection_to_mode[sel];
    /* Start again with 3 full hearts. */
    M8_HEART_VALUES = (unsigned char)((M8_HEART_VALUES & 0xF0u) | 0x02u);
    M8_HEART_PARTIAL = 0xFFu;
    M8_IS_UPDATING_MODE = 0u;                    /* EndGameMode */
    M8_GAME_SUBMODE = 0u;
    if (sel == 0x02u) {                          /* Retry: submode 1, updating */
        M8_GAME_SUBMODE = 0x01u;
        M8_IS_UPDATING_MODE = (unsigned char)(M8_IS_UPDATING_MODE + 1u);
    }
    (void)core_silence_all_sound();              /* shared RAM/native reset */
}

static void animate_selection(void)
{
    unsigned char i, sel, attr;
    if (M8_OBJ_TIMER_1 == 0u) {
        handle_activated();
        return;
    }
    for (i = 0u; i < 5u; ++i) RAM(M8_DYN_TILE_BUF + i) = k_flash_record[i];
    sel = (unsigned char)(M8_GAME_SUBMODE & 0x03u);
    RAM(M8_DYN_TILE_BUF + 1u) = k_flash_attrs_lo[sel];
    attr = (M8_OBJ_TIMER_1 & 0x04u) ? 0x55u : 0x00u;
    RAM(M8_DYN_TILE_BUF + 3u) = attr;
    /* The NES transfers the record at the next NMI: recolour then. */
    render_plane_defer(1u);
    draw_selection(sel, attr ? 1u : 0u);
    render_plane_defer(0u);
}

void mode8_continue_question_update(void)
{
    if (M8_IS_UPDATING_MODE == 0u) {
        init_mode8();
        return;
    }
    if (M8_GAME_SUBMODE & 0x80u) {
        animate_selection();
        return;
    }
    if (M8_BUTTONS_PRESSED & M8_BTN_START) {
        M8_GAME_SUBMODE = (unsigned char)(M8_GAME_SUBMODE | 0x80u);
        M8_OBJ_TIMER_1 = 0x40u;                  /* flash $40 frames */
        return;
    }
    if (M8_BUTTONS_PRESSED & M8_BTN_SELECT) {
        M8_TUNE0_REQUEST = 0x01u;                /* rupee-taken blip */
        M8_GAME_SUBMODE = (unsigned char)(M8_GAME_SUBMODE + 1u);
        if (M8_GAME_SUBMODE == 0x03u) M8_GAME_SUBMODE = 0u;
    }
    draw_cursor();
}
