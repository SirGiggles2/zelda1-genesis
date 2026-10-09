/* scratch_state.h — canonical owner of NES zero-page scratch ($0000-$001F).
 *
 * Per docs/audit/state_contract.md: each NES RAM byte gets exactly one
 * canonical name. NES Z1 reuses zero-page across many subsystems via
 * context switching (cave/combat/enemy/world/targeting share $0000-$0009
 * etc.); without a canonical home, every subsystem header re-declares
 * the same byte under a different name and the alias-collision verifier
 * reports a wall of cross-subsystem aliases that are by-design NES
 * scratch sharing rather than bugs.
 *
 * Canonical names live here. Subsystem headers expose semantic aliases
 * via `#define CAVE_TMP0 ZP_TMP0` etc., NOT via raw `#define X RAM(0xN)`.
 * The alias chain preserves runtime semantics (same lvalue) while
 * letting the verifier (which scans for `#define X RAM(literal)`) see
 * only ONE definition per byte.
 *
 * Names cite NES Zelda 1 disassembly conventions where they exist
 * (CUR_LEVEL, FRAME_COUNTER, MODE_VALUE, SUBMODE_VALUE — these live in
 * progress_state.h, not here, since they are progress-owned not scratch).
 *
 * Range covered: $0000-$001F (NES zero-page scratch). $0010+ semantic
 * fields stay in their owning subsystem headers (progress_state.h owns
 * CUR_LEVEL/FRAME_COUNTER/MODE_VALUE/SUBMODE_VALUE; item_state.h owns
 * SAVE_SLOT_INDEX). Only purely-scratch bytes ($0000-$0009) are
 * canonicalized here.
 */

#ifndef SCRATCH_STATE_H
#define SCRATCH_STATE_H

#include "platform_abi.h"

/* Pure scratch slots — NES zero-page bytes used as transient state by
 * whichever subsystem currently holds the CPU. */
#define ZP_TMP0    RAM(0x0000)
#define ZP_TMP1    RAM(0x0001)
#define ZP_TMP2    RAM(0x0002)
#define ZP_TMP3    RAM(0x0003)
#define ZP_TMP4    RAM(0x0004)
#define ZP_TMP5    RAM(0x0005)
#define ZP_TMP6    RAM(0x0006)
#define ZP_TMP7    RAM(0x0007)
#define ZP_TMP8    RAM(0x0008)
#define ZP_TMP9    RAM(0x0009)
#define ZP_TMPA    RAM(0x000A)
#define ZP_TMPB    RAM(0x000B)
#define ZP_TMPC    RAM(0x000C)
#define ZP_TMPD    RAM(0x000D)
#define ZP_TMPE    RAM(0x000E)
#define ZP_TMPF    RAM(0x000F)

/* NES Z_Rand seed bytes — semantic but live in zero-page scratch.
 * Per reference/aldonunez/Variables.inc:8 — `Random := $18`. NES code
 * reads Random+0/+1/+2 for three decorrelated PRNG bytes.
 *
 * Subsystem aliases:
 *   CAVE_RANDOM_A → ZP_RNG_A ($0019 = Random+1)
 *   CAVE_RANDOM_B → ZP_RNG_B ($001A = Random+2)
 *   ENEMY_RNG_A   → ZP_RNG_BASE ($0018 = Random+0) — DIFFERENT from cave
 *   ENEMY_RNG_B   → ZP_RNG_A ($0019 = Random+1)
 *
 * Past audit confusion: scratch_state.h originally exposed only ZP_RNG_A
 * + ZP_RNG_B which named Random+1/+2. Drained code that needs Random+0
 * (NES base PRNG read) goes through ZP_RNG_BASE. Renaming for full
 * clarity (PLUS1/PLUS2) deferred to avoid breaking many consumers.
 */
#define ZP_RNG_BASE  RAM(0x0018)   /* NES Random+0 */
#define ZP_RNG_A     RAM(0x0019)   /* NES Random+1 */
#define ZP_RNG_B     RAM(0x001A)   /* NES Random+2 */

#endif /* SCRATCH_STATE_H */
