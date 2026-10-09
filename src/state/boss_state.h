#ifndef BOSS_STATE_H
#define BOSS_STATE_H

#include "platform_abi.h"
#include "enemy_state.h"
#include "room_state.h"

/* Phase 8 Task 8.1 — Boss Framework state model.
 *
 * NES source: Variables.inc + CommonVars.inc (address ground truth);
 *             Z_05.asm:8154-8250 (CreateRoomObjects);
 *             Z_07.asm:771-820 (MoveAndDrawRoomItem / room item slot 19);
 *             Z_07.asm:5453 (RoomKillCount inc).
 * Drained C:  NONE (state model — no drained function candidate).
 * Coverage:   N/A (substrate #define mapping only).
 * Stance:     ADOPT — pure NES address mapping; bodies live in
 *             src/game/enemies/bosses/boss_framework.c.
 *
 * All addresses verified against reference/aldonunez/Variables.inc
 * (`RoomItemId := $AB`) and CommonVars.inc (`ObjRoomItemId := $98`,
 * `RoomKillCount := $34F`). Working RAM at $0000-$07FF mapped through
 * nes_ram[]; SRAM at $6000-$7FFF used by LBA tables (already aliased
 * in dungeon_state.h).
 *
 * Boss room data has two surfaces:
 *   1. The boss enemy itself in slots 1..6 — already covered by
 *      enemy_state.h (ENEMY_BOSS_HP_PHASE, ENEMY_GLEEOK_*,
 *      ENEMY_MANHANDLA_*, ENEMY_LAMNOLA_*, ENEMY_GOHMA_*,
 *      ENEMY_DIGDOGGER_COUNT, ENEMY_STATUE_*, etc.) — this header
 *      does NOT redefine those.
 *   2. The room-item reward in object slot 19 — heart container,
 *      triforce piece, or per-room L-block reward. NES uses ObjState
 *      ($98+19) + ObjType ($EB+19) + ObjX ($70+19) + ObjY ($84+19).
 *      Slot 19 is the persistent room-item slot.
 */

/* ---- Constants ----------------------------------------------------- */

/* Persistent room-item slot per Z_07.asm:813 ("Room item is in object
 * slot $13") = decimal 19. Used by MoveAndDrawRoomItem,
 * AnimateRoomItemObject, CreateRoomObjects. */
#define BOSS_ROOM_ITEM_SLOT          19u

/* Room item type "none" sentinel per Z_07.asm:797. RoomItemId == $3F
 * means no item dropped; MoveAndDrawRoomItem returns early. */
#define BOSS_ROOM_ITEM_NONE          0x3Fu

/* Room item types referenced by Z_05.asm:8230 (heart container OW),
 * Z_05.asm:8216 (triforce piece UW), and the LevelBlockAttrsE table.
 * Authoritative item IDs from reference/aldonunez/Z_07.asm
 * @ItemIdToDescriptor table. */
#define BOSS_ITEM_ID_HEART_CONTAINER 0x1Au
#define BOSS_ITEM_ID_TRIFORCE_PIECE  0x1Bu
#define BOSS_ITEM_ID_MASTER_SWORD_PLACEHOLDER 0x03u  /* Z_05.asm:8174 — "no item" stand-in for L-block attr */

/* OW heart-container room is $5F per Z_05.asm:8244 (`CPX #$5F`). */
#define BOSS_OW_HEART_CONTAINER_ROOM 0x5Fu
#define BOSS_OW_HEART_CONTAINER_X    0xC0u  /* Z_05.asm:8235 */
#define BOSS_OW_HEART_CONTAINER_Y    0x90u  /* Z_05.asm:8236 */
#define BOSS_GAMEMODE_PLAY           0x05u  /* Z_05.asm:8240 — only mode 5 spawns the OW heart */

/* LevelBlockAttrsE/F bit masks for room-item + secret-trigger fields.
 * Z_05.asm:8178 (AND #$1F room item), Z_05.asm:8189 (AND #$07 secret),
 * Z_05.asm:8201 (AND #$40 push block in LBA_D). */
#define BOSS_LBA_E_ITEM_MASK         0x1Fu
#define BOSS_LBA_F_SECRET_MASK       0x07u
#define BOSS_LBA_D_PUSH_BLOCK_MASK   0x40u
#define BOSS_LBA_E_TRIFORCE_X_OFFSET 0x08u   /* Z_05.asm:8220 — triforce shifts left 8px */

/* Secret-trigger codes per Z_05.asm:8190-8193. 3 = "last boss" item
 * gated by boss death; 7 = "foes for item" gated by RoomKillCount. */
#define BOSS_SECRET_TRIGGER_LAST_BOSS  0x03u
#define BOSS_SECRET_TRIGGER_FOES_ITEM  0x07u

/* ---- Per-room reward state (object slot 19) ----------------------- */

/* RoomItemId — global type code of the current room-item reward.
 * NES Variables.inc:52 `RoomItemId := $AB`. Set by CreateRoomObjects;
 * read by MoveAndDrawRoomItem / AnimateRoomItemObject. */
#define BOSS_ROOM_ITEM_ID            RAM(0x00ABu)

/* ObjState[19] — room-item activation flag.
 *   $00 = active (visible, ready to take)
 *   $FF = deactivated (already taken, or "none")
 * Z_05.asm:8157 stores 0; Z_05.asm:8181/8195/8249 decrements (->$FF).
 * Uses the canonical ObjState alias from enemy_state.h:
 * 0x00AC + 19 = 0x00BF. Keep separate from RoomItemId ($00AB). */
#define BOSS_ROOM_ITEM_STATE         ENEMY_STATE_TIMER(BOSS_ROOM_ITEM_SLOT)

/* ObjType[19] / ObjX[19] / ObjY[19] — room-item coords. Set by
 * CreateRoomObjects @StoreLocation (Z_05.asm:8208-8210). */
#define BOSS_ROOM_ITEM_TYPE          ENEMY_TYPE(BOSS_ROOM_ITEM_SLOT)
#define BOSS_ROOM_ITEM_X             ENEMY_X(BOSS_ROOM_ITEM_SLOT)
#define BOSS_ROOM_ITEM_Y             ENEMY_Y(BOSS_ROOM_ITEM_SLOT)

/* RoomKillCount — kills accumulated in the current room.
 * NES CommonVars.inc:10 `RoomKillCount := $34F`. Incremented by
 * Z_07.asm:5453 on every monster death; consumed by the secret-trigger
 * @ModifyObjCountByHistoryUW path (Z_05.asm:4042) and the "foes for
 * item" secret (BOSS_SECRET_TRIGGER_FOES_ITEM). */
#define BOSS_ROOM_KILL_COUNT         ROOM_OW_CUR_KILL_TOTAL

/* CurLevel — 0 = OW, 1..9 = UW level number. Used by CreateRoomObjects
 * Z_05.asm:8161 to branch into the OW heart-container path vs the
 * UW per-room-item path. Mirror of CUR_LEVEL in progress_state.h
 * (defined here as alias for clarity within the boss-framework body). */
#define BOSS_CUR_LEVEL               CUR_LEVEL

/* GameMode — Z_05.asm:8240 gates OW heart-container spawn on mode 5
 * (Mode_PlayCellar / play). */
#define BOSS_GAMEMODE                MODE_VALUE

#endif /* BOSS_STATE_H */
