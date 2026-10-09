/* Phase 9 Task 9.7 — save serializer probe.
 *
 * Probe RAM contract ($FF7EE0..$FF7EEF):
 *   [0]   = 'S' (0x53) magic
 *   [1]   = 'V' (0x56) magic
 *   [2]   = version (1)
 *   [3]   = total tests run (5)
 *   [4]   = passes
 *   [5]   = bits 0..7 (1 = pass)
 *   [6..15] reserved
 *
 * Bit map:
 *   bit0: round_trip            — serialize → mutate live RAM → deserialize → state restored
 *   bit1: magic_validate        — validate() returns 1 immediately after serialize()
 *   bit2: bad_magic_rejected    — corrupt magic_lo, validate() returns 0
 *   bit3: bad_checksum_rejected — corrupt one inventory byte, validate() returns 0
 *   bit4: cross_slot_isolation  — slot-0 serialize leaves slot-1 region untouched
 */

#ifndef SRC_STATE_PROBES_SAVE_SERIALIZER_PROBE_H
#define SRC_STATE_PROBES_SAVE_SERIALIZER_PROBE_H

#define SAVE_SERIALIZER_PROBE_BASE  0x00FF7EE0UL

#ifdef __cplusplus
extern "C" {
#endif

void save_serializer_probe_run(void);

#ifdef __cplusplus
}
#endif

#endif /* SRC_STATE_PROBES_SAVE_SERIALIZER_PROBE_H */
