#ifndef AUDIO_DISPATCH_H
#define AUDIO_DISPATCH_H

/* Plan v5b Tier-5 T5.5 — audio dispatcher.
 *
 * Single source of truth for music transitions. Watches the tuple
 * (gamemode $0012, scene, room_id) each tick; on change, looks up
 * the target song per docs/audit/audio_routing.md and fires
 * music_play() once.
 *
 * Replaces per-call-site music_play() scattered through RoomRom
 * (scene-toggle re-fire) + src/debug/a4_probe_main.c (boot+enter).
 * Those keep firing for now (dispatcher just re-confirms; one extra
 * music_play() per session is harmless since change_song is idempotent
 * on identical bitmap). Future cleanup removes them once dispatcher
 * proven stable across all transitions.
 *
 * Song bitmap IDs (per src/audio_driver.asm change_song table):
 *   $80  title / demo (multi-phrase)
 *   $40  underworld dungeon (multi-phrase, phrase $0F..$11)
 *   $20  overworld (single-phrase via bit-loop)
 *   $10  ending (multi-phrase, phrase $11..$19)
 *   $06  Zelda fanfare (boss kill / triforce reveal)
 *   $01  OW alt (early phrase, kept for parity with NES bitmap layout)
 *
 * GameMode IDs (per reference/aldonunez/Z_07.asm UpdateMode table):
 *   $00  Demo / title
 *   $01  FileSelect
 *   $05  GameMode_Play (OW vs UW resolved via CUR_LEVEL/scene)
 *   $06  GameOver
 *   $07  Dying (cycles palette during 9-frame wipe)
 *   $08  ContinueQuestion
 *
 * Scene constants (must mirror RoomRom/src/main.c scene_t):
 *   0 = SCENE_OW
 *   1 = SCENE_UW
 *   2 = SCENE_CAVE
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Per-tick poll. Reads nes_ram[$0012] gamemode + caller-supplied
 * scene/room. Compares against last-published tuple; on change,
 * dispatches one music_play() call.
 *
 * Safe to call every roomrom_debug_tick() frame; cost is 3 byte
 * compares + (rarely) one music_play(). */
void audio_dispatch_tick(unsigned char scene, unsigned char room_id);

/* Reset internal "last" state — call from roomrom_debug_enter so
 * the first post-enter tick re-fires whatever song the new state
 * demands (covers the case where debug_enter changed scene/room
 * without going through audio_dispatch_tick). */
void audio_dispatch_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_DISPATCH_H */
