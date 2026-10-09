#include "pause_state.h"

/* Phase 6 Task 6.10.1 — Paused flag storage.
 *
 * Single byte mirroring NES Z1 `Paused` ($E0). Boots at OFF; voluntary
 * toggle is bare-START edge-press in main.c; involuntary is set by
 * potion-drink in Task 6.9. */

unsigned char g_paused = ROOMROM_PAUSE_OFF;
