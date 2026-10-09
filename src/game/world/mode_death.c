/* GameMode $11: Link dies (T-097).
 *
 * NES source: reference/aldonunez/Z_05.asm InitMode11 (2047-2096) and
 * UpdateMode11Death_Full (2521-2704); Z_07.asm UpdateMode11Death.
 * Drained C: none for this mode. Stance: REPLACE (per-line port; every RAM
 * cell from Variables.inc, the previous file guessed most of them:
 * ObjTimer+11 was $3A, ObjDir $C4, IsSprite0CheckActive $EC, ...).
 *
 * InitMode11 (IsUpdatingMode 0): Sub0 hides the sprites, Link flashes
 * (ObjInvincibilityTimer $10) for ObjTimer $21 frames; Sub1 sets the death
 * fade cycle and four turns and starts the update.
 * UpdateMode11Death: Sub0/1 attribute halves for name table 2, Sub2 copies
 * the 22 play-area rows there, Sub3/4 cue every play-area attribute to
 * palette 3 ($60/$62), Sub5 the death colors ($5E), Sub6 even name table,
 * Sub7 Link spins (down, right, up, left) DeathTurns times, Sub8 fades,
 * Sub9 grey Link ($2C), SubA the spark, SubB "GAME OVER" ($46), SubC mode 8.
 *
 * The name-table-2 copy is how the NES redraws the room under the new
 * attributes; on the Genesis the room is already on the plane, so the
 * copy only advances CurRow (the same 22 ticks) and the attribute cues
 * recolor the plane (transfer_buf_drain). Sprites go through the main
 * loop's mode 11 hooks (Link pose / spark from the NES cells).
 */

#include "platform_abi.h"
#include "world_dispatch.h"  /* world_animate_world_fading (drained) */

/* Variables.inc */
#define GAME_MODE              RAM(0x0012u)
#define GAME_SUBMODE           RAM(0x0013u)
#define IS_UPDATING_MODE       RAM(0x0011u)
#define TILE_BUF_SELECTOR      RAM(0x0014u)
#define CUR_SAVE_SLOT          RAM(0x0016u)
#define OBJ_TIMER(s)           RAM((unsigned short)(0x0028u + (s)))
#define OBJ_X0                 RAM(0x0070u)
#define OBJ_Y0                 RAM(0x0084u)
#define OBJ_DIR0               RAM(0x0098u)
#define OBJ_STATE0             RAM(0x00ACu)
#define PAUSED                 RAM(0x00E0u)
#define IS_SPRITE0_CHECK       RAM(0x00E3u)
#define DEATH_TURNS            RAM(0x00E5u)
#define CUR_ROW                RAM(0x00E9u)
#define CUR_OPENED_DOORS       RAM(0x00EEu)
#define CUR_PPU_CONTROL        RAM(0x00FFu)
#define SPRITES(i)             RAM((unsigned short)(0x0200u + (i)))
#define OBJ_GRID_OFFSET0       RAM(0x0394u)
#define OBJ_INVINCIBILITY0     RAM(0x04F0u)
#define FADE_CYCLE             RAM(0x051Cu)
#define PREV_OPENED_DOORS      RAM(0x0521u)
#define TUNE1_REQUEST          RAM(0x0602u)
#define EFFECT_REQUEST         RAM(0x0603u)
#define TUNE0_REQUEST          RAM(0x0604u)
#define DEATH_COUNTS(s)        RAM((unsigned short)(0x0630u + (s)))
#define HEART_PARTIAL          RAM(0x0670u)
#define FRAME_COUNTER          RAM(0x0015u)

extern unsigned char room_get_unique_room_id(void);
extern void roomrom_combat_animate_link_base(void);
/* Main-loop sprite hooks (RoomRom/src/main.c). */
extern void roomrom_mode11_hide_sprites(void);   /* HideAllSprites */

/* 0 = Link drawn, 1 = spark in Sprites+72..79, 2 = both hidden. */
static unsigned char s_spark_state;

unsigned char mode11_spark_state(void) { return s_spark_state; }

/* Z_07.asm DecrementInvincibilityTimer for Link (X = 0). */
static void decrement_link_invincibility(void)
{
    if (OBJ_INVINCIBILITY0 == 0u) return;
    if ((FRAME_COUNTER & 1u) != 0u) return;
    OBJ_INVINCIBILITY0 = (unsigned char)(OBJ_INVINCIBILITY0 - 1u);
}

/* Link_EndMoveAndAnimate outside mode 5/4/6: a grid offset that is a
 * nonzero multiple of 8 is truncated, then AnimateLinkBase. */
static void link_end_move_and_animate(void)
{
    unsigned char g = OBJ_GRID_OFFSET0;
    if (g != 0u && (g & 0x07u) == 0u) OBJ_GRID_OFFSET0 = 0u;
    roomrom_combat_animate_link_base();
}

static void init_mode11(void)
{
    decrement_link_invincibility();
    link_end_move_and_animate();
    if (GAME_SUBMODE == 0u) {
        s_spark_state = 0u;
        roomrom_mode11_hide_sprites();
        PAUSED = 0u;
        HEART_PARTIAL = 0u;
        GAME_SUBMODE = (unsigned char)(GAME_SUBMODE + 1u);
        OBJ_INVINCIBILITY0 = 0x10u;    /* Link flashes */
        OBJ_TIMER(0u) = 0x21u;
        return;
    }
    /* InitMode11_Sub1 */
    if (OBJ_TIMER(0u) != 0u) return;
    if ((room_get_unique_room_id() & 0x3Eu) != 0x3Eu)
        PREV_OPENED_DOORS = CUR_OPENED_DOORS;   /* LayOutDoorsPrev: same doors */
    FADE_CYCLE = 0x60u;                          /* DeathPaletteCycle */
    OBJ_TIMER(10u) = 0x02u;
    GAME_SUBMODE = 0u;
    CUR_ROW = 0u;
    OBJ_STATE0 = 0u;
    DEATH_TURNS = 0x04u;
    OBJ_DIR0 = 0x04u;
    IS_UPDATING_MODE = (unsigned char)(IS_UPDATING_MODE + 1u);
    TUNE0_REQUEST = 0x80u;                       /* SilenceSound */
    EFFECT_REQUEST = 0x80u;
}

static void select_and_advance(unsigned char selector)
{
    TILE_BUF_SELECTOR = selector;
    GAME_SUBMODE = (unsigned char)(GAME_SUBMODE + 1u);
}

static void sub7_spin(void)
{
    unsigned char a;
    if (DEATH_TURNS == 0u) {
        GAME_SUBMODE = (unsigned char)(GAME_SUBMODE + 1u);
        return;
    }
    if (OBJ_TIMER(11u) == 0u) {
        OBJ_TIMER(11u) = 0x05u;
        a = OBJ_DIR0;
        if ((a & 0x02u) != 0u) {        /* LSR LSR: carry = left */
            DEATH_TURNS = (unsigned char)(DEATH_TURNS - 1u);
            OBJ_DIR0 = 0x04u;           /* down */
        } else {
            a = (unsigned char)(a >> 2);  /* down -> right, up -> left */
            OBJ_DIR0 = (a != 0u) ? a : 0x08u;  /* right -> up */
        }
    }
    link_end_move_and_animate();
}

static void sub_a_spark(void)
{
    unsigned char tile, x;
    if (OBJ_TIMER(11u) != 0u) return;
    s_spark_state = 1u;
    tile = (DEATH_TURNS >= 0x06u) ? 0x62u : 0x64u;
    SPRITES(72u) = OBJ_Y0;
    SPRITES(76u) = OBJ_Y0;
    SPRITES(73u) = tile;
    SPRITES(77u) = tile;
    SPRITES(74u) = 0x01u;
    SPRITES(78u) = 0x41u;
    x = OBJ_X0;
    SPRITES(75u) = x;
    SPRITES(79u) = (unsigned char)(x + 8u);
    DEATH_TURNS = (unsigned char)(DEATH_TURNS - 1u);
    if (DEATH_TURNS != 0u) return;
    TUNE0_REQUEST = 0x10u;              /* "heart taken" */
    s_spark_state = 2u;
    SPRITES(72u) = 0xF8u;
    SPRITES(76u) = 0xF8u;
    OBJ_TIMER(11u) = 0x2Eu;
    GAME_SUBMODE = (unsigned char)(GAME_SUBMODE + 1u);
}

static void update_mode11(void)
{
    switch (GAME_SUBMODE) {
    case 0x01u:
        TUNE1_REQUEST = 0x80u;          /* death tune, then Sub0 */
        /* fall through */
    case 0x00u:
        /* Attribute half for name table 2 (CopyPlayAreaAttrsHalfTo
         * DynTransferBuf): no Genesis counterpart. */
        GAME_SUBMODE = (unsigned char)(GAME_SUBMODE + 1u);
        break;
    case 0x02u:
        /* CopyNextRowToTransferBuf: one row a frame into name table 2. */
        CUR_ROW = (unsigned char)(CUR_ROW + 1u);
        if (CUR_ROW == 0x16u) {
            GAME_SUBMODE = (unsigned char)(GAME_SUBMODE + 1u);
            IS_SPRITE0_CHECK = 1u;      /* WriteAndEnableSprite0 */
        }
        break;
    case 0x03u: select_and_advance(0x60u); break;   /* attrs top half */
    case 0x04u: select_and_advance(0x62u); break;   /* attrs bottom half */
    case 0x05u:
        IS_SPRITE0_CHECK = 0u;
        select_and_advance(0x5Eu);                  /* death colors */
        break;
    case 0x06u:
        CUR_PPU_CONTROL = (unsigned char)(CUR_PPU_CONTROL & 0xFEu);
        GAME_SUBMODE = (unsigned char)(GAME_SUBMODE + 1u);
        break;
    case 0x07u: sub7_spin(); break;
    case 0x08u:
        if (world_animate_world_fading() == 0u)
            GAME_SUBMODE = (unsigned char)(GAME_SUBMODE + 1u);
        break;
    case 0x09u:
        TILE_BUF_SELECTOR = 0x2Cu;                  /* grey Link */
        DEATH_TURNS = 0x0Fu;
        OBJ_TIMER(11u) = 0x18u;
        GAME_SUBMODE = (unsigned char)(GAME_SUBMODE + 1u);
        break;
    case 0x0Au: sub_a_spark(); break;
    case 0x0Bu:
        if (OBJ_TIMER(11u) != 0u) break;
        OBJ_TIMER(11u) = 0x60u;
        select_and_advance(0x46u);                  /* "GAME OVER" */
        break;
    case 0x0Cu:
        if (OBJ_TIMER(11u) != 0u) break;
        IS_UPDATING_MODE = 0u;                      /* EndGameMode */
        GAME_SUBMODE = 0u;
        GAME_MODE = 0x08u;
        TUNE1_REQUEST = 0x40u;                      /* game over music */
        if (DEATH_COUNTS(CUR_SAVE_SLOT) != 0xFFu)
            DEATH_COUNTS(CUR_SAVE_SLOT) =
                (unsigned char)(DEATH_COUNTS(CUR_SAVE_SLOT) + 1u);
        break;
    default:
        break;
    }
}

void mode11_death_update(void)
{
    if (IS_UPDATING_MODE == 0u) init_mode11();
    else update_mode11();
}
