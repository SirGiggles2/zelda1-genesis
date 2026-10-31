#ifndef LINK_STATE_H
#define LINK_STATE_H

#include "platform_abi.h"

/* ---------------------------------------------------------------------------
 * Phase 6 Task 6.1 — typed LinkState.
 *
 * NES Z1 keeps Link in object slot 0. Per-object fields are arrays in WRAM;
 * the Link slice is the [0] entry in each. The shape below mirrors that
 * slice as a typed struct so Phase 6 systems consume `players[i].x` instead
 * of `RAM(NES_OBJ_X+i)`. PlayerState is a typedef alias used by Phase 13;
 * Phase 6 reads `players[0]` only.
 *
 * NES source authority: reference/aldonunez/Variables.inc + Z_07.asm.
 * Field naming follows the NES ObjXxx prefix so cross-references stay
 * obvious in drained C and parity oracle traces.
 *
 * Sizes are chosen to match the NES storage class:
 *   - 8-bit fields (uint8_t)  for byte-wide WRAM cells.
 *   - 16-bit fields (int16_t) for fractional-position accumulators that
 *     need a sign in Genesis-native code.
 *
 * The legacy RAM(...) shims at the bottom of this header are kept until
 * the last consumer has migrated, so no caller breaks during the
 * incremental promotion.
 *
 * NOTE: this header is included from contexts that pull in SGDK
 * `<types.h>` (engine main.c via `<genesis.h>`) AND from drained C
 * that does not. SGDK's types.h `#define`s `int16_t`/`uint8_t` to
 * its own short names if stdint hasn't been seen first, which then
 * makes a later `<stdint.h>` clash. We sidestep the whole tangle by
 * using plain C primitives — `unsigned char`/`signed char`/`signed
 * short` — which mean the same on the m68k-elf toolchain. Sizes are
 * the same as the original int16_t/uint8_t plan; the comment above
 * each field still names the NES width.
 * ------------------------------------------------------------------------ */

/* Facing / direction constants are intentionally NOT redefined here.
 * engine (`roomrom_sprites.h` + `engine/src/main.c`) ships its own
 * `link_face_t` and `link_dir_t` enums whose tokens (`LINK_FACE_DOWN`,
 * `LINK_DIR_NONE`, …) collide with any preprocessor `#define` of the
 * same name — the macro expansion turns the enum body into garbage at
 * preprocess time. Phase 6 code that wants the NES bitfield encoding of
 * `dir` uses raw literals (0x01 RIGHT, 0x02 LEFT, 0x04 DOWN, 0x08 UP)
 * or a future per-subsystem header. engine keeps its ordinal enum
 * until Phase 6 sweeps it. */

#include "player_records.h"

/* Legacy RAM shims — kept while consumers migrate. These will be deleted
 * in a follow-up commit once the last raw RAM(...) reference is gone. */
#define LINK_MOVING_DIR       F_ram_000F_R3
#define LINK_ROOM_SCRATCH     F_ram_0059_R2
#define LINK_HALT_FLAG        F_ram_066C_R4
#define MODE11_DEATH_TIMER    F_MODE11_DEATH_TIMER_R
#define DEATH_FRAME_COUNTER   F_ram_0602_R3

#endif /* LINK_STATE_H */
