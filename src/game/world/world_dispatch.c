/* world_dispatch.c — native overworld dispatch (Phase 4 entry).
 *
 * Phase 4 first port: world_get_object_middle. Pure C, no shims.
 * Drain MATCH per Gate 1 finding 4_1n_world_get_object_middle.
 */

#include "world_dispatch.h"
#include "world_state.h"      /* WORLD_TMP2/3, OBJ_X/_Y/_STATUS_FLAGS */
#include "platform_abi.h"     /* nes_ram[] direct access for SRAM ($6000+) tables */

/* NES SRAM base. Cartridge data tables (LevelBlockAttrsF,
 * LevelInfo_*) live at $6000+offset and are accessed through nes_ram[]
 * since the bridge layer mirrors SRAM into the same address space. */
#define NES_SRAM_BASE 0x6000u

unsigned int world_get_shortcut_or_item_xy_for_room(unsigned int room_id)
{
    /* NES GetShortcutOrItemXYForRoom (Z_01.asm:4002). Drain at
     * world_runtime.c:6-13. Drain MATCH per finding 4_1n_c.
     *
     *   LDA LevelBlockAttrsF, Y     ; SRAM $6AFE + room_id
     *   AND #$30 / LSR x4           ; isolate bits 4-5 -> type_idx
     *   LDA LevelInfo_ShortcutOrItemPosArray, Y  ; SRAM $6BA7 + type_idx
     *   PHA / AND #$0F / ASL x4     ; low nibble << 4 = Y
     *   PLA / AND #$F0              ; high nibble = X
     *   RTS                          ; A=X, Y=Y
     *
     * Native packs (X, Y) into one return: (X << 8) | Y. */
    const unsigned char lookup =
        nes_ram[NES_SRAM_BASE + 0x0AFEu + (room_id & 0xFFu)];
    const unsigned char type_idx = (unsigned char)((lookup & 0x30u) >> 4);
    const unsigned char entry =
        nes_ram[NES_SRAM_BASE + 0x0BA7u + type_idx];
    const unsigned char y = (unsigned char)((entry & 0x0Fu) << 4);
    const unsigned char x = (unsigned char)(entry & 0xF0u);
    return ((unsigned int)x << 8) | (unsigned int)y;
}

unsigned int world_get_shortcut_or_item_xy(void)
{
    /* NES GetShortcutOrItemXY (Z_01.asm:3993): LDY RoomId / fall-through
     * to GetShortcutOrItemXYForRoom. Native passes CUR_ROOM_ID = RAM($00EB). */
    return world_get_shortcut_or_item_xy_for_room((unsigned int)CUR_ROOM_ID);
}

unsigned int world_animate_world_fading(void)
{
    /* NES AnimateWorldFading (Z_01.asm:4701). Drain at
     * src/oracle/world/world_runtime.c:29-66. Drain MATCH per Gate 1
     * finding 4_1n_d (with one fixed scratch-state divergence noted
     * below).
     *
     * NES uses ZP $00 as both initial val-stash AND loop counter.
     * After the @CopyPalette loop completes, $00 = 0 (decremented to
     * exit). Drain stores 8 to WORLD_TMP0 then uses a local `count`
     * variable, leaving WORLD_TMP0 = 8 — that's a scratch-state
     * divergence vs NES. Native faithfully decrements WORLD_TMP0 in
     * the loop so $00 final value matches NES (= 0). */
    if (WORLD_FADE_TIMER != 0u) {
        return 1u;  /* timer not expired — wait */
    }

    /* Cycle value, with bit-7 mirror for reverse fade. NES `EOR #$83`. */
    unsigned char val = WORLD_FADE_STEP;
    if (val & 0x80u) {
        val = (unsigned char)(val ^ 0x83u);
    }
    WORLD_TMP0 = val;

    /* Encode (val) into SRAM index: ((val << 3) + val) & $FC = val*9
     * with low 2 bits cleared. */
    unsigned char sram_idx =
        (unsigned char)(((unsigned char)(val << 3) + val) & 0xFCu);

    unsigned char pos = TRANSFER_BUF_POS;
    /* Record header: PPU address $3F08, length 8. */
    TRANSFER_BUF_BYTE(pos) = 0x3Fu; pos++;
    TRANSFER_BUF_BYTE(pos) = 0x08u; pos++;
    TRANSFER_BUF_BYTE(pos) = 0x08u; pos++;

    /* Loop counter — NES uses $00 (= WORLD_TMP0). 8 byte palette copy
     * from LevelInfo_PaletteCycles (SRAM $6BFA + sram_idx). */
    WORLD_TMP0 = 8u;
    while (WORLD_TMP0 != 0u) {
        TRANSFER_BUF_BYTE(pos) =
            nes_ram[NES_SRAM_BASE + 0x0BFAu + sram_idx];
        sram_idx = (unsigned char)(sram_idx + 1u);
        pos      = (unsigned char)(pos + 1u);
        WORLD_TMP0 = (uint8_t)(WORLD_TMP0 - 1u);
    }

    /* End marker + commit transfer-buf cursor. */
    TRANSFER_BUF_BYTE(pos) = 0xFFu;
    TRANSFER_BUF_POS = pos;

    /* Advance fade step. (step & $0F) == 4 = quarter-cycle done. */
    WORLD_FADE_STEP = (uint8_t)(WORLD_FADE_STEP + 1u);
    if ((WORLD_FADE_STEP & 0x0Fu) == 4u) {
        return 0u;  /* this frame's fade slice complete */
    }

    /* Continue fade — wait 10 frames. */
    WORLD_FADE_TIMER = 10u;
    return 1u;
}

void world_check_mazes(void)
{
    /* NES CheckMazes (Z_01.asm:4791). Drain at world_runtime.c:68-109.
     * Drain MATCH per finding 4_1n_b. Tables baked in inline:
     *   ForestMazeDirs   = $08, $02, $04, $02 (down, left, right(?), left)
     *   MountainMazeDirs = $08, $08, $08, $08 (down, down, down, down)
     *
     * NES uses ObjDir bits ($08=down, $04=right, $02=left, $01=right —
     * actually 6502 conventions vary; here we follow NES Z_01.asm
     * literal byte values). Drain uses LINK_DIR macro which reads
     * RAM($0098) = ObjDir. */
    static const unsigned char forest_dirs[4]   = { 0x08u, 0x02u, 0x04u, 0x02u };
    static const unsigned char mountain_dirs[4] = { 0x08u, 0x08u, 0x08u, 0x08u };

    const unsigned char step = WORLD_MAZE_STEP;
    const unsigned char dir  = LINK_DIR;
    const unsigned char room = CUR_ROOM_ID;

    /* Forest maze ($61). */
    if (room == 0x61u) {
        if (dir != forest_dirs[step]) {
            /* Mismatch: $01 (allow exit) else reset + lock-in-room. */
            if (dir == 0x01u) {
                return;
            }
            WORLD_MAZE_STEP = 0u;
            PREV_ROOM_ID    = room;  /* drain alias: PREV_ROOM_ID = NES NextRoomId ($00EC) */
            return;
        }
        /* Match. Last step? Play secret tune. Else advance + lock. */
        if (step == 3u) {
            WORLD_SECRET_SFX = 4u;   /* drain alias for Tune1Request */
            return;
        }
        WORLD_MAZE_STEP = (uint8_t)(WORLD_MAZE_STEP + 1u);
        PREV_ROOM_ID    = room;
        return;
    }

    /* Not forest or mountain → reset only. */
    if (room != 0x1Bu) {
        WORLD_MAZE_STEP = 0u;
        return;
    }

    /* Mountain maze ($1B). */
    if (dir == mountain_dirs[step]) {
        if (step == 3u) {
            WORLD_SECRET_SFX = 4u;
            return;
        }
        WORLD_MAZE_STEP = (uint8_t)(WORLD_MAZE_STEP + 1u);
        PREV_ROOM_ID    = room;
        return;
    }
    /* Mismatch in mountain: $02 (left) allows exit, else reset. */
    if (dir == 0x02u) {
        return;
    }
    WORLD_MAZE_STEP = 0u;
    PREV_ROOM_ID    = room;
}

void world_get_object_middle(unsigned int slot)
{
    /* NES GetObjectMiddle (Z_01.asm:5498). Drain at
     * src/oracle/world/world_runtime.c:19-27. Drain MATCH per finding
     * 4_1n_world_get_object_middle.
     *
     *   $02 = $03 = 8                   ; default offset = 8 (full sprite center)
     *   if (ObjAttr+X & $40) LSR $02    ; half-width → offset = 4
     *   $02 = ObjX+X + $02              ; mid-X
     *   $03 = ObjY+X + $03              ; mid-Y
     *
     * ObjAttr = $04BF per Variables.inc; bit $40 = "half width" flag
     * for collision detection. */
    /* NES source: GetObjectMiddle above; drained C: this same body.
     * Coverage: shared collision center, full/half-width and byte wrap.
     * Stance: EXTEND; publish the two final scratch bytes once. No call
     * or interrupt consumer reads their temporary offset values. */
    const unsigned char offset_x = (OBJ_STATUS_FLAGS(slot) & 0x40u) ? 4u : 8u;
    WORLD_TMP2 = (uint8_t)(OBJ_X(slot) + offset_x);
    WORLD_TMP3 = (uint8_t)(OBJ_Y(slot) + 8u);
}

/* FillTileMap (Z_07.asm): PlayAreaTiles $6530..$67EF := [0A]. Leaves the
 * NES pointer [00:01] at $67F0, as the loop does. */
void world_fill_tile_map(void)
{
    const unsigned char tile = (unsigned char)nes_ram[0x000Au];
    unsigned int a;
    for (a = 0x6530u; a < 0x67F0u; ++a)
        nes_ram[a] = tile;
    nes_ram[0x0000u] = 0xF0u;
    nes_ram[0x0001u] = 0x67u;
}
