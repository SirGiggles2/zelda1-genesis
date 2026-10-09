#ifndef SRC_GAME_OPTIONS_PROBES_OPTIONS_PERSISTENCE_PROBE_H
#define SRC_GAME_OPTIONS_PROBES_OPTIONS_PERSISTENCE_PROBE_H

/* Phase 9 Task 9.2 — Options SRAM persistence in-ROM probe.
 *
 * Boot-time invocation from RoomRom/src/main.c. Publishes a 16-byte
 * pass/fail block at $FF7E90 (the slot adjacent to the Task 9.1
 * options_probe block at $FF7E80). Reader:
 * tools/debug/probes/probe_options_persistence.lua. */

#define OPTIONS_PERSISTENCE_PROBE_BASE        0x00FF7E90UL
#define OPTIONS_PERSISTENCE_PROBE_TEST_COUNT  8u

#ifdef __cplusplus
extern "C" {
#endif

void options_persistence_probe_run(void);

#ifdef __cplusplus
}
#endif

#endif /* SRC_GAME_OPTIONS_PROBES_OPTIONS_PERSISTENCE_PROBE_H */
