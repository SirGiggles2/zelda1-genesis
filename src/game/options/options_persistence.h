#ifndef SRC_GAME_OPTIONS_OPTIONS_PERSISTENCE_H
#define SRC_GAME_OPTIONS_OPTIONS_PERSISTENCE_H

/* Phase 9 Task 9.2 — Options SRAM persistence.
 *
 * Drain Rule D1 stance: GREENFIELD (sanctioned). NES Z1 has no options
 * menu and no SRAM persistence layer; this is Genesis-native infra.
 *
 * Layered on top of options_runtime (Task 9.1) and
 * src/sgdk_adapter/sram_options_io.{h,c}. Owns the SRAM $800..$81F
 * region; save slots at $000..$7FF stay owned by sram_save_*.
 *
 * Lifecycle:
 *   Boot:
 *     1. options_runtime_init()                        (defaults)
 *     2. options_persistence_load_or_default()         (overlay SRAM)
 *   Save (user accepts options menu / continue / death):
 *     1. options_persistence_commit()                  (serialize+write)
 *
 * Failure handling:
 *   - Blank / uninitialized SRAM detected (all 0x00 OR all 0xFF over
 *     the OPTIONS_STATE_SIZE bytes) -> defaults are kept and SRAM
 *     is NOT touched (caller must commit() to seed it).
 *   - Apply rejects (bad magic / bad checksum / bad version / bad
 *     range) -> defaults are kept; SRAM is NOT auto-overwritten so
 *     the user can recover by issuing a deliberate commit.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Probe the SRAM image and either apply it (returns 1) or fall back
 * to the defaults already loaded by options_runtime_init (returns 0).
 * Does NOT call options_runtime_init itself — caller orders that. */
unsigned char options_persistence_load_or_default(void);

/* Serialize the live options state and commit it to SRAM. Touches
 * only the locked OptionsState region (logical 0x800..0x81F). */
void options_persistence_commit(void);

/* Helper: returns 1 if the buffer is uninitialized SRAM (all 0x00 or
 * all 0xFF), 0 otherwise. Public so the persistence probe can use it. */
unsigned char options_persistence_image_is_blank(const unsigned char *buf,
                                                 unsigned int size);

#ifdef __cplusplus
}
#endif

#endif /* SRC_GAME_OPTIONS_OPTIONS_PERSISTENCE_H */
