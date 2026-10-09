/* Phase 7 Task 7.7 step 1 — Enemy Room Matrix loader.
 *
 * NES source: reference/aldonunez/Z_05.asm:1700-1820 (InitMode_EnterRoom
 *             monster-list parse) + Z_05.asm:1885-1962 (AssignObjSpawnPositions)
 *             + Z_05.asm:3534/3982 (ModifyObjCountByHistoryOW/UW).
 * Drained C:  NONE (room-init monster-list parser not drained — only the
 *             post-init bookkeeping in src/oracle/enemies/enemy_common_runtime.c).
 * Coverage:   NONE
 * Stance:     GREENFIELD (legal — drain_coverage.json has no candidate row).
 *
 * The original Phase 7 Task 7.2 stub (obj_list_for_room → NULL) is replaced
 * with the full NES room-init monster spawn pipeline:
 *
 *   1. attr_c = LevelBlockAttrsC[room_id]; template_id = attr_c & $3F
 *   2. attr_d = LevelBlockAttrsD[room_id]
 *      if (attr_d & $80) template_id += $40            ; bit 7 -> bit 6
 *   3. foe_idx = (attr_c >> 6) & $03
 *      count   = LevelInfo_FoeCounts[foe_idx]
 *      if (template_id >= $32 && template_id < $62) count = 1   ; boss override
 *   4. ModifyObjCountByHistory{OW|UW}(template_id, count)        ; step 3
 *   5. if (GameMode == $09) { template_id = 0; count = 0; }      ; cellar
 *   6. RoomObjCount = count; if (count == 0 || template_id == 0) skip
 *   7. if (template_id < $62)
 *         for X in 1..count: ObjType[X] = template_id
 *      else
 *         src = z1_obj_lists + obj_list_offsets[template_id - $62]
 *         for X in 0..count-1: ObjType[1+X] = src[X]
 *   8. RoomObjTemplateType = ObjType[1]
 *   9. AssignObjSpawnPositions()                                  ; step 2
 *  10. for X in 1..11: if (ObjType[X]) enemy_init_fns[ObjType[X]](X)
 *
 * Step 1 lands the parser + ObjType fill + RoomObjCount/RoomObjTemplateType
 * writes. AssignObjSpawnPositions and ModifyObjCountByHistory* land in
 * subsequent step commits — for step 1, default spawn coords come from
 * SpawnPosListAddrs[ObjDir] but without IsSafeToSpawn rejection; history
 * modify is a no-op (pristine first-entry behavior).
 *
 * Hard rule WT-5: file lives at src/game/enemies/, not engine/.
 */

#include "obj_lists.h"
#include "platform_abi.h"
#include "enemy_state.h"
#include "dungeon_state.h"
#include "progress_state.h"   /* CUR_LEVEL, SAVEFILE_PTR_LO/HI */
#include "room_state.h"       /* ROOM_HISTORY */

/* Step 2 — IsSafeToSpawn dependency (NES Z_05.asm:2009 GetCollidableTileStill).
 * Already drained at src/game/combat/collision_dispatch.c:161. */
extern unsigned char collision_get_collidable_tile_still(unsigned int slot);

/* Step 3 — GetRoomFlags drained at src/game/room/room_dispatch.c:50.
 * Returns current room's flag byte from SRAM, also stashes ptr in
 * SAVEFILE_PTR_LO/HI (zp $00/$01 alias) for follow-up writes. */
extern unsigned char room_get_room_flags(void);

/* NES ObjLists.dat — 201 bytes, 30 ObjList templates concatenated. */
const unsigned char z1_obj_lists[ENEMY_OBJLISTS_LEN] = {
    /* ObjList00 @ 0  (5) */ 0x03, 0x03, 0x04, 0x03, 0x04,
    /* ObjList01 @ 5  (4) */ 0x03, 0x04, 0x03, 0x04,
    /* ObjList02 @ 9  (6) */ 0x1A, 0x1A, 0x02, 0x01, 0x02, 0x01,
    /* ObjList03 @ 15 (4) */ 0x01, 0x02, 0x01, 0x02,
    /* ObjList04 @ 19 (6) */ 0x01, 0x0F, 0x02, 0x01, 0x10, 0x02,
    /* ObjList05 @ 25 (6) */ 0x0F, 0x1A, 0x10, 0x1A, 0x0F, 0x1A,
    /* ObjList06 @ 31 (5) */ 0x09, 0x08, 0x08, 0x08, 0x08,
    /* ObjList07 @ 36 (4) */ 0x08, 0x07, 0x08, 0x07,
    /* ObjList08 @ 40 (5) */ 0x08, 0x09, 0x08, 0x09, 0x08,
    /* ObjList09 @ 45 (5) */ 0x0A, 0x07, 0x0A, 0x07, 0x07,
    /* ObjList10 @ 50 (6) */ 0x03, 0x0A, 0x04, 0x0A, 0x04, 0x04,
    /* ObjList11 @ 56 (8) */ 0x4A, 0x00, 0x00, 0x00, 0x13, 0x13, 0x00, 0x13,
    /* ObjList12 @ 64 (8) */ 0x4A, 0x00, 0x00, 0x00, 0x1B, 0x1B, 0x1B, 0x1B,
    /* ObjList13 @ 72 (8) */ 0x2B, 0x2B, 0x2B, 0x13, 0x13, 0x1B, 0x1B, 0x1B,
    /* ObjList14 @ 80 (8) */ 0x16, 0x30, 0x30, 0x1B, 0x1B, 0x16, 0x00, 0x00,
    /* ObjList15 @ 88 (8) */ 0x2B, 0x2B, 0x2B, 0x23, 0x23, 0x24, 0x23, 0x24,
    /* ObjList16 @ 96 (8) */ 0x2B, 0x2B, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00,
    /* ObjList17 @104 (6) */ 0x2B, 0x2B, 0x13, 0x13, 0x17, 0x17,
    /* ObjList18 @110 (8) */ 0x2B, 0x2B, 0x0C, 0x0B, 0x0B, 0x30, 0x30, 0x30,
    /* ObjList19 @118 (8) */ 0x2B, 0x2B, 0x05, 0x05, 0x05, 0x1B, 0x1B, 0x1B,
    /* ObjList20 @126 (8) */ 0x4A, 0x00, 0x00, 0x00, 0x17, 0x17, 0x17, 0x17,
    /* ObjList21 @134 (8) */ 0x4A, 0x00, 0x00, 0x00, 0x23, 0x24, 0x23, 0x24,
    /* ObjList22 @142 (6) */ 0x16, 0x0C, 0x0B, 0x0C, 0x0B, 0x16,
    /* ObjList23 @148 (8) */ 0x2B, 0x2B, 0x2B, 0x27, 0x27, 0x27, 0x27, 0x27,
    /* ObjList24 @156 (8) */ 0x05, 0x06, 0x06, 0x05, 0x06, 0x05, 0x00, 0x00,
    /* ObjList25 @164 (5) */ 0x23, 0x23, 0x24, 0x23, 0x24,
    /* ObjList26 @169 (8) */ 0x2B, 0x17, 0x23, 0x23, 0x17, 0x24, 0x17, 0x24,
    /* ObjList27 @177 (8) */ 0x2D, 0x2D, 0x2D, 0x2C, 0x23, 0x24, 0x23, 0x24,
    /* ObjList28 @185 (8) */ 0x2D, 0x2D, 0x2D, 0x2C, 0x0C, 0x0B, 0x0C, 0x0B,
    /* ObjList29 @193 (8) */ 0x2D, 0x2D, 0x2D, 0x2C, 0x27, 0x27, 0x27, 0x27,
};

/* Offsets into z1_obj_lists, indexed by (template_id - $62). 30 entries. */
static const unsigned char obj_list_offsets[30] = {
    0,   5,   9,   15,  19,  25,  31,  36,  40,  45,
    50,  56,  64,  72,  80,  88,  96,  104, 110, 118,
    126, 134, 142, 148, 156, 164, 169, 177, 185, 193,
};

const unsigned char *obj_list_for_template(unsigned char template_id)
{
    /* NES Z_05.asm:1791 @PlaceList: template_id - $62 -> ObjListAddrs entry. */
    if (template_id < 0x62u) return (const unsigned char *)0;
    unsigned char idx = (unsigned char)(template_id - 0x62u);
    if (idx >= 30u) return (const unsigned char *)0;
    return &z1_obj_lists[obj_list_offsets[idx]];
}

/* Legacy shim — pre-Task-7.7 stub. Returns NULL; callers should migrate to
 * enemy_room_load_objects + obj_list_for_template. */
const unsigned char *obj_list_for_room(unsigned char room_id,
                                       unsigned char scene_id)
{
    (void)room_id;
    (void)scene_id;
    return (const unsigned char *)0;
}

/* Phase 7 Task 7.7 step 3 — ModifyObjCountByHistoryOW.
 *
 * NES source: reference/aldonunez/Z_05.asm:ModifyObjCountByHistoryOW.
 * Drained C: NONE (active native body here; translated z_05.asm retained).
 * Coverage: PARTIAL (T-196 missing out-of-history partial-kill branch).
 * Stance: EXTEND the existing history/count owner.
 *
 * If room is in RoomHistory[0..5]: subtract kill-count (flags & 7) from
 * spawn count, or reset both to 0 when kill-count == 7.
 *
 * If room is NOT in history but kill-count == 7 (stale fully-cleared
 * marker), zero the kill-count bits in the SRAM flag byte. The NES
 * original writes via ($00),Y with Y=$FF — port writes to the same
 * cell room_get_room_flags read (SAVEFILE_PTR + room). Partial kill counts
 * still subtract outside history; only a fully cleared room replenishes.
 */
static void modify_count_by_history_ow(unsigned char room_id,
                                       unsigned char *template_io,
                                       unsigned char *count_io)
{
    unsigned char in_history = 0u;
    signed char y;
    for (y = 5; y >= 0; --y) {
        if ((unsigned char)ROOM_HISTORY((unsigned char)y) == room_id) {
            in_history = 1u;
            break;
        }
    }
    unsigned char flags = room_get_room_flags();
    unsigned char kc = (unsigned char)(flags & 0x07u);
    if (!in_history) {
        if (kc == 7u) {
            unsigned int ptr = (unsigned int)(unsigned char)SAVEFILE_PTR_LO
                             | ((unsigned int)(unsigned char)SAVEFILE_PTR_HI << 8);
            nes_ram[ptr + room_id] = (unsigned char)(flags & 0xF8u);
            return;
        }
    }
    if (kc == 0u) return;
    if (kc == 7u) {
        *template_io = 0u;
        *count_io = 0u;
        return;
    }
    if (*count_io >= kc) {
        *count_io = (unsigned char)(*count_io - kc);
    } else {
        *template_io = 0u;
        *count_io = 0u;
    }
}

/* Phase 7 Task 7.7 step 3 — ModifyObjCountByHistoryUW.
 *
 * NES source: reference/aldonunez/Z_05.asm:3982-4034.
 * Drained C:  NONE.  Stance: GREENFIELD.
 *
 * UW kill-tracking uses LevelKillCounts[room] (per-room byte at $0560+room)
 * and flag-byte bits 6-7 ($C0 = "all defeated"). Recurring-foe template
 * IDs ($00-$31, $3A, $3B, $49+) replenish on re-entry; non-recurring
 * (bosses, items, NPCs) stay cleared.
 */
static void modify_count_by_history_uw(unsigned char room_id,
                                       unsigned char *template_io,
                                       unsigned char *count_io)
{
    unsigned char in_history = 0u;
    signed char y;
    for (y = 5; y >= 0; --y) {
        if ((unsigned char)ROOM_HISTORY((unsigned char)y) == room_id) {
            in_history = 1u;
            break;
        }
    }
    if (!in_history) {
        unsigned char flags = room_get_room_flags();
        if ((flags & 0xC0u) == 0xC0u) {
            unsigned char tid = *template_io;
            unsigned char recurring = 0u;
            if (tid < 0x32u) recurring = 1u;
            else if (tid == 0x3Au || tid == 0x3Bu) recurring = 1u;
            else if (tid >= 0x49u) recurring = 1u;
            if (!recurring) {
                *template_io = 0u;
                *count_io = 0u;
                return;
            }
            /* Recurring: clear all-defeated bits + zero kill-counts. */
            unsigned int ptr = (unsigned int)(unsigned char)SAVEFILE_PTR_LO
                             | ((unsigned int)(unsigned char)SAVEFILE_PTR_HI << 8);
            nes_ram[ptr + room_id] = (unsigned char)(flags & 0x3Fu);
            RAM(0x0560u + room_id) = 0u;
            return;
        }
        /* Not-all-defeated path falls through to subtract LevelKillCounts. */
    }
    /* @CalcObjCount — subtract LevelKillCounts[room]. */
    unsigned char lkc = (unsigned char)RAM(0x0560u + room_id);
    if (*count_io >= lkc) {
        *count_io = (unsigned char)(*count_io - lkc);
    } else {
        *template_io = 0u;
        *count_io = 0u;
    }
}

/* The object list ID InitMode_EnterRoom leaves in [$02] after the history
 * and cellar adjustments: the AssignObjSpawnPositions parameter. The
 * stored RoomObjTemplateType ($35F) is only written when objects are
 * placed, so it is stale for an empty room. */
static unsigned char s_room_template_id = 0u;

unsigned char enemy_room_template_id(void)
{
    return s_room_template_id;
}

/* NES Z_05.asm:1700-1820 verbatim port of the monster-list parse + ObjType
 * fill. Returns 1 if any objects were loaded, 0 if room is empty.
 *
 * Cellar (mode 9) override IS applied here per NES — those rooms get 0
 * monster-list-id but AssignObjSpawnPositions injects 4 blue keese.
 */
unsigned char enemy_room_load_objects(unsigned char room_id)
{
    /* Step 1 — parse template_id + count from LevelBlockAttrs C/D. */
    unsigned char attr_c = (unsigned char)DUNGEON_LBA_C(room_id);
    unsigned char attr_d = (unsigned char)DUNGEON_LBA_D(room_id);
    unsigned char template_id = (unsigned char)(attr_c & 0x3Fu);
    if (attr_d & 0x80u) {
        template_id = (unsigned char)(template_id + 0x40u);
    }
    unsigned char foe_idx = (unsigned char)((attr_c >> 6) & 0x03u);
    unsigned char count = (unsigned char)DUNGEON_LEVEL_FOE_COUNTS(foe_idx);

    /* NES Z_05.asm:1727 — boss / non-recurring foe override: count = 1. */
    if (template_id >= 0x32u && template_id < 0x62u) {
        count = 1u;
    }

    /* Phase 7 Task 7.7 step 3 — ModifyObjCountByHistory{OW,UW}.
     * NES Z_05.asm:1740-1746 dispatches by CurLevel. Adjusts count
     * + template_id based on RoomHistory + per-room kill flags so
     * re-entry to a partially / fully cleared room spawns the
     * surviving subset (or zero monsters). */
    if ((unsigned char)CUR_LEVEL == 0u) {
        modify_count_by_history_ow(room_id, &template_id, &count);
    } else {
        modify_count_by_history_uw(room_id, &template_id, &count);
    }

    /* NES Z_05.asm:1750 — mode 9 cellar override. nes_ram is the
     * a4-register substrate base from src/abi/platform_abi.h. */
    unsigned char game_mode = nes_ram[0x0012u];
    if (game_mode == 0x09u) {
        template_id = 0u;
        count = 0u;
    }

    /* Step 7 — store count + skip if empty. */
    s_room_template_id = template_id;
    DUNGEON_ROOM_OBJ_COUNT = count;
    if (count == 0u || template_id == 0u) {
        return 0u;
    }

    /* Step 7a — fill ObjType array. */
    if (template_id < 0x62u) {
        /* Repeat single template across slots 1..count. */
        unsigned char x;
        for (x = 0u; x < count; ++x) {
            OBJ(NES_OBJ_TYPE, (unsigned int)(x + 1u)) = template_id;
        }
    } else {
        /* Copy ObjList[N] into slots 1..count. */
        const unsigned char *src = obj_list_for_template(template_id);
        if (src == (const unsigned char *)0) {
            DUNGEON_ROOM_OBJ_COUNT = 0u;
            return 0u;
        }
        unsigned char y;
        for (y = 0u; y < count; ++y) {
            OBJ(NES_OBJ_TYPE, (unsigned int)(y + 1u)) = src[y];
        }
    }

    /* Step 8 — RoomObjTemplateType = first template. */
    DUNGEON_ROOM_TEMPLATE_TYPE = (unsigned char)OBJ(NES_OBJ_TYPE, 1u);
    return 1u;
}

/* Phase 7 Task 7.7 step 2 — AssignObjSpawnPositions verbatim port.
 *
 * NES source: reference/aldonunez/Z_05.asm:1885-1996.
 * Drained C:  NONE (room-init spawn-coord assignment not drained).
 * Coverage:   NONE
 * Stance:     GREENFIELD (legal — drain_coverage.json has no candidate row).
 *
 * Per-direction spawn lists (from Z_05.asm:1431-1445), 9 entries each.
 * Each byte packs (col<<4)|row in nibbles → ObjX = col*16, ObjY = (row<<4)|$0D.
 *
 * NES bit-scan @ 1906-1911: ObjDir is 1-of {$01,$02,$04,$08} (R/L/D/U) —
 * Y = bit-position of LSB → 0..3 indexes spawn list.
 */
static const unsigned char spawn_pos_list_0[9] = {
    0x55u, 0xB5u, 0x78u, 0x98u, 0x7Au, 0x9Au, 0x6Cu, 0xACu, 0x8Du
};
static const unsigned char spawn_pos_list_1[9] = {
    0x82u, 0x63u, 0xA3u, 0x75u, 0x95u, 0x77u, 0x97u, 0x5Au, 0xBAu
};
static const unsigned char spawn_pos_list_2[9] = {
    0xA3u, 0x75u, 0xB5u, 0x96u, 0x87u, 0x99u, 0x7Au, 0xBAu, 0xACu
};
static const unsigned char spawn_pos_list_3[9] = {
    0x63u, 0x55u, 0x95u, 0x76u, 0x88u, 0x79u, 0x5Au, 0x9Au, 0x6Cu
};
static const unsigned char *const spawn_pos_lists[4] = {
    spawn_pos_list_0, spawn_pos_list_1, spawn_pos_list_2, spawn_pos_list_3,
};

/* NES CellarKeeseXs @ Z_05.asm:1876-1880. */
static const unsigned char cellar_keese_xs[4] = { 0x20u, 0x60u, 0x90u, 0xD0u };
static const unsigned char cellar_keese_ys[4] = { 0x9Du, 0x5Du, 0x7Du, 0x9Du };

/* NES FindNextEdgeSpawnCell (Z_05.asm:3406) + the InitMonsterFromEdge
 * coordinate extraction (Z_07.asm:5509). CurEdgeSpawnCell $525 packs the
 * square column (low nibble) and row (high nibble). Step one square
 * counterclockwise until the play-area tile is < $84, or back at the start
 * cell (which is then kept). T-012: the old port sampled the collision
 * probe at the object position, folded IsDistanceSafeToSpawn into the
 * search and clamped the start cell; the NES tests the distance once, in
 * InitObject, after the cell is stored. */
void enemy_edge_spawn_next(unsigned int slot)
{
    const unsigned char start = (unsigned char)RAM(0x0525u);
    unsigned char cell = start;
    for (;;) {
        const unsigned char low = (unsigned char)(cell & 0x0Fu);
        unsigned char hi;
        if (low == 0u) cell = (unsigned char)(cell + 0x10u);
        else if (low == 0x0Fu) cell = (unsigned char)(cell - 0x10u);
        hi = (unsigned char)(cell & 0xF0u);
        if (hi == 0xE0u) cell++;
        else if (hi == 0x40u) cell--;
        {
            /* FetchTileMapAddr + AddToInt16At0: [00:01] -> the column. */
            const unsigned short addr = (unsigned short)(NES_PLAY_AREA_BASE +
                (unsigned short)(cell & 0x0Fu) * (2u * NES_TILE_COL_STRIDE));
            /* 8-bit SEC / SBC #$40 / LSR x3. */
            const unsigned char row = (unsigned char)((unsigned char)((cell & 0xF0u) - 0x40u) >> 3);
            RAM(0x0000u) = (unsigned char)(addr & 0xFFu);
            RAM(0x0001u) = (unsigned char)(addr >> 8);
            if ((unsigned char)RAM(addr + row) < 0x84u)
                break;
        }
        if (cell == start) break;
    }
    RAM(0x000Au) = cell;                       /* [0A]: the search cell */
    RAM(0x0525u) = cell;
    ENEMY_X(slot) = (unsigned char)((cell & 0x0Fu) << 4);
    ENEMY_Y(slot) = (unsigned char)((cell & 0xF0u) - 3u);
}

/* NES Abs of an 8-bit difference (SBC then Abs): wraps, so Link at X 0
 * and a monster at X $F0 are $10 apart. */
static unsigned char nes_abs_sub(unsigned char a, unsigned char b)
{
    unsigned char d = (unsigned char)(a - b);
    return (d & 0x80u) ? (unsigned char)(0u - d) : d;
}

/* NES IsDistanceSafeToSpawn (Z_05.asm): 1 when Link is at least $22 away
 * on either axis. */
unsigned char enemy_edge_distance_safe(unsigned int slot)
{
    if (nes_abs_sub((unsigned char)OBJ(NES_OBJ_X, 0u), (unsigned char)ENEMY_X(slot)) >= 0x22u)
        return 1u;
    return (unsigned char)(nes_abs_sub((unsigned char)OBJ(NES_OBJ_Y, 0u),
                                       (unsigned char)ENEMY_Y(slot)) >= 0x22u);
}

/* NES Z_05.asm:2006 IsSafeToSpawn — returns 1 if unsafe, 0 if safe. */
static unsigned char is_safe_to_spawn(unsigned int slot)
{
    (void)collision_get_collidable_tile_still(slot);
    unsigned char tile  = (unsigned char)ENEMY_COLLIDED_TILE(slot);
    unsigned char floor = (unsigned char)ENEMY_DUNGEON_TILE_FLOOR;
    if (tile >= floor) return 1u;
    unsigned char dx = nes_abs_sub((unsigned char)OBJ(NES_OBJ_X, 0u),
                                   (unsigned char)OBJ(NES_OBJ_X, slot));
    if (dx >= 0x22u) return 0u;
    unsigned char dy = nes_abs_sub((unsigned char)OBJ(NES_OBJ_Y, 0u),
                                   (unsigned char)OBJ(NES_OBJ_Y, slot));
    if (dy < 0x22u) return 1u;
    return 0u;
}

/* NES bit-scan: ObjDir = $01/$02/$04/$08 → idx = 0/1/2/3. */
static unsigned char dir_to_spawn_list_index(unsigned char dir)
{
    unsigned char idx = 0u;
    while (idx < 3u && (dir & 0x01u) == 0u) {
        dir = (unsigned char)(dir >> 1);
        ++idx;
    }
    return idx;
}

void enemy_assign_spawn_positions(unsigned char room_id, unsigned char template_id)
{
    /* NES SetupObjRoomBounds (Z_05.asm:6409) writes 5 bytes per scene
     * from ObjectRoomBoundsOW/UW into RoomBoundLeft/Right/Up/Down +
     * ObjectFirstUnwalkableTile.
     *
     *   ObjectRoomBoundsOW: $11 $E0 $4E $CD $89
     *   ObjectRoomBoundsUW: $21 $D0 $5E $BD $78
     *
     * OW FirstUnwalkableTile = $89 (NES truth, verified by
     * build/probes/nes_spawn_67_exact.lua dump at room $67).
     * UW = $78. Our prior $C0/$A0 was wrong — caused IsSafeToSpawn to
     * accept too many tiles (e.g. cliffs / water at $89..$BF count as
     * walkable when they should block), shifting spawn positions and
     * letting projectiles fly through unwalkable tiles. */
    if ((unsigned char)CUR_LEVEL == 0u) {
        ENEMY_DUNGEON_TILE_FLOOR = 0x89u;  /* OW per NES ObjectRoomBoundsOW[4] */
    } else {
        ENEMY_DUNGEON_TILE_FLOOR = 0x78u;  /* UW per NES ObjectRoomBoundsUW[4] */
    }

    /* Y holds the running cycle index — first set to RoomObjCount per
     * NES line 1886 (governs cellar Y-pick when we skip the main loop). */
    unsigned char y_cycle = (unsigned char)DUNGEON_ROOM_OBJ_COUNT;
    unsigned char skip_main = 0u;

    /* NES line 1890-1893: skip if template == 0 or Zelda ($37). */
    if (template_id == 0u || template_id == 0x37u) skip_main = 1u;

    /* NES line 1896-1900: OW rooms with monsters from the edges (LBA_F
     * bit 3) skip the spawn lists; InitObject places each monster at an
     * edge cell on its first update (enemy_loop_init_object). */
    if (!skip_main && (unsigned char)CUR_LEVEL == 0u &&
        (DUNGEON_LBA_F(room_id) & 0x08u) != 0u)
        skip_main = 1u;

    /* NES line 1902-1903: redundant count==0 gate (already covered above). */
    if (!skip_main && (unsigned char)DUNGEON_ROOM_OBJ_COUNT == 0u) skip_main = 1u;

    if (!skip_main) {
        /* NES Z_05.asm:1906 reads `LDA ObjDir` (Variables.inc:51:
         * ObjDir := $98), i.e. nes_ram[$0098] = Link's facing direction.
         * Earlier code used OBJ(NES_OBJ_DIR, 0u) which resolves to
         * nes_ram[$000F] — the ZP_TMPF scratch dir cell, NOT Link's
         * ObjDir array. Scratch usually held a stale value, so the
         * spawn-list index was effectively random.
         * Read the right cell ($0098 = Link's slot-0 ObjDir). */
        unsigned char list_idx =
            dir_to_spawn_list_index((unsigned char)nes_ram[0x0098u]);
        const unsigned char *list = spawn_pos_lists[list_idx];
        unsigned char y = (unsigned char)DUNGEON_SPAWN_CYCLE;
        unsigned int x;
        {
            unsigned int iter_cap = 0u;
            for (x = 1u; x < 0x0Au; ) {
                unsigned char b = list[y];
                OBJ(NES_OBJ_X, x) = (unsigned char)((b & 0x0Fu) << 4);
                OBJ(NES_OBJ_Y, x) = (unsigned char)((b & 0xF0u) | 0x0Du);
                unsigned char unsafe = is_safe_to_spawn(x);
                if (!unsafe) {
                    ++x;
                }
                ++y;
                if (y >= 9u) y = 0u;
                /* Defense: cap retries so a state with no safe spot
                 * can't spin forever. The NES loop has no cap and hangs
                 * there (NMI off, measured: OW screen $07 staged with
                 * Link on the start spot $78,$8D). No real room entry
                 * reaches that state, so the cap never fires in play. */
                if (++iter_cap >= 90u) {
                    ++x;
                    ++y;
                    if (y >= 9u) y = 0u;
                    iter_cap = 0u;
                }
            }
        }
        DUNGEON_SPAWN_CYCLE = y;
        y_cycle = y;
    }

    /* NES @AssignSpecialPositions @ Z_05.asm:1946. */
    unsigned char game_mode = nes_ram[0x0012u];
    if (game_mode == 0x09u) {
        /* Cellar — 4 blue keese. NES bug-feature: cellar_keese_ys[Y]
         * uses Y-cycle-index, NOT loop X. When mode 9 hits via the
         * step-1 (template=count=0) override, y_cycle = 0, so all 4
         * keese share Y = $9D. */
        unsigned char y_idx_clamped = (y_cycle < 4u) ? y_cycle : 0u;
        int xx;
        for (xx = 3; xx >= 0; --xx) {
            OBJ(NES_OBJ_TYPE, (unsigned int)(xx + 1)) = 0x1Bu;
            OBJ(NES_OBJ_X,    (unsigned int)(xx + 1)) = cellar_keese_xs[xx];
            OBJ(NES_OBJ_Y,    (unsigned int)(xx + 1)) = cellar_keese_ys[y_idx_clamped];
        }
        /* RoomObjCount stays 0 (mode 9 zeroed it above): SaveKillCountUW
         * then marks the cellar cleared, as on the NES (T-171 lockstep,
         * L1 bow cellar). */
        return;
    }

    /* NES @CheckCaves @ Z_05.asm:1964 — modes $0B/$0C inject cave
     * dweller into slot 1. CurLevel == 0 (OW caves) only. */
    if (game_mode == 0x0Bu || game_mode == 0x0Cu) {
        unsigned int xx;
        for (xx = 1u; xx <= 8u; ++xx) {
            OBJ(NES_OBJ_TYPE, xx) = 0u;
        }
        unsigned char lba_b = (unsigned char)(DUNGEON_LBA_B(room_id) & 0xFCu);
        /* SBC #$40 → wraparound subtraction on u8. */
        unsigned char cave_idx = (unsigned char)((lba_b - 0x40u) >> 2);
        OBJ(NES_OBJ_TYPE, 1u) = (unsigned char)(0x6Au + cave_idx);
        /* RoomObjCount keeps the OW room's count (NES writes only
         * ObjType+1 here). */
    }
}
