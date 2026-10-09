/* src/fs_handoff.h — FS → transpiled gameplay/register-name handoff. */
#ifndef FS_HANDOFF_H
#define FS_HANDOFF_H
#include <stdint.h>

/* Tear down native FS VDP state then jump to fs_to_transpiled_trampoline.
 * Does not return.
 *
 * slot 0/1/2: load saved game (transpiled @ChoseSlot path).
 * slot 3: register-name screen (CurSaveSlot >= 3 → GameMode = slot+$0B = $0E).
 *
 * v6.handoff: caller picks slot 3 for empty saves (no SRAM yet); when SRAM
 * detection lands, occupied slots use 0/1/2 to enter gameplay directly.
 */
void fs_handoff_to_transpiled(uint8_t slot);

/* Set to 1 when the player commits to a slot; g_fs_handoff_slot carries
 * which one. The File Select never enters gameplay itself — the host loop
 * polls these and performs the transition, so src/frontend/ keeps no
 * dependency on the gameplay runtime. CurSaveSlot ($0016) is seeded by
 * fs_handoff_to_transpiled before these are set. */
extern unsigned char g_fs_handoff_requested;
extern unsigned char g_fs_handoff_slot;

#endif
