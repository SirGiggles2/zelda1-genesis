/* draw_dispatch.h — native sprite-descriptor / OAM draw pipeline
 * (Phase 4).
 *
 * Native rewrite of the DrawObjectMirrored / DrawObjectNotMirrored
 * chain in reference/aldonunez/Z_01.asm + the Anim_Write* OAM-mirror
 * writers. Both ROMs link.
 *
 * Pipeline:
 *   draw_object_mirrored / draw_object_not_mirrored
 *     -> draw_object_with_type
 *     -> draw_object_with_anim
 *     -> draw_object_with_anim_and_specific_sprites
 *     -> anim_write_horizontally_flippable_sprite_pair
 *        | anim_write_mirrored_sprite_pair
 *     -> anim_write_sprite_pair
 *     -> anim_write_sprite_pair_not_flashing -- writes 2 sprites
 *        to nes_ram[$0200..$02FF] OAM mirror.
 *
 * Replaces transpile-bridge shim chain c_draw_object_*. See
 * tools/audit/drain_findings/4_12n_draw_object.md.
 */

#ifndef DRAW_DISPATCH_H
#define DRAW_DISPATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* DrawObjectMirrored. frame in ZP_TMPD entry-side ($000D) per asm
 * convention: actual D0=frame is loaded by callers via ZP_TMPD.
 * Native version takes frame explicitly. NES Z_01.asm:2058. */
void draw_object_mirrored(unsigned char frame, unsigned int slot);

/* DrawObjectNotMirrored. Same as mirrored except mirrored=0.
 * NES Z_01.asm:2069. */
void draw_object_not_mirrored(unsigned char frame, unsigned int slot);

/* DrawObjectMirrored variant: frame already in TMPD via D0; here
 * frame is passed explicit. Used by per-monster updaters that
 * compute their own frame. NES DrawObjectMirroredWithFrame. */
void draw_object_mirrored_with_frame(unsigned char frame, unsigned int slot);

/* DrawObjectNotMirroredWithFrame. */
void draw_object_not_mirrored_with_frame(unsigned char frame, unsigned int slot);

/* AnimateItemObject (Z_07.asm:1955). Reads item lifetime timer at
 * RAM($03A8 + slot); skips draw if (timer >= $F0 && (timer & 1)==0)
 * (early-flash). Else fetches obj pos to sprite descriptor, looks
 * up item descriptor + slot, dispatches to draw_item_by_slot. */
void draw_animate_item_object(unsigned char item_id, unsigned int slot);

/* DrawItemBySlot (Z_07.asm:2023). Computes per-slot sprite attribute
 * (with palette flash for item slots $16/$1A/$1B/$19, plus
 * additive override for slots $00/$04/$02/$07/$0B), then writes
 * static item sprites. */
void draw_item_by_slot(unsigned int item_slot, unsigned int slot);
/* Anim_WriteItemSprites with caller-staged [00]/[01]/[04]/[05]/[0C]/[0F]. */
void draw_anim_write_item_sprites(unsigned int slot, unsigned int item_slot);
void draw_fairy(unsigned int slot);   /* DrawFairy (Z_04) */
/* Ending Link: fixed $48/$4C OAM offsets, specific item slot $21. */
void draw_link_ending_pose(void);
/* T-056: Anim_WriteStaticItemSpritesWithAttributes (Z_01.asm): attributes,
 * object slot, item slot; [00]/[01] already hold the position. */
void draw_static_item_sprites(unsigned char attrs, unsigned int slot,
                              unsigned int item_slot);

/* DrawItemInInventory (Z_07.asm:2011). Reads item value from
 * RAM($0657+slot) into TMP4, then DrawItemBySlot. */
void draw_item_in_inventory(unsigned int item_slot, unsigned int slot);
unsigned char draw_item_frame_tile(unsigned char item_slot, unsigned char frame);
unsigned char draw_item_icon(unsigned char item_slot, unsigned char item_value,
                             unsigned char *attr_out);

/* DrawArrow (Z_07.asm:3908) + OffsetAndDrawArrow + L_DrawArrowOrBoomerang.
 * Used by enrt_draw_shot dispatch when OBJ_TYPE == $5B. */
void draw_arrow(unsigned int slot);
/* DrawArrowOrBoomerangAndCheckCollisions @PrepareArrow: spark frame. */
void draw_arrow_spark(unsigned int slot);

/* DrawBoomerangAndCheckCollision/CalcBoomerangFrame tail. NES Z_07.asm:
 * 4202-4305. Update code owns timing and collision; this writes item
 * slot $1D with the NES eight-phase frame and attribute cycle. */
void draw_boomerang(unsigned int slot);

/* DrawSwordShotOrMagicShot (Z_07.asm:3437). Used by enrt_draw_shot
 * dispatch when OBJ_TYPE in $57/$58/$59 (player or monster sword/magic). */
void draw_sword_shot_or_magic_shot(unsigned int slot);

/* DrawObjectMirroredOverLink (Z_04.asm:763). Bypasses the rolling
 * sprite cursor; hardcodes LeftSpriteOffset=$40 / RightSpriteOffset=$44
 * (OAM sprites $10 / $11). Used by Like-Like capture path. */
void draw_object_mirrored_over_link(unsigned char frame, unsigned int slot);

/* DrawObjectNotMirroredOverLink (Z_04.asm:756). DRAW_MIRRORED=0
 * variant of the above; used by Wallmaster's captured-Link draw branch
 * (the hand sprite is asymmetric so it can't share its left and right
 * tiles). */
void draw_object_not_mirrored_over_link(unsigned char frame, unsigned int slot);

/* SpriteOffsets[41] (Z_01.asm:2035). Indexed by RollingSpriteIndex
 * ($0341); produces left/right OAM byte offsets. Public so per-family
 * sprite-patch fixups (Wallmaster Keese-tile bug) can resolve the
 * post-draw OAM positions. */
extern const unsigned char k_sprite_offsets[41];

/* WriteBossSprite (Z_04.asm:5844) tail-calls Anim_EndWriteSprite
 * (Z_01.asm:5393). Composed: writes 4 OAM bytes (y/tile/attr/x) at the
 * rolling sprite cursor + cycles cursor. Used by per-boss draw bodies
 * (Aquamentus / Dodongo / Ganon). Phase 7 Task 7.4 step 10. */
void draw_write_boss_sprite(unsigned char tile, unsigned char x,
                            unsigned char y, unsigned char attr);

#ifdef __cplusplus
}
#endif

#endif /* DRAW_DISPATCH_H */
