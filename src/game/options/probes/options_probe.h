#ifndef SRC_GAME_OPTIONS_PROBES_OPTIONS_PROBE_H
#define SRC_GAME_OPTIONS_PROBES_OPTIONS_PROBE_H

/* Phase 9 Task 9.1 — Options runtime in-ROM probe entry point.
 *
 * Boot-time invocation from RoomRom/src/main.c. Publishes a 16-byte
 * pass/fail block at $FF7E80; tools/debug/probes/probe_options_runtime.lua
 * reads it. See options_probe.c for block layout. */

#define OPTIONS_PROBE_BASE         0x00FF7E80UL
#define OPTIONS_PROBE_TEST_COUNT   10u

#ifdef __cplusplus
extern "C" {
#endif

void options_probe_run(void);

#ifdef __cplusplus
}
#endif

#endif /* SRC_GAME_OPTIONS_PROBES_OPTIONS_PROBE_H */
