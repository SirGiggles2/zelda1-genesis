/* Phase 6 Task 6.11.1 / 6.11.3 — Link damage path.
 *
 * NES authority:
 *   Z_01.asm:5666 HarmLink            entry from monster collision
 *   Z_01.asm:5691 Link_BeHarmed       16-bit damage decode + ring divide
 *   Z_01.asm:5718 (HeartPartial sub)  borrow-from-full-hearts arithmetic
 *   Z_01.asm:5756 @HandleDied         death gate
 *   Z_07.asm:5756 DecrementInvincibilityTimer
 *                                     -1 every other frame (FrameCounter LSB)
 *
 * Damage encoding (NES 16-bit):
 *   high byte ($0D) = full hearts to subtract  (clipped to HeartValues low nib)
 *   low  byte ($0E) = HeartPartial fraction    (4-px granularity, $00..$FF)
 *
 * Ring tier divides 16-bit damage by 2 per ring level (1 ring = /2,
 * 2 rings = /4 — same as NES `LSR $0D / ROR $0E` loop). InvRing 0/1/2
 * = no/blue/red ring.
 *
 * Invincibility: NES post-harm value not yet evidenced; scaffold uses
 * $10 (16) ticks decremented every 2 frames per DecrementInvincibilityTimer.
 *
 * Coverage: PARTIAL — subtract math + invincibility tick + shove physics
 * (Task 6.11.4) land here; pre-impact dispatch (HarmLink entry from
 * monster collision) deferred to Task 6.11.2 (blocked on enemy state).
 *
 * Task 6.11.4 BeginShove (Z_01.asm:6470 Link-defender path):
 *   - Direction code in NES bits ($01=right, $02=left, $04=down, $08=up)
 *   - Initial axis from defender_dir & 0x03 (non-zero = horizontal)
 *   - Direction polarity from monster vs Link X (or Y) compare
 *   - ObjShoveDir |= $80 (first-frame marker)
 *   - ObjInvincibilityTimer = $18 (24 frames; replaces apply()'s $10)
 *   - ObjShoveDistance      = $20 (32 pixels remaining)
 */

#ifndef ROOMROM_LINK_DAMAGE_H
#define ROOMROM_LINK_DAMAGE_H

void roomrom_link_damage_init(void);

/* Apply NES-encoded damage. dmg_hi = full hearts, dmg_lo = HeartPartial.
 * Reads InvRing for the tier divide. Returns 1 if Link died, 0 otherwise.
 * Sets invincibility timer on non-fatal hit. */
unsigned char roomrom_link_damage_apply(unsigned char dmg_hi,
                                        unsigned char dmg_lo);

/* NES Z_07.asm:5756 — decrement timer once every 2 frames. Caller passes
 * the global frame counter so a single bit (LSB) gates the DEC. */
void roomrom_link_damage_tick(unsigned char frame_counter);

unsigned char roomrom_link_damage_invincible(void);
unsigned char roomrom_link_damage_dead(void);

/* Task 6.11.4 BeginShove — Link-defender entry. Computes shove direction
 * away from monster, sets ObjShoveDir / ObjInvincibilityTimer ($18) /
 * ObjShoveDistance ($20). No-op if Link is already invincible.
 *
 * defender_dir: Link's current ObjDir ($01/$02/$04/$08).
 * defender_grid_offset: Link's ObjGridOffset (0 = aligned to grid).
 * monster_{x,y} / link_{x,y}: signed pixel coords (Genesis room space).
 *
 * Returns 1 if shove was applied, 0 if blocked (already invincible). */
unsigned char roomrom_link_shove_begin(unsigned char defender_dir,
                                       unsigned char defender_grid_offset,
                                       signed short monster_x,
                                       signed short monster_y,
                                       signed short link_x,
                                       signed short link_y);

/* Per-frame applier. While ObjShoveDistance > 0, decrement and step Link
 * one pixel along ObjShoveDir. Returns the dx/dy step (callers add to
 * Link's position). Output (0,0) = no shove this frame. */
void roomrom_link_shove_tick(signed char *out_dx, signed char *out_dy);

unsigned char roomrom_link_shove_active(void);
unsigned char roomrom_link_shove_dir(void);
unsigned char roomrom_link_shove_distance(void);

#endif
