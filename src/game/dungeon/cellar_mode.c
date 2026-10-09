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

#define R(a) nes_ram[(a)]
static unsigned char returning;
extern void roomrom_combat_animate_link_base(void);   /* AnimateLinkBase */

static void end_prepare(void)
{
    R(0x13)=0; R(0x11)=0; R(0x0F)=0; R(0xAC)=0;
    R(0xC0)=0; R(0xD3)=0; R(0x4F0)=0;
}

unsigned char cellar_try_enter(void)
{
    unsigned short i;
    unsigned char room, tile, saved;
    if (R(0x10)==0 || R(0x12)!=5 || R(0x5A)!=0 || R(0x394)!=0 ||
        (R(0x70)&15)!=0 || (R(0x84)&15)!=13 ||
        (R(0xAC)&0xC0)==0x40) return 0;
    saved=R(0x49E);
    tile=collision_get_collidable_tile_still(0);
    R(0x49E)=saved;
    if (tile<0x70 || tile>=0x74) return 0;
    /* Z_05 CheckWarps increments an 8-bit X through installed RAM,
     * without stopping at the ten declared cellar entries. L3Q1
     * resolves its raft cellar through a later palette byte ($0F).
     * Preserve raw A/B indexing, including $FF candidates; one full
     * index cycle bounds malformed input without hanging the host. */
    for (i=0;i<256u;++i) {
        room=R(0x6BB2 + i);
        if (R(0x687E + room)!=R(0xEB) && R(0x68FE + room)!=R(0xEB)) continue;
        room_save_kill_count_uw();
        R(0x527)=R(0xEB);
        R(0xEB)=room; R(0x5B)=9; R(0x12)=0x10;
        end_prepare();
        returning=0;
        return 1;
    }
    return 0;
}

unsigned char cellar_check_exit(void)
{
    unsigned char room, pos;
    if (R(0x12)!=9 || R(0x84)>=0x40 || !(R(0x3F8)&8)) return 0;
    room=R(0xEB); pos=R(0x697E + room);
    R(0xEB)=R((R(0x70)<0x80 ? 0x687E : 0x68FE)+room);
    room_mark_room_visited();
    R(0x12)=0xA; end_prepare();
    R(0x70)=pos&0xF0; R(0x84)=(unsigned char)((pos<<4)|13);
    returning=1;
    return 1;
}

static void subroom_start(void)
{
    R(0xE9)=0; R(0xEE)=0;
    room_inc_submode();
}

static void fade(void)
{
    if (!world_animate_world_fading()) room_inc_submode();
}

unsigned char cellar_mode_tick(void)
{
    unsigned char sub=R(0x13);
    if (R(0x10)==0) { returning=0; return 0; }
    if (R(0x12)==0x10 && R(0x5B)==9) {
        if (!R(0x11)) {
            /* InitMode10: GetCollidableTileStill (ObjCollidedTile = the
             * stairs tile Link stands on), INC IsUpdatingMode. */
            (void)collision_get_collidable_tile_still(0);
            R(0x11)=1;
        } else {
            /* UpdateMode10Stairs_Full on stairs: GameMode = TargetMode,
             * EndGameMode, then AnimateAndDrawLinkBehindBackground ->
             * Link_EndMoveAndAnimate (mode 9 now: AnimateLinkBase). */
            R(0x12)=9; (void)room_end_game_mode();
            cellar_host_draw(2);
        }
        return 1;
    }
    if (R(0x12)==9 && !R(0x11)) {
        switch(sub) {
        case 0: subroom_start(); break;
        case 1: room_set_fade_cycle_and_advance_submode(0); fade(); break;
        case 2: case 7: fade(); break;
        case 3:
            /* LayoutRoom_SubmodeTask: the submode is the NES's next one
             * at the frame's end, the Genesis layout can outlast it. */
            R(0xE9)=0; room_inc_submode(); cellar_host_layout(1); break;
        case 4: (void)room_copy_next_row_advance_submode(); break;
        case 5: room_init_mode9_transfer_attrs(); break;
        case 6: room_set_fade_cycle_and_advance_submode(0xA0); fade(); break;
        case 8:
            room_reset_player_state();   /* InitMode_EnterRoom */
            cellar_host_enter(); room_reset_inv_obj_state();
            R(0x70)=R(0x527)==R(0x687E + R(0xEB)) ? 0x30 : 0xC0;
            R(0x84)=0x41; R(0x98)=4; R(0x394)=0xE4;
            R(0x11)=0; R(0x53)=0;
            room_inc_submode(); break;
        case 9:
            R(0x3F8)=R(0x98); cellar_host_walk();
            if (R(0x84)==0x5D) { R(0xAC)=0; R(0x5A)=1; R(0x11)=1; }
            break;
        }
        /* DrawSpritesBetweenRooms does not draw Link in UW. EnterCellar
         * also keeps him hidden; only WalkCellar calls UpdatePlayer. */
        cellar_host_draw(sub==9); return 1;
    }
    if (R(0x12)==0xA && returning) {
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
            room_fill_play_area_attrs(R(0xEB));
            room_init_mode3_sub3(); break;
        case 7: room_init_mode3_sub4(); R(0xE3)=0; break;
        case 8: room_set_fade_cycle_and_advance_submode(0x80); fade(); break;
        case 10: room_init_mode_a_sub_a_go_to_mode4(); break;
        }
        cellar_host_draw(0); return 1;
    }
    if (returning && R(0x12)==4) {
        if (!R(0x11)) {
            R(0x98)=4; R(0x53)=0; R(0x394)=0;
            cellar_host_enter(); R(0x11)=1;
        } else {
            room_go_to_next_mode_play_level_song();
        }
        cellar_host_draw(0); return 1;
    }
    if (returning && R(0x12)==5 && !R(0x11)) {
        room_init_mode5_play_palette_row7(); R(0x11)=1;
        /* InitMode5Play explicitly draws Link after its between-room clear. */
        cellar_host_draw(1); returning=0; return 1;
    }
    if (R(0x12)!=9 && R(0x12)!=0xA && R(0x12)!=4) returning=0;
    return 0;
}
