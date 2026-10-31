/* NES source: Z_05 CheckWarps, CheckSubroom, InitMode9, InitModeA,
 * InitMode_EnterRoom; Z_07 EndGameMode/StepOutside.
 * Drained C: src/oracle/room/room_mode_runtime.c, world_runtime.c;
 * active room_dispatch/world_dispatch/enemy_loop room-entry helpers.
 * Coverage: FULL native cellar orchestration and Link visibility boundaries.
 * Stance: EXTEND those helpers; no generic scene warp or return latch. */
#include "cellar_mode.h"
#include "platform_abi.h"
#include "../world/level_info_install.h"
#include "../room/room_dispatch.h"
#include "../world/world_dispatch.h"
#include "../combat/collision_dispatch.h"

static unsigned char returning;
extern void roomrom_combat_animate_link_base(void);   /* AnimateLinkBase */

static void end_prepare(void)
{
    F_ram_0013_N3=0; F_ram_0011_N3=0; F_ram_000F_N3=0; F_ram_00AC_N4=0;
    F_ram_00C0_N3=0; F_ram_00D3_N3=0; F_ram_04F0_N4=0;
}

unsigned char cellar_try_enter(void)
{
    unsigned short i;
    unsigned char room, tile, saved;
    if (F_ram_0010_N4==0 || F_ram_0012_N4!=5 || F_ram_005A_N!=0 || F_ram_0394_N3!=0 ||
        (F_ram_0070_N5&15)!=0 || (F_ram_0084_N5&15)!=13 ||
        (F_ram_00AC_N4&0xC0)==0x40) return 0;
    saved=F_ram_049E_N2;
    tile=collision_get_collidable_tile_still(0);
    F_ram_049E_N2=saved;
    if (tile<0x70 || tile>=0x74) return 0;
    /* Z_05 CheckWarps increments an 8-bit X through installed RAM,
     * without stopping at the ten declared cellar entries. L3Q1
     * resolves its raft cellar through a later palette byte ($0F).
     * Preserve raw A/B indexing, including $FF candidates; one full
     * index cycle bounds malformed input without hanging the host. */
    for (i=0;i<256u;++i) {
        room=F_ram_6BB2_X(i);
        if (F_ram_687E_X7(room)!=F_ram_00EB_N4 && F_ram_68FE_X6(room)!=F_ram_00EB_N4) continue;
        room_save_kill_count_uw();
        F_CellarSourceRoomId_N=F_ram_00EB_N4;
        F_ram_00EB_N4=room; F_ram_005B_N=9; F_ram_0012_N4=0x10;
        end_prepare();
        returning=0;
        return 1;
    }
    return 0;
}

unsigned char cellar_check_exit(void)
{
    unsigned char room, pos;
    if (F_ram_0012_N4!=9 || F_ram_0084_N5>=0x40 || !(F_ram_03F8_N3&8)) return 0;
    room=F_ram_00EB_N4; pos=F_ram_697E_X(room);
    F_ram_00EB_N4=nes_ram[((F_ram_0070_N5<0x80 ? 0x687E : 0x68FE)+room)]; /* native_ram bulk:dyn_select */
    room_mark_room_visited();
    F_ram_0012_N4=0xA; end_prepare();
    F_ram_0070_N5=pos&0xF0; F_ram_0084_N5=(unsigned char)((pos<<4)|13);
    returning=1;
    return 1;
}

static void subroom_start(void)
{
    F_ram_00E9_N2=0; F_ram_00EE_N3=0;
    room_inc_submode();
}

static void fade(void)
{
    if (!world_animate_world_fading()) room_inc_submode();
}

unsigned char cellar_mode_tick(void)
{
    unsigned char sub=F_ram_0013_N3;
    if (F_ram_0010_N4==0) { returning=0; return 0; }
    if (F_ram_0012_N4==0x10 && F_ram_005B_N==9) {
        if (!F_ram_0011_N3) {
            /* InitMode10: GetCollidableTileStill (ObjCollidedTile = the
             * stairs tile Link stands on), INC IsUpdatingMode. */
            (void)collision_get_collidable_tile_still(0);
            F_ram_0011_N3=1;
        } else {
            /* UpdateMode10Stairs_Full on stairs: GameMode = TargetMode,
             * EndGameMode, then AnimateAndDrawLinkBehindBackground ->
             * Link_EndMoveAndAnimate (mode 9 now: AnimateLinkBase). */
            F_ram_0012_N4=9; (void)room_end_game_mode();
            cellar_host_draw(2);
        }
        return 1;
    }
    if (F_ram_0012_N4==9 && !F_ram_0011_N3) {
        switch(sub) {
        case 0: subroom_start(); break;
        case 1: room_set_fade_cycle_and_advance_submode(0); fade(); break;
        case 2: case 7: fade(); break;
        case 3:
            /* LayoutRoom_SubmodeTask: the submode is the NES's next one
             * at the frame's end, the Genesis layout can outlast it. */
            F_ram_00E9_N2=0; room_inc_submode(); cellar_host_layout(1); break;
        case 4: (void)room_copy_next_row_advance_submode(); break;
        case 5: room_init_mode9_transfer_attrs(); break;
        case 6: room_set_fade_cycle_and_advance_submode(0xA0); fade(); break;
        case 8:
            room_reset_player_state();   /* InitMode_EnterRoom */
            cellar_host_enter(); room_reset_inv_obj_state();
            /* InitMode_EnterRoom ends in RunCrossRoomTasksAndBeginUpdate-
             * Mode_EnterPlayModes -> MarkRoomVisited (Z_05.asm:1831,
             * Z_07.asm:1547): the passage itself is visited. Live Redux NES:
             * bit $20 of passage $07 (Q1 L5) 0 -> $20 on entry (T-257). */
            room_mark_room_visited();
            F_ram_0070_N5=F_CellarSourceRoomId_N==F_ram_687E_X7(F_ram_00EB_N4) ? 0x30 : 0xC0;
            F_ram_0084_N5=0x41; F_ram_0098_N6=4; F_ram_0394_N3=0xE4;
            F_ram_0011_N3=0; F_ram_0053_N3=0;
            room_inc_submode(); break;
        case 9:
            F_ram_03F8_N3=F_ram_0098_N6; cellar_host_walk();
            if (F_ram_0084_N5==0x5D) { F_ram_00AC_N4=0; F_ram_005A_N=1; F_ram_0011_N3=1; }
            break;
        }
        /* DrawSpritesBetweenRooms does not draw Link in UW. EnterCellar
         * also keeps him hidden; only WalkCellar calls UpdatePlayer. */
        cellar_host_draw(sub==9); return 1;
    }
    if (F_ram_0012_N4==0xA && returning) {
        /* The layout's NES frames (k_cellar_return_tl) run no submode. */
        if (cellar_host_clock_busy()) { cellar_host_draw(0); return 1; }
        switch(sub) {
        case 0: subroom_start(); break;
        case 1: room_init_mode_a_sub1(); break;
        case 2: room_set_fade_cycle_and_advance_submode(0x20); fade(); break;
        case 3: fade(); cellar_host_prepare(); break;
        case 9: fade(); break;
        case 4:
            /* LayoutRoom_SubmodeTask: CurRow 0, submode 5 on the NES's
             * frame (the clock sets it). */
            cellar_host_return_layout(); break;
        case 5: (void)room_copy_next_row_advance_submode(); break;
        case 6:
            /* InitModeA_Sub6 fills the destination attributes before
             * queuing their top half; the shared mode-3 sender does not. */
            room_fill_play_area_attrs(F_ram_00EB_N4);
            room_init_mode3_sub3(); break;
        case 7: room_init_mode3_sub4(); F_ram_00E3_N=0; break;
        case 8: room_set_fade_cycle_and_advance_submode(0x80); fade(); break;
        case 10: room_init_mode_a_sub_a_go_to_mode4(); break;
        }
        cellar_host_draw(0); return 1;
    }
    if (returning && F_ram_0012_N4==4) {
        if (!F_ram_0011_N3) {
            F_ram_0098_N6=4; F_ram_0053_N3=0; F_ram_0394_N3=0;
            cellar_host_enter(); F_ram_0011_N3=1;
        } else {
            room_go_to_next_mode_play_level_song();
        }
        cellar_host_draw(0); return 1;
    }
    if (returning && F_ram_0012_N4==5 && !F_ram_0011_N3) {
        room_init_mode5_play_palette_row7(); F_ram_0011_N3=1;
        /* InitMode5Play explicitly draws Link after its between-room clear. */
        cellar_host_draw(1); returning=0; return 1;
    }
    if (F_ram_0012_N4!=9 && F_ram_0012_N4!=0xA && F_ram_0012_N4!=4) returning=0;
    return 0;
}
