#ifndef SRC_GAME_ENEMIES_ENEMY_LOOP_H
#define SRC_GAME_ENEMIES_ENEMY_LOOP_H

/* Enemy slot iterator + type dispatch.
 *
 * Phase 7 Task 7.2 step 2 framework. Wires drained walker family
 * (src/oracle/enemies/ runtime files, linked into Debug.md per
 * commit b5026c1a) into Debug.md gameplay tick.
 *
 * Verdict source: debates/2026-05-09-phase7-task-7-2-design/synthesis.md
 *   Q1=(a) port ObjLists, Q2=(c) scroll-stable branch only,
 *   Q3=(b) function-pointer table indexed by ENEMY_TYPE,
 *   Q4=(c) overworld $7C first probe, Q5=(b) reserve SAT slots + router.
 *
 * Drain Rule D1 stance: EXTEND. This file owns the iterator + dispatch
 * shell; every enrt_init_/enrt_update_ call goes to drained C.
 *
 * Hard rule WT-5 (RoomRom freeze, 2026-05-09): new gameplay code lives
 * here under src/game/enemies/, not RoomRom/src/. Tasks 7.3-7.7 fan out
 * by filling NULL slots in enemy_init_fns[ENEMY_TYPE_MAX] /
 * enemy_update_fns[ENEMY_TYPE_MAX] without modifying this file.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Top of NES InitObject_JumpTable at Z_07.asm:5601 has 80+ entries.
 * 0x80 covers the gameplay enemy/object range; tile-objects + caves
 * (>= 0x5F) are gated separately (not via this dispatch). */
#define ENEMY_LOOP_TYPE_MAX 0x80u

/* NES Z1 has 11 active object slots (1..11). Slot 0 is reserved for
 * the projectile/sword owned by Link. NES Z_07.asm:5466 InitObject
 * preamble uses CurObjIndex to walk slots 1..11 inclusive. */
#define ENEMY_LOOP_SLOT_FIRST 1u
#define ENEMY_LOOP_SLOT_LAST  11u
#define ENEMY_LOOP_SLOT_COUNT (ENEMY_LOOP_SLOT_LAST - ENEMY_LOOP_SLOT_FIRST + 1u)

typedef void (*enemy_init_fn)(unsigned int slot);
typedef void (*enemy_update_fn)(unsigned int slot);

extern const enemy_init_fn   enemy_init_fns[ENEMY_LOOP_TYPE_MAX];
extern const enemy_update_fn enemy_update_fns[ENEMY_LOOP_TYPE_MAX];

/* Called on room load and on scroll-finalize. Decodes the room's
 * ObjList template into ObjType+slot for slots 1..11, clears scratch
 * state, and dispatches enemy_init_fns[ENEMY_TYPE(slot)] when set. */
void enemy_loop_room_init(unsigned char room_id, unsigned char scene_id,
                          unsigned char level, unsigned char quest);
/* Genuine mode-4 entry into an identity already seen by native loading. */
void enemy_loop_room_reenter(unsigned char room_id, unsigned char scene_id,
                             unsigned char level, unsigned char quest);

/* Empty-clear every enemy slot (1..11) WITHOUT respawning. Used by
 * cave_init so caves start on a NES-fresh object page (no surviving OW
 * enemy whose work-cell ENEMY_PUSH_TIMER $0412+slot aliases the cave text
 * char index $0416 at slot 4). */
void enemy_loop_enter_cave_slots(void);   /* T-143: NES InitMode_EnterRoom object reset for a cave */
void enemy_loop_enter_cave_objects(unsigned char room_id); /* T-171: monster list + spawn spots + tile object */

/* Called every frame INSIDE the scroll-stable branch of the gameplay
 * tick (Q2=c). Iterates slots 1..11, dispatches
 * enemy_update_fns[ENEMY_TYPE(slot)] when alive + non-NULL. */
void enemy_loop_tick(void);
/* NES UpdateObject uninitialized path + InitObject (Z_07.asm:5244, 5466). */
void enemy_loop_init_object(unsigned int slot, unsigned char t);

/* NES UpdateMode5Play tail after the object loop: heart warning; in the
 * OW (in_uw == 0) the sea sound and CheckZora. */
void enemy_loop_play_tail(unsigned char in_uw);

/* Test hook (Phase 7 first probe — Q4=c overworld $7C):
 * Forcibly spawn one slow octorok in slot N at (x,y) with a fixed
 * direction. Used by tools/debug/probe_walker_parity.lua to seed a
 * deterministic frame trace before the per-room template lookup
 * lands in Task 7.7.
 */
void enemy_loop_force_spawn_slow_octorock(unsigned int slot,
                                          unsigned char x,
                                          unsigned char y,
                                          unsigned char dir);

/* Step 8 generic seed: spawn ANY enemy type. Used by multi-slot probe
 * to seed moblin/goriya/stalfos in slots 2/3/4 alongside the slot-1
 * octorok and verify step-7 dispatch rows actually tick. */
void enemy_loop_force_spawn_typed(unsigned int slot,
                                  unsigned char enemy_type,
                                  unsigned char x,
                                  unsigned char y,
                                  unsigned char dir);

/* Diagnostic accessors for the slot iterator (used by probes). */
unsigned int  enemy_loop_alive_count(void);
unsigned char enemy_loop_get_type(unsigned int slot);

#ifdef __cplusplus
}
#endif

#endif /* SRC_GAME_ENEMIES_ENEMY_LOOP_H */
