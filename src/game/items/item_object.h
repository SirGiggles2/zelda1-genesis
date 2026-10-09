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

#ifdef __cplusplus
}
#endif

#endif /* ITEM_OBJECT_H */
