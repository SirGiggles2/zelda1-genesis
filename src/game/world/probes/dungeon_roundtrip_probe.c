/* Phase F dungeon round-trip probe — see dungeon_roundtrip_probe.h.
 *
 * Verifies Phase B (manifest hit) + Phase C (UW→OW dispatch + source
 * replay) end-to-end for all 18 (level, quest) tuples without joypad
 * scripting or savestates.
 */

#include "dungeon_roundtrip_probe.h"
#include "../transition.h"
#include "../level_info_install.h"
#include "../../dungeon/uw_render.h"
#include "../../../../RoomRom/data/levelinfo_start_rooms.h"
#include "../../../../RoomRom/src/roomrom_main_state.h"
#include "platform_abi.h"

/* Canonical OW source latch — Phase F harness routes every dungeon
 * exit back to a single fixed OW position so we can byte-compare
 * outcome.dest_link_x/y against the latched source_link_x/y. */
#define PROBE_SRC_ROOM 0x77u
#define PROBE_SRC_X    120
#define PROBE_SRC_Y    0x8Du
#define PROBE_SRC_FACE 0u            /* ROOMROM_MAIN_LINK_FACE_DOWN */
#define PROBE_SRC_TILE 0x70u         /* collapsed stair representative */

/* Genesis playfield top in pixels (HUD rows = 7, 8 px tall). Must
 * match ROOMROM_WARP_PLAYFIELD_TOP_PX in transition.c. */
#define PROBE_PLAYFIELD_TOP_PX 56

/* Pass-flag bits. */
#define PROBE_F_MANIFEST_HIT 0x01u
#define PROBE_F_STAIR_FOUND  0x02u
#define PROBE_F_DETECT_FIRED 0x04u
#define PROBE_F_DEST_MATCH   0x08u
#define PROBE_F_FULL_PASS    (PROBE_F_MANIFEST_HIT | PROBE_F_STAIR_FOUND | \
                              PROBE_F_DETECT_FIRED | PROBE_F_DEST_MATCH)

/* Returns 1 if (col, row) satisfies the alignment gates in
 * detect_warp_uw_to_ow (rules 3 + 4): link_x even-multiple-of-16
 * and link_y & 0x0F == 0x05 (only odd tile_rows reach this).
 *
 * Per the inverse of transition.c rule 5:
 *   link_y = row*8 + (PLAYFIELD_TOP_PX - 0x0B + 0x10) for even row
 *          = row*8 + (PLAYFIELD_TOP_PX - 0x0B + 0x08) for odd row
 * The closed-form below assumes the canonical "step into stair" pose:
 * link_y = row*8 + 45  for odd rows (gives low nibble 0x05). */
static unsigned char probe_align_ok(unsigned char col, unsigned char row)
{
    if ((col & 0x01u) != 0u) {
        /* link_x = col*8 must be multiple of 16 → col must be even. */
        return 0u;
    }
    if ((row & 0x01u) == 0u) {
        /* Even rows cannot satisfy link_y & 0x0F == 0x05. */
        return 0u;
    }
    return 1u;
}

void dungeon_roundtrip_probe_run(void)
{
    volatile unsigned char *block =
        (volatile unsigned char *)DUNGEON_ROUNDTRIP_PROBE_BASE;
    unsigned char level;
    unsigned char quest;
    unsigned char rows_ok = 0u;
    unsigned char saved_quest = roomrom_main_current_quest();
    unsigned char saved_level = roomrom_uw_room_render_get_level();
    unsigned char saved_render_quest = roomrom_uw_room_render_get_quest();
    unsigned char saved_cur_level = nes_ram[0x0010u];

    /* Clear magic so Lua sees "not yet published". */
    block[0] = 0u;
    block[1] = 0u;
    block[2] = 0u;
    block[3] = 0u;
    block[4] = 0u;
    block[5] = 0u;
    block[6] = 0u;
    block[7] = 0u;

    for (level = 1u; level <= 9u; ++level) {
        for (quest = 1u; quest <= 2u; ++quest) {
            unsigned char row_idx =
                (unsigned char)((level - 1u) * 2u + (quest - 1u));
            unsigned char row_off =
                (unsigned char)(DUNGEON_ROUNDTRIP_PROBE_HEADER +
                                row_idx * DUNGEON_ROUNDTRIP_PROBE_ROW_BYTES);
            unsigned char pass_flags = 0u;
            unsigned char start_room = 0u;
            unsigned char stair_col = 0xFFu;
            unsigned char stair_row = 0xFFu;
            unsigned char col;
            unsigned char row;
            short link_x;
            short link_y;
            rr_warp_save_state_t *save_mut;
            rr_warp_outcome_t outcome;
            unsigned char hit;

            /* Wipe row slot up-front so previous loop values can't bleed
             * if we 'continue' early. */
            block[row_off + 0] = 0u;
            block[row_off + 1] = 0u;
            block[row_off + 2] = 0u;
            block[row_off + 3] = 0u;

            /* F1: manifest lookup (Phase B). */
            if (!levelinfo_start_room_for(level, quest, &start_room)) {
                block[row_off + 3] = pass_flags;
                continue;
            }
            pass_flags |= PROBE_F_MANIFEST_HIT;

            /* F2: install this (level, quest)'s UW LBA + LevelInfo +
             * tell the renderer/coordinator which level+quest is live.
             * roomrom_main_set_quest also writes the master quest cell
             * detect_warp_uw_to_ow reads via roomrom_main_current_quest(). */
            level_info_install_uw(level, quest);
            roomrom_uw_room_render_set_level(level);
            roomrom_uw_room_render_set_quest(quest);
            roomrom_main_set_quest(quest);

            /* F3: scan start_room's BG blob for a UW→OW exit tile.
             * Accepts BOTH $70..$73 (cellar-style stair) and $7D
             * (dungeon-entrance doorway). The Phase C dispatch was
             * extended to take $7D too — see transition.c:detect_warp_uw_to_ow.
             * Prefer (col even, row odd) so the alignment gate passes. */
            for (row = 0u; row < 22u && stair_col == 0xFFu; ++row) {
                for (col = 0u; col < 32u; ++col) {
                    unsigned char raw =
                        roomrom_uw_room_render_raw_tile_at_room(
                            level, quest, start_room, col, row);
                    unsigned char is_exit_tile =
                        (unsigned char)((raw >= 0x70u && raw <= 0x73u) ||
                                        raw == 0x7Du);
                    if (is_exit_tile && probe_align_ok(col, row)) {
                        stair_col = col;
                        stair_row = row;
                        break;
                    }
                }
            }
            if (stair_col == 0xFFu) {
                /* Fall back to first exit-tile at ANY (col, row) so
                 * bit 1 reports correctly even when alignment fails. */
                for (row = 0u; row < 22u && stair_col == 0xFFu; ++row) {
                    for (col = 0u; col < 32u; ++col) {
                        unsigned char raw =
                            roomrom_uw_room_render_raw_tile_at_room(
                                level, quest, start_room, col, row);
                        if ((raw >= 0x70u && raw <= 0x73u) ||
                            raw == 0x7Du) {
                            stair_col = col;
                            stair_row = row;
                            break;
                        }
                    }
                }
            }
            if (stair_col == 0xFFu) {
                /* No exit tile found at all. Sample (14, 20) for
                 * diagnostic display. */
                unsigned char sample =
                    roomrom_uw_room_render_raw_tile_at_room(
                        level, quest, start_room, 14u, 20u);
                block[row_off + 0] = sample;
                block[row_off + 1] = 0xFFu;
                block[row_off + 2] = start_room;
                block[row_off + 3] = pass_flags;
                continue;
            }
            /* If the stair found is at an even row (alignment fails),
             * try odd rows 19, 21, 17 at the same column to find an
             * alignment-compatible tile. NES UW rooms are 22 tiles
             * tall (rows 0..21); entrance doorway tends to cluster
             * near the south boundary. */
            if ((stair_row & 0x01u) == 0u) {
                static const unsigned char k_odd_rows[] = {21u, 19u, 17u, 15u, 13u, 11u, 9u, 7u, 5u, 3u, 1u};
                unsigned char i;
                for (i = 0u; i < sizeof(k_odd_rows); ++i) {
                    unsigned char r = k_odd_rows[i];
                    /* Look at same col first, then sweep cols. */
                    unsigned char c;
                    for (c = 0u; c < 32u; c += 2u) {
                        unsigned char raw =
                            roomrom_uw_room_render_raw_tile_at_room(
                                level, quest, start_room, c, r);
                        if ((raw >= 0x70u && raw <= 0x73u) ||
                            raw == 0x7Du) {
                            stair_col = c;
                            stair_row = r;
                            goto found_aligned;
                        }
                    }
                }
                found_aligned: ;
            }
            pass_flags |= PROBE_F_STAIR_FOUND;
            /* Stash stair (col, row) in row[0..1] BEFORE detect runs.
             * Final dest_scene + dest_level will overwrite these if
             * detect fires; the slots only show (col, row) on
             * stair-only / detect-miss rows. */
            block[row_off + 0] = stair_col;
            block[row_off + 1] = stair_row;

            /* F4: pre-latch source_* with canonical OW entry. */
            roomrom_world_transition_set_latched_source_for_probe(
                PROBE_SRC_ROOM,
                (short)PROBE_SRC_X,
                (short)PROBE_SRC_Y,
                PROBE_SRC_FACE,
                PROBE_SRC_TILE);

            /* F5: compute synthetic link_x/y per the inverse of rules
             * 3..5 in transition.c. col is even → link_x = col*8 is
             * 16-aligned. row is odd → link_y = row*8 + 45 has low
             * nibble 0x05. foot_y = link_y + 0x0B lands at row*8 + 56
             * = PLAYFIELD_TOP_PX + row*8, so y_in_play = row*8 and
             * tile_row = row. */
            link_x = (short)((unsigned int)stair_col << 3);
            link_y = (short)(((unsigned int)stair_row << 3) + 45u);

            /* Clear UET so rule 1 passes. apply_warp_outcome of an
             * earlier loop iteration may have left UET != 0; explicit
             * reset is safe + idempotent. */
            roomrom_main_set_underground_exit_type(0u);

            /* F6: drive detect_warp_uw_to_ow via the Phase F forwarder. */
            save_mut = roomrom_world_transition_save_state_mut();
            hit = roomrom_world_transition_check_uw_to_ow_for_probe(
                start_room, link_x, link_y,
                0,    /* grid_offset = 0 (rule 2 pass) */
                0u,   /* UET = 0 (rule 1 pass) */
                save_mut,
                &outcome);
            if (!hit) {
                block[row_off + 3] = pass_flags;
                continue;
            }
            pass_flags |= PROBE_F_DETECT_FIRED;

            /* F7: verify outcome.dest_link_x/y match the source latch
             * (Phase C must replay source coords pixel-exact). */
            if (outcome.dest_link_x == (short)PROBE_SRC_X &&
                outcome.dest_link_y == (short)PROBE_SRC_Y) {
                pass_flags |= PROBE_F_DEST_MATCH;
            }

            block[row_off + 0] = outcome.dest_scene;
            block[row_off + 1] = outcome.dest_level;
            block[row_off + 2] = outcome.dest_room_id;
            block[row_off + 3] = pass_flags;
            (void)stair_col; (void)stair_row;
            if (pass_flags == PROBE_F_FULL_PASS) {
                rows_ok = (unsigned char)(rows_ok + 1u);
            }
        }
    }

    /* NES source: Z_06.asm:LoadLevelInfo; active tables are scene state.
     * Drained C: level_info_install_ow/uw. Coverage: probe isolation.
     * Stance: EXTEND. The sweep ends at L9Q2, so restore every owner it
     * mutates before gameplay reads room attributes or enemy lists. */
    if (saved_cur_level == 0u) {
        level_info_install_ow();
    } else {
        level_info_install_uw(saved_level, saved_render_quest);
    }
    nes_ram[0x0010u] = saved_cur_level;
    roomrom_uw_room_render_set_level(saved_level);
    roomrom_uw_room_render_set_quest(saved_render_quest);
    roomrom_main_set_quest(saved_quest);

    /* Publish header LAST. Lua poll loop waits for magic before
     * snapshotting the rest of the block — write magic last so we
     * never expose a half-populated block. */
    block[3] = rows_ok;
    block[2] = DUNGEON_ROUNDTRIP_PROBE_VERSION;
    block[1] = 0x46u;  /* 'F' */
    block[0] = 0x57u;  /* 'W' */
}
