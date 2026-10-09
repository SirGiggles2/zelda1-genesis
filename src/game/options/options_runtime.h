#ifndef SRC_GAME_OPTIONS_OPTIONS_RUNTIME_H
#define SRC_GAME_OPTIONS_OPTIONS_RUNTIME_H

/* Phase 9 Task 9.1 — Redux Options Runtime API.
 *
 * Drain Rule D1 stance: GREENFIELD. NES Z1 has no options menu; this
 * subsystem is sanctioned new-build per master plan §Phase 9 (debate
 * 004). State serialization layout is in options_state.h.
 *
 * Lifecycle:
 *   1. options_runtime_init() at boot — loads defaults if the SRAM
 *      mirror is uninitialized (version == OPTIONS_VERSION_NONE) or
 *      fails magic / checksum / range validation.
 *   2. options_runtime_apply(buf, size) consumes a serialized image
 *      from SRAM (Task 9.2 path) and migrates from any older version.
 *   3. options_get_*() / options_set_*() are the gameplay-side
 *      consumers (Task 9.4 wiring).
 *   4. options_runtime_serialize(buf, size) emits the wire image for
 *      the SRAM commit path (Task 9.2).
 *
 * Validation rules:
 *   - bool ids: clamp value to {0, 1}.
 *   - enum ids: reject when >= COUNT for that enum (no write).
 *   - numeric ids: clamp to [MIN, MAX].
 *
 * Migration: v0 (== uninitialized) -> v1 = "load defaults". v1 ->
 * vN+ defined when introduced.
 */

#include "options_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Reset to defaults. Called from boot OR when validation fails. */
void options_runtime_init(void);

/* Apply a serialized image to the live state. Returns 1 if accepted,
 * 0 if rejected (magic / checksum / version unknown -> caller falls
 * back to defaults). Also runs migration v_old -> CURRENT. */
unsigned char options_runtime_apply(const unsigned char *buf,
                                    unsigned int size);

/* Emit live state into `buf` (must be at least OPTIONS_STATE_SIZE).
 * Recomputes checksum + magic + version. Returns bytes written. */
unsigned int options_runtime_serialize(unsigned char *buf,
                                       unsigned int size);

/* Generic getter — returns 0 for unknown id. */
unsigned char options_get(unsigned int id);

/* Generic setter — runs validation, no-op on failure. */
void options_set(unsigned int id, unsigned char value);

/* Returns the live OPTIONS_VERSION_* value. */
unsigned char options_get_version(void);

/* Validation entry point — recomputes magic + checksum + ranges
 * over the live state. Returns 1 = ok, 0 = invalid. */
unsigned char options_runtime_validate(void);

/* Test hook — read raw byte at struct offset. Used by the options
 * probe (Task 9.1 verification) without exposing the struct. */
unsigned char options_runtime_peek(unsigned int offset);

#ifdef __cplusplus
}
#endif

#endif /* SRC_GAME_OPTIONS_OPTIONS_RUNTIME_H */
