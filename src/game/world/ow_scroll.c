/* NES source: Z_07 CheckScreenEdge, UpdateMode4and6EnterLeave;
 * Z_05 InitMode6, InitMode7Submodes, UpdateMode7ScrollSubmode, ScrollWorld,
 * InitMode4 (Sub1-3, InitMode_EnterRoom @Method2), GetPassedDoorType,
 * LeavingRoomRelativePositions / EnteringRoomRelativePositions.
 * Drained C: src/oracle/room/room_mode_runtime.c (mode boundaries/sub2/6/7),
 * room_load_runtime.c (entry/reset); native room_dispatch.c counterparts;
 * world_animate_world_fading (world_dispatch.c).
 * Coverage: PARTIAL (native OW + UW sequencing and camera/Link displacement).
 * Stance: EXTEND. Rendering stays behind the existing host/adapter boundary.
 * NES PPU transfers become preparation ticks; their byte addresses are not
 * Genesis VDP addresses. No new gameplay state is stored in renderer slots.
 * T-131: the UW takes the same modes with its CurLevel rules (walk out of
 * false/bombable walls, 2 px/frame horizontal and every-4th-frame vertical
 * scrolling, dark-room fades, walk in through the entered door), replacing
 * the fixed-duration Genesis dungeon scroll.
 * Smooth vertical scroll (user 2026-09-28, option Room scroll = SMOOTH,
 * the default): ScrollWorld moves the camera and Link 8 px on every 2nd
 * (OW) / 4th (UW) frame. The NES RAM keeps that exact cadence; only the
 * picture glides, 4 px (OW) / 2 px (UW) every frame, the horizontal
 * speeds, and lands on the last row step (measured with
 * tools/lockstep/transition_smooth.py). CLASSIC shows the NES steps.
 */
#include "ow_scroll.h"
#include "platform_abi.h"
#include "world_dispatch.h"              /* world_animate_world_fading */
#include "../dungeon/door_state.h"       /* uw_door_state_get_type */
#include "../dungeon/uw_dark.h"          /* uw_dark_is_dark_room */
#include "../room/room_dispatch.h"       /* room_save_kill_count_uw/_ow */
#include "render/ow_render.h"             /* roomrom_ow_room_render_prepare_layout */
#include "../options/options_consumer.h" /* options_consumer_get_room_scroll */
#include "../options/options_state.h"    /* OPTIONS_SCROLL_SMOOTH */

#define UPD nes_ram[0x11u]
#define MODE nes_ram[0x12u]
#define SUB nes_ram[0x13u]
#define ROW nes_ram[0xE9u]
#define CUR_LEVEL        nes_ram[0x10u]
#define GRID             nes_ram[0x394u]
#define OBJ_DIR          nes_ram[0x98u]
#define FADE_CYCLE       nes_ram[0x51Cu]
#define CANDLE_STATE     nes_ram[0x51Fu]

static unsigned char direction;
static unsigned char target_room;
static unsigned char source_room;
static unsigned char column;
static unsigned short pixels;
static unsigned char link_hidden;
static unsigned char s_catch_up;   /* NES frames to replay (T-145) */
/* Smooth vertical picture: frames since the first row step (0xFF = none
 * yet), Link's Y before it, and the Y where ScrollWorld stops moving him. */
static unsigned char s_vk = 0xFFu;
static short s_vstart_y;
static short s_vfinal_y;

unsigned char ow_scroll_take_catch_up(void)
{
    unsigned char n = s_catch_up;
    s_catch_up = 0u;
    return n;
}

unsigned char ow_scroll_edge(short x, short y, unsigned char dir,
                             signed char grid, unsigned char room)
{
    if (grid != 0) return 0u;
    /* Z_07 PlayerScreenEdgeBounds. Test BEFORE a step, at equality. */
    if (dir == 1u && x == 0xF0) return (room & 15u) < 15u ? 1u : 0x80u;
    if (dir == 2u && x == 0) return (room & 15u) > 0u ? 2u : 0x80u;
    if (dir == 4u && y == 0xDD) return room < 0x70u ? 3u : 0x80u;
    if (dir == 8u && y == 0x3D) return room >= 0x10u ? 4u : 0x80u;
    return 0u;
}

void ow_scroll_begin(unsigned char dir, unsigned char target)
{
    direction = dir;
    target_room = target;
    source_room = nes_ram[0xEBu];
    pixels = 0u;
    column = 0xFFu;
    link_hidden = 0u;
    s_vk = 0xFFu;
    MODE = 6u;
    SUB = UPD = 0u;
    if (CUR_LEVEL == 0u) GRID = 0u;
    /* CheckScreenEdge publishes the chosen direction before mode 6.
     * In particular, reversing at the arrival edge must not leave the
     * previous crossing's direction in the room-entry/spawn consumers. */
    OBJ_DIR = dir < 3u ? dir : (dir == 3u ? 4u : 8u);
    nes_ram[0xACu] = nes_ram[0xC0u] = nes_ram[0xD3u] = 0u;
    nes_ram[0x4F0u] = 0u;
}

void ow_scroll_begin_enter(unsigned char nes_dir)
{
    /* T-132: GoToNextModePlayLevelSong after the level-entry curtain
     * starts mode 4 at submode 0 (InitMode_EnterRoom). */
    direction = nes_dir == 1u ? 1u : nes_dir == 2u ? 2u : nes_dir == 4u ? 3u : 4u;
    target_room = source_room = nes_ram[0xEBu];
    pixels = 0u;
    column = 0xFFu;
    link_hidden = 1u;
    OBJ_DIR = nes_dir;
    MODE = 4u;
    SUB = UPD = 0u;
}

unsigned char ow_scroll_column(void) { return column; }
unsigned short ow_scroll_pixels(void) { return pixels; }

static unsigned char smooth_v_active(void)
{
    return MODE == 7u && direction >= 3u && s_vk != 0xFFu &&
           options_consumer_get_room_scroll() == OPTIONS_SCROLL_SMOOTH;
}

unsigned short ow_scroll_display_pixels(void)
{
    unsigned short d;
    if (!smooth_v_active()) return pixels;
    /* First row step frame shows one speed's worth; 22 steps x 8 px =
     * 176 px are reached (k + 1) * speed = 176, i.e. 21 periods + the
     * period's remaining frames after the first step: the frame the last
     * row step lands on (OW k = 43 / UW k = 87, mode 7 Sub4-7). */
    d = (unsigned short)((s_vk + 1u) * (CUR_LEVEL != 0u ? 2u : 4u));
    return d < 176u ? d : 176u;
}

short ow_scroll_display_link_y(short y)
{
    short d;
    if (!smooth_v_active()) return y;
    d = (short)ow_scroll_display_pixels();
    if (direction == 3u) {                  /* down: Link rides up */
        d = (short)(s_vstart_y - d);
        return d > s_vfinal_y ? d : s_vfinal_y;
    }
    d = (short)(s_vstart_y + d);            /* up: Link rides down */
    return d < s_vfinal_y ? d : s_vfinal_y;
}
unsigned char ow_scroll_link_hidden(void) { return link_hidden; }

/* Door type (FindDoorAttrByDoorBit) of the doorway in NES direction d. */
static unsigned char door_type(unsigned char d)
{
    return uw_door_state_get_type((d & 1u) ? DOOR_DIR_E : (d & 2u) ? DOOR_DIR_W :
                                  (d & 4u) ? DOOR_DIR_S : DOOR_DIR_N);
}

static unsigned char opposite(unsigned char d)
{
    return (d & 1u) ? 2u : (d & 2u) ? 1u : (d & 4u) ? 8u : 4u;
}

static unsigned char grid_at_limit(void)
{
    return (GRID == 0u || GRID == 0x08u || GRID == 0xF8u) ? 1u : 0u;
}

void ow_scroll_enter_room_uw(short *x)
{
    unsigned char d = OBJ_DIR, t, y;
    nes_ram[0x53u] = d;                              /* DoorwayDir */
    nes_ram[0x54u] = 0u;                             /* TriggeredDoorCmd */
    nes_ram[0x55u] = (unsigned char)(opposite(d) & nes_ram[0xEEu]);
    if (nes_ram[0x55u]) nes_ram[0x54u] = 0x02u;      /* close command */
    if (d & 0x03u) *x = (d & 0x01u) ? 0x00 : 0xF0;
    y = (d & 0x05u) ? 1u : 0u;                       /* GetPassedDoorType */
    t = (unsigned char)(door_type(opposite(d)) & 7u);
    if (!(t == 0u || t == 5u || t == 6u)) y = (unsigned char)(y + 2u);
    {
        static const unsigned char k_entering[4] = { 0x18u, 0xE8u, 0x28u, 0xD8u };
        GRID = k_entering[y];
    }
    nes_ram[0x3A8u] = 0u;
}

unsigned char ow_scroll_tick(short *x, short *y)
{
    unsigned char uw = CUR_LEVEL != 0u;
    column = 0xFFu;
    if (MODE == 6u) {
        if (!UPD) {
            unsigned char i;
            /* InitMode6: SaveKillCount (Z_05.asm, T-013: was saved on the
             * edge tick, a frame before the NES). */
            if (uw) room_save_kill_count_uw();
            else room_save_kill_count_ow(nes_ram[0xEBu]);
            room_reset_player_state();   /* ResetPlayerState: ObjState, InvClock */
            nes_ram[0x64u] = 0u;  /* LadderSlot */
            for (i = 13u; i < 19u; ++i) nes_ram[0xACu + i] = 0u;
            if (uw) {
                /* InitMode6: hide sprites; a false wall plays the secret
                 * tune; false and bombable walls leave Link $28 px to walk
                 * to the edge (LeavingRoomRelativePositions). */
                unsigned char t = (unsigned char)(door_type(OBJ_DIR) & 7u);
                link_hidden = 1u;
                if (t == 2u) nes_ram[0x602u] = 0x04u;
                GRID = (t >= 2u && t < 5u) ? ((OBJ_DIR & 0x05u) ? 0xD8u : 0x28u) : 0u;
            }
            UPD = 1u;
        } else if (!grid_at_limit()) {
            link_hidden = 0u;                    /* Link_EndMoveAndAnimate draws */
            return OW_SCROLL_WALK;
        } else {
            MODE = 7u;
            SUB = UPD = 0u;
            GRID = 0u;
        }
    } else if (MODE == 7u && !UPD) {
        switch (SUB) {
        case 0u:
            /* InitMode7_Sub0: a whirlwind teleport scrolls from
             * WhirlwindPrevRoomId ($EA), the room left of the level's
             * entrance (T-171 t171_flute_whirlwind). */
            if (nes_ram[0x522u] != 0u) nes_ram[0xEBu] = nes_ram[0xEAu];
            /* The pond refills before the scroll (AnimatePond until
             * SecretColorCycle is 0; T-171). */
            if (nes_ram[0x51Au] != 0u) { room_animate_pond(); break; }
            SUB = 1u;
            break;
        case 1u:
            /* InitMode7_Sub1: PrevOpenedDoors = CurOpenedDoors, the
             * entering side becomes CurOpenedDoors, DEC PrevRow, then
             * CalculateNextRoom. The UW door model publishes its own
             * $EE/$521; the OW keeps the NES bookkeeping (T-171). */
            if (!uw) {
                nes_ram[0x521u] = nes_ram[0xEEu];
                nes_ram[0xEEu] = opposite(OBJ_DIR);
                nes_ram[0xEDu] = (unsigned char)(nes_ram[0xEDu] - 1u);
            }
            /* CalculateNextRoom -> CalculateNextRoomForDoor (every
             * passable side, OW and UW): [$E7] = the direction bit,
             * [$04E4] = the room's unique ID (GetUniqueRoomId, write-only
             * on the NES; ROM bank 5 $B522). */
            nes_ram[0xE7u] = OBJ_DIR;
            nes_ram[0x4E4u] = room_get_unique_room_id();
            nes_ram[0xECu] = target_room;
            if (uw) link_hidden = 1u;
            ROW = 21u;
            SUB = 2u;
            break;
        case 2u:
            /* NES transfers all 22 rows before scrolling. Use that budget
             * to stage the native room incrementally (16 metatile columns). */
            if (ROW >= 6u) column = (unsigned char)(21u - ROW);
            /* T-172: InitMode_EnterRoom's tile-object lookup reads the
             * layout summary; build it on a row-copy tick. */
            if (!uw && ROW == 2u) roomrom_ow_room_render_prepare_layout(target_room);
            if (ROW-- == 0u) SUB = 3u;
            break;
        case 3u: case 4u: ++SUB; break;
        case 5u:
            /* InitMode7_Sub5: fade to black before scrolling into a dark
             * room from a light or candle-lit one. */
            FADE_CYCLE = 0u;
            if (uw && uw_dark_is_dark_room(target_room) &&
                (!uw_dark_is_dark_room(source_room) || CANDLE_STATE != 0u)) {
                CANDLE_STATE = 0u;
                FADE_CYCLE = 0x40u;
                SUB = 6u;
                break;
            }
            /* fall through: InitMode7_Finish */
        default:
            if (SUB == 6u && world_animate_world_fading() != 0u) break;
            nes_ram[0xEBu] = target_room;
            SUB = 0u;
            UPD = 1u;
            break;
        }
    } else if (MODE == 7u) {
        if (s_vk != 0xFFu && s_vk < 0xFEu) ++s_vk;
        switch (SUB) {
        case 0u:
            SUB = direction == 4u ? 1u : 2u;
            ROW = direction == 4u ? 22u : 0xFFu;
            break;
        case 1u: SUB = 2u; break;
        case 2u:
            nes_ram[0xE6u] = (unsigned char)((nes_ram[0x15u] + 1u) & (uw ? 3u : 1u));
            SUB = 3u;
            break;
        case 3u:
            if (direction < 3u) {
                /* ScrollWorldH: 2 px a frame in the UW, 4 in the OW. */
                unsigned char speed = uw ? 2u : 4u;
                pixels += speed;
                if (direction == 1u && *x > 0) *x -= speed;
                if (direction == 2u && *x < 0xF0) *x += speed;
                if (pixels == 256u) SUB = 4u;
            } else if ((nes_ram[0x15u] & (uw ? 3u : 1u)) == nes_ram[0xE6u]) {
                if (s_vk == 0xFFu) {
                    /* First row step: where ScrollWorld will leave Link
                     * (down: while Y >= $3E, Y -= 8; up: while Y < $DD,
                     * Y += 8). */
                    short f = *y;
                    if (direction == 4u) { while (f < 0xDD) f += 8; }
                    else { while (f >= 0x3E) f -= 8; }
                    s_vk = 0u;
                    s_vstart_y = *y;
                    s_vfinal_y = f;
                }
                if (direction == 4u) {
                    if (*y < 0xDD) *y += 8;
                    /* Up has a final row-underflow tick with no camera move. */
                    if (ROW-- == 0u) SUB = 4u;
                } else {
                    if (*y >= 0x3E) *y -= 8;
                    if (++ROW == 21u) SUB = 4u;
                }
                if (pixels < 176u) pixels += 8u;
            }
            if (SUB == 4u) ROW = nes_ram[0xEDu] = 0xFFu;
            break;
        case 4u: SUB = direction == 4u ? 6u : 5u; break;
        case 5u: SUB = 6u; break;
        case 6u:
            /* UpdateMode7Scroll_Sub6: after scrolling into a dark room,
             * CurRow = 0 tells mode 4 to re-copy the play area. */
            if (uw && uw_dark_is_dark_room(nes_ram[0xEBu])) {
                ROW = 0u;
                SUB = 7u;
                break;
            }
            /* fall through: Sub7 */
        default: MODE = 4u; SUB = 1u; UPD = 0u; break;
        }
    } else if (MODE == 4u) {
        if (SUB == 1u) {
            /* InitMode4 Sub1 re-copies the play area (22 rows) after a
             * scroll into a dark room; the Genesis plane already shows
             * the room, so it takes one frame here (faster than NES).
             * T-145: the NES spends 22 frames (one row each, CurRow 0 ->
             * $16); the caller runs the other 21 frames' NES frame work
             * (ow_scroll_take_catch_up), so FrameCounter/Random/timers
             * match when play resumes (t111 room $66: 21 steps behind). */
            if (ROW < 0x80u) {
                s_catch_up = 21u;
                ROW = 0x16u;
            }
            SUB = 2u;
        } else if (SUB == 2u) {
            /* InitMode4 Sub2: a light room entered from an unlit dark room
             * fades to light (cycle $C0). */
            SUB = 0u;
            if (uw && !uw_dark_is_dark_room(nes_ram[0xEBu]) &&
                uw_dark_is_dark_room(source_room) && CANDLE_STATE == 0u) {
                FADE_CYCLE = 0xC0u;
                SUB = 3u;
            } else {
                CANDLE_STATE = 0u;               /* InitMode4_GoToSub0 */
            }
        } else if (SUB == 3u) {
            if (world_animate_world_fading() == 0u) {
                SUB = 0u;
                CANDLE_STATE = 0u;
            }
        } else if (!UPD) {
            /* InitMode_EnterRoom (OW and UW). */
            nes_ram[0x3A8u] = 0u;
            if (!uw) {
                /* Method 2 in the OW: TriggeredDoorDir = the entering
                 * side if it is in CurOpenedDoors, command 2 (T-171;
                 * ow_scroll_enter_room_uw does the UW). */
                nes_ram[0x54u] = 0u;
                nes_ram[0x55u] = (unsigned char)(opposite(OBJ_DIR) & nes_ram[0xEEu]);
                if (nes_ram[0x55u]) nes_ram[0x54u] = 0x02u;
            }
            UPD = 1u;
            if (uw) link_hidden = 1u;
            else GRID = 0u;
            nes_ram[0x70u] = (unsigned char)*x;
            nes_ram[0x84u] = (unsigned char)*y;
            return OW_SCROLL_ENTER;
        } else if (!grid_at_limit()) {
            link_hidden = 0u;
            return OW_SCROLL_WALK;
        } else { MODE = 5u; SUB = UPD = 0u; GRID = 0u; }
    } else if (MODE == 5u) {
        UPD = 1u;
        return OW_SCROLL_PLAY;
    }
    nes_ram[0x70u] = (unsigned char)*x;
    nes_ram[0x84u] = (unsigned char)*y;
    return OW_SCROLL_BUSY;
}
