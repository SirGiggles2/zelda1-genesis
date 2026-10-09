/* level_info_install.h — install NES Z1 per-level SRAM tables.
 *
 * NES Z1 ships LevelBlockAttrs A..F (6 x 128 bytes) + LevelInfo (256 bytes)
 * per level in PRG-ROM. On real cart these are SRAM-resident from the
 * factory (or get written once by the boot loader). Our Genesis port
 * never wrote them, so consumers reading $697E (LBA_C), $69FE (LBA_D),
 * and $6BA2 (FoeCounts) saw zeros and produced no enemies.
 *
 * level_info_install_ow() copies the OW tables (already shipped as
 * the front of rooms_overworld[]) into NES RAM at the addresses the
 * drained code expects. Call once at scene_load (OW) or any time the
 * underlying level changes (Phase 5 EnterRoom).
 *
 * UW tables ship via data/rooms/dungeons.c blob; install when UW
 * level loads. */

#ifndef LEVEL_INFO_INSTALL_H
#define LEVEL_INFO_INSTALL_H

void level_info_install_ow(void);
unsigned char level_info_ow_start_room(void);
void level_info_install_uw(unsigned char level, unsigned char quest);
/* Second-quest LevelInfo patch for UW level 1..9 (Z_06 UpdateMode2Load_Full);
 * called by level_info_install_uw when quest == 2. */
void level_info_apply_q2_patch(unsigned char level);
void level_info_apply_q2_ow_patch(void);
/* GameMode 2 step 0 (level block), 1 (level info), 2 (Q2 patches). */
void level_info_mode2_step(unsigned char step, unsigned char level,
                           unsigned char quest);

#endif
