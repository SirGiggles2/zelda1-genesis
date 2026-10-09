/* enemy_render.h — NES OAM mirror -> Genesis SAT bridge.
 *
 * NES source:
 *   reference/aldonunez/Z_01.asm:5365 Anim_WriteSprite (drained body)
 *   reference/aldonunez/Z_01.asm:4958 SpriteOffsets (verbatim table)
 *   reference/aldonunez/Z_01.asm:3088 CycleCurSpriteIndex
 *   reference/aldonunez/Variables.inc:82 Sprites := $0200 (OAM mirror)
 *   reference/aldonunez/Variables.inc:87 RollingSpriteIndex := $0341
 *
 * Drained C: NONE prior. This file ships the drained Anim_WriteSprite +
 * a Genesis-native end-of-tick sweep that copies NES OAM mirror at
 * $0200..$02FF into the SGDK SAT.
 *
 * Stance: GREENFIELD per Drain Rule D1 — drain_coverage.json has no
 * candidate row for the sprite render primitive. NES asm wins ties.
 *
 * Bridge layout:
 *   - anim_write_sprite_drained(tile, slot)    drained Z_01.asm:5365 body.
 *     Writes 4 OAM bytes at nes_ram[$0200+offset] + cycles
 *     RollingSpriteIndex.
 *   - enemy_render_sweep_oam_to_sat()           Genesis-native sweep.
 *     Reads NES OAM mirror, writes SGDK SAT slots 32-72 for enemy
 *     sprites (Link/items keep 0-31). Caller invokes once per tick
 *     after enemy_loop_tick + before SYS_doVBlankProcess.
 *
 * Tile-ID translation (initial pass):
 *   NES tile id -> SCENE_OBJ tile_base + nes_tile_id. NES Z1 sprite
 *   tiles live at $00..$BF in 8x16 mode. Our SCENE_OBJ slot at
 *   roomrom_vram_map.h::ROOMROM_SPR_TILE_BASE+44 holds enemy CHR.
 *   Per-enemy refined mapping lands in follow-up commits.
 */

#ifndef ENEMY_RENDER_H
#define ENEMY_RENDER_H

void anim_write_sprite_drained(unsigned int tile, unsigned int slot);
/* Manual boss OAM producers need the OAM sweep; Patra uses native pairs. */
unsigned char enemy_render_needs_oam_sweep(void);
void enemy_render_sweep_oam_to_sat(void);
void enemy_render_reset_oam(void);
/* Prepare fixed spawn/death FX with other persistent sprite CHR. */
void enemy_render_prepare_fx_chr(void);

/* 2026-05-15 native renderer. Reads per-slot latched sprite state
 * (populated by anim_write_sprite_drained), emits <= 11 Genesis SAT
 * entries per frame. ~50 → ~11 SAT writes per frame on busy rooms. */
void enemy_render_native_sweep(void);
void enemy_render_native_reset(void);

/* Published by enemy_render_native_sweep: highest SAT slot index that
 * received a write this frame, + 1 (i.e. the DMA count). Lets the
 * gameplay tick DMA only the slots in use instead of all 64. */
extern unsigned char g_enemy_render_last_sat_slot;

/* Phase A 2026-05-15 cache feeder for native draw_dispatch.c path.
 * anim_write_sprite_pair_not_flashing writes OAM mirror directly via
 * DRAW_OAM_TILE/Y/X/ATTR macros and bypasses anim_write_sprite_drained
 * (which is the drain-shim path used by oracle/enemies/*_runtime.c).
 * Without this hook, natively-dispatched enemies (tektite, octorok,
 * leever, etc.) leave the s_enemy_* cache empty and render nothing
 * via enemy_render_native_sweep. Call from the LEFT-half iteration of
 * the writer with the just-emitted tile/attrs/x/y. Single-latch
 * semantics: first call per ENEMY_THROWER_SLOT per frame wins; later
 * calls are dropped (Phase E will replace with multi-latch). */
__attribute__((always_inline)) void enemy_render_publish_pair_left(unsigned char tile,
                                    unsigned char attrs,
                                    unsigned char x,
                                    unsigned char y);
void enemy_render_publish_native_pair(unsigned char left_tile,
                                      unsigned char left_attrs,
                                      unsigned char right_tile,
                                      unsigned char right_attrs,
                                      unsigned char left_x,
                                      unsigned char right_x,
                                      unsigned char y);

/* Phase D 2026-05-15 — death-spark + spawn-cloud frame publisher.
 * NES Z_07.asm:4977 AnimateAndDrawMetaObject draws the spark or cloud
 * sprite per frame during dying / spawning sequences. The drained
 * update_meta_object body (enemy_walker_bridge.c:796) skips the draw
 * call for now; this publisher fills the s_enemy_* cache so the
 * Genesis-native sweep emits a SAT entry for the spark/cloud frame.
 *
 * Reads ENEMY_METASTATE(slot):
 *   ms < $10: spawning-cloud path. Frame = ms & $03 -> cloud tile.
 *   ms >= $10: death-spark path. Frame = (ms - $10) & $03 -> spark tile.
 *
 * Frame tile maps come from NES Z1 sprite tile IDs (bomb-cloud = item
 * slot 1, death-spark = item slot $24). Approximate via direct tile
 * IDs $60..$66 (cloud) / $66..$6C (spark) since Anim_WriteItemSprites
 * indirection isn't yet wired in the cache path. */
void enemy_render_publish_meta(unsigned int slot);

/* T-110 bomb / fire weapon slots $10/$11. The owner resets its slot and
 * re-adds this frame's sprites on every update (NES OAM semantics:
 * tile, attrs, x, y). add_item routes the tile to the ITEM atlas;
 * add_cloud draws DrawCloud frame 1..3 as a mirrored pair. */
void enemy_render_weapon_reset_all(void);
void enemy_render_weapon_reset(unsigned char slot);
void enemy_render_weapon_add_item(unsigned char slot, unsigned char tile,
                                  unsigned char attrs, unsigned char x,
                                  unsigned char y);
void enemy_render_weapon_add_obj(unsigned char slot, unsigned char tile,
                                 unsigned char attrs, unsigned char x,
                                 unsigned char y);
void enemy_render_weapon_add_cloud(unsigned char slot, unsigned char frame,
                                   unsigned char attrs, unsigned char x,
                                   unsigned char y);

unsigned short enemy_render_item_sat(unsigned char nes_tile, unsigned char nes_attrs);
/* T-097: death spark ($62/$64) in Link's slots, sprite palette of the NES attrs. */
unsigned short enemy_render_spark_sat(unsigned char nes_tile, unsigned char nes_attrs);
#endif
