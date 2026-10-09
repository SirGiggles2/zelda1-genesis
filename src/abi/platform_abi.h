/* platform_abi.h — minimal C-side interface to the NES-RAM shadow region
 * that transpiled asm uses via A4-relative addressing.
 *
 * Two ABI variants per debate 006 D3 (one header, build-time switch):
 *
 * 1. Debug.md build (default, NO -DROOMROM_BUILD): A4-pinned `nes_ram`.
 *    - `register volatile unsigned char *nes_ram asm("a4")` reserves A4 globally
 *    - `-ffixed-a4` flag tells gcc to never clobber it
 *    - Boot shell `src/debug/a4_probe_asm.s` (entry point `main:`) loads
 *      `lea 0x00FF8000,%a4` before jumping to debug_main_after_a4().
 *      This is the Debug.md NES RAM base — NOT $FF0000. The lower
 *      $FF0000-$FF7FFF region is reserved for SGDK runtime (BSS,
 *      stack guard, system globals). NES work-RAM mirror lives at
 *      $FF8000-$FF87FF. `tools/debug/test_debug_contract.py` enforces
 *      this constant in both the asm and `a4_probe_main.c:10`.
 *      (The retired pre-Debug title boot used `src/genesis_shell.asm`
 *      with A4 = $FF0000; that boot is NOT linked into Debug.md.)
 *    - Net effect: `nes_ram[offset]` compiles to `move.b (off,A4)` —
 *      the same addressing mode the transpiled asm uses. Zero perf cost.
 *      Probes that need to read raw absolute RAM cells must use
 *      $FF8000+offset, not $FF0000+offset.
 *
 * 2. RoomRom build (`-DROOMROM_BUILD`): regular global pointer.
 *    - `extern volatile unsigned char *nes_ram` — no register binding
 *    - RoomRom boot init (`RoomRom/src/boot/nes_ram_init.c`) allocates
 *      `static unsigned char roomrom_nes_ram[0x800]` and sets pointer
 *      BEFORE any drained code runs
 *    - SGDK-friendly: A4 not reserved, no `-ffixed-a4` collision
 *    - Cost: one extra indirection per RAM access vs the retired A4 boot path
 *
 * Both variants expose identical `RAM(off)` / `OBJ(off, slot)` macros so
 * drained C in `src/oracle/<subsystem>/x_runtime.c` (and future native
 * code in `src/game/`) compiles unchanged for either target.
 *
 * No stdint.h (SGDK vendored toolchain ships no GCC builtin headers).
 * Primitive C types on m68k-elf:
 *   unsigned char  8
 *   unsigned short 16
 *   unsigned int   32
 *   unsigned long  32
 */
#ifndef NES_ABI_H
#define NES_ABI_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ROOMROM_BUILD
/* RoomRom variant: regular global pointer. Initialized at boot by
 * RoomRom/src/boot/nes_ram_init.c BEFORE any drained code runs. */
extern volatile unsigned char *nes_ram;
#else
/* Debug.md variant: A4-register pinned. Boot shell loads A4 = $FF0000
 * before C entry; -ffixed-a4 flag prevents gcc from clobbering. */
register volatile unsigned char *nes_ram asm("a4");
#endif

/* SGDK memory_base.h defines an object-like RAM (0xE0FF0000). No project
 * code uses it (T-093 audit); our function-like RAM(off) replaces it. */
#ifdef RAM
#undef RAM
#endif
#define RAM(off) (nes_ram[(off)])

/* Debug/probe sentinels (T-109). Previously written straight into
 * nes_ram[$07E0..$07FF], which is NES WorldFlags for levels 7-9
 * ($077F..$07FE): every frame they overwrote the room flags (items taken,
 * doors, clears) of L7-9 rooms $61-$7F, and saves carried the damage.
 * Index i = old NES address - $07E0. Probes locate the array with
 * @SYM:g_debug_sentinel@ (tools/debug/run_probe.py). */
extern volatile unsigned char g_debug_sentinel[32];
#define DBG_SENTINEL(i) (g_debug_sentinel[(i)])

/* Slot-indexed accessor: NES RAM offsets are often base + slot (with
 * slot 0..11 for objects). Same encoding as transpiled `(off,A4,D2.W)`.
 */
#define OBJ(off, slot) (nes_ram[(off) + (slot)])

/* NES-RAM offsets used by the walker / MoveObject path. Kept in one
 * place so a bank migration or data-layout change only edits here.
 * Values match the asm comments at tools/transpile_6502.py:5260-5269
 * and the P48 body below it.
 */
#define NES_OBJ_DIR         0x000F
#define NES_TMP0            0x0000
#define NES_TMP1            0x0001
#define NES_TMP2            0x0002
#define NES_TMP3            0x0003
#define NES_TMP4            0x0004
#define NES_SHOT_COLLISION_FLAG 0x000E
#define NES_POS_GRID_LIMIT  0x010E
#define NES_NEG_GRID_LIMIT  0x010F
#define NES_OBJ_X           0x0070
#define NES_OBJ_Y           0x0084
#define NES_OBJ_GRID_OFFSET 0x0394
#define NES_OBJ_POS_FRAC    0x03A8
#define NES_OBJ_QSPD_FRAC   0x03BC
#define NES_BOUND_LEFT      0x0346
#define NES_BOUND_RIGHT     0x0347
#define NES_BOUND_TOP       0x0348
#define NES_BOUND_BOTTOM    0x0349
#define NES_OBJ_TYPE        0x034F

/* ---- Plan W: shared ABI cells ------------------------------------------- */
/* Multi-use scratch pointer / misc */
#define NES_TILE_XFER_PTR_LO     0x0000
#define NES_TILE_XFER_PTR_HI     0x0001
#define NES_SCRATCH_2            0x0002
#define NES_SCRATCH_3            0x0003
#define NES_SCRATCH_4            0x0004
#define NES_SCRATCH_5            0x0005

/* Game-mode / slot / ticks */
#define NES_CUR_LEVEL            0x0010
#define NES_GAME_MODE_PREV       0x0011
#define NES_GAME_MODE            0x0012
#define NES_SUB_MODE             0x0013
#define NES_ROOM_XFER_BUF_SELECT 0x0014
#define NES_FRAME_TICK           0x0015
#ifndef NES_SAVE_SLOT
#define NES_SAVE_SLOT            0x0016
#endif

/* Tile-transfer / play-area constants */
#define NES_TILE_XFER_COL        0x00E8
#define NES_TILE_XFER_ROW        0x00E9
#define NES_CUR_ROOM_ID          0x00EB
#define NES_PPU_MASK_SHADOW      0x00FE
#define NES_TILE_XFER_BUF_IDX    0x0301
#define NES_TILE_XFER_BUF_BASE   0x0302
#define NES_TILE_XFER_BUF_END    0x0325
#define NES_PLAY_AREA_BASE       0x6530u
#define NES_TILE_COL_STRIDE      0x16u

/* OAM */
#define NES_OAM_BASE             0x0200

/* Room / progress cells */
#define NES_ROOM_LAYOUT_SCRATCH  0x051A
#define NES_ROOM_ID_ALT          0x0526
#define NES_ROOM_HISTORY_IDX     0x0620
#define NES_ROOM_HISTORY_BASE    0x0621
#define NES_CONTINUE_COUNT_BASE  0x0630
#define NES_ITEMS_BY_LEVEL_BASE  0x0657

/* Link / mode-11 death */
#define NES_LINK_MOVING_DIR      0x000F
#define NES_MODE11_DEATH_TIMER   0x0033
#define NES_LINK_ROOM_SCRATCH    0x0059
#define NES_DEATH_FRAME_COUNTER  0x0602
#define NES_LINK_HALT_FLAG       0x066C

/* Audio */
#define NES_SFX_PRIMARY          0x0600

/* Per-slot object bases (add `+ slot`) */
#define NES_OBJ_TILE_X_BASE      0x0070
#define NES_OBJ_TILE_Y_BASE      0x0084
#define NES_OBJ_FLAG_BASE        0x0098
#define NES_OBJ_STATE_BASE       0x00AC
#define NES_OBJ_SHOVE_DIR_BASE   0x00C0
#define NES_OBJ_SHOVE_DIST_BASE  0x00D3
#define NES_OBJ_TYPE_BASE        0x034F
#define NES_OBJ_ALIGN_FLAG_BASE  0x0394
#define NES_OBJ_ANIM_CNTR_BASE   0x03D0
#define NES_OBJ_HFLIP_BASE       0x03E4
#define NES_OBJ_METASTATE_BASE   0x0405
#define NES_OBJ_TILE_NEXT_BASE   0x049E
#define NES_OBJ_INV_TIMER_BASE   0x04F0

/* SRAM (NES $6000 base) */
#define NES_SRAM_BASE                 0x6000u
#define NES_SRAM_ROOM_UNIQUE_ID_BASE  0x09FE
#define NES_SRAM_ROOM_FLAGS_PTR_LO    0x0BAF
#define NES_SRAM_ROOM_FLAGS_PTR_HI    0x0BB0

#define CARRY_SET 0x100u

#ifdef __cplusplus
}
#endif

#endif /* NES_ABI_H */
