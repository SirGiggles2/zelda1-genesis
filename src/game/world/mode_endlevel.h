/* Phase 9.7 Mode 12 EndLevel public API.
 * NES source: reference/aldonunez/Z_05.asm:5534 UpdateMode12EndLevel_Full.
 * 5-sub-state machine; caller invokes per VBlank at GameMode==$12. */

#ifndef MODE_ENDLEVEL_H
#define MODE_ENDLEVEL_H

void mode12_endlevel_update(void);

#endif
