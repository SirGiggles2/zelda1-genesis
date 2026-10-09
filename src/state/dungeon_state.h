#ifndef DUNGEON_STATE_H
#define DUNGEON_STATE_H

#include "platform_abi.h"
#include "scratch_state.h"

/* Phase 5 Task 5.1: Dungeon State Model.
 *
 * NES source: Variables.inc (address ground truth).
 * Drained C:  NONE (state model — no drained function candidate).
 * Coverage:   N/A (substrate #define mapping only).
 * Stance:     ADOPT — pure NES address mapping, no logic.
 *
 * All addresses verified against reference/aldonunez/Variables.inc.
 * Working RAM at $0000-$07FF; SRAM at $6000-$7FFF mapped through nes_ram[].
 * CUR_LEVEL and CUR_ROOM_ID live in progress_state.h / world_state.h
 * (already defined; included here for documentation completeness).
 */

/* ---- Current quest --------------------------------------------------------
 * NES $062D: QuestNumbers[save_slot] — 0 = first quest, non-0 = second quest.
 * Save slot 0 is at $062D, slot 1 at $062E, slot 2 at $062F.
 * Use DUNGEON_CUR_QUEST(slot) for the active save slot, or
 * DUNGEON_QUEST_NUMBER for the live working-RAM copy (same cell when slot 0).
 */
#define DUNGEON_QUEST_NUMBER(slot)   RAM(0x062D + (unsigned char)(slot))

/* ---- Triforce / compass / map ownership -----------------------------------
 * NES Variables.inc:
 *   InvTriforce   = $671 — bit N set means triforce piece N+1 owned (9 bits)
 *   InvCompass    = $667 — compass owned per level (first quest), bit N = level N+1
 *   InvMap        = $668 — map owned per level (first quest)
 *   InvCompass9   = $669 — compass owned per level (second quest)
 *   InvMap9       = $66A — map owned per level (second quest)
 *   InvKeys       = $66E — current key count
 */
#define DUNGEON_INV_TRIFORCE         RAM(0x0671)
#define DUNGEON_INV_COMPASS          RAM(0x0667)
#define DUNGEON_INV_MAP              RAM(0x0668)
#define DUNGEON_INV_COMPASS9         RAM(0x0669)
#define DUNGEON_INV_MAP9             RAM(0x066A)
#define DUNGEON_INV_KEYS             RAM(0x066E)

/* ---- Room visit / clear / door flags (WorldFlags) -------------------------
 * NES $067F: WorldFlags[128] — one byte per room, packed:
 *   bit 5 = room visited (UW)
 *   bit 3 = room cleared (all monsters dead)
 *   bit 0 = locked/bombed door opened OR item already taken
 *
 * LevelInfo_WorldFlagsAddr ($6BAF/$6BB0) is a pointer (lo/hi) into SRAM
 * that gives the start of the 128-byte world-flags block for the current
 * level.  Access via NES_SRAM_ROOM_FLAGS_PTR_LO/HI (platform_abi.h).
 *
 * WorldKillCount ($627): running kill count for the current OW/UW world.
 * LevelKillCounts ($560): per-level kill counts (10 bytes, one per level).
 */
#define DUNGEON_WORLD_FLAGS(room)    RAM(0x067F + (unsigned char)(room))
#define DUNGEON_WORLD_KILL_COUNT     RAM(0x0627)
#define DUNGEON_LEVEL_KILL_COUNT(lv) RAM(0x0560 + (unsigned char)(lv))
#define DUNGEON_ROOM_FLAGS_PTR_LO    RAM(NES_SRAM_ROOM_FLAGS_PTR_LO)
#define DUNGEON_ROOM_FLAGS_PTR_HI    RAM(NES_SRAM_ROOM_FLAGS_PTR_LO + 1u)

/* World-flags bit masks */
#define DUNGEON_WFLAG_VISITED        0x20u
#define DUNGEON_WFLAG_CLEARED        0x08u
#define DUNGEON_WFLAG_DOOR_OPENED    0x01u

/* ---- LevelInfo (SRAM-backed level descriptor) ----------------------------
 * Loaded on level transition from save data.  All offsets from NES $6B7E.
 * Variables.inc:
 *   LevelInfo_FoeCounts           = $6BA2 (10 bytes, foe count per room-group)
 *   LevelInfo_StartY              = $6BA6
 *   LevelInfo_ShortcutOrItemPosArray = $6BA7
 *   LevelInfo_SubmenuMapRotation  = $6BAB
 *   LevelInfo_StatusBarMapXOffset = $6BAC
 *   LevelInfo_StartRoomId         = $6BAD
 *   LevelInfo_TriforceRoomId      = $6BAE
 *   LevelInfo_WorldFlagsAddr lo   = $6BAF  (see NES_SRAM_ROOM_FLAGS_PTR_LO)
 *   LevelInfo_WorldFlagsAddr hi   = $6BB0
 *   LevelInfo_LevelNumber         = $6BB1
 */
#define DUNGEON_LEVEL_FOE_COUNTS(i)  RAM(0x6BA2 + (unsigned char)(i))
#define DUNGEON_LEVEL_START_Y        RAM(0x6BA6)
#define DUNGEON_LEVEL_START_ROOM     RAM(0x6BAD)
#define DUNGEON_LEVEL_TRIFORCE_ROOM  RAM(0x6BAE)
#define DUNGEON_LEVEL_NUMBER         RAM(0x6BB1)

/* ---- LevelBlockAttrs cache ------------------------------------------------
 * LevelBlockAttrsByteF ($04CD per Variables.inc:135): cached byte from
 * LevelBlockAttrsF[room_id] for the current room. Set once on room entry
 * (InitMode_EnterRoom).
 *
 * F-byte bit-meaning is NOT the dark-room flag (Task 5.8 ground-truth
 * 2026-05-07 corrected an earlier wrong comment).
 *
 * Authoritative dark-room predicate: `IsDarkRoom_Bank5` at
 * reference/aldonunez/Z_05.asm:7795 reads `LevelBlockAttrsE, Y` &
 * `$80` — bit 7 of the AttrsE TABLE (not the cached F-byte).
 *
 * AttrsE base in NES SRAM: `$6A7E` (Variables.inc:328); RoomRom-side
 * SRAM-relative offset is `$0A7E + room_id`. The drained
 * src/game/room/room_dispatch.c:75 `room_is_dark_room()` reads this
 * exact location. NOTE: standalone RoomRom has only 2 KB nes_ram
 * (RoomRom/src/boot/nes_ram_init.c:25), so $0A7E is OUT OF BOUNDS
 * there — RoomRom must use the generated `uw_dark_rooms` master
 * table at runtime; the dispatch fn is safe to call only on host
 * environments with full NES SRAM mapped.
 */
#define DUNGEON_ATTRS_BYTE_F         RAM(0x04CD)
#define DUNGEON_ATTRS_E_BASE         0x0A7Eu
#define DUNGEON_ATTRS_E_DARK_BIT     0x80u

/* ---- Object / monster room state -----------------------------------------
 * Variables.inc:
 *   RoomObjCount       = $34E — number of monsters placed this room
 *   RoomObjTemplateType = $35F — object template type (first object type)
 *   SpawnCycle         = $524 — cycling index into spawn position list
 */
#define DUNGEON_ROOM_OBJ_COUNT       RAM(0x034E)
#define DUNGEON_ROOM_TEMPLATE_TYPE   RAM(0x035F)
#define DUNGEON_SPAWN_CYCLE          RAM(0x0524)

/* ---- Door / transition state ----------------------------------------------
 * Variables.inc:
 *   DoorwayDir         = $53 — direction Link entered from (bitmask)
 *   UndergroundExitType = $5A — 0 = entered walking; non-0 = exiting cave
 *   ShutterTrigger     = $4CE — shutter door arming flag (ROOM_SHUTTER_TRIGGERED)
 *   BlockPushComplete  = $4CF — push-block triggered secret (ROOM_BLOCK_SECRET_FLAG)
 *   CurOpenedDoors     = $EE  — bitmask of doors opened this room (CUR_OPENED_DOORS)
 *   TriggeredDoorCmd   = $54  — door command from collision (ROOM_OPEN_DOOR_TIMER)
 *   TriggeredDoorDir   = $55  — direction of triggered door  (ROOM_OPEN_DOOR_ARG)
 *
 * Note: ShutterTrigger, BlockPushComplete, CurOpenedDoors, TriggeredDoorCmd,
 * and TriggeredDoorDir already have aliases in room_state.h.  Listed here
 * for cross-reference completeness.
 */
#define DUNGEON_DOORWAY_DIR          RAM(0x0053)
#define DUNGEON_UNDERGROUND_EXIT     RAM(0x005A)

/* ---- Dark room / candle state --------------------------------------------
 * Variables.inc:
 *   BrighteningRoom = $51E — set when candle/lantern has been used in room
 *   CandleState     = $51F — candle animation state (same cell as ROOM_SCROLL_STATE)
 */
#define DUNGEON_BRIGHTENING_ROOM     RAM(0x051E)

/* ---- Save file SRAM layout (per slot) ------------------------------------
 * World-flags SRAM base per save slot, first quest:
 *   Slot 0: $6092 (SaveFileAWorldFlags0)
 *   Slot 1: $6212 (SaveFileAWorldFlags1)
 *   Slot 2: $6392 (SaveFileAWorldFlags2)
 * Each block is 128 bytes × 9 levels = 1152 bytes.
 *
 * Quest number SRAM per slot:
 *   Slot 0: $651B (SaveFileAQuestNumber0)
 *   Slot 1: $651C
 *   Slot 2: $651D
 */
#define DUNGEON_SRAM_WORLD_FLAGS_BASE_S0  0x6092u
#define DUNGEON_SRAM_WORLD_FLAGS_BASE_S1  0x6212u
#define DUNGEON_SRAM_WORLD_FLAGS_BASE_S2  0x6392u
#define DUNGEON_SRAM_QUEST_NUMBER_S0      0x651Bu
#define DUNGEON_SRAM_QUEST_NUMBER_S1      0x651Cu
#define DUNGEON_SRAM_QUEST_NUMBER_S2      0x651Du
#define DUNGEON_SRAM_WORLD_FLAGS_STRIDE   0x0180u  /* 128 rooms × 1 byte */

/* ---- LevelBlockAttrs SRAM tables (128 bytes each) ------------------------
 * LevelBlockAttrsA = $687E — room layout variant A
 * LevelBlockAttrsB = $68FE — room layout B: cave field [7:2], inner/outer pal [1:0]
 * LevelBlockAttrsC = $697E — monster list ID
 * LevelBlockAttrsD = $69FE — monster count
 * LevelBlockAttrsE = $6A7E — room item type
 * LevelBlockAttrsF = $6AFE — general flags (dark room bit 4, edge-spawn bit 3, …)
 */
#define DUNGEON_LBA_A(room)          RAM(0x687Eu + (unsigned char)(room))
#define DUNGEON_LBA_B(room)          RAM(0x68FEu + (unsigned char)(room))
#define DUNGEON_LBA_C(room)          RAM(0x697Eu + (unsigned char)(room))
#define DUNGEON_LBA_D(room)          RAM(0x69FEu + (unsigned char)(room))
#define DUNGEON_LBA_E(room)          RAM(0x6A7Eu + (unsigned char)(room))
#define DUNGEON_LBA_F(room)          RAM(0x6AFEu + (unsigned char)(room))

/* ---- RoomRom bridge state ------------------------------------------------
 * RoomRom (RoomRom/src/main.c) maintains its own scene/room/redux state:
 *   s_scene    — SCENE_UW / SCENE_OW / SCENE_CAVE
 *   s_room_id  — current room (mirrors NES CUR_ROOM_ID while in RoomRom)
 *   s_redux    — palette/CHR redux toggle
 *
 * These are RoomRom-private.  Genesis substrate code reads/writes the
 * canonical NES RAM cells (CUR_ROOM_ID = RAM(0x00EB), CUR_LEVEL =
 * RAM(0x0010)) and expects RoomRom to sync from those on scene load.
 * No additional bridge macros needed here until Phase 12 promotion.
 */

#endif /* DUNGEON_STATE_H */
