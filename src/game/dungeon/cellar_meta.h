/* UW cellar pair lookup (RoomRom-side).
 *
 * Slice-1 (Task 5.6) verifies UW→cellar entry + cellar→source exit
 * via the warp coordinator. This module wraps the generated table
 * (RoomRom/data/uw_l1q1_cellar_pairs) with two lookup helpers used by
 * the coordinator's detect_warp_uw + by the in-ROM Gate D probe.
 *
 * NES source: reference/aldonunez/Z_05.asm:CheckWarps UW branch
 *             (line 7264-7282); LevelInfo_CellarRoomIdArray decode
 *             (Variables.inc:340).
 */

#ifndef ROOMROM_UW_CELLAR_META_H
#define ROOMROM_UW_CELLAR_META_H

/* Returns 1 + writes *out_cellar if `room_id` is a cellar source for
 * `(level, quest)`. Slice-1 manifest: L1Q1 only. */
unsigned char roomrom_uw_cellar_for_source(unsigned char level,
                                           unsigned char quest,
                                           unsigned char room_id,
                                           unsigned char *out_cellar);

/* Returns 1 + writes *out_source if `cellar_id` has an associated
 * source room in the slice-1 manifest. Used for sanity assertions
 * (Gate D probe), NOT for cellar-exit destination resolution —
 * exit uses save state's source_room_id (latched on entry per
 * NES CellarSourceRoomId convention). */
unsigned char roomrom_uw_cellar_source_for_cellar(unsigned char level,
                                                  unsigned char quest,
                                                  unsigned char cellar_id,
                                                  unsigned char *out_source);

/* Returns 1 if `room_id` is in the slice-1 manifest's cellar set
 * (used by coordinator to distinguish cellar-exit from cellar-entry
 * dispatch). */
unsigned char roomrom_uw_room_is_cellar(unsigned char level,
                                        unsigned char quest,
                                        unsigned char room_id);

#endif /* ROOMROM_UW_CELLAR_META_H */
