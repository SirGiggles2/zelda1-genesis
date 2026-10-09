/* level_info_install.c — install NES Z1 per-level SRAM tables.
 *
 * NES source: PRG-ROM holds 6 attribute sub-tables (A..F, 128 bytes
 *             each = 768 total) per "level block" plus a 256-byte
 *             LevelInfo block that contains FoeCounts + assorted
 *             per-level constants. Real NES cart starts with these
 *             pre-populated in SRAM ($687E..$6C7D). On power-up the
 *             InitSaveRam check at Z_05.asm:7375 only clears RAM
 *             from $6530..$6FFF when the sentinel is wrong; otherwise
 *             it leaves the PRG-loaded tables intact.
 *
 *             Our Genesis port never wrote those tables, so every
 *             consumer reading $697E (LBA_C), $69FE (LBA_D), or
 *             $6BA2 (FoeCounts) saw zeros — enemy_room_load_objects
 *             returned 0 and ObjType[1..count] stayed empty.
 *
 * Drained C:  NONE (PRG->SRAM copy not part of any drained body).
 * Stance:     GREENFIELD — substrate fix. Per Drain Rule D1,
 *             tools/audit/drain_coverage.json has no candidate row
 *             for the level-info loader.
 *
 * Data already ships in repo:
 *   data/rooms/overworld.c  rooms_overworld[]  (LevelBlock + LevelInfo OW)
 *   data/rooms/dungeons.c   rooms_dungeons[]   (4 LevelBlocks UW + 9 LevelInfo UW)
 *
 * NES SRAM layout (per Variables.inc):
 *   $687E .. $68FD   LevelBlockAttrsA (128 bytes)
 *   $68FE .. $697D   LevelBlockAttrsB
 *   $697E .. $69FD   LevelBlockAttrsC
 *   $69FE .. $6A7D   LevelBlockAttrsD
 *   $6A7E .. $6AFD   LevelBlockAttrsE
 *   $6AFE .. $6B7D   LevelBlockAttrsF
 *   $6B7E .. $6C7D   LevelInfo  (256 bytes)
 *     +0x24 = FoeCounts ($6BA2) — first byte of LevelInfo_FoeCounts.
 *
 * Source-blob layout (matches NES SRAM exactly):
 *   rooms_overworld[0..767]    = LevelBlock (6 sub-tables)
 *   rooms_overworld[768..1023] = LevelInfo
 *   rooms_dungeons[0..767]      = LevelBlockUW1Q1
 *   rooms_dungeons[768..1535]   = LevelBlockUW2Q1
 *   rooms_dungeons[1536..2303]  = LevelBlockUW1Q2
 *   rooms_dungeons[2304..3071]  = LevelBlockUW2Q2
 *   rooms_dungeons[3072 + (level-1)*256 .. +255] = LevelInfoUW<level>
 *
 * NES Z_05.asm InitMode2Load (Z_06.asm:202) applies per-quest patches
 * AFTER the base block lands; those patches are PER-QUEST replacement
 * of specific bytes (see LevelBlockAttrsBQ2Replacement{Offsets,Values}).
 * We pick the pre-patched LevelBlockUW{N}Q{Q} block directly per quest
 * — no patch step needed for L1-L6 / L7-L9.
 */

#include "level_info_install.h"
#include "platform_abi.h"
#include "../../../data/rooms/dungeons_offsets.h"
#include "../../../RoomRom/src/roomrom_main_state.h"  /* roomrom_main_current_quest */

extern const unsigned char rooms_overworld[];
extern const unsigned char rooms_dungeons[];

/* NES SRAM addresses per Variables.inc. */
#define NES_LBA_A_BASE          0x687Eu
#define NES_LBA_BLOCK_BYTES     768u
#define NES_LEVEL_INFO_BASE     0x6B7Eu
#define NES_LEVEL_INFO_BYTES    256u

/* Blob offsets per data/rooms/MANIFEST.json. */
#define BLOB_OW_LEVELBLOCK_OFF  0u
#define BLOB_OW_LEVELINFO_OFF   768u

#define BLOB_UW_BLOCK_BYTES     768u
#define BLOB_UW_LEVELINFO_BASE  3072u
#define BLOB_UW_LEVELINFO_STRIDE 256u

/* SGDK's optimized memcpy (sgdk/inc/memory.h), bound without the SGDK
 * headers this module does not include. */
extern void sgdk_memcpy(void *to, const void *from, unsigned short len) __asm__("memcpy");

static void copy_to_nes_ram(unsigned short dst_nes_addr,
                            const unsigned char *src,
                            unsigned int bytes)
{
    /* T-172: block copy (the byte loop was 6k instructions of every level
     * load, mode 3 Sub8 7 frames vs NES 4). */
    sgdk_memcpy(&nes_ram[dst_nes_addr], src, (unsigned short)bytes);
}

/* NES Z_06.asm UpdateMode2Load_Full @PatchQ2Rooms: 8 LevelBlockAttrsB
 * replacements from the ROM tables (LDY #7 .. BPL), then 7 immediate
 * writes. Called after the OW LevelBlock copy whenever Q2 is active. */
/* room_dispatch.c: LevelInfo $6B92 = Link's color (InitMode3_Sub1). */
extern void room_patch_level_palette_link_color(void);
/* OW layout summary cache and column precompute: the level block changed
 * (ow_render.c). */
extern void roomrom_ow_room_render_layout_drop(void);
extern void roomrom_ow_room_render_prepare_drop(void);

static void lba_changed(void)
{
    roomrom_ow_room_render_layout_drop();
    roomrom_ow_room_render_prepare_drop();
}

/* @PatchQ2Rooms cells (block offsets from $687E). */
#define Q2_OW_FIXED_CELLS 7u
static const unsigned short k_q2_ow_fixed_off[Q2_OW_FIXED_CELLS] = {
    0x180u + 11u, 0x180u + 60u, 0x180u + 116u, 60u, 116u, 0x280u + 60u, 0x280u + 116u
};
static const unsigned char k_q2_ow_fixed_val[Q2_OW_FIXED_CELLS] = {
    0x7Bu, 0x7Bu, 0x5Au, 0x72u, 0x72u, 0x01u, 0x00u
};

static unsigned char is_q2_ow_patch_cell(unsigned short k)
{
    const unsigned char *offs = &rooms_dungeons[ROOMROM_OW_Q2_ATTRB_REPL_OFFSETS_OFF];
    unsigned char i;
    for (i = 0u; i < 8u; ++i)
        if (k == (unsigned short)(0x80u + offs[i])) return 1u;
    for (i = 0u; i < Q2_OW_FIXED_CELLS; ++i)
        if (k == k_q2_ow_fixed_off[i]) return 1u;
    return 0u;
}

/* T-172: 1 when the level block in RAM differs from src (cells the Q2
 * patch rewrites right after are left out when skip_q2_cells). Reinstalls
 * of the same block (mode-3 continue, cave exits) kept dropping the OW
 * room layout cache, ~5k instructions of InitMode3_Sub8 (t013_continue:
 * 4 frames vs NES 3). */
static unsigned char lba_differs(const unsigned char *src, unsigned char skip_q2_cells)
{
    const unsigned long *a = (const unsigned long *)(unsigned long)&nes_ram[NES_LBA_A_BASE];
    const unsigned long *b = (const unsigned long *)(const void *)src;
    const unsigned long *const end = a + NES_LBA_BLOCK_BYTES / 4u;
    if ((unsigned long)src & 1u) return 1u;
    do {
        if (*a != *b) {
            const unsigned short w = (unsigned short)(b - (const unsigned long *)(const void *)src);
            unsigned short k;
            if (!skip_q2_cells) return 1u;
            for (k = (unsigned short)(w * 4u); k < (unsigned short)(w * 4u + 4u); ++k)
                if (nes_ram[NES_LBA_A_BASE + k] != src[k] && !is_q2_ow_patch_cell(k))
                    return 1u;
        }
        ++a;
        ++b;
    } while (a != end);
    return 0u;
}

void level_info_apply_q2_ow_patch(void)
{
    const unsigned char *offs = &rooms_dungeons[ROOMROM_OW_Q2_ATTRB_REPL_OFFSETS_OFF];
    const unsigned char *vals = &rooms_dungeons[ROOMROM_OW_Q2_ATTRB_REPL_VALUES_OFF];
    unsigned char changed = 0u;
    signed char i;
    for (i = 7; i >= 0; --i) {
        const unsigned short a = (unsigned short)(NES_LBA_A_BASE + 0x80u + offs[i]);
        if (nes_ram[a] != vals[i]) changed = 1u;
        nes_ram[a] = vals[i];                               /* AttrsB $68FE */
    }
    for (i = 0; i < (signed char)Q2_OW_FIXED_CELLS; ++i) {
        const unsigned short a = (unsigned short)(NES_LBA_A_BASE + k_q2_ow_fixed_off[i]);
        if (nes_ram[a] != k_q2_ow_fixed_val[i]) changed = 1u;
        nes_ram[a] = k_q2_ow_fixed_val[i];   /* AttrsD+11/60/116, A+60/116, F+60/116 */
    }
    if (changed) lba_changed();
}

void level_info_install_ow(void)
{
    const unsigned char q2 = (unsigned char)(roomrom_main_current_quest() == 2u);
    if (lba_differs(&rooms_overworld[BLOB_OW_LEVELBLOCK_OFF], q2)) lba_changed();
    copy_to_nes_ram(NES_LBA_A_BASE,
                    &rooms_overworld[BLOB_OW_LEVELBLOCK_OFF],
                    NES_LBA_BLOCK_BYTES);
    copy_to_nes_ram(NES_LEVEL_INFO_BASE,
                    &rooms_overworld[BLOB_OW_LEVELINFO_OFF],
                    NES_LEVEL_INFO_BYTES);
    /* T-007: the active OW install ignored the quest, so Q2 overworld ran
     * with Q1 room attributes (secret/cave/door data of 8+7 cells). */
    if (roomrom_main_current_quest() == 2u) {
        level_info_apply_q2_ow_patch();
    }    room_patch_level_palette_link_color();
}

/* OW LevelInfo_StartRoomId ($6BAD in the OW install), read while a level's
 * LevelInfo is installed: InitMode3_Sub1 starts the OW there when
 * CaveSourceRoomId is $FF. */
unsigned char level_info_ow_start_room(void)
{
    return rooms_overworld[BLOB_OW_LEVELINFO_OFF + (0x6BADu - NES_LEVEL_INFO_BASE)];
}

/* NES source: Z_06 UpdateMode2Load_Full; Z_05 CheckWarps.
 * Drained C: existing installer below and cellar_mode.c:cellar_try_enter.
 * Coverage: PARTIAL (read-only metadata consumers previously used L1 tables).
 * Stance: EXTEND the installed-record owner without a second state copy. */
const unsigned char *level_info_uw_attributes(unsigned char level,
                                               unsigned char quest)
{
    if (level == 0u || level > 9u || quest == 0u || quest > 2u) return 0;
    return &rooms_dungeons[(quest == 2u ? 2u * BLOB_UW_BLOCK_BYTES : 0u) +
                           (level <= 6u ? 0u : BLOB_UW_BLOCK_BYTES)];
}

const unsigned char *level_info_uw_cellars(unsigned char level,
                                           unsigned char quest)
{
    unsigned int src, k;
    if (level_info_uw_attributes(level, quest) == 0) return 0;
    if (quest == 1u)
        return &rooms_dungeons[BLOB_UW_LEVELINFO_BASE +
                              (unsigned int)(level - 1u) * BLOB_UW_LEVELINFO_STRIDE +
                              0x34u];
    src = ROOMROM_UW_Q2_LI_REPL_BASE_OFF;
    for (k = 1u; k < level; ++k)
        src += rooms_dungeons[ROOMROM_UW_Q2_LI_REPL_SIZES_OFF + k - 1u];
    /* Cellar array $6BB2 lies eleven bytes after Q2 patch destination $6BA7. */
    return &rooms_dungeons[src + (0x6BB2u - ROOMROM_UW_Q2_LI_PATCH_DEST)];
}

void level_info_apply_q2_patch(unsigned char level)
{
    /* NES: LDY Sizes-1,X / @loop LDA ($00),Y / STA $6BA7,Y / DEY / BPL.
     * Copies Sizes[level-1]+1 bytes (one past the array, as the NES does). */
    const unsigned char *sizes = &rooms_dungeons[ROOMROM_UW_Q2_LI_REPL_SIZES_OFF];
    unsigned int src = ROOMROM_UW_Q2_LI_REPL_BASE_OFF;
    unsigned int k;
    unsigned int count;
    if (level == 0u || level > 9u) return;
    for (k = 1u; k < level; ++k) src += sizes[k - 1u];
    count = (unsigned int)sizes[level - 1u] + 1u;
    copy_to_nes_ram(ROOMROM_UW_Q2_LI_PATCH_DEST, &rooms_dungeons[src], count);
}

void level_info_install_uw(unsigned char level, unsigned char quest)
{
    /* Dungeons blob layout:
     *   L1-L6 Q1 = block 0 (offset 0)
     *   L7-L9 Q1 = block 1 (offset 768)
     *   L1-L6 Q2 = block 2 (offset 1536)
     *   L7-L9 Q2 = block 3 (offset 2304)
     * level: 1..9, quest: 1 or 2. Clamp out-of-range to L1Q1. */
    if (level == 0u || level > 9u) level = 1u;
    if (quest == 0u || quest > 2u) quest = 1u;
    copy_to_nes_ram(NES_LBA_A_BASE, level_info_uw_attributes(level, quest),
                    NES_LBA_BLOCK_BYTES);

    /* LevelInfoUW<level> at base 3072 + (level-1)*256. */
    unsigned int info_off = BLOB_UW_LEVELINFO_BASE +
                            ((unsigned int)(level - 1u)) * BLOB_UW_LEVELINFO_STRIDE;
    copy_to_nes_ram(NES_LEVEL_INFO_BASE,
                    &rooms_dungeons[info_off],
                    NES_LEVEL_INFO_BYTES);

    /* Z_06.asm UpdateMode2Load_Full: second quest overwrites LevelInfo from
     * $6BA7 (shortcut/item positions, map rotation/offset, start + triforce
     * room, world-flags pointer, level number, cellars, boss room, map mask,
     * status-bar map). ROM-verified 2026-09-24: reproduces all Q2 start,
     * rotation, triforce, status-bar X offset and marker values. */
    if (quest == 2u) {
        level_info_apply_q2_patch(level);
    }
    room_patch_level_palette_link_color();

    /* LevelInfo_WorldFlagsAddr ($6BAF/$6BB0) comes from the ROM record
     * unmodified. ROM-verified 2026-09-24 (pointer-read, 252-byte records):
     * OW $067F, L1-L6 $06FF, L7-L9 $077F -- three separate live NES flag
     * regions. The former $067F override + zeroing (a workaround for the
     * old 256-stride misread that produced $FFFF/$A672) pointed dungeons at
     * the overworld flags and wiped them on every dungeon entry; removed. */
}

/* NES GameMode 2 one step a tick (T-171): InitMode2_Sub0 copies the level
 * block, InitMode2_Sub1 the level info, UpdateMode2Load_Full applies the
 * second-quest patches (Z_06.asm:60-110, 202). level 0 = overworld. */
void level_info_mode2_step(unsigned char step, unsigned char level,
                           unsigned char quest)
{
    const unsigned char q2 = (unsigned char)(quest == 2u);
    if (step == 0u) {
        const unsigned char *src = &rooms_overworld[BLOB_OW_LEVELBLOCK_OFF];
        if (level != 0u && level <= 9u)
            src = level_info_uw_attributes(level, q2 ? 2u : 1u);
        if (lba_differs(src, (unsigned char)(q2 && level == 0u))) lba_changed();
        copy_to_nes_ram(NES_LBA_A_BASE, src, NES_LBA_BLOCK_BYTES);
    } else if (step == 1u) {
        const unsigned char *src = &rooms_overworld[BLOB_OW_LEVELINFO_OFF];
        if (level != 0u && level <= 9u)
            src = &rooms_dungeons[BLOB_UW_LEVELINFO_BASE +
                                  ((unsigned int)(level - 1u)) * BLOB_UW_LEVELINFO_STRIDE];
        copy_to_nes_ram(NES_LEVEL_INFO_BASE, src, NES_LEVEL_INFO_BYTES);
        room_patch_level_palette_link_color();
    } else if (q2) {
        if (level == 0u) level_info_apply_q2_ow_patch();
        else level_info_apply_q2_patch(level);
    }
}
