/* item_dispatch.h — native item subsystem dispatch (Phase 4).
 *
 * Native rewrite of src/oracle/items/item_runtime.c. Both ROMs link.
 * Per debate 007 verdict: drop NES-emulation shims, native code uses
 * Genesis-native primitives + drain-prefix functions where safe.
 */

#ifndef ITEM_DISPATCH_H
#define ITEM_DISPATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Top-level item-pickup dispatcher. Sets sfx + freeze flag, then
 * dispatches by ItemIdToDescriptor item-class:
 *   class 0 — direct slot value or class0_complex (triforce/letter/clock)
 *   class 1 — incremental (+1, +5 rupees, hearts, key tune)
 *   class 2 — graded (rings, swords) — also triggers palette patch
 *
 * NES TakeItem (item_runtime.c:110-137). */
void item_take_item(unsigned char item_id);

#ifdef __cplusplus
}
#endif

#endif /* ITEM_DISPATCH_H */
