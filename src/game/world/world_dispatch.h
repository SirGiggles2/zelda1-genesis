/* world_dispatch.h — native overworld + object subsystem dispatch
 * (debate 006 D2 follow-up; Phase 4 entry point per master plan).
 *
 * Both ROMs link this. Debug.md (post-cutover) replaces
 * src/gen/z_01.c worldrt_* / objrt_* / sprrt_* / progrt_* / trprt_*
 * callsites with native equivalents from src/game/world/. RoomRom
 * calls directly from main.c when SCENE_OW logic ports.
 *
 * Native impl mirrors src/oracle/world/ reference (drained C, MATCH-
 * verified per per-function findings) and reference/aldonunez/Z_*.asm
 * spec. NO transpile-bridge shims (z01_/z07_/c_/progrt_/objrt_/
 * sprrt_) — pure C + src/state/world_state.h typed accessors +
 * src/sgdk_adapter/ render API.
 *
 * Phase 4 first port: world_get_object_middle (smallest, no shims).
 * Subsequent ports per Phase 3 D1 loop: per-function diff against
 * NES asm before commit, Gate 1 finding doc per port.
 */

#ifndef WORLD_DISPATCH_H
#define WORLD_DISPATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Compute the (X+8, Y+8) middle pixel of an object slot — or
 * (X+4, Y+8) if ObjAttr bit $40 is set (half-width). Result lands in
 * WORLD_TMP2 (mid-X) + WORLD_TMP3 (mid-Y). NES GetObjectMiddle at
 * Z_01.asm:5498. Used by collision detection in subsequent ports. */
void world_get_object_middle(unsigned int slot);
/* FillTileMap: PlayAreaTiles ($6530..$67EF) := [0A]; [00:01] = $67F0. */
void world_fill_tile_map(void);

/* Maze step-tracker for the forest ($61) and mountain ($1B) overworld
 * mazes. Reads LINK_DIR + CUR_ROOM_ID + WORLD_MAZE_STEP, advances or
 * resets the step, and either lets Link exit (right in forest, left in
 * mountain) or pins the next room to current. Plays "secret found"
 * tune (Tune1Request = 4 = WORLD_SECRET_SFX) on the 4th matching step.
 *
 * Mirrors NES CheckMazes (Z_01.asm:4791). Drain at
 * src/oracle/world/world_runtime.c:68-109. Pure C, no shims. */
void world_check_mazes(void);

/* Decode the (X, Y) screen coordinate for the shortcut (overworld) or
 * item (underworld) of the given room, packed as `(X << 8) | Y` so a
 * single unsigned int return carries both. Mirrors NES
 * GetShortcutOrItemXYForRoom (Z_01.asm:4002). Reads the per-room
 * attribute byte from `LevelBlockAttrsF` (NES SRAM offset $6AFE +
 * room_id), extracts a 2-bit type index from bits 4-5, and indexes
 * `LevelInfo_ShortcutOrItemPosArray` at $6BA7 to get the packed
 * position byte (high nibble = X*16, low nibble = Y/16). */
unsigned int world_get_shortcut_or_item_xy_for_room(unsigned int room_id);

/* No-arg variant: looks up the shortcut/item XY for the current room
 * (CUR_ROOM_ID = $00EB). Mirrors NES GetShortcutOrItemXY (Z_01.asm:3993). */
unsigned int world_get_shortcut_or_item_xy(void);

/* Stream one frame of the overworld palette-fade animation into the
 * dynamic transfer buffer. Called per VBlank from the world tick.
 *
 *   WORLD_FADE_TIMER ($0034 = ObjTimer+12) gates: nonzero returns 1
 *     (in-progress, no transfer this frame).
 *   WORLD_FADE_STEP  ($051C = FadeCycle) drives table indexing
 *     (forward + reverse via bit-7 mirror via XOR $83).
 *
 * Builds a transfer-buf record:
 *   [pos]   = $3F        ; PPU address high (palette area)
 *   [pos+1] = $08        ; PPU address low  (bottom half BG palette)
 *   [pos+2] = $08        ; record length
 *   [pos+3..10] = LevelInfo_PaletteCycles[sram_idx..+7]   (SRAM $6BFA)
 *   [pos+11] = $FF       ; end marker
 *
 * Returns 1 while fade is in progress; 0 when the cycle (step & $0F)
 * reaches 4 (one full quarter of the cycle complete = frame done).
 *
 * Mirrors NES AnimateWorldFading (Z_01.asm:4701). */
unsigned int world_animate_world_fading(void);

#ifdef __cplusplus
}
#endif

#endif /* WORLD_DISPATCH_H */
