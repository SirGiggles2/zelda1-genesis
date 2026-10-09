/* targeting_dispatch.h — native targeting subsystem dispatch (Phase 4).
 *
 * Native rewrite of src/oracle/combat/targeting_runtime.c.
 * Distance + direction calculations between two object slots. Both ROMs
 * link. Used by enemy AI per-monster updaters.
 */

#ifndef TARGETING_DISPATCH_H
#define TARGETING_DISPATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Compute axis distance + direction between two coords. Reads/writes
 * TARGET_H_DIR / TARGET_MIN_COORD / TARGET_MAX_COORD / TARGET_DIR_ACCUM.
 * Returns the unsigned distance. NES GetOneDirectionAndDistanceToTarget. */
unsigned char targeting_get_one_direction_and_distance_to_target(
    unsigned char target_coord, unsigned char origin_coord);

/* Compute H+V distance + direction from origin slot toward target slot.
 * Writes TARGET_H_DIR / TARGET_V_DIR / TARGET_H_DIST / TARGET_V_DIST.
 * NES GetDirectionsAndDistancesToTarget. */
void targeting_get_directions_and_distances_to_target(
    unsigned char target_slot, unsigned int origin_slot);

/* Refine an axis speed-table mid index into a diagonal speed index based
 * on H_DIST/V_DIST proportions. Returns the refined index.
 * NES CalcDiagonalSpeedIndex. */
unsigned int targeting_calc_diagonal_speed_index(unsigned int mid_speed_idx);

#ifdef __cplusplus
}
#endif

#endif /* TARGETING_DISPATCH_H */
