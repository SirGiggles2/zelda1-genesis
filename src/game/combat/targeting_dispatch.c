/* targeting_dispatch.c — native targeting subsystem dispatch (Phase 4).
 *
 * Drain MATCH (verified-by-use). Pure C; no oracle dependencies.
 * NES sources: GetOneDirectionAndDistanceToTarget,
 *              GetDirectionsAndDistancesToTarget,
 *              CalcDiagonalSpeedIndex.
 * Drain provenance: src/oracle/combat/targeting_runtime.c.
 */

#include "targeting_dispatch.h"
#include <stdint.h>
#include "platform_abi.h"
#include "targeting_state.h"   /* TARGET_DIR_ACCUM/MIN/MAX/H_DIST/V_DIST/H_DIR/V_DIR */
#include "world_state.h"       /* OBJ_X / OBJ_Y */

unsigned char targeting_get_one_direction_and_distance_to_target(
    unsigned char target_coord, unsigned char origin_coord)
{
    /* drain at targeting_runtime.c:3-19. */
    TARGET_MIN_COORD = target_coord;
    TARGET_MAX_COORD = origin_coord;
    if (origin_coord < target_coord) {
        TARGET_MAX_COORD = target_coord;
        TARGET_MIN_COORD = origin_coord;
        TARGET_H_DIR = (uint8_t)((unsigned char)TARGET_H_DIR >> 1);
    }
    {
        const unsigned char dist =
            (unsigned char)((unsigned char)TARGET_MAX_COORD -
                            (unsigned char)TARGET_MIN_COORD);
        if (dist < 9u) {
            TARGET_DIR_ACCUM = (uint8_t)((unsigned char)TARGET_DIR_ACCUM + 1u);
        }
        return dist;
    }
}

void targeting_get_directions_and_distances_to_target(
    unsigned char target_slot, unsigned int origin_slot)
{
    /* drain at targeting_runtime.c:21-30. */
    TARGET_H_DIR = 2u;
    TARGET_H_DIST = targeting_get_one_direction_and_distance_to_target(
        (unsigned char)OBJ_X(target_slot),
        (unsigned char)OBJ_X(origin_slot));
    TARGET_V_DIR = (uint8_t)TARGET_H_DIR;
    TARGET_H_DIR = 8u;
    TARGET_V_DIST = targeting_get_one_direction_and_distance_to_target(
        (unsigned char)OBJ_Y(target_slot),
        (unsigned char)OBJ_Y(origin_slot));
}

unsigned int targeting_calc_diagonal_speed_index(unsigned int mid_speed_idx)
{
    /* drain at targeting_runtime.c:32-67. */
    TARGET_DIR_ACCUM = (uint8_t)mid_speed_idx;
    TARGET_MIN_COORD = 0xFFu;
    {
        unsigned char h = (unsigned char)TARGET_H_DIST;
        unsigned char v = (unsigned char)TARGET_V_DIST;
        if (h < v) {
            TARGET_H_DIST = v;
            TARGET_V_DIST = h;
            TARGET_MIN_COORD = 1u;
            const unsigned char tmp = h;
            h = v;
            v = tmp;
        }
        if ((unsigned char)(h - v) < 8u) {
            return (unsigned int)(unsigned char)TARGET_DIR_ACCUM;
        }
    }
    for (;;) {
        const unsigned char idx =
            (unsigned char)((unsigned char)TARGET_DIR_ACCUM +
                            (unsigned char)TARGET_MIN_COORD);
        TARGET_DIR_ACCUM = idx;
        if (idx == 0u || idx == 8u) {
            break;
        }
        {
            const unsigned char new_diff =
                (unsigned char)((unsigned char)TARGET_H_DIST -
                                (unsigned char)TARGET_V_DIST);
            TARGET_H_DIST = new_diff;
            if (new_diff < (unsigned char)TARGET_V_DIST) {
                break;
            }
        }
    }
    return (unsigned int)(unsigned char)TARGET_DIR_ACCUM;
}
