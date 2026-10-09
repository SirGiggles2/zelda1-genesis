/* Phase 9.7 Mode 11 Death public API.
 * NES source: reference/aldonunez/Z_05.asm:2521 UpdateMode11Death_Full.
 * 13-sub-state machine; caller invokes per VBlank at GameMode==$11. */

#ifndef MODE_DEATH_H
#define MODE_DEATH_H

void mode11_death_update(void);
/* 0 = Link drawn, 1 = spark in Sprites+72..79, 2 = Link/spark hidden. */
unsigned char mode11_spark_state(void);

#endif
