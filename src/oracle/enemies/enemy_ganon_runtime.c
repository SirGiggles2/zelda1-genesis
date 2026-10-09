/* Phase 8 Task 8.10 — Ganon drain.
 *
 * NES source: reference/aldonunez/Z_04.asm
 *               9599 InitGanon
 *              10321 UpdateGanon (+ JT @10324)
 *              10334 Ganon_ScenePhase0
 *              10393 Ganon_ScenePhase1
 *              10411 Ganon_DrawBodyFrame0
 *              10424 Ganon_ScenePhase2
 *              10450 Ganon_RandomizeLocation (already drained
 *                    enrt_ganon_randomize_location, enemy_boss_runtime.c:239)
 *              10460 Ganon_MoveAndShoot
 *              10491 Ganon_UpdateBrownState
 *              10521 Ganon_Dying
 *              10593 Ganon_SetUpBurstRays
 *              10622 Ganon_DrawCloud
 *              10647 Ganon_DrawBurst
 *              10737..10772 Ganon_GetCurCloud{Left,Right,Top,Bottom}
 *                    (already drained — enemy_boss_runtime.c:380..392)
 *              10788 Ganon_DrawBody
 *              10838 Ganon_CheckCollisions
 *              10954..10960 Ganon_AppendPaletteRowTransferRecord_*
 *              10994 Ganon_DrawAshes
 *              10999 Ganon_ActivateRoomItem (already drained
 *                    enrt_ganon_activate_room_item, enemy_boss_runtime.c:366)
 *
 * Drained C:  PARTIAL — six already-drained Ganon helpers in
 *              enemy_boss_runtime.c plus reused primitives:
 *                enrt_play_boss_hit_cry_if_needed, enrt_play_boss_death_cry,
 *                enrt_update_candle, c_anim_write_sprite,
 *                c_draw_object_mirrored_with_frame,
 *                c_draw_object_not_mirrored_with_frame,
 *                core_reset_obj_metastate_and_timer,
 *                core_reset_shove_info_and_inv_timer, c_reset_obj_metastate,
 *                colrt_check_monster_sword_collision,
 *                colrt_check_monster_arrow_or_rod_collision,
 *                lcrt_check_link_collision_preinit,
 *                sprrt_anim_fetch_obj_pos.
 *
 * Coverage:   PARTIAL — InitGanon + UpdateGanon umbrella + ScenePhases +
 *             Ganon_Dying + DrawBody + DrawAshes + DrawCloud + DrawBurst +
 *             SetUpBurstRays + CheckCollisions +
 *             AppendPaletteRowTransferRecord_{Brown,Blue,Triforce} drained
 *             per-line. Ganon movement and burst rays share the complete
 *             BlueWizzrobe primitives in enemy_wizzrobe_runtime.c
 *             (Z_04.asm:7100-7230), including collidable-tile response.
 *             play_sample writes SampleRequest ($0601) for
 *             audio_requests.c (T-171). A connected Ganon encounter
 *             remains unverified.
 *
 * Stance:     EXTEND — composes drained shared Wizzrobe movement.
 *             Ganon encounter and ending remain integration TODO.
 */

#include "enemy_runtime_private.h"
#include "enemy_wizzrobe_runtime.h"
#include "platform_abi.h"
#include "core/core_dispatch.h"
#include "../combat/collision_runtime.h"
#include "../combat/link_collision_runtime.h"
#include "../world/sprite_runtime.h"

/* NES STA SampleRequest: the request cell audio_requests.c consumes.
 * audio_sfx_play takes a sample index 1..7, not this bitmap ($10 asked
 * for "sample 16", so InitGanon's roar never played; T-171). */
static void play_sample(unsigned char sample_id)
{
    RAM(0x0601u) = sample_id;
}

/* ---------- NES static tables (verbatim bytes) ---------- */

static const unsigned char GanonBurstDirs[8] = {
    0x01u, 0x02u, 0x04u, 0x05u, 0x06u, 0x08u, 0x09u, 0x0Au
};

static const unsigned char GanonBurstSpriteAttrs[10] = {
    0x00u, 0x00u, 0x40u, 0x00u, 0x80u, 0xC0u, 0x80u, 0x00u, 0x40u, 0x00u
};

static const unsigned char GanonBurstTiles[10] = {
    0x00u, 0x00u, 0xEEu, 0xEEu, 0xE8u, 0x30u, 0x30u, 0xE8u, 0x30u, 0x30u
};

static const unsigned char GanonFrameImages[24] = {
    0x06u, 0x08u, 0x07u, 0x09u, 0x00u, 0x00u, 0x01u, 0x01u,
    0x02u, 0x02u, 0x03u, 0x03u, 0x04u, 0x00u, 0x05u, 0x01u,
    0x04u, 0x04u, 0x05u, 0x05u, 0x00u, 0x04u, 0x01u, 0x05u
};

static const unsigned char GanonSpriteOffsetsX[4] = { 0x00u, 0x10u, 0x00u, 0x10u };
static const unsigned char GanonSpriteOffsetsY[4] = { 0x00u, 0x00u, 0x10u, 0x10u };
static const unsigned char GanonSpriteHFlips[4]   = { 0x00u, 0x01u, 0x00u, 0x01u };

static const unsigned char GanonColorTransferRecord[8] = {
    0x3Fu, 0x1Cu, 0x04u, 0x0Fu, 0x07u, 0x17u, 0x27u, 0xFFu
};

static const unsigned char GanonColorSets[9] = {
    0x07u, 0x17u, 0x30u, 0x16u, 0x2Cu, 0x3Cu, 0x27u, 0x06u, 0x16u
};

/* ---------- Ganon-specific RAM macros (per ObjVars.inc + Variables.inc) ---------- */

#define GANON_SCENE_PHASE              RAM(0x0445u)
#define GANON_OBJ_PHASE(slot)          OBJ(0x042Cu, (slot))
#define GANON_OBJ_ANIMATION_FRAME(s)   OBJ(0x046Bu, (s))
#define GANON_OBJ_CLOUD_DIST(slot)     OBJ(0x0478u, (slot))
#define GANON_ITEM_TYPE_TO_LIFT        RAM(0x0505u)
#define GANON_BRIGHTENING_ROOM         RAM(0x051Eu)
#define GANON_FADE_CYCLE               RAM(0x051Cu)
#define GANON_FRAME_COUNTER            RAM(0x0015u)
#define GANON_DYNTILEBUF_LEN           RAM(0x0301u)
#define GANON_DYNTILEBUF(idx)          RAM(0x0302u + (idx))
#define GANON_INV_ARROW                RAM(0x0659u)
#define GANON_CUR_OBJ_INDEX            RAM(0x0340u)
#define GANON_SONG_REQUEST             RAM(0x0600u)
#define GANON_TUNE1_REQUEST            RAM(0x0602u)
#define GANON_OBJ_REM_DISTANCE(slot)   OBJ(0x0394u, (slot))
#define GANON_OBJ_TIMER_SLOT0          RAM(0x0028u)  /* ObjTimer + 0 */
#define GANON_OBJ_STATE_SLOT0          RAM(0x00ACu)  /* ObjState + 0 */
#define GANON_ROOM_ITEM_STATE          OBJ(0x00ACu, 19u) /* ObjState+19 = room item */
#define GANON_ROOM_ITEM_X              OBJ(NES_OBJ_X, 19u)
#define GANON_ROOM_ITEM_Y              OBJ(NES_OBJ_Y, 19u)
#define GANON_ROOM_KILL_COUNT          RAM(NES_OBJ_TYPE) /* ObjType+0 = kill counter */
#define GANON_SHOT_OBJ_STATE_18        OBJ(0x00ACu, 18u)
#define GANON_SCRATCH_X                ZP_TMP0      /* [00] */
#define GANON_SCRATCH_Y                ZP_TMP1      /* [01] */
#define GANON_SCRATCH_03               ZP_TMP3      /* [03] sprite attrs */
#define GANON_SCRATCH_07               RAM(0x0007u) /* [07] loop scratch */
#define GANON_SCRATCH_0F               RAM(0x000Fu) /* [0F] H-flip flag */
#define GANON_COLLISION_06             RAM(0x0006u) /* [06] collision result */

/* ---------- Forward decls ---------- */

static void ganon_draw_body(unsigned int slot);
static void ganon_draw_body_frame0(unsigned int slot);
static void ganon_scene_phase0(unsigned int slot);
static void ganon_scene_phase1(unsigned int slot);
static void ganon_scene_phase2(unsigned int slot);
static void ganon_move_and_shoot(unsigned int slot);
static void ganon_update_brown_state(unsigned int slot);
static void ganon_dying(unsigned int slot);
static void ganon_set_up_burst_rays(unsigned int slot);
static void ganon_draw_cloud(unsigned int slot);
static void ganon_draw_burst(unsigned int slot);
static void ganon_draw_ashes(unsigned int slot);
static void ganon_check_collisions(unsigned int slot);
static void ganon_append_palette_row_transfer_record(unsigned char y_end);

/* ---------- InitGanon (Z_04.asm:9599) ---------- */

void enrt_init_ganon(unsigned int slot)
{
    /* Invincible to everything but sword and arrow. */
    ENEMY_INVINCIBILITY(slot) = 0xFAu;

    /* Halt Link via slot-0 ObjState + ObjTimer = $40 (scene phase 0 timer). */
    GANON_OBJ_STATE_SLOT0 = 0x40u;
    GANON_OBJ_TIMER_SLOT0 = 0x40u;

    /* STA SampleRequest $02, then PlaySample (ORA / STA) the roar $10. */
    play_sample(0x02u);
    RAM(0x0601u) = (unsigned char)(RAM(0x0601u) | 0x10u);

    /* JMP ResetObjMetastateAndTimer. */
    core_reset_obj_metastate_and_timer(slot);
}

/* ---------- UpdateGanon (Z_04.asm:10321) — JT over Ganon_ScenePhase ---------- */

void enrt_update_ganon(unsigned int slot)
{
    switch (GANON_SCENE_PHASE) {
    case 0u:  ganon_scene_phase0(slot); break;
    case 1u:  ganon_scene_phase1(slot); break;
    default:  ganon_scene_phase2(slot); break;
    }
}

/* ---------- Ganon_ScenePhase0 (Z_04.asm:10334) ---------- */

static void ganon_scene_phase0(unsigned int slot)
{
    GANON_ITEM_TYPE_TO_LIFT = 0x1Bu;

    if (GANON_OBJ_TIMER_SLOT0 != 0u) {
        /* Timer running → only check time-to-shout. */
        if (GANON_OBJ_TIMER_SLOT0 == 0x01u) {
            play_sample(0x02u); /* Boss hit/hurt sample = Ganon shouting. */
        }
        return;
    }

    /* Timer expired — brighten room. */
    enrt_update_candle();
    GANON_BRIGHTENING_ROOM = 0x00u;

    /* On first frame of fade-to-light, play Triforce/Ganon song. */
    if (GANON_FADE_CYCLE == 0xC0u) {
        GANON_SONG_REQUEST = 0x02u;
    }

    /* If fade cycle has not ended, draw Ganon. */
    if ((GANON_FADE_CYCLE & 0x0Fu) != 0x04u) {
        ganon_draw_body_frame0(slot);
        return;
    }

    /* Set Link's timer to $C0 for next phase, advance scene phase. */
    GANON_OBJ_TIMER_SLOT0 = 0xC0u;
    GANON_SCENE_PHASE++;
    ganon_draw_body_frame0(slot);
}

/* ---------- Ganon_ScenePhase1 (Z_04.asm:10393) ---------- */

static void ganon_scene_phase1(unsigned int slot)
{
    GANON_ITEM_TYPE_TO_LIFT = 0x1Bu;

    if (GANON_OBJ_TIMER_SLOT0 != 0u) {
        ganon_draw_body_frame0(slot);
        return;
    }

    /* Timer expired: unhalt Link, clear lift, queue level 9 song, advance. */
    GANON_OBJ_STATE_SLOT0 = 0x00u;
    GANON_ITEM_TYPE_TO_LIFT = 0x00u;
    GANON_SONG_REQUEST = 0x20u;
    GANON_SCENE_PHASE++;
    ganon_draw_body_frame0(slot);
}

/* ---------- Ganon_DrawBodyFrame0 (Z_04.asm:10411) ---------- */

static void ganon_draw_body_frame0(unsigned int slot)
{
    GANON_OBJ_ANIMATION_FRAME(slot) = 0x00u;
    ganon_draw_body(slot);
}

/* ---------- Ganon_ScenePhase2 (Z_04.asm:10424) ---------- */

static void ganon_scene_phase2(unsigned int slot)
{
    if (GANON_OBJ_PHASE(slot) != 0u) {
        ganon_dying(slot);
        return;
    }

    ganon_check_collisions(slot);
    enrt_play_boss_hit_cry_if_needed(slot);

    if (ENEMY_STATE_TIMER(slot) != 0u) {
        ganon_update_brown_state(slot);
        return;
    }

    /* State 0: Blue. */
    if (ENEMY_MOVE_TIMER(slot) == 0u) {
        ganon_move_and_shoot(slot);
        return;
    }

    if (ENEMY_MOVE_TIMER(slot) == 0x01u) {
        /* Timer = 1: randomize Ganon's location for next-frame teleport. */
        enrt_ganon_randomize_location(slot);
    }

    /* Else timer > 1: only draw. */
    ganon_draw_body(slot);
}

/* ---------- Ganon_MoveAndShoot (Z_04.asm:10460) ---------- */

static void ganon_move_and_shoot(unsigned int slot)
{
    unsigned char frame;

    /* Animation frame +1, wraps after 6. */
    frame = (unsigned char)(GANON_OBJ_ANIMATION_FRAME(slot) + 1u);
    if (frame == 0x06u) frame = 0x00u;
    GANON_OBJ_ANIMATION_FRAME(slot) = frame;

    /* Move like a blue wizzrobe teleporting. */
    GANON_OBJ_REM_DISTANCE(slot) = 0x01u;
    enrt_blue_wizzrobe_turn_sometimes_and_move_and_check_tile(slot);

    /* Shoot a fireball every $40 frames. */
    if ((GANON_FRAME_COUNTER & 0x3Fu) == 0u) {
        c_shoot_fireball(0x56u, slot);
    }
}

/* ---------- Ganon_UpdateBrownState (Z_04.asm:10491) ---------- */

static void ganon_update_brown_state(unsigned int slot)
{
    unsigned char fc = GANON_FRAME_COUNTER;

    /* NES LDA FrameCounter / LSR / BCC: odd frames decrement the state
     * and (translucent phase) draw; the drain used even frames (T-171). */
    if ((fc & 0x01u) != 0u) {
        unsigned char st = (unsigned char)(ENEMY_STATE_TIMER(slot) - 1u);
        ENEMY_STATE_TIMER(slot) = st;
        if (st == 0u) {
            /* Ganon back to blue: randomize location + palette swap. */
            enrt_ganon_randomize_location(slot);
            ganon_append_palette_row_transfer_record(0x05u); /* Blue. */
            return;
        }
    }

    /* Draw branch: opaque when state >= $30, else translucent half-rate. */
    if (ENEMY_STATE_TIMER(slot) >= 0x30u) {
        ganon_draw_body(slot);
        return;
    }
    if ((fc & 0x01u) != 0u) {
        ganon_draw_body(slot);
    }
}

/* ---------- Ganon_Dying (Z_04.asm:10521) ---------- */

static void ganon_dying(unsigned int slot)
{
    unsigned char phase;

    /* INC Ganon_ObjPhase, with $FF→0 clamp back to $FF. */
    phase = (unsigned char)(GANON_OBJ_PHASE(slot) + 1u);
    if (phase == 0u) phase = 0xFFu;
    GANON_OBJ_PHASE(slot) = phase;

    if (phase < 0x50u) {
        ganon_draw_body(slot);
        return;
    }

    if (phase == 0x50u) {
        /* Set up burst + ashes + position offset. */
        ganon_append_palette_row_transfer_record(0x08u); /* Triforce. */
        /* NES ADC #$07 / ADC #$08 without CLC: the record copy ends on
         * CPY #$08 (equal, carry set), so X += 8 and Y += 8 + carry out
         * of the X add (T-171). */
        {
            const unsigned int sx = (unsigned int)ENEMY_X(slot) + 0x07u + 1u;
            ENEMY_X(slot) = (unsigned char)sx;
            ENEMY_Y(slot) = (unsigned char)(ENEMY_Y(slot) + 0x08u + (sx >> 8));
        }
        ganon_set_up_burst_rays(slot);
        enrt_play_boss_death_cry();
        GANON_SONG_REQUEST = 0x02u;
    }

    ganon_draw_ashes(slot);

    if (phase < 0xA0u) {
        ganon_draw_burst(slot);
        return;
    }

    if (phase == 0xA0u) {
        /* Activate the room item (triforce of power). */
        enrt_ganon_activate_room_item();
        GANON_ROOM_ITEM_X = ENEMY_X(slot);
        GANON_ROOM_ITEM_Y = ENEMY_Y(slot);
        GANON_ROOM_KILL_COUNT = (unsigned char)(GANON_ROOM_KILL_COUNT + 1u);
    }
    /* phase > $A0: nothing to do. */
}

/* ---------- Ganon_SetUpBurstRays (Z_04.asm:10593) ---------- */

static void ganon_set_up_burst_rays(unsigned int slot)
{
    /* Loop Y=7..0; access slots 9..2 (Y+2). */
    int y;
    for (y = 7; y >= 0; --y) {
        unsigned int dst = (unsigned int)(y + 2);
        /* CLC / ADC #4 on X; ADC #4 on Y keeps that carry (no CLC). */
        const unsigned int sx = (unsigned int)(unsigned char)ENEMY_X(slot) + 0x04u;
        ENEMY_X(dst) = (unsigned char)sx;
        ENEMY_Y(dst) = (unsigned char)((unsigned char)ENEMY_Y(slot) + 0x04u + (sx >> 8));
        ENEMY_DIR(dst) = GanonBurstDirs[y];
    }
}

/* ---------- Ganon_DrawCloud (Z_04.asm:10622) ---------- */

static void ganon_draw_cloud(unsigned int slot)
{
    /* Save the coordinates [00]/[01]. */
    unsigned char saved_x = (unsigned char)GANON_SCRATCH_X;
    unsigned char saved_y = (unsigned char)GANON_SCRATCH_Y;

    /* Frame image: $0C if cloud-distance >= 6, else $0D (low density). */
    unsigned char frame = (GANON_OBJ_CLOUD_DIST(slot) >= 0x06u) ? 0x0Cu : 0x0Du;

    /* No horizontal flipping. */
    GANON_SCRATCH_0F = 0x00u;
    c_draw_object_mirrored_with_frame(frame, slot);

    GANON_SCRATCH_X = saved_x;
    GANON_SCRATCH_Y = saved_y;
}

/* ---------- Ganon_DrawBurst (Z_04.asm:10647) ---------- */

static void ganon_draw_burst(unsigned int slot)
{
    unsigned int ray;
    unsigned char saved_idx;

    /* Decrement cloud-distance every 8 frames if non-zero. */
    if (GANON_OBJ_CLOUD_DIST(slot) != 0u) {
        if ((GANON_FRAME_COUNTER & 0x07u) == 0u) {
            GANON_OBJ_CLOUD_DIST(slot) = (unsigned char)(GANON_OBJ_CLOUD_DIST(slot) - 1u);
        }
    }

    /* Draw the four diagonal-corner clouds. */
    enrt_ganon_get_cur_cloud_left(slot);
    enrt_ganon_get_cur_cloud_top(slot);
    ganon_draw_cloud(slot);
    enrt_ganon_get_cur_cloud_right(slot);
    ganon_draw_cloud(slot);
    enrt_ganon_get_cur_cloud_left(slot);
    enrt_ganon_get_cur_cloud_bottom(slot);
    ganon_draw_cloud(slot);
    enrt_ganon_get_cur_cloud_right(slot);
    ganon_draw_cloud(slot);

    /* Top + bottom clouds. */
    GANON_SCRATCH_X = ENEMY_X(slot);
    enrt_ganon_get_cur_cloud_top(slot);
    ganon_draw_cloud(slot);
    enrt_ganon_get_cur_cloud_bottom(slot);
    ganon_draw_cloud(slot);

    /* Left + right clouds. */
    enrt_ganon_get_cur_cloud_left(slot);
    GANON_SCRATCH_Y = ENEMY_Y(slot);
    ganon_draw_cloud(slot);
    enrt_ganon_get_cur_cloud_right(slot);
    ganon_draw_cloud(slot);

    /* Loop slots 2..9: move + draw 8 burst rays. */
    saved_idx = GANON_CUR_OBJ_INDEX;
    for (ray = 2u; ray < 10u; ++ray) {
        GANON_CUR_OBJ_INDEX = (unsigned char)ray;

        /* Move every frame for slot < 5 or = 7; else 3-of-4. */
        if (ray < 5u || ray == 7u) {
            enrt_blue_wizzrobe_move(ray);
        } else if ((GANON_FRAME_COUNTER & 0x03u) != 0u) {
            enrt_blue_wizzrobe_move(ray);
        }

        /* Anim_FetchObjPosForSpriteDescriptor + write tile $XX with attrs. */
        (void)sprrt_anim_fetch_obj_pos(ray);
        GANON_SCRATCH_03 = (unsigned char)((GANON_FRAME_COUNTER & 0x03u)
                                            | GanonBurstSpriteAttrs[ray]);
        c_anim_write_sprite(GanonBurstTiles[ray], ray);
    }
    GANON_CUR_OBJ_INDEX = saved_idx;
}

/* ---------- Ganon_DrawBody (Z_04.asm:10788) ---------- */

static void ganon_draw_body(unsigned int slot)
{
    /* Loop Y=3..0 over four 16x16 corners. */
    int corner;
    for (corner = 3; corner >= 0; --corner) {
        unsigned char part = (unsigned char)corner;
        unsigned char anim = GANON_OBJ_ANIMATION_FRAME(slot);
        unsigned char idx = (unsigned char)((anim << 2) + part);
        unsigned char frame_image;

        GANON_SCRATCH_X = (unsigned char)(ENEMY_X(slot) + GanonSpriteOffsetsX[part]);
        GANON_SCRATCH_Y = (unsigned char)(ENEMY_Y(slot) + GanonSpriteOffsetsY[part]);
        GANON_SCRATCH_0F = GanonSpriteHFlips[part];
        GANON_SCRATCH_07 = part;

        frame_image = GanonFrameImages[idx];
        c_draw_object_not_mirrored_with_frame(frame_image, slot);
    }
}

/* ---------- Ganon_DrawAshes (Z_04.asm:10994) ---------- */

static void ganon_draw_ashes(unsigned int slot)
{
    (void)sprrt_anim_fetch_obj_pos(slot);
    c_draw_object_not_mirrored_with_frame(0x0Bu, slot);
}

/* ---------- Ganon_CheckCollisions (Z_04.asm:10838) ---------- */

static void ganon_check_collisions(unsigned int slot)
{
    /* Calculate Ganon midpoint at offset (+$10, +$10) → [02]/[03]. */
    RAM(0x0002u) = (unsigned char)(ENEMY_X(slot) + 0x10u);
    RAM(0x0003u) = (unsigned char)(ENEMY_Y(slot) + 0x10u);

    if (ENEMY_HIT_REACTION(0u) == 0u) {
        /* Link not invincible: init partial-collision-check return cells, run check. */
        RAM(0x0006u) = 0x00u;
        RAM(0x0009u) = 0x00u;
        RAM(0x000Cu) = 0x00u;
        RAM(0x0000u) = 0x00u;
        lcrt_check_link_collision_preinit(slot);
    }

    if (ENEMY_STATE_TIMER(slot) == 0u) {
        /* Blue Ganon. */
        if (ENEMY_MOVE_TIMER(slot) != 0u) {
            return; /* Visible — can't be harmed. */
        }
        colrt_check_monster_sword_collision(slot, 0x0Du);

        if (ENEMY_METASTATE(slot) != 0u) {
            /* Restore HP, set state $FF (vulnerable to silver arrows), brown palette. */
            ENEMY_HP(slot) = 0xF0u;
            ENEMY_STATE_TIMER(slot) = (unsigned char)(ENEMY_STATE_TIMER(slot) - 1u);
            ganon_append_palette_row_transfer_record(0x02u); /* Brown. */
        }

        if (ENEMY_HIT_REACTION(slot) != 0u) {
            enrt_play_boss_hit_cry_if_needed(slot);
            ENEMY_MOVE_TIMER(slot) = 0x40u;
        }

        c_reset_obj_metastate(slot);
        core_reset_shove_info_and_inv_timer(slot);
        return;
    }

    /* Brown Ganon (state != 0): only silver arrow can hurt. */
    if (GANON_INV_ARROW != 0x02u) {
        return;
    }
    GANON_COLLISION_06 = 0x00u;

    if (GANON_SHOT_OBJ_STATE_18 != 0x10u) {
        return; /* No arrow in flight. */
    }
    colrt_check_monster_arrow_or_rod_collision(slot, 18u);
    if (GANON_COLLISION_06 == 0u) {
        return;
    }

    /* Arrow hit → start dying phase. */
    GANON_OBJ_PHASE(slot) = (unsigned char)(GANON_OBJ_PHASE(slot) + 1u);
    ENEMY_HIT_REACTION(slot) = 0x28u;
    GANON_OBJ_CLOUD_DIST(slot) = 0x08u;
}

/* ---------- Ganon_AppendPaletteRowTransferRecord_{Brown,Blue,Triforce}
 *            (Z_04.asm:10954..10960). y_end:
 *              0x02 = Brown, 0x05 = Blue, 0x08 = Triforce. ---------- */

static void ganon_append_palette_row_transfer_record(unsigned char y_end)
{
    unsigned char buf_idx = GANON_DYNTILEBUF_LEN;
    unsigned char i;
    int rev;
    unsigned char y;

    /* Copy 8-byte template into DynTileBuf[len..len+8). */
    for (i = 0u; i < 8u; ++i) {
        GANON_DYNTILEBUF((unsigned char)(buf_idx + i)) = GanonColorTransferRecord[i];   /* X wraps */
    }
    GANON_DYNTILEBUF_LEN = (unsigned char)(buf_idx + 8u);

    /* Overwrite three colors with y_end..y_end-2. NES STA DynTileBuf+4,X
     * is absolute: entries 4..6 of the buffer, wherever this record went
     * (T-171). */
    y = y_end;
    for (rev = 2; rev >= 0; --rev) {
        GANON_DYNTILEBUF((unsigned int)(4u + (unsigned int)rev)) = GanonColorSets[y];
        --y;
    }
}
