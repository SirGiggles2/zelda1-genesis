/* UW cellar metadata for levels1..9 in both quests. ROM-backed queries
 * share level_info_install's records, including Q2 replacements.
 * NES source: Z_05 CheckWarps / CheckSubroom; Variables.inc cellar array. */

#ifndef ROOMROM_UW_CELLAR_META_H
#define ROOMROM_UW_CELLAR_META_H

/* Returns 1 + writes *out_cellar if `room_id` is a cellar source for
 * `(level, quest)` within its ten declared cellar IDs. This metadata
 * view is not CheckWarps: runtime entry may scan beyond the list (L3Q1). */
unsigned char roomrom_uw_cellar_for_source(unsigned char level,
                                           unsigned char quest,
                                           unsigned char room_id,
                                           unsigned char *out_cellar);

/* Diagnostic source A for a cellar. Real exits select source A or B using
 * Link X (cellar_check_exit); this helper does not choose an exit. */
unsigned char roomrom_uw_cellar_source_for_cellar(unsigned char level,
                                                  unsigned char quest,
                                                  unsigned char cellar_id,
                                                  unsigned char *out_source);

/* Returns 1 if room_id belongs to this level/quest's ten-entry cellar list. */
unsigned char roomrom_uw_room_is_cellar(unsigned char level,
                                        unsigned char quest,
                                        unsigned char room_id);

#endif /* ROOMROM_UW_CELLAR_META_H */
