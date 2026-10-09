#ifndef GAME_ENEMIES_BOSSES_BOSS_GLEEOK_H
#define GAME_ENEMIES_BOSSES_BOSS_GLEEOK_H

/* Phase 8 Task 8.5 — Gleeok public API.
 *
 * NES source: Z_07.asm:5295 rows $42/$43/$44/$45 -> Z_04.asm:8601
 *             UpdateGleeok (4-neck dispatch); $46 -> Z_04.asm:8527
 *             UpdateGleeokHead.
 *             Z_07.asm:5601 rows $42-$45 -> Z_04.asm:7649 InitGleeok;
 *             $46 -> enrt_init_gleeok_head (drained).
 * Drained C:  src/oracle/enemies/enemy_gleeok_runtime.c
 *             (enrt_init_gleeok_head, enrt_update_gleeok,
 *              enrt_gleeok_check_collisions,
 *              enrt_gleeok_store_ref_seg_distance,
 *              enrt_gleeok_set_segment_x/y,
 *              enrt_gleeok_contract_segment_x/y/segment,
 *              enrt_gleeok_dec_head_timer, enrt_gleeok_ignore_segment).
 *             Drain primary for the segment-mgmt slice.
 * Coverage:   PARTIAL — top-level InitGleeok + UpdateGleeokHead +
 *             8 gleeok-specific primitives (draw_body, fetch_neck_addrs,
 *             move_neck, move_head, calc_segment_limits, stretch_neck,
 *             draw_head_and_check_collisions,
 *             draw_segment_and_check_collisions) NOT drained — this TU
 *             carries native NES ports for them.
 * Stance:     EXTEND — drained per-segment helpers consumed verbatim;
 *             native bridge supplies the rest from per-line Z_04.asm
 *             transcription. Faithful to NES (including the Z_04:8944
 *             "SBC ObjX+3,X" oddity flagged UNKNOWN in NES disasm).
 */

void boss_gleeok_init(unsigned int slot);
void boss_gleeok_update_head(unsigned int slot);

#endif /* GAME_ENEMIES_BOSSES_BOSS_GLEEOK_H */
