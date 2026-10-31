/* item_object.h — public dispatch hook for dropped-item ($60) slot tick.
 *
 * NES UpdateItem port. Registered at enemy_update_fns[0x60].
 */

#ifndef ITEM_OBJECT_H
#define ITEM_OBJECT_H

#ifdef __cplusplus
extern "C" {
#endif

void item_object_update(unsigned int slot);
/* Read-only handoff from the retained extra arrow/rod context bank.
 * Main owns that temporary producer until its native migration resumes. */
unsigned char roomrom_item_extra_arrow_pose(unsigned char player,
                                            unsigned char *x, unsigned char *y);
#ifdef ZELDA_DROP_TEST
void item_object_spawn_test_drops(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ITEM_OBJECT_H */
