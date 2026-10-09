/* mode_endlevel.c — GameMode $12 (end of level: triforce piece taken).
 *
 * NES source: reference/aldonunez/Z_05.asm InitMode12 (5517) and
 * UpdateMode12EndLevel_Full (5534); EndGameMode12 (Z_05.asm, after
 * CalculateNextRoomForDoor); EndGameMode (Z_07.asm:1683).
 * Drained C: none (the previous scaffold used wrong cells: ObjTimer $30,
 * TileBufSelector $7A, World_IsFillingHearts $0640, ObjX+12 $8C, and
 * no-op callees; it stalled in Sub2).
 * Coverage: FULL state machine. Stance: REPLACE (T-013 evidence:
 * t013_route NES frames f11866-f12403, cells $12/$13/$28/$14/$63/$7C/$7D/
 * $505 per frame).
 *
 * Frame flow (IsUpdatingMode $11 = 0 on the first frame -> InitMode12):
 *   Init  End Level song, curtain columns $7C=$20 / $7D=$01, ObjTimer
 *         $30, FillTileMap($24), IsUpdatingMode++, ItemTypeToLift $1B.
 *   Sub0  wait for ObjTimer, then ObjTimer = $30.
 *   Sub1  flash: TileBufSelector $18 (level palette) / $78 (white bottom
 *         half) by ObjTimer & 7; at 0 start filling hearts ($63 = 2).
 *   Sub2  UpdateHeartsAndRupees until the hearts are full; ObjTimer $80.
 *   Sub3  after ObjTimer, UpdateWorldCurtainEffect until the decreasing
 *         column < $11; ObjTimer $80.
 *   Sub4  after ObjTimer, EndGameMode12 -> GameMode 2 (level exit load).
 * The Genesis side (Link lifting the triforce, the blank curtain
 * columns, the exit load) is drawn/run by RoomRom/src/main.c hooks.
 */

#include "platform_abi.h"
#include "mode_endlevel.h"
#include "progress_dispatch.h"   /* progress_update_world_curtain_effect */
#include "world_dispatch.h"      /* world_fill_tile_map */
#include "hud/hud_dispatch.h"    /* hud_world_fill_hearts, hud_tick_native_rupees */

/* NES Variables.inc. */
#define M12_IS_UPDATING_MODE   RAM(0x0011u)
#define M12_GAME_SUBMODE       RAM(0x0013u)
#define M12_TILE_BUF_SELECTOR  RAM(0x0014u)
#define M12_FRAME_COUNTER      RAM(0x0015u)
#define M12_OBJ_TIMER_LINK     RAM(0x0028u)   /* ObjTimer */
#define M12_FILLING_HEARTS     RAM(0x0063u)   /* World_IsFillingHearts */
#define M12_CURTAIN_DEC_COL    RAM(0x007Cu)   /* ObjX+12 */
#define M12_CURTAIN_INC_COL    RAM(0x007Du)   /* ObjX+13 */
#define M12_SONG_REQUEST       RAM(0x0600u)
#define M12_TUNE0_REQUEST      RAM(0x0604u)
#define M12_ITEM_TYPE_TO_LIFT  RAM(0x0505u)
#define M12_E7                 RAM(0x00E7u)
#define M12_CUR_PPU_CTRL_2000  RAM(0x00FFu)
#define M12_CUR_PPU_MASK_2001  RAM(0x00FEu)


/* Genesis presentation / flow hooks (RoomRom/src/main.c). */
extern void roomrom_mode12_begin(void);                 /* InitMode12 visuals */
extern void roomrom_mode12_draw(void);                  /* DrawLinkLiftingItem */
extern void roomrom_mode12_blank_column(unsigned char col);
extern void roomrom_mode12_exit(void);                  /* after EndGameMode12 */

static void init_mode12(void)
{
    M12_SONG_REQUEST = 0x04u;          /* "End Level" song */
    M12_CURTAIN_DEC_COL = 0x20u;
    M12_CURTAIN_INC_COL = 0x01u;
    M12_OBJ_TIMER_LINK = 0x30u;
    RAM(0x000Au) = 0x24u;              /* FillTileMap($24) */
    world_fill_tile_map();
    M12_IS_UPDATING_MODE = (unsigned char)(M12_IS_UPDATING_MODE + 1u);
    M12_ITEM_TYPE_TO_LIFT = 0x1Bu;     /* triforce */
    roomrom_mode12_begin();
}

static void sub0(void)
{
    if (M12_OBJ_TIMER_LINK != 0u) return;
    M12_OBJ_TIMER_LINK = 0x30u;
    M12_GAME_SUBMODE = (unsigned char)(M12_GAME_SUBMODE + 1u);
}

static void sub1(void)
{
    unsigned char y = 0x18u;           /* LevelInfo_PalettesTransferBuf */
    if (M12_OBJ_TIMER_LINK == 0u) {
        M12_FILLING_HEARTS = 0x02u;    /* StartFillingHearts */
        M12_GAME_SUBMODE = (unsigned char)(M12_GAME_SUBMODE + 1u);
        return;
    }
    if ((M12_OBJ_TIMER_LINK & 0x07u) >= 0x04u)
        y = 0x78u;                     /* WhitePaletteBottomHalfTransferBuf */
    M12_TILE_BUF_SELECTOR = y;
}

static void set_delay_and_advance(void)
{
    M12_OBJ_TIMER_LINK = 0x80u;
    M12_GAME_SUBMODE = (unsigned char)(M12_GAME_SUBMODE + 1u);
}

static void sub2(void)
{
    /* UpdateHeartsAndRupees: World_FillHearts then World_ChangeRupees. */
    hud_world_fill_hearts();
    hud_tick_native_rupees(M12_FRAME_COUNTER);
    if (M12_FILLING_HEARTS != 0u) return;
    set_delay_and_advance();
}

static void sub3(void)
{
    if (M12_OBJ_TIMER_LINK != 0u) return;
    {
        /* UpdateWorldCurtainEffect copies the (blank) tile-map columns
         * [ObjX+13] and [ObjX+12] to the screen, then moves them inward. */
        unsigned char dec = M12_CURTAIN_DEC_COL, inc = M12_CURTAIN_INC_COL;
        progress_update_world_curtain_effect();
        if (M12_CURTAIN_DEC_COL != dec) {
            if (inc < 32u) roomrom_mode12_blank_column(inc);
            if (dec < 32u) roomrom_mode12_blank_column(dec);
        }
    }
    if (M12_CURTAIN_DEC_COL >= 0x11u) return;
    set_delay_and_advance();
}

static void sub4(void)
{
    if (M12_OBJ_TIMER_LINK != 0u) return;
    /* HideAllSprites is the exit load's job on Genesis. */
    M12_CUR_PPU_CTRL_2000 = (unsigned char)(M12_CUR_PPU_CTRL_2000 & 0xFBu);
    /* EndGameMode12: EndGameMode (IsUpdatingMode, submode = 0), [E7] and
     * CurLevel 0, GameMode 2, UndergroundExitType 2, song silenced. The
     * Genesis exit load swaps the scene and sets CurLevel/exit type. */
    M12_IS_UPDATING_MODE = 0u;
    M12_GAME_SUBMODE = 0u;
    M12_E7 = 0u;
    RAM(0x0012u) = 0x02u;
    M12_TUNE0_REQUEST = 0x80u;
    M12_CUR_PPU_MASK_2001 = (unsigned char)(M12_CUR_PPU_MASK_2001 & 0xFEu);
    roomrom_mode12_exit();
}

void mode12_endlevel_update(void)
{
    if (M12_IS_UPDATING_MODE == 0u) {
        init_mode12();
        return;
    }
    roomrom_mode12_draw();             /* HideObjectSprites + DrawLinkLiftingItem */
    switch (M12_GAME_SUBMODE) {
    case 0x00u: sub0(); break;
    case 0x01u: sub1(); break;
    case 0x02u: sub2(); break;
    case 0x03u: sub3(); break;
    case 0x04u: sub4(); break;
    default:    break;
    }
}
