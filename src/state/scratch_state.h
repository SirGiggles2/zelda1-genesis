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
#define ZP_TMP0    F_ram_0000_R3
#define ZP_TMP1    F_ram_0001_R2
#define ZP_TMP2    F_ram_0002_R2
#define ZP_TMP3    F_ram_0003_R3
#define ZP_TMP4    F_ram_0004_R2
#define ZP_TMP5    F_ram_0005_R2
#define ZP_TMP6    F_ram_0006_R2
#define ZP_TMP7    F_ram_0007_R2
#define ZP_TMP8    F_ZP_TMP8_R
#define ZP_TMP9    F_ZP_TMP9_R2
#define ZP_TMPA    F_ZP_TMPA_R2
#define ZP_TMPB    F_ZP_TMPB_R2
#define ZP_TMPC    F_ram_000C_R2
#define ZP_TMPD    F_ram_000D_R2
#define ZP_TMPE    F_ram_000E_R3
#define ZP_TMPF    F_ram_000F_R4

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
#define ZP_RNG_BASE  F_ram_0018_R3   /* NES Random+0 */
#define ZP_RNG_A     F_ram_0019_R2   /* NES Random+1 */
#define ZP_RNG_B     F_ZP_RNG_B_R2   /* NES Random+2 */

#endif /* SCRATCH_STATE_H */
