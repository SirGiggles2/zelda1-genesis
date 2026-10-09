/* engine warp coordinator implementation (Task 5.4).
 *
 * NES source authority:
 *   reference/aldonunez/Z_05.asm:CheckWarps   (line 7213)
 *   reference/aldonunez/Z_05.asm:HandleWarpOW (line 7313)
 *
 * Drained C: room_dispatch/world_dispatch mode helpers.
 * Coverage: PARTIAL (OW coordinator; cellar ownership extended).
 * Stance: EXTEND. NES disasm wins ties.
 *
 * Slice 1 limits OW->UW entry to manifest hits only. L2-L9 selectors
 * resolve cleanly (rule 6 passes) but rule 7 manifest membership rejects
 * them, incrementing the unsupported-selector counter so probes can
 * surface the bring-up path.
 */

#include "../combat/collision_dispatch.h"   /* GetCollidableTileStill (T-128) */
#include "transition.h"
#include "platform_abi.h"  /* nes_ram for audio cell writes */
#include "../../../engine/src/engine_state.h"
#include "ow_meta.h"
#include "level_info_install.h"
#include "render/ow_render.h"     /* Phase 12.2 promoted */
#include "../dungeon/uw_render.h"  /* Phase 12.2 promoted */
#include "../dungeon/cellar_mode.h"
#include "../dungeon/cellar_meta.h"  /* Phase 12.2 promoted */
#include "../../../engine/data/levelinfo_start_rooms.h"
#include "../world/render/sprite_render.h"  /* LINK_FACE_* enum */

/* Canonical UW Level 1 entrance spawn — matches the debug-entry
 * defaults in main.c (players[0].x = 120, players[0].y = 133). */
#define ROOMROM_WARP_UW_SPAWN_X  120
#define ROOMROM_WARP_UW_SPAWN_Y  133

/* Link's foot-tile is 8 px below the registered Y position. The
 * playfield top is ROOMROM_HUD_ROWS * 8 = 56. The (col, row) NES BG
 * tile coords used for warp-tile lookup mirror NES GetCollidableTileStill
 * sampling at (ObjX, ObjY) with playfield Y origin offset. */
#define ROOMROM_WARP_PLAYFIELD_TOP_PX  ((short)(ROOMROM_HUD_ROWS * 8u))

static rr_warp_state_t       s_state;
static rr_warp_save_state_t  s_save;
static unsigned char         s_unsupported_selector_count;

/* Task 5.6: latched flag set in IDLE on a UW-stair detect, consumed in
 * LOAD to bump the right counter. 0 = OW warp / UW-stair entry,
 * 1 = UW-stair cellar exit. */
static unsigned char         s_pending_cellar_exit;
static unsigned char         s_cellar_entry_count;
static unsigned char         s_cellar_exit_count;

/* Phase C (2026-05-24) — UET clear tracker. NES Z_07.asm:3200 clears
 * UndergroundExitType when Link finishes a full-tile step in OW (grid
 * offset rolls from non-zero to 0). Tracks last frame's grid offset
 * so we detect the rollover edge. Static lifetime: per-session. */
static signed char           s_prev_grid_offset;

/* Forward declaration — defined alongside init below; called from
 * tick IDLE state which is also below. */
static void roomrom_world_transition_ow_step_uet_clear(unsigned char scene);


static void clear_save_state(void)
{
    s_save.version = 1u;
    s_save.source_room_id = 0u;
    s_save.source_underground_entrance_tile = 0u;
    s_save.source_underground_entrance_tile_raw = 0u;
    s_save.source_link_x = 0;
    s_save.source_link_y = 0;
    s_save.source_link_face = 0u;
    s_save.dest_scene = ROOMROM_MAIN_SCENE_UW;
    s_save.dest_level = 0u;
    s_save.dest_quest = 0u;
    s_save.dest_room_id = 0u;
    s_save.dest_link_x = 0;
    s_save.dest_link_y = 0;
    s_save.dest_link_face = 0u;
}

void roomrom_world_transition_init(void)
{
    s_state = RR_WARP_IDLE;
    s_unsupported_selector_count = 0u;
    s_pending_cellar_exit = 0u;
    s_cellar_entry_count = 0u;
    s_cellar_exit_count = 0u;
    s_prev_grid_offset = 0;
    clear_save_state();
}

/* Phase C (2026-05-24) — UET-clear edge detector for OW.
 *
 * NES Z_07.asm:3170-3201 truncates ObjGridOffset to 0 when grid_offset &
 * #$07 == 0 (a full tile-step completed). Inside mode 5 (OW), the
 * truncation path ALSO clears UndergroundExitType so the next-tile warp
 * check is unblocked.
 *
 * Genesis grid_offset is signed; we look for a non-zero → zero rollover
 * each frame while in OW scene. On detected rollover, clear UET. UW/CAVE
 * scenes don't clear (NES holds UET non-zero while underground). */
static void roomrom_world_transition_ow_step_uet_clear(unsigned char scene)
{
    signed char cur = roomrom_main_current_link_grid_offset();
    if (scene == ROOMROM_MAIN_SCENE_OW) {
        if (s_prev_grid_offset != 0 && cur == 0) {
            if (roomrom_main_underground_exit_type() != 0u) {
                roomrom_main_set_underground_exit_type(0u);
            }
        }
    }
    s_prev_grid_offset = cur;
}

unsigned char roomrom_world_transition_cellar_entry_count(void)
{
    return s_cellar_entry_count;
}

unsigned char roomrom_world_transition_cellar_exit_count(void)
{
    return s_cellar_exit_count;
}

unsigned char roomrom_world_transition_is_active(void)
{
    return (unsigned char)(s_state != RR_WARP_IDLE);
}

const rr_warp_save_state_t *roomrom_world_transition_save_state(void)
{
    return &s_save;
}

/* Phase F (2026-05-25) — probe-only writers. See transition.h for the
 * rationale + contract. Production code MUST NOT call these. */

static unsigned char collapse_warp_tile(unsigned char raw);

rr_warp_save_state_t *roomrom_world_transition_save_state_mut(void)
{
    return &s_save;
}

void roomrom_world_transition_set_latched_source_for_probe(
    unsigned char source_room_id,
    short         source_link_x,
    short         source_link_y,
    unsigned char source_link_face,
    unsigned char source_underground_entrance_tile_raw)
{
    s_save.version = 1u;
    s_save.source_room_id = source_room_id;
    s_save.source_link_x = source_link_x;
    s_save.source_link_y = source_link_y;
    s_save.source_link_face = source_link_face;
    s_save.source_underground_entrance_tile_raw =
        source_underground_entrance_tile_raw;
    s_save.source_underground_entrance_tile =
        collapse_warp_tile(source_underground_entrance_tile_raw);
}

/* Forward declaration of the file-static detector. Defined later in
 * this TU; the forwarder below lets the probe call it without
 * exposing the symbol via the public header. */
static unsigned char detect_warp_uw_to_ow(unsigned char source_room_id,
                                          short link_x, short link_y,
                                          signed char grid_offset,
                                          unsigned char underground_exit_type,
                                          rr_warp_save_state_t *save_out,
                                          rr_warp_outcome_t   *outcome_out);

unsigned char roomrom_world_transition_check_uw_to_ow_for_probe(
    unsigned char source_room_id,
    short         link_x,
    short         link_y,
    signed char   grid_offset,
    unsigned char underground_exit_type,
    rr_warp_save_state_t *save_out,
    rr_warp_outcome_t    *outcome_out)
{
    return detect_warp_uw_to_ow(source_room_id, link_x, link_y,
                                grid_offset, underground_exit_type,
                                save_out, outcome_out);
}

unsigned char roomrom_world_transition_unsupported_selector_count(void)
{
    return s_unsupported_selector_count;
}

/* NES audio silence post-warp: clear Tune1Request ($0602) +
 * FluteTimer ($003C). audio_dispatch_tick consumes these cells so
 * writing 0 stops any in-progress death/secret tune at warp boundary
 * (matches NES Z_05.asm warp-fade behavior). */
void roomrom_audio_silence_for_warp(void)
{
    nes_ram[0x0602u] = 0u;  /* Tune1Request */
    nes_ram[0x003Cu] = 0u;  /* FluteTimer */
}

/* Rule 8: NES collapses warp-stair tiles ($70/$71/$72/$73) into a single
 * representative `$70` for `UndergroundEntranceTile` storage
 * ([Z_05.asm:7331-7332]). Tiles `$24` and `$88` pass through. */
static unsigned char collapse_warp_tile(unsigned char raw)
{
    if (raw >= 0x70u && raw <= 0x73u) {
        return 0x70u;
    }
    return raw;
}

/* Shared rule helpers (P1-1): used by both OW + UW detect_warp branches.
 * NES alignment + raw-tile sampling lives once. */
static unsigned char y_in_playfield(short link_y, short *y_out)
{
    short foot_y = (short)(link_y + 0x0B);
    if (foot_y < ROOMROM_WARP_PLAYFIELD_TOP_PX) return 0u;
    *y_out = (short)(foot_y - ROOMROM_WARP_PLAYFIELD_TOP_PX);
    return 1u;
}

/* OW detect_warp (rules 1-8 from Task 5.4 spec). Returns 1 + populates
 * save+outcome on hit. */
static unsigned char detect_warp_ow(unsigned char source_room_id,
                                    short link_x, short link_y,
                                    signed char grid_offset,
                                    unsigned char underground_exit_type,
                                    rr_warp_save_state_t *save_out,
                                    rr_warp_outcome_t   *outcome_out)
{
    unsigned char tile_col;
    unsigned char tile_row;
    unsigned char raw_tile;
    unsigned char attr_b;
    unsigned char selector;
    unsigned char level;
    unsigned char dest_room = 0u;
    short y_in_play;

    /* Rule 1: NES `LDA UndergroundExitType / ORA ObjGridOffset / BNE Exit`. */
    if (underground_exit_type != 0u) {
        return 0u;
    }

    /* Rule 2. */
    if (grid_offset != 0) {
        return 0u;
    }

    /* Rules 3-5: NES CheckWarps (Z_05.asm): ObjX a multiple of $10 (room
     * $22: of 8, the wide Level 6 entrance), ObjY & $0F = $0D, then
     * GetCollidableTileStill on PlayAreaTiles (T-128: Link now stops where
     * NES stops, on the $24 square below the entrance, so the old shifted
     * Y rule ($x5), the room $37 X exception and the Genesis tile cache no
     * longer matched). */
    if (source_room_id == 0x22u) {
        if (((unsigned)link_x & 0x07u) != 0u) return 0u;
    } else if (((unsigned)link_x & 0x0Fu) != 0u) {
        return 0u;
    }
    if (((unsigned)link_y & 0x0Fu) != 0x0Du) {
        return 0u;
    }
    if (!roomrom_ow_room_render_is_stable()) {
        return 0u;
    }
    (void)tile_col; (void)tile_row; (void)y_in_play;
    nes_ram[0x0070u] = (unsigned char)link_x;
    nes_ram[0x0084u] = (unsigned char)link_y;
    /* NES Z_07.asm @CheckWarps saves ObjCollidedTile around CheckWarps.
     * Sampling the standing tile may update that scratch cell, but the
     * dungeon selector must leave the preceding collision result intact. */
    {
        unsigned char collided_before = nes_ram[0x049Eu];
        raw_tile = collision_get_collidable_tile_still(0u);
        nes_ram[0x049Eu] = collided_before;
    }
    if (raw_tile != 0x24u && raw_tile != 0x88u &&
        !(raw_tile >= 0x70u && raw_tile <= 0x73u)) {
        return 0u;
    }

    /* Rule 6: selector = attr_b & 0xFC, then < 0x40 for level dispatch. */
    attr_b = roomrom_ow_meta_attr_b(source_room_id);
    selector = (unsigned char)(attr_b & 0xFCu);
    if (!roomrom_ow_meta_is_level_selector(selector)) {
        return 0u;
    }
    level = roomrom_ow_meta_level_from_selector(selector);

    /* Rule 7: manifest gate. Phase B (2026-05-24) reads quest from
     * roomrom_main_current_quest() — manifest now covers all 18
     * (level, quest) tuples. Manifest miss = silent rejection plus
     * unsupported-selector counter bump for probes. */
    {
        unsigned char quest = roomrom_main_current_quest();
        if (!levelinfo_start_room_for(level, quest, &dest_room)) {
            if (s_unsupported_selector_count < 0xFFu) {
                s_unsupported_selector_count++;
            }
            return 0u;
        }

        /* Rule 8: tile collapse for storage. */
        save_out->version = 1u;
        save_out->source_room_id = source_room_id;
        save_out->source_underground_entrance_tile_raw = raw_tile;
        save_out->source_underground_entrance_tile = collapse_warp_tile(raw_tile);
        save_out->source_link_x = link_x;
        save_out->source_link_y = link_y;
        save_out->source_link_face = roomrom_main_current_link_face();
        save_out->dest_scene = ROOMROM_MAIN_SCENE_UW;
        save_out->dest_level = level;
        save_out->dest_quest = quest;
        save_out->dest_room_id = dest_room;
        save_out->dest_link_x = ROOMROM_WARP_UW_SPAWN_X;
        save_out->dest_link_y = ROOMROM_WARP_UW_SPAWN_Y;
        save_out->dest_link_face = ROOMROM_MAIN_LINK_FACE_DOWN;

        outcome_out->dest_scene = ROOMROM_MAIN_SCENE_UW;
        outcome_out->dest_level = level;
        outcome_out->dest_quest = quest;
        outcome_out->dest_room_id = dest_room;
    }
    outcome_out->dest_link_x = ROOMROM_WARP_UW_SPAWN_X;
    outcome_out->dest_link_y = ROOMROM_WARP_UW_SPAWN_Y;
    outcome_out->dest_link_face = ROOMROM_MAIN_LINK_FACE_DOWN;
    outcome_out->dest_redux_flag = roomrom_main_current_redux_flag();
    return 1u;
}

/* Phase C (2026-05-24) — UW→OW dungeon exit arm.
 *
 * Fires when Link is inside a dungeon (level 1..9), stepping on a stair
 * tile ($70..$73) in the dungeon's entrance room (start_room_id per
 * levelinfo_start_rooms manifest), AND the save state has a valid
 * latched source (set by detect_warp_ow on the original OW→UW entry).
 *
 * NES authority: Z_05.asm:7493 EndGameMode12 path (`STA UndergroundExitType`
 * with #$02), Z_01.asm:2990 (cave exit pattern). Genesis collapses the
 * stair-down + restore-OW chain into a single coordinator outcome
 * because we don't model NES game-mode dispatch separately.
 *
 * Returns 1 + fills save_out + outcome_out for a UW→OW dungeon exit. */
static unsigned char detect_warp_uw_to_ow(unsigned char source_room_id,
                                          short link_x, short link_y,
                                          signed char grid_offset,
                                          unsigned char underground_exit_type,
                                          rr_warp_save_state_t *save_out,
                                          rr_warp_outcome_t   *outcome_out)
{
    unsigned char tile_col;
    unsigned char tile_row;
    unsigned char raw_tile;
    unsigned char level;
    unsigned char quest;
    unsigned char start_room = 0u;
    short y_in_play;

    /* Rule 1: UET blocks re-trigger. */
    if (underground_exit_type != 0u) return 0u;
    /* Rule 2: grid alignment (X). */
    if (grid_offset != 0) return 0u;
    /* Rule 3: X alignment. UW is not the OW $22 special-case. */
    if (((unsigned)link_x & 0x0Fu) != 0u) return 0u;
    /* Rule 4 dropped for UW→OW: NES Z1 dungeon exit is mode-based
     * (EndGameMode12 fires on south-scroll boundary), not Y-aligned
     * like cellar stairs. The $7D entrance doorway tile sits at
     * row 20 across all 18 start_rooms, which low-nibble-$05
     * alignment cannot reach. Phase F probe surfaced this. */
    /* Rule 5: must be inside a dungeon. OW (level 0) does not exit. */
    level = roomrom_uw_room_render_get_level();
    if (level == 0u) return 0u;
    quest = roomrom_uw_room_render_get_quest();
    /* Rule 6: source room must be the dungeon's entrance room. */
    if (!levelinfo_start_room_for(level, quest, &start_room)) return 0u;
    if (source_room_id != start_room) return 0u;
    /* Rule 7: latched source must exist (a real OW→UW entry happened). */
    if (save_out->source_room_id == 0u) return 0u;
    /* Rule 8: raw tile must be either a stair ($70..$73) OR the
     * dungeon entrance doorway tile ($7D) — Phase F probe found that
     * all 18 start_rooms have $7D at the south-center exit doorway
     * (col 14, row 20), not the cellar-stair $70..$73 range. NES Z1
     * dungeon exits via mode change on south-scroll-edge from
     * start_room; we collapse that to a tile-id check on the doorway
     * pattern. */
    if (!y_in_playfield(link_y, &y_in_play)) return 0u;
    tile_col = (unsigned char)((link_x >> 3) & 0x1Fu);
    tile_row = (unsigned char)((y_in_play >> 3) & 0x1Fu);
    raw_tile = roomrom_uw_room_render_raw_tile_at_room(level, quest,
                                                       source_room_id,
                                                       tile_col, tile_row);
    if (!((raw_tile >= 0x70u && raw_tile <= 0x73u) ||
          raw_tile == 0x7Du)) {
        return 0u;
    }

    /* Hit: replay latched source into save's dest fields + route to OW. */
    save_out->dest_scene = ROOMROM_MAIN_SCENE_OW;
    save_out->dest_level = 0u;
    save_out->dest_quest = roomrom_main_current_quest();
    save_out->dest_room_id = save_out->source_room_id;
    save_out->dest_link_x = save_out->source_link_x;
    save_out->dest_link_y = save_out->source_link_y;
    save_out->dest_link_face = save_out->source_link_face;

    outcome_out->dest_scene = ROOMROM_MAIN_SCENE_OW;
    outcome_out->dest_level = 0u;
    outcome_out->dest_quest = roomrom_main_current_quest();
    outcome_out->dest_room_id = save_out->source_room_id;
    outcome_out->dest_link_x = save_out->source_link_x;
    outcome_out->dest_link_y = save_out->source_link_y;
    outcome_out->dest_link_face = save_out->source_link_face;
    outcome_out->dest_redux_flag = roomrom_main_current_redux_flag();
    return 1u;
}

/* T-132: NES EndGameMode12 (Z_05.asm CalculateNextRoomForDoor: NextRoomId
 * >= $80 leaves the level). Outcome = the latched OW entrance. Returns 0
 * when no OW entry was latched. */
unsigned char roomrom_world_transition_level_exit(rr_warp_outcome_t *out)
{
    /* NES: walking out of the start room (NextRoomId >= $80) always leaves
     * the level; InitMode3_Sub1 then starts the OW at CaveSourceRoomId
     * ($526, set by LoadLevel), or at the OW StartRoomId when it is $FF.
     * The latched entrance only knew the walked-in route (T-171: a level
     * reached any other way scrolled to the room below instead). */
    {
        const unsigned char src = nes_ram[0x0526u];
        s_save.source_room_id = (src != 0xFFu) ? src : level_info_ow_start_room();
    }
    s_save.dest_scene = ROOMROM_MAIN_SCENE_OW;
    s_save.dest_level = 0u;
    s_save.dest_quest = roomrom_main_current_quest();
    s_save.dest_room_id = s_save.source_room_id;
    s_save.dest_link_x = s_save.source_link_x;
    s_save.dest_link_y = s_save.source_link_y;
    s_save.dest_link_face = s_save.source_link_face;
    out->dest_scene = ROOMROM_MAIN_SCENE_OW;
    out->dest_level = 0u;
    out->dest_quest = s_save.dest_quest;
    out->dest_room_id = s_save.source_room_id;
    out->dest_link_x = s_save.source_link_x;
    out->dest_link_y = s_save.source_link_y;
    out->dest_link_face = s_save.source_link_face;
    out->dest_redux_flag = roomrom_main_current_redux_flag();
    return 1u;
}

void roomrom_world_transition_tick(void)
{
    rr_warp_outcome_t outcome;

    switch (s_state) {

    case RR_WARP_IDLE: {
        /* Rule 0: scene dispatch. OW → detect_warp_ow; UW → detect_warp_uw. */
        unsigned char scene = roomrom_main_current_scene();
        unsigned char hit = 0u;
        if (scene == ROOMROM_MAIN_SCENE_OW) {
            hit = detect_warp_ow(roomrom_main_current_room_id(),
                                 roomrom_main_current_link_x(),
                                 roomrom_main_current_link_y(),
                                 roomrom_main_current_link_grid_offset(),
                                 roomrom_main_underground_exit_type(),
                                 &s_save,
                                 &outcome);
        } else if (scene == ROOMROM_MAIN_SCENE_UW) {
            unsigned char rid_before = roomrom_main_current_room_id();
            /* T-187: mode9 owns cellar entry/exit; preserve the OW latch. */
            if (cellar_try_enter()) {
                if (s_cellar_entry_count != 0xFFu) ++s_cellar_entry_count;
                return;
            }
            if (nes_ram[0x0012u] != 5u) return;
            /* Phase C: if no cellar hit, try dungeon-exit (UW→OW) arm. */
            if (!hit) {
                hit = detect_warp_uw_to_ow(rid_before,
                                           roomrom_main_current_link_x(),
                                           roomrom_main_current_link_y(),
                                           roomrom_main_current_link_grid_offset(),
                                           roomrom_main_underground_exit_type(),
                                           &s_save,
                                           &outcome);
            }
        }
        if (hit && scene == ROOMROM_MAIN_SCENE_OW &&
            s_save.dest_scene == ROOMROM_MAIN_SCENE_UW) {
            /* T-132: NES CheckWarps enters mode $10 on this frame; the
             * stairs / load / curtain / walk-in sequence is main.c's. */
            /* NES HandleWarpOW stores the raw standing tile before
             * @LoadLevel changes CurLevel. The level-entry coordinator
             * stores its own copy but must keep this NES RAM cell too:
             * t111 L5 entry was left with the previous $74 instead of
             * the actual $24 entrance tile. */
            nes_ram[0x0065u] = s_save.source_underground_entrance_tile_raw;
            outcome.dest_scene    = s_save.dest_scene;
            outcome.dest_level    = s_save.dest_level;
            outcome.dest_quest    = s_save.dest_quest;
            outcome.dest_room_id  = s_save.dest_room_id;
            outcome.dest_link_x   = s_save.dest_link_x;
            outcome.dest_link_y   = s_save.dest_link_y;
            outcome.dest_link_face = s_save.dest_link_face;
            outcome.dest_redux_flag = roomrom_main_current_redux_flag();
            roomrom_audio_silence_for_warp();
            roomrom_main_begin_level_entry(&outcome);
            s_state = RR_WARP_IDLE;
        } else if (hit) {
            s_state = RR_WARP_PREPARE;
        }
        /* Phase C: UET clear on OW grid-aligned step (NES Z_07.asm:3200). */
        roomrom_world_transition_ow_step_uet_clear(scene);
        return;
    }

    case RR_WARP_PREPARE:
        /* Slice-1 PREPARE is a 0-frame placeholder: the save state was
         * latched in IDLE on the warp hit. ANIM is also 0-frame in
         * slice 1, so we fall straight into LOAD here. Future slices
         * (mode-$10) will hold ANIM for stairs/fade frames. */
        s_state = RR_WARP_ANIM;
        /* fallthrough into ANIM logic in same tick */
        /* fallthrough */

    case RR_WARP_ANIM:
        s_state = RR_WARP_LOAD;
        /* fallthrough into LOAD logic in same tick */
        /* fallthrough */

    case RR_WARP_LOAD:
        /* Phase C: dest_scene + dest_link_x/y come from save state; LOAD
         * is scene-agnostic. UW entries pick UW_SPAWN_X/Y via
         * detect_warp_ow's save population; OW exits pick latched
         * source_link_x/y via detect_warp_uw_to_ow's save population. */
        outcome.dest_scene    = s_save.dest_scene;
        outcome.dest_level    = s_save.dest_level;
        outcome.dest_quest    = s_save.dest_quest;
        outcome.dest_room_id  = s_save.dest_room_id;
        outcome.dest_link_x   = s_save.dest_link_x;
        outcome.dest_link_y   = s_save.dest_link_y;
        outcome.dest_link_face = s_save.dest_link_face;
        outcome.dest_redux_flag = roomrom_main_current_redux_flag();

        roomrom_audio_silence_for_warp();
        roomrom_main_apply_warp_outcome(&outcome);
        if (s_pending_cellar_exit) {
            if (s_cellar_exit_count < 0xFFu) s_cellar_exit_count++;
        } else if (roomrom_main_current_scene() == ROOMROM_MAIN_SCENE_UW &&
                   roomrom_uw_room_is_cellar(s_save.dest_level,
                                             s_save.dest_quest,
                                             s_save.dest_room_id)) {
            if (s_cellar_entry_count < 0xFFu) s_cellar_entry_count++;
        }
        s_pending_cellar_exit = 0u;
        s_state = RR_WARP_RESUME;
        return;

    case RR_WARP_RESUME:
        /* Slice-1 RESUME is a single-frame guard. main.c sees
         * is_active() == 1 for the same frame LOAD ran in (because of
         * the same-frame fall-through above) and again on this
         * follow-up tick; movement is suppressed for one extra frame so
         * input held during warp doesn't leak into the new room's first
         * tick. */
        s_state = RR_WARP_IDLE;
        return;

    case RR_WARP_ABORT:
    default:
        s_state = RR_WARP_IDLE;
        clear_save_state();
        return;
    }
}
