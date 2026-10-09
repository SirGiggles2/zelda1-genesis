/* dyn_tile_dispatch.c — native play-area dynamic tile editing.
 *
 * NES sources:
 *   ChangeTileObjTiles   @ Z_07.asm:1114
 *   ChangePlayMapSquareOW @ Z_05.asm:6102
 *   WriteSquareOW        @ Z_05.asm:5942
 *
 * Drain Rule D1 EXTEND — no native drain in src/oracle/world/. Bodies
 * translated NES-asm to C per spec, with PrimarySquaresOW /
 * SecondarySquaresOW / PlayAreaColumnAddrs data tables ADOPTed verbatim.
 *
 * c_shims.asm:4669 carries `c_change_tile_obj_tiles` for the legacy
 * bank build but tools/debug/build_debug.py does not link c_shims.asm,
 * so this native body is the only resolved primitive in Debug.md.
 */

#include "dyn_tile_dispatch.h"
#include "core_dispatch.h"     /* core_map_screen_pos_to_ppu_addr,
                                  core_add_to_int16_at_0 */
#include "platform_abi.h"      /* RAM, OBJ, NES_OBJ_X, NES_OBJ_Y,
                                  NES_TILE_XFER_BUF_BASE */
#include "room_state.h"        /* ROOM_TILE_XFER_BUF_IDX,
                                  ROOM_TILE_XFER_BUF */

/* NES PlayAreaColumnAddrs (Z_07.asm:336): 32 LE 16-bit pointers into
 * nes_ram (range $6530..$67DA). Duplicated from collision_dispatch.c
 * (same NES source, both files use independently). */
static const unsigned char k_play_area_column_addrs[64] = {
    0x30u, 0x65u, 0x46u, 0x65u, 0x5Cu, 0x65u, 0x72u, 0x65u,
    0x88u, 0x65u, 0x9Eu, 0x65u, 0xB4u, 0x65u, 0xCAu, 0x65u,
    0xE0u, 0x65u, 0xF6u, 0x65u, 0x0Cu, 0x66u, 0x22u, 0x66u,
    0x38u, 0x66u, 0x4Eu, 0x66u, 0x64u, 0x66u, 0x7Au, 0x66u,
    0x90u, 0x66u, 0xA6u, 0x66u, 0xBCu, 0x66u, 0xD2u, 0x66u,
    0xE8u, 0x66u, 0xFEu, 0x66u, 0x14u, 0x67u, 0x2Au, 0x67u,
    0x40u, 0x67u, 0x56u, 0x67u, 0x6Cu, 0x67u, 0x82u, 0x67u,
    0x98u, 0x67u, 0xAEu, 0x67u, 0xC4u, 0x67u, 0xDAu, 0x67u
};

/* NES PrimarySquaresOW (Z_05.asm:5731): 56 first-tile ids per
 * primary-square index. Used to map a primary tile back to a square
 * index for type-3 lookup. */
static const unsigned char k_primary_squares_ow[56] = {
    0x24u, 0x6Fu, 0xF3u, 0xFAu, 0x98u, 0x90u, 0x8Fu, 0x95u,
    0x8Eu, 0x90u, 0x74u, 0x76u, 0xF3u, 0x24u, 0x26u, 0x89u,
    0x03u, 0x04u, 0x70u, 0xC8u, 0xBCu, 0x8Du, 0x8Fu, 0x93u,
    0x95u, 0xC4u, 0xCEu, 0xD8u, 0xB0u, 0xB4u, 0xAAu, 0xACu,
    0xB8u, 0x9Cu, 0xA6u, 0x9Au, 0xA2u, 0xA0u, 0xE5u, 0xE6u,
    0xE7u, 0xE8u, 0xE9u, 0xEAu, 0xC0u, 0xE0u, 0x78u, 0x7Au,
    0x7Eu, 0x80u, 0xCCu, 0xD0u, 0xD4u, 0xDCu, 0x89u, 0x84u
};

/* NES SecondarySquaresOW (Z_05.asm:5740): 16 4-tile records (square
 * indexes 0..15 = type-3 squares, 4 tile bytes each). */
static const unsigned char k_secondary_squares_ow[64] = {
    0x24u, 0x24u, 0x24u, 0x24u, 0x6Fu, 0x6Fu, 0x6Fu, 0x6Fu,
    0xF3u, 0xF3u, 0xF3u, 0xF3u, 0xFAu, 0xFAu, 0xFAu, 0xFAu,
    0x98u, 0x95u, 0x26u, 0x26u, 0x90u, 0x95u, 0x90u, 0x95u,
    0x8Fu, 0x90u, 0x8Fu, 0x90u, 0x95u, 0x96u, 0x95u, 0x96u,
    0x8Eu, 0x93u, 0x90u, 0x95u, 0x90u, 0x95u, 0x92u, 0x97u,
    0x74u, 0x74u, 0x75u, 0x75u, 0x76u, 0x77u, 0x76u, 0x77u,
    0xF3u, 0x24u, 0xF3u, 0x24u, 0x24u, 0x24u, 0x24u, 0x24u,
    0x26u, 0x26u, 0x26u, 0x26u, 0x89u, 0x88u, 0x8Bu, 0x88u
};

/* NES WriteSquareOW (Z_05.asm:5942). Inputs (caller-staged):
 *   $00:$01 — 16-bit pointer to top-left tile of the 4-tile square.
 *   $05     — primary tile id (= the "tile+0" byte for type-1).
 *   $0D     — square index ($10+ = type-1, < $10 = type-3 lookup).
 *   Y reg   — entry value $00 (caller does LDY #$00 before JSR).
 * Writes 4 tiles at offsets [+0, +1, +$16, +$17] from [00:01]:
 *   $16 = NES_TILE_COL_STRIDE; column stride between adjacent
 *   16-pixel-wide play-area columns. */
void dyn_tile_write_square_ow(void)
{
    const unsigned char square_index = (unsigned char)RAM(0x000Du);
    const unsigned short ptr =
        (unsigned short)(((unsigned short)RAM(0x0001u) << 8) |
                         (unsigned short)RAM(0x0000u));
    if (square_index >= 0x10u) {
        /* Type-1: 4 contiguous CHR tiles starting at primary. */
        const unsigned char tile0 = (unsigned char)RAM(0x0005u);
        nes_ram[ptr + 0x00u] = (uint8_t)(tile0 + 0u);
        nes_ram[ptr + 0x01u] = (uint8_t)(tile0 + 1u);
        nes_ram[ptr + 0x16u] = (uint8_t)(tile0 + 2u);
        nes_ram[ptr + 0x17u] = (uint8_t)(tile0 + 3u);
        return;
    }
    /* Type-3: square_index * 4 indexes the secondary 4-tile record. */
    const unsigned char base = (unsigned char)((square_index & 0x0Fu) << 2);
    nes_ram[ptr + 0x00u] = k_secondary_squares_ow[base + 0u];
    nes_ram[ptr + 0x01u] = k_secondary_squares_ow[base + 1u];
    nes_ram[ptr + 0x16u] = k_secondary_squares_ow[base + 2u];
    nes_ram[ptr + 0x17u] = k_secondary_squares_ow[base + 3u];
}

void dyn_tile_change_play_map_square_ow(unsigned int slot)
{
    /* X = ObjX & $F0, then >> 2 = column-address byte-pair index. */
    const unsigned char obj_x = (unsigned char)RAM(NES_OBJ_X + slot);
    const unsigned char col_idx = (unsigned char)((obj_x & 0xF0u) >> 2);
    /* Stash column-address pointer into [$00:$01]. */
    RAM(0x0000u) = k_play_area_column_addrs[col_idx];
    RAM(0x0001u) = k_play_area_column_addrs[col_idx + 1u];
    /* (ObjY & $F0) - $40 then >> 3 = row offset within column. */
    const unsigned char obj_y = (unsigned char)RAM(NES_OBJ_Y + slot);
    const unsigned char row_off =
        (unsigned char)(((unsigned char)((obj_y & 0xF0u) - 0x40u)) >> 3);
    /* AddToInt16At0 folds A into [$00:$01]. NES asm does not touch
     * $04 in this path; leave it untouched. */
    (void)core_add_to_int16_at_0(row_off);

    /* Default: type-1 square with primary in [05]. Square index $10+
     * doesn't matter, only that it's >= $10. */
    unsigned char square_index = 0x10u;
    const unsigned char primary = (unsigned char)RAM(0x0005u);

    if (primary < 0x27u || primary >= 0xF3u) {
        /* Type-3: scan k_primary_squares_ow[14..1] for a match. */
        unsigned char idx = 0x0Eu;
        while (idx > 0u) {
            if (k_primary_squares_ow[idx] == primary) {
                square_index = idx;
                break;
            }
            idx = (unsigned char)(idx - 1u);
        }
        if (idx == 0u) {
            /* No match — fall through with square_index unchanged
             * from the BNE-loop exit value. NES asm: DEX on $0E down
             * to 1; if BEQ never taken, X holds $00 at @Write entry.
             * We approximate by using square_index = 0 (type-3, base
             * 0 = 4 copies of $24, the floor tile). Matches the NES
             * "couldn't find primary" fallthrough behaviour. */
            square_index = 0u;
        }
    }
    /* Stage square_index in [$0D] for WriteSquareOW + invoke. */
    RAM(0x000Du) = square_index;
    dyn_tile_write_square_ow();
}

void dyn_tile_change_tile_obj_tiles(unsigned int tile, unsigned int slot)
{
    /* [05] holds the first tile. */
    const unsigned char first_tile = (unsigned char)tile;
    RAM(0x0005u) = first_tile;
    /* [03] = ObjX, [02] = ObjY (NES MapScreenPosToPpuAddr inputs). */
    RAM(0x0003u) = (unsigned char)RAM(NES_OBJ_X + slot);
    RAM(0x0002u) = (unsigned char)RAM(NES_OBJ_Y + slot);
    core_map_screen_pos_to_ppu_addr();
    /* DynTileBufLen at $0301; records start at $0302. Two 5-byte
     * records + 1-byte end marker = 11 bytes. After write,
     * DynTileBufLen += $0A (the end marker is overwritten by the
     * next call's first record byte). */
    const unsigned char buf_off = (unsigned char)ROOM_TILE_XFER_BUF_IDX;
    /* High byte of PPU address into both records' byte 0. */
    const unsigned char ppu_hi = (unsigned char)RAM(0x0000u);
    ROOM_TILE_XFER_BUF((unsigned short)buf_off + 0u) = ppu_hi;
    ROOM_TILE_XFER_BUF((unsigned short)buf_off + 5u) = ppu_hi;
    /* Low byte of PPU address: record 1 = ppu_lo, record 2 = ppu_lo+1. */
    const unsigned char ppu_lo = (unsigned char)RAM(0x0001u);
    ROOM_TILE_XFER_BUF((unsigned short)buf_off + 1u) = ppu_lo;
    ROOM_TILE_XFER_BUF((unsigned short)buf_off + 6u) = (uint8_t)(ppu_lo + 1u);
    /* First tile twice in each record (bytes 3,4 and 8,9). */
    ROOM_TILE_XFER_BUF((unsigned short)buf_off + 3u) = first_tile;
    ROOM_TILE_XFER_BUF((unsigned short)buf_off + 4u) = first_tile;
    ROOM_TILE_XFER_BUF((unsigned short)buf_off + 8u) = first_tile;
    ROOM_TILE_XFER_BUF((unsigned short)buf_off + 9u) = first_tile;
    /* If tile >= $46 && < $F3: add 2 to record 2 bytes, +1 to byte
     * 4 in each record. Result: rec1 = $X0,$X1 / rec2 = $X2,$X3. */
    if (first_tile >= 0x46u && first_tile < 0xF3u) {
        const unsigned char tile_p2 = (unsigned char)(first_tile + 2u);
        ROOM_TILE_XFER_BUF((unsigned short)buf_off + 8u) = tile_p2;
        ROOM_TILE_XFER_BUF((unsigned short)buf_off + 9u) = tile_p2;
        ROOM_TILE_XFER_BUF((unsigned short)buf_off + 4u) =
            (uint8_t)(ROOM_TILE_XFER_BUF((unsigned short)buf_off + 4u) + 1u);
        ROOM_TILE_XFER_BUF((unsigned short)buf_off + 9u) =
            (uint8_t)(ROOM_TILE_XFER_BUF((unsigned short)buf_off + 9u) + 1u);
    }
    /* Count byte $82 (2 tiles vertically) at byte 2 of each record. */
    ROOM_TILE_XFER_BUF((unsigned short)buf_off + 2u) = 0x82u;
    ROOM_TILE_XFER_BUF((unsigned short)buf_off + 7u) = 0x82u;
    /* End marker $FF at byte 10. */
    ROOM_TILE_XFER_BUF((unsigned short)buf_off + 10u) = 0xFFu;
    /* Bump buffer length by 10 (next call overwrites the $FF). */
    ROOM_TILE_XFER_BUF_IDX = (uint8_t)(buf_off + 0x0Au);

    /* SwitchBank #$05 — no-op on Genesis (single linear address space). */

    /* Update the in-memory play-area square. */
    dyn_tile_change_play_map_square_ow(slot);

    /* SwitchBank #$04 if ReturnToBank4 set, then clear it. NES
     * Variables.inc: ReturnToBank4 := $00F7. On Genesis this flag is
     * still tracked (some legacy paths set it) but bank-switching is
     * a no-op. We only need to clear the flag for behavioral parity. */
    if ((unsigned char)RAM(0x00F7u) != 0u) {
        /* SwitchBank #$04 no-op. */
    }
    RAM(0x00F7u) = 0u;
}
