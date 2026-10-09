#ifndef SRC_GAME_ENEMIES_OBJ_LISTS_H
#define SRC_GAME_ENEMIES_OBJ_LISTS_H

/* Phase 7 Task 7.2 step 2 — NES ObjLists.dat verbatim port.
 *
 * NES source: reference/aldonunez/dat/ObjLists.dat (201 bytes)
 *             reference/aldonunez/Z_05.asm:1450 ObjLists / @PlaceList
 *             InitObject_JumpTable consumer at Z_07.asm:5601
 *
 * Each ObjList is a contiguous run of ObjType bytes that maps slot index
 * (1..N) to the InitObject_JumpTable row to call when the room loads.
 * The per-room template_id -> ObjList offset mapping lives in the NES
 * @PlaceList logic (Z_05.asm:1798) which Task 7.7 will port. Until then
 * obj_list_for_room() is a stub returning NULL.
 *
 * Drain Rule D1: ADOPT (data port, function drain N/A).
 * Hard rule WT-5: file lives at src/game/enemies/, not engine/data/.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define ENEMY_OBJLISTS_LEN 201u

extern const unsigned char z1_obj_lists[ENEMY_OBJLISTS_LEN];

/* Returns a pointer to the start of the ObjList for (room_id, scene_id),
 * or NULL when no list applies (room is empty / Task 7.7 stub returns
 * NULL universally). Caller iterates ObjType bytes until the list-end
 * sentinel ($00 in trailing fill or external length count) — Task 7.7
 * lands the per-room length table.
 *
 * scene_id: 0 = overworld, 1 = underworld L1.. per scene bank id.
 */
const unsigned char *obj_list_for_room(unsigned char room_id,
                                       unsigned char scene_id);

/* Phase 7 Task 7.7 step 1 — Returns pointer to ObjList[N] entry for
 * template_id when template_id >= $62, else NULL. Indexes via
 * obj_list_offsets[template_id - $62]. */
const unsigned char *obj_list_for_template(unsigned char template_id);

/* Phase 7 Task 7.7 step 1 — NES Z_05.asm:1700-1820 monster-list parse.
 * Reads LBA_C[room_id]/LBA_D[room_id], parses template_id + count,
 * applies boss override + cellar override, fills ObjType[1..count],
 * writes RoomObjCount + RoomObjTemplateType. Returns 1 if any objects
 * loaded, 0 if room is empty. AssignObjSpawnPositions deferred to
 * step 2; ModifyObjCountByHistory{OW,UW} deferred to step 3. */
unsigned char enemy_room_load_objects(unsigned char room_id);
/* Object list ID [$02] after enemy_room_load_objects (history / cellar
 * adjusted): the AssignObjSpawnPositions parameter. */
unsigned char enemy_room_template_id(void);

/* Phase 7 Task 7.7 step 2 — NES Z_05.asm:1885-1996 AssignObjSpawnPositions
 * verbatim port. Walks SpawnPosListAddrs[ObjDir] in 9-entry cycles to
 * assign ObjX/ObjY for slots 1..9, calling IsSafeToSpawn to skip
 * unwalkable / Link-adjacent positions. Mode 9 cellar -> 4 blue keese
 * via CellarKeeseXs/Ys; modes $B/$C cave -> cave-dweller in slot 1.
 * Caller passes room_id (for LBA_F edge-spawn check) + template_id
 * (RoomObjTemplateType, NES $02 alias). */
void enemy_assign_spawn_positions(unsigned char room_id,
                                  unsigned char template_id);

/* NES FindNextEdgeSpawnCell + InitMonsterFromEdge placement: next
 * walkable edge cell into CurEdgeSpawnCell $525 and the slot's X/Y. */
void enemy_edge_spawn_next(unsigned int slot);
/* NES IsDistanceSafeToSpawn: 1 when Link is at least $22 away. */
unsigned char enemy_edge_distance_safe(unsigned int slot);

#ifdef __cplusplus
}
#endif

#endif /* SRC_GAME_ENEMIES_OBJ_LISTS_H */
