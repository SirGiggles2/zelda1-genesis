#ifndef NES_RAM_SYNC_H
#define NES_RAM_SYNC_H

/* Plan v5a Tier-1 bridge — sync C-side state into NES RAM mirror cells
 * so future NES native consumers (FluteTimer, Link_HandleInput, AI
 * chase-target, HUD readers) see live values without per-call adapters.
 *
 * Each entry-point is called once per roomrom_debug_tick() frame from
 * RoomRom/src/main.c. All three are byte-wide writes into nes_ram[];
 * cost is negligible (<10 cycles each).
 *
 *   T1.1  nes_ram_sync_input        — $00F8 ButtonsPressed (edge)
 *                                     $00FA ButtonsDown    (held)
 *   T1.2  nes_ram_sync_inventory_hearts
 *                                   — $066F HeartValues (hi=max, lo=cur)
 *                                     $0670 HeartPartial
 *   T1.3  nes_ram_sync_link_face    — $008C ObjDir[0]
 *
 * NES source: reference/aldonunez/Z_07.asm:660-705 ReadInputs;
 *             Variables.inc:257-258 HeartValues/HeartPartial;
 *             Variables.inc ObjDir[0]=$008C.
 *
 * Drained C : NEW (this file). EXTENDs existing per-tick sync block
 *             added to roomrom_debug_tick on 2026-05-16.
 * Coverage  : NONE for the NES-write side; FULL for the C-side reads
 *             (joypad: SGDK JOY_readJoypad; hearts: g_inventory;
 *             face: players[0].face).
 * Stance    : EXTEND.
 */

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* T1.1 — translate SGDK joypad bits into NES $FA/$FB layout.
 *   held         = current frame held bits   (raw or AB-swapped)
 *   edge_pressed = bits new this frame       (same swap convention)
 *
 * NES bit layout (Z_07 ReadInputs result):
 *   $80 A   $40 B   $20 Select   $10 Start
 *   $08 Up  $04 Down $02 Left    $01 Right
 *
 * Caller passes both pre-computed because RoomRom main already derives
 * `pressed = joy & ~s_joy_prev` before this call and may AB-swap it;
 * recomputing prev->edge inside this helper would race that flow.
 *
 * Writes:
 *   nes_ram[$00F8] = ButtonsPressed (edge: bits set this frame, cleared
 *                    next)
 *   nes_ram[$00FA] = ButtonsDown    (held: 1 while button is down)
 */
void nes_ram_sync_input(u16 held, u16 edge_pressed);

/* T-013: controller 2 (NES ReadInputs reads both pads every NMI):
 *   nes_ram[$00F9] = ButtonsPressed+1, nes_ram[$00FB] = ButtonsDown+1.
 * Same bit layout; Genesis C = Select. Pad 2 Up+A in the pause menu is
 * the NES save / continue menu (UpdateMenuActive, Z_05.asm). */
void nes_ram_sync_input2(u16 held, u16 edge_pressed);

/* T1.2 — pull live heart cells from nes_ram[$066F/$0670] back into
 * g_inventory. nes_ram is the canonical store under drained combat
 * (link_collision_link_be_harmed writes LINK_HEARTS = RAM($066F) on
 * enemy contact); g_inventory is the Genesis-side cache the HUD and
 * options reader consume. The prior direction (g_inventory -> nes_ram)
 * stomped combat's per-frame heart decrement and Link never took
 * damage. Seed the nes_ram side once at gameplay enter via
 * nes_ram_seed_inventory_hearts() so the first per-frame pull doesn't
 * read 0. */
void nes_ram_sync_inventory_hearts(void);

/* Plan v5b — one-shot push of g_inventory hearts into nes_ram[$066F/
 * $0670] at gameplay enter, after options_consumer_apply_inventory_at_
 * start() has populated g_inventory from SRAM. Mirrors the pattern of
 * nes_ram_seed_sword_level: substrate is canonical, Genesis-side state
 * seeds it once and then reads back. */
void nes_ram_seed_inventory_hearts(void);

/* T1.3 — refresh ObjDir[0] from the canonical C-side face. AI chase
 * targets read this cell each tick; today's seed-once-at-debug-enter
 * goes stale on any C-side face change.
 *
 * 2026-05-16 bug fix: address was $008C (= ObjY[slot 8], a wrong cell)
 * and value was the raw link_face_t enum (0..3) — both wrong. ObjDir[0]
 * is at $0098 and the cell holds the NES dir BITMAP ($01=R/$02=L/
 * $04=D/$08=U). Without that fix collision_check_link_collision_preinit
 * always read OR(LinkDir, MonsterDir) == MonsterDir → broken parry, AI
 * chased a Link who appeared to face nowhere. */
void nes_ram_sync_link_face(void);

/* Plan v5 — publish sword swing pose into NES weapon slot 13 and the
 * Genesis-native sword beam into NES weapon slot 14. The
 * collision battery's only "is the sword swinging?" test is
 *   OBJ_STATE(13) == 2
 * and sword-shot enemy collision already checks slot 14 as an active
 * projectile when OBJ_STATE(14) == $10.
 * (see collision_check_monster_sword_collision in
 * src/game/combat/collision_dispatch.c). Until this sync existed the
 * cells were always 0 and sword/beam phantom-stabbed nothing.
 *
 * Writes per tick:
 *   nes_ram[$0098 + 13]  = sword NES dir bitmap (mirrors Link's facing)
 *   nes_ram[$00AC + 13]  = swing state (0 idle / 2 full-extend)
 *   nes_ram[$0070 + 13]  = sword X
 *   nes_ram[$0084 + 13]  = sword Y
 * The sword shot (slot 14) is an NES object (src/game/items/sword_shot.c).
 *
 * Idle/inactive frames clear the corresponding weapon slots. */
void nes_ram_sync_sword(void);


/* Plan v5 — seed ITEM_SWORD_LEVEL ($0657 ITEMS_BY_LEVEL[0]) to wood-
 * sword tier on gameplay enter. Without a non-zero value the damage
 * table k_sword_damage_points[level-1] indexes out-of-range / picks 0.
 * RoomRom currently has no inventory pickup UI so the level is seeded
 * directly. */
void nes_ram_seed_sword_level(unsigned char level);

#ifdef __cplusplus
}
#endif

#endif /* NES_RAM_SYNC_H */
